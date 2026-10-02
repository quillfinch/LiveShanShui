// Live Shan Shui - notification-area icon.
//
// Uses only Shell_NotifyIconW. The icon image is generated at runtime with GDI
// (a few filled shapes) so the product ships as a single binary with no .ico assets
// to lose, and so the tray art can reflect the active scene's accent colour.
#pragma once
#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <string>

namespace lp {

enum class TrayCommand {
    TogglePanel = 1,
    Reshuffle,
    PauseToggle,
    TogglePet,
    OpenConfig,
    Reload,
    Quit,
};

class Tray {
public:
    Tray();
    ~Tray();
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // `onCommand` is invoked on the thread that pumps messages.
    bool Create(HWND owner, const std::function<void(TrayCommand)>& onCommand);
    void Destroy();

    void SetTooltip(const std::wstring& text);
    void SetAccent(COLORREF accent);   // regenerates the icon
    void SetPaused(bool paused);
    // Shows a balloon; silently ignored when notifications are disabled by policy.
    void Balloon(const wchar_t* title, const wchar_t* text, DWORD flags = NIIF_INFO);

    // Call from the owner's window procedure.
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);

    // The owner window this icon is registered against (null before Create).
    HWND Window() const { return m_nid.hWnd; }
    bool IsCreated() const { return m_created; }

private:
    void RebuildIcon();

    NOTIFYICONDATAW m_nid{};
    HICON m_icon = nullptr;
    std::function<void(TrayCommand)> m_cb;
    bool m_created = false;
    bool m_paused = false;
    COLORREF m_accent = RGB(64, 196, 255);
    std::wstring m_tooltip = L"Live Shan Shui";
    // Context-menu command ids are offset so they never collide with 0.
    static constexpr UINT kMenuBase = 100;
};

} // namespace lp
