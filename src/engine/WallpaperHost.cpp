#include "WallpaperHost.h"
#include "../core/Log.h"
#include <vector>

namespace lp {

namespace {

constexpr UINT kSpawnWorkerW = 0x052C;
constexpr wchar_t kHostClass[] = L"LivePaper.Host";

// Registers a minimal window class. We never paint through WM_PAINT - the swap chain
// owns every pixel - but the window still needs a class to exist as a desktop child.
ATOM EnsureHostClass() {
    static ATOM atom = 0;
    if (atom) return atom;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;                       // no CS_* flags: nothing to redraw
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kHostClass;
    atom = RegisterClassExW(&wc);
    if (!atom) LP_LOGW(L"host: RegisterClassEx failed %lu", GetLastError());
    return atom;
}

// Walks top-level windows looking for the icon view and its sibling WorkerW.
HWND FindDesktopWorker() {
    struct Found { HWND defViewHost; HWND workerSibling; };
    Found f{};

    EnumWindows([](HWND top, LPARAM lp) -> BOOL {
        Found* out = (Found*)lp;
        HWND defView = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
        if (!defView) return TRUE;
        out->defViewHost = top;
        // The wallpaper WorkerW is the sibling that follows the DefView host.
        HWND sib = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
        if (sib) out->workerSibling = sib;
        return FALSE; // first DefView wins
    }, (LPARAM)&f);

    if (f.workerSibling) { LP_LOGD(L"host: found sibling WorkerW %p", (void*)f.workerSibling); return f.workerSibling; }
    if (f.defViewHost)  { LP_LOGD(L"host: using DefView host %p", (void*)f.defViewHost);  return f.defViewHost; }
    return nullptr;
}

} // namespace

struct WallpaperHost::Impl {
    HWND window = nullptr;
    HWND parent = nullptr;
    HWND originalParent = nullptr;
    DWORD parentPid = 0;
    DesktopRect rect{};
};

// Size of the host's client area, which is the space a child window is positioned in.
// For a desktop host this normally starts at (0,0), but measuring it rather than
// assuming is what makes the wallpaper fit every resolution, taskbar layout and
// multi-monitor arrangement.
DesktopRect WallpaperHost::HostClientRect() const {
    HWND parent = m_impl->parent;
    if (!parent || !IsWindow(parent)) {
        // Not attached yet: fall back to the virtual desktop so callers get a sane size.
        RECT vs{ GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                 GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                 GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
        return DesktopRect{ vs.left, vs.top, vs.right - vs.left, vs.bottom - vs.top };
    }
    RECT rc{};
    if (!GetClientRect(parent, &rc)) return VirtualDesktop();
    DesktopRect r{ 0, 0, rc.right - rc.left, rc.bottom - rc.top };
    if (r.width < 16 || r.height < 16) return VirtualDesktop();
    return r;
}

WallpaperHost::WallpaperHost() : m_impl(new Impl()) {}

WallpaperHost::~WallpaperHost() {
    Detach();
    delete m_impl;
}

bool WallpaperHost::Attach() {
    Impl* d = m_impl;
    Detach();

    HWND progman = FindWindowW(L"Progman", nullptr);
    HWND worker = nullptr;

    if (progman) {
        // This message is what makes Explorer spawn the wallpaper WorkerW. It is a
        // documented-by-observation behaviour and is harmless if Explorer ignores it.
        DWORD_PTR res = 0;
        SendMessageTimeoutW(progman, kSpawnWorkerW, 0, 0, SMTO_NORMAL, 1000, &res);
        worker = FindDesktopWorker();
    }

    // Windows 11 24H2 keeps the icon view in a top-level WorkerW rather than Progman.
    if (!worker) {
        EnumWindows([](HWND top, LPARAM lp) -> BOOL {
            wchar_t cls[64]{};
            GetClassNameW(top, cls, _countof(cls));
            if (_wcsicmp(cls, L"WorkerW") != 0) return TRUE;
            if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) { *(HWND*)lp = top; return FALSE; }
            return TRUE;
        }, (LPARAM)&worker);
    }
    if (!worker) worker = progman;
    if (!worker) {
        LP_LOGE(L"host: no desktop window found (Explorer not running?)");
        RecordFailure(L"Could not find the desktop window", 0);
        return false;
    }

    if (!EnsureHostClass()) return false;

    // The host is a CHILD window, so its coordinates are relative to the parent's
    // client area - NOT screen coordinates. Passing the virtual desktop rect straight
    // through is what made the wallpaper cover only part of the screen (and sit
    // offset) whenever the parent's client origin was not exactly the screen origin.
    DesktopRect r = HostClientRect();
    d->rect = r;

    // WS_EX_TOOLWINDOW keeps us out of Alt+Tab; a child window cannot be activated
    // anyway, but this also keeps the shell from treating us as an app window.
    HWND w = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kHostClass, L"LivePaper",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, r.width, r.height, worker, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!w) {
        LP_LOGE(L"host: CreateWindowEx failed %lu", GetLastError());
        RecordFailure(L"Could not create the wallpaper host window", (long)GetLastError());
        return false;
    }

    d->window = w;
    d->parent = worker;
    d->originalParent = GetParent(w);

    // Now that the parent is known, re-read the real client area (the pre-create
    // value fell back to the virtual desktop, which is usually but not always the
    // same thing) and make sure the window exactly covers it.
    d->rect = HostClientRect();

    // Record the parent process so we can notice an Explorer restart.
    GetWindowThreadProcessId(worker, &d->parentPid);

    // Place at the bottom of the z-order inside the host so any other desktop child
    // windows (icon views, etc.) stay above us.
    SetWindowPos(w, HWND_BOTTOM, r.x, r.y, r.width, r.height, SWP_NOACTIVATE);

    LP_LOGI(L"host: attached hwnd=%p parent=%p pid=%lu client=%d,%d %dx%d",
            (void*)w, (void*)worker, d->parentPid, r.x, r.y, r.width, r.height);
    return true;
}

void WallpaperHost::Detach() {
    Impl* d = m_impl;
    if (d->window) {
        DestroyWindow(d->window);
        d->window = nullptr;
    }
    d->parent = nullptr;
    d->parentPid = 0;
}

bool WallpaperHost::IsAttached() const {
    Impl* d = m_impl;
    return d->window != nullptr && IsWindow(d->window) && IsWindow(d->parent);
}

HWND WallpaperHost::Window() const { return m_impl->window; }

void WallpaperHost::Layout(const DesktopRect& r) {
    Impl* d = m_impl;
    if (!d->window) return;
    d->rect = r;
    // SWP_NOACTIVATE + HWND_BOTTOM: never steal focus, never cover icons.
    SetWindowPos(d->window, HWND_BOTTOM, r.x, r.y, r.width, r.height,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    LP_LOGD(L"host: layout %d,%d %dx%d", r.x, r.y, r.width, r.height);
}

// Re-checks that the window exactly covers the host's client area and fixes it if not.
// Called periodically because resolution changes, taskbar auto-hide, monitor hotplug
// and DPI changes can all leave the wallpaper the wrong size. Returns true if it
// changed anything, so the caller can resize the swap chain to match.
bool WallpaperHost::EnsureLayout(DesktopRect* actual) {
    Impl* d = m_impl;
    if (!d->window || !IsWindow(d->window)) return false;

    DesktopRect want = HostClientRect();
    RECT cur{};
    if (!GetWindowRect(d->window, &cur)) return false;
    int curW = cur.right - cur.left;
    int curH = cur.bottom - cur.top;

    if (actual) *actual = want;

    // A couple of pixels of slack avoids fighting the shell over rounding.
    if (std::abs(curW - want.width) <= 1 && std::abs(curH - want.height) <= 1) return false;

    LP_LOGI(L"host: client area changed (%dx%d -> %dx%d), resizing wallpaper",
            curW, curH, want.width, want.height);
    Layout(want);
    return true;
}

void WallpaperHost::LayoutMonitor(HMONITOR mon) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return;
    DesktopRect r{ mi.rcMonitor.left, mi.rcMonitor.top,
                   mi.rcMonitor.right - mi.rcMonitor.left,
                   mi.rcMonitor.bottom - mi.rcMonitor.top };
    Layout(r);
}

bool WallpaperHost::NeedsReattach() const {
    Impl* d = m_impl;
    if (!d->window || !d->parent) return true;
    if (!IsWindow(d->window) || !IsWindow(d->parent)) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(d->parent, &pid);
    if (d->parentPid && pid != d->parentPid) return true;  // Explorer restarted
    // A re-parented window means the shell rebuilt the desktop.
    if (GetParent(d->window) != d->parent) return true;
    return false;
}

DesktopRect WallpaperHost::VirtualDesktop() {
    DesktopRect r;
    r.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    // Guard against the metrics API failing during a display change.
    if (r.width <= 0 || r.height <= 0) {
        r.x = r.y = 0;
        r.width = GetSystemMetrics(SM_CXSCREEN);
        r.height = GetSystemMetrics(SM_CYSCREEN);
    }
    return r;
}

HMONITOR WallpaperHost::PrimaryMonitor() {
    POINT p{0, 0};
    return MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY);
}

int WallpaperHost::MonitorCount() {
    int count = 0;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT, LPARAM lp) -> BOOL {
        ++*(int*)lp;
        return TRUE;
    }, (LPARAM)&count);
    return count;
}

} // namespace lp
