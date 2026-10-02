#include "Win32Compat.h"
#include "Log.h"

namespace lp::abi {

int MonitorRefreshHz(HWND hwnd) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
            return (int)dm.dmDisplayFrequency;
    }
    HDC dc = GetDC(nullptr);
    int hz = GetDeviceCaps(dc, VREFRESH);
    ReleaseDC(nullptr, dc);
    return hz > 1 ? hz : 60;
}

bool IsOnBattery() {
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) return false;
    return sps.ACLineStatus == 0; // 0 = offline (battery), 255 = unknown
}

bool IsFullscreenAppInForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    // The desktop / shell windows are never "fullscreen apps".
    wchar_t cls[64]{};
    GetClassNameW(fg, cls, _countof(cls));
    if (_wcsicmp(cls, L"Progman") == 0 || _wcsicmp(cls, L"WorkerW") == 0 ||
        _wcsicmp(cls, L"Shell_TrayWnd") == 0 || _wcsicmp(cls, L"ApplicationManager_DesktopShellWindow") == 0)
        return false;

    RECT wr{};
    if (!GetWindowRect(fg, &wr)) return false;
    if (IsIconic(fg)) return false;

    HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return false;

    // Treat "covers the whole monitor" plus "not a tool window" as exclusive-ish.
    const RECT& m = mi.rcMonitor;
    bool covers = wr.left <= m.left && wr.top <= m.top && wr.right >= m.right && wr.bottom >= m.bottom;
    if (!covers) return false;

    LONG_PTR style = GetWindowLongPtrW(fg, GWL_STYLE);
    if (style & WS_CHILD) return false;
    return true;
}

bool IsSessionLocked() {
    // A locked or disconnected session reports a different input desktop than "Default".
    HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
    if (!desk) return true;
    wchar_t name[64]{};
    DWORD needed = 0;
    bool locked = false;
    if (GetUserObjectInformationW(desk, UOI_NAME, name, sizeof(name), &needed)) {
        locked = (_wcsicmp(name, L"Default") != 0);
    }
    CloseDesktop(desk);
    return locked;
}

bool IsDesktopCovered() {
    // "Occluded" means the wallpaper is not visible anywhere: some non-shell window
    // covers (almost) the entire virtual desktop. This is deliberately a coverage
    // ratio rather than a z-order probe: GDI visible-region queries on our WorkerW
    // child lie, because the shell's icon DefView window sits above the wallpaper at
    // all times and would report permanent occlusion.
    struct CoverCtx { double desktopArea; double covered; DWORD selfPid; RECT vd; } ctx{};
    RECT vd{ GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
             GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
             GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
    ctx.desktopArea = (double)(vd.right - vd.left) * (vd.bottom - vd.top);
    ctx.selfPid = GetCurrentProcessId();
    ctx.vd = vd;

    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = (CoverCtx*)lp;
        if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
        // Our own windows (panel, tray tooltips) never count as covering the desktop.
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == c->selfPid) return TRUE;

        wchar_t cls[64]{};
        GetClassNameW(h, cls, _countof(cls));
        if (_wcsicmp(cls, L"Progman") == 0 || _wcsicmp(cls, L"WorkerW") == 0 ||
            _wcsicmp(cls, L"Shell_TrayWnd") == 0 || _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
            _wcsicmp(cls, L"ApplicationManager_DesktopShellWindow") == 0)
            return TRUE;
        // Cloaked windows (suspended UWP, windows on another virtual desktop) own no
        // visible pixels, so they must not count either.
        if (auto dwm = (HMODULE)GetModuleHandleW(L"dwmapi.dll")) {
            typedef HRESULT(WINAPI * DwmGetWindowAttributeFn)(HWND, DWORD, void*, DWORD);
            auto fn = (DwmGetWindowAttributeFn)GetProcAddress(dwm, "DwmGetWindowAttribute");
            if (fn) {
                int cloaked = 0;
                // DWMWA_CLOAKED = 14; numeric to avoid a newer SDK dependency.
                if (SUCCEEDED(fn(h, 14, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
            }
        }

        RECT wr{};
        if (!GetWindowRect(h, &wr)) return TRUE;
        LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
        if (style & WS_CHILD) return TRUE;
        const RECT& d = c->vd;
        long ix = wr.left > d.left ? wr.left : d.left, iy = wr.top > d.top ? wr.top : d.top;
        long ir = wr.right < d.right ? wr.right : d.right, ib = wr.bottom < d.bottom ? wr.bottom : d.bottom;
        if (ir > ix && ib > iy)
            c->covered += (double)(ir - ix) * (ib - iy);
        return TRUE;
    }, (LPARAM)&ctx);

    return ctx.desktopArea > 0 && ctx.covered >= ctx.desktopArea * 0.92;
}

} // namespace lp::abi
