// LivePaper - single-instance engine plus a small command protocol.
//
// The engine owns a message-only server window named `kIpcWindowClass`. Clients
// (including a second invocation of this exe) find it with FindWindowW and send one
// of the commands below. This avoids a pipe thread, a socket, or a second process:
// the engine already runs a message loop for the tray icon and hotkey.
#pragma once
#include <windows.h>
#include <string>

namespace lp {

namespace ipc {

// Commands are passed as WM_APP+n to the engine's hidden window.
enum class Command : unsigned {
    ShowPanel   = 1,
    HidePanel   = 2,
    TogglePause = 3,
    NextScene   = 4,
    PrevScene   = 5,
    SetScene    = 6,    // lParam = SceneId
    ApplyConfig = 7,    // re-read config.ini and apply it
    Capture     = 8,    // wParam = index used to name the output file
    Quit        = 9,
    Reshuffle   = 10,   // re-randomise the current scene
    Reload      = 11,   // re-attach to the desktop (after an Explorer restart)
    ReportState = 12,   // returns the current scene in the message result
    Poll        = 13,   // presence probe; returns non-zero when the engine is alive
    DumpState   = 14,   // write a live state snapshot next to the config for --status
};

constexpr wchar_t kWindowClass[] = L"LivePaper.Ipc";
constexpr wchar_t kWindowTitle[] = L"LivePaper.Engine";
constexpr wchar_t kMutexName[]   = L"Global\\LivePaper.SingleInstance";
constexpr wchar_t kShowPanelMsgName[] = L"LivePaper.ShowPanel";

// Translates a Command into the window message the engine listens for.
UINT ToMessage(Command cmd);

// Finds a running engine window; returns nullptr when none is running.
HWND FindEngine();

// Sends a command. Returns false when no engine is running.
bool Send(Command cmd, WPARAM wParam = 0, LPARAM lParam = 0);

// Sends a command and waits briefly for the engine's reply value. Used by the CLI to
// report state (the current scene) built from what the engine is really showing.
bool SendSync(Command cmd, WPARAM wParam, LPARAM lParam, LRESULT* reply, UINT timeoutMs = 400);

// Registers/unregisters the engine window class. Safe to call repeatedly.
bool RegisterWindowClass(HINSTANCE instance);
void UnregisterWindowClass(HINSTANCE instance);

// Creates the hidden engine window. Returns nullptr on failure.
HWND CreateEngineWindow(HINSTANCE instance, WNDPROC proc, void* userData);

// Single-instance guard. Returns false when another engine already holds it.
bool AcquireSingleInstance();
void ReleaseSingleInstance();

} // namespace ipc
} // namespace lp
