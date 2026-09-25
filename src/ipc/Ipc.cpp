#include "Ipc.h"
#include "../core/Log.h"

namespace lp::ipc {

namespace {
    HANDLE g_mutex = nullptr;
    constexpr UINT kBase = WM_APP + 0x100;
}

UINT ToMessage(Command cmd) { return kBase + (UINT)cmd; }

bool RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;   // replaced by the caller via SetWindowLongPtr
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    ATOM a = RegisterClassExW(&wc);
    if (!a && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LP_LOGE(L"ipc: RegisterClassEx failed %lu", GetLastError());
        return false;
    }
    return true;
}

void UnregisterWindowClass(HINSTANCE instance) {
    UnregisterClassW(kWindowClass, instance);
}

HWND CreateEngineWindow(HINSTANCE instance, WNDPROC proc, void* userData) {
    // A message-only window is invisible, has no taskbar presence, and is still
    // findable via FindWindowW - exactly what a control endpoint needs.
    HWND w = CreateWindowExW(0, kWindowClass, kWindowTitle, 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, instance, userData);
    if (!w) {
        LP_LOGE(L"ipc: CreateWindowEx failed %lu", GetLastError());
        return nullptr;
    }
    SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)proc);
    return w;
}

HWND FindEngine() {
    HWND w = FindWindowW(kWindowClass, kWindowTitle);
    return (w && IsWindow(w)) ? w : nullptr;
}

bool Send(Command cmd, WPARAM wParam, LPARAM lParam) {
    HWND w = FindEngine();
    if (!w) return false;
    // PostMessage so the caller never blocks on a busy engine; the engine's message
    // loop drains these on its own thread.
    if (!PostMessageW(w, ToMessage(cmd), wParam, lParam)) {
        LP_LOGW(L"ipc: PostMessage(%u) failed %lu", (unsigned)cmd, GetLastError());
        return false;
    }
    return true;
}

bool SendSync(Command cmd, WPARAM wParam, LPARAM lParam, LRESULT* reply, UINT timeoutMs) {
    HWND w = FindEngine();
    if (!w) return false;
    DWORD_PTR result = 0;
    // A short timeout keeps the CLI responsive if the engine is mid-frame; the
    // engine's handler is trivial and never blocks, so this resolves immediately.
    if (!SendMessageTimeoutW(w, ToMessage(cmd), wParam, lParam,
                             SMTO_ABORTIFHUNG | SMTO_NORMAL, timeoutMs, &result)) {
        return false;
    }
    if (reply) *reply = (LRESULT)result;
    return true;
}

bool AcquireSingleInstance() {
    if (g_mutex) return true;
    // A named mutex is the cheapest reliable single-instance guard; the name is in
    // the Global namespace so it also catches a per-session relaunch.
    g_mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!g_mutex) {
        // Fall back to a session-local name if Global\ is not permitted.
        g_mutex = CreateMutexW(nullptr, FALSE, L"LivePaper.SingleInstance");
        if (!g_mutex) return true;   // cannot tell; allow startup
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_mutex);
        g_mutex = nullptr;
        return false;
    }
    return true;
}

void ReleaseSingleInstance() {
    if (g_mutex) {
        CloseHandle(g_mutex);
        g_mutex = nullptr;
    }
}

} // namespace lp::ipc
