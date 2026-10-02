// Live Shan Shui - notification-area icon.
//
// Uses only Shell_NotifyIconW. The icon image is generated at runtime with GDI so the
// product ships as a single binary with no .ico assets to lose, and so the tray art can
// reflect the active scene's accent colour and the paused state.
#include "Tray.h"
#include "../core/Log.h"
#include <shellapi.h>
#include <vector>
#include <algorithm>

namespace lp {

namespace {

constexpr UINT kTrayCallback = WM_APP + 0x200;
// One stable id for the single icon this process owns.
constexpr UINT kTrayIconId = 1;

// Context-menu command ids.
constexpr UINT kMenuPanel  = 101;
constexpr UINT kMenuPause  = 103;
constexpr UINT kMenuReload = 104;
constexpr UINT kMenuConfig = 105;
constexpr UINT kMenuQuit   = 106;
constexpr UINT kMenuShuffle = 107;
constexpr UINT kMenuPet     = 108;

// The tray glyph: a rounded frame with three filaments, echoing the "lines" scene.
HICON MakeIcon(COLORREF accent, bool paused, int size) {
    if (size < 8) size = 16;

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, size, size);
    ReleaseDC(nullptr, screen);
    if (!dc || !color) {
        if (dc) DeleteDC(dc);
        if (color) DeleteObject(color);
        return nullptr;
    }

    HGDIOBJ oldBmp = SelectObject(dc, color);
    RECT all{ 0, 0, size, size };
    FillRect(dc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // Rounded frame.
    HPEN frame = CreatePen(PS_SOLID, std::max(1, size / 10),
                           paused ? RGB(150, 150, 160) : accent);
    HGDIOBJ oldPen = SelectObject(dc, frame);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int inset = size / 5;
    RoundRect(dc, inset, inset, size - inset, size - inset, size / 4, size / 4);

    // Three filaments inside the frame.
    HPEN line = CreatePen(PS_SOLID, std::max(1, size / 14),
                          paused ? RGB(110, 110, 120) : RGB(255, 255, 255));
    SelectObject(dc, line);
    int a = size / 3, b = size * 2 / 3;
    MoveToEx(dc, a, a, nullptr); LineTo(dc, b, b);
    MoveToEx(dc, b, a, nullptr); LineTo(dc, a, b);
    MoveToEx(dc, size / 2, a, nullptr); LineTo(dc, size / 2, b);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(frame);
    DeleteObject(line);
    SelectObject(dc, oldBmp);

    // Read the colour bitmap back, then rebuild it as premultiplied 32bpp with the
    // luminance of each pixel becoming its alpha (black -> fully transparent).
    BITMAP bm{};
    GetObject(color, sizeof(bm), &bm);
    const int bmw = bm.bmWidth, bmh = bm.bmHeight;
    std::vector<BYTE> src((size_t)bm.bmWidthBytes * bmh);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bmw;
    bi.bmiHeader.biHeight = -bmh;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC refDc = GetDC(nullptr);
    if (!GetDIBits(refDc, color, 0, bmh, src.data(), &bi, DIB_RGB_COLORS))
        std::fill(src.begin(), src.end(), (BYTE)0);

    BITMAPV5HEADER v5{};
    v5.bV5Size = sizeof(BITMAPV5HEADER);
    v5.bV5Width = bmw;
    v5.bV5Height = -bmh;
    v5.bV5Planes = 1;
    v5.bV5BitCount = 32;
    v5.bV5Compression = BI_BITFIELDS;
    v5.bV5RedMask = 0x00FF0000;
    v5.bV5GreenMask = 0x0000FF00;
    v5.bV5BlueMask = 0x000000FF;
    v5.bV5AlphaMask = 0xFF000000;

    void* dibBits = nullptr;
    HBITMAP dib = CreateDIBSection(refDc, (BITMAPINFO*)&v5, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    ReleaseDC(nullptr, refDc);

    HICON icon = nullptr;
    if (dib && dibBits) {
        BYTE* dst = (BYTE*)dibBits;
        for (int y = 0; y < bmh; ++y) {
            for (int x = 0; x < bmw; ++x) {
                size_t s = (size_t)y * bm.bmWidthBytes + (size_t)x * 4;
                BYTE b = src[s + 0], g = src[s + 1], r = src[s + 2];
                int lum = (r * 30 + g * 59 + b * 11) / 100;
                if (lum < 0) lum = 0;
                if (lum > 255) lum = 255;
                BYTE* px = dst + ((size_t)y * bmw + x) * 4;
                // CreateIconIndirect expects premultiplied BGRA.
                px[0] = (BYTE)(b * lum / 255);
                px[1] = (BYTE)(g * lum / 255);
                px[2] = (BYTE)(r * lum / 255);
                px[3] = (BYTE)lum;
            }
        }
        HBITMAP monochrome = CreateBitmap(bmw, bmh, 1, 1, nullptr);
        ICONINFO ii{};
        ii.fIcon = TRUE;
        ii.hbmColor = dib;
        ii.hbmMask = monochrome;
        icon = CreateIconIndirect(&ii);
        if (monochrome) DeleteObject(monochrome);
        DeleteObject(dib);
    }

    DeleteObject(color);
    DeleteDC(dc);
    return icon;
}

} // namespace

Tray::Tray() = default;

Tray::~Tray() { Destroy(); }

bool Tray::Create(HWND owner, const std::function<void(TrayCommand)>& onCommand) {
    if (m_created) return true;

    // Hard guarantee of at most one tray icon per session.
    //
    // The engine is already single-instance, but the tray icon is the one resource
    // that lingers visibly if ownership is ever released uncleanly (Windows keeps a
    // ghost icon until the notification area is refreshed). A session-wide lock means
    // an extra process can never add a second icon - it simply runs without one. The
    // handle is intentionally held for the process lifetime.
    static HANDLE s_trayLock = nullptr;
    if (!s_trayLock) {
        s_trayLock = CreateMutexW(nullptr, TRUE, L"Local\\LiveShanShui.TrayIcon");
        if (s_trayLock && GetLastError() == ERROR_ALREADY_EXISTS) {
            LP_LOGW(L"tray: another Live Shan Shui instance already owns the tray icon");
            CloseHandle(s_trayLock);
            s_trayLock = nullptr;
            return false;
        }
    }

    m_cb = onCommand;
    RebuildIcon();
    if (!m_icon) {
        LP_LOGW(L"tray: icon generation failed");
        return false;
    }

    m_nid = {};
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = owner;
    m_nid.uID = kTrayIconId;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    m_nid.uCallbackMessage = kTrayCallback;
    m_nid.hIcon = m_icon;
    wcsncpy_s(m_nid.szTip, m_tooltip.c_str(), _TRUNCATE);

    // Clear any stale registration for this id first, so an earlier unclean exit can
    // never leave a second entry occupying the same slot.
    Shell_NotifyIconW(NIM_DELETE, &m_nid);

    if (!Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        LP_LOGW(L"tray: Shell_NotifyIcon(NIM_ADD) failed %lu", GetLastError());
        return false;
    }
    m_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &m_nid);
    m_created = true;
    LP_LOGI(L"tray: created (single instance)");
    return true;
}

void Tray::Destroy() {
    if (m_created) {
        // Deleted twice on purpose: the first clears the NOTIFYICON_VERSION_4 entry,
        // the second any legacy entry left by an older build. Both are harmless.
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_created = false;
    }
    if (m_icon) {
        DestroyIcon(m_icon);
        m_icon = nullptr;
    }
}

void Tray::SetTooltip(const std::wstring& text) {
    m_tooltip = text;
    if (!m_created) return;
    wcsncpy_s(m_nid.szTip, text.c_str(), _TRUNCATE);
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void Tray::SetAccent(COLORREF accent) {
    if (accent == m_accent) return;
    m_accent = accent;
    RebuildIcon();
    if (m_created && m_icon) {
        m_nid.hIcon = m_icon;
        m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }
}

void Tray::SetPaused(bool paused) {
    if (paused == m_paused) return;
    m_paused = paused;
    RebuildIcon();
    if (m_created && m_icon) {
        m_nid.hIcon = m_icon;
        m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }
}

void Tray::RebuildIcon() {
    HICON next = MakeIcon(m_accent, m_paused, GetSystemMetrics(SM_CXSMICON));
    if (!next) return;
    if (m_icon) DestroyIcon(m_icon);
    m_icon = next;
}

void Tray::Balloon(const wchar_t* title, const wchar_t* text, DWORD flags) {
    if (!m_created) return;
    NOTIFYICONDATAW nid = m_nid;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = flags;
    nid.uTimeout = 5000;
    wcsncpy_s(nid.szInfoTitle, title ? title : L"Live Shan Shui", _TRUNCATE);
    wcsncpy_s(nid.szInfo, text ? text : L"", _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

bool Tray::HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) {
    if (msg != kTrayCallback || !m_cb) return false;
    if (result) *result = 0;

    // NOTIFYICON_VERSION_4 packs the event in LOWORD(lp).
    UINT event = LOWORD(lp);
    switch (event) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            m_cb(TrayCommand::TogglePanel);
            return true;
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP: {
            POINT pt{};
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            if (!menu) return true;
            AppendMenuW(menu, MF_STRING, kMenuPanel, L"Control panel...");
            AppendMenuW(menu, MF_STRING, kMenuShuffle, L"Shuffle this wallpaper");
            AppendMenuW(menu, MF_STRING, kMenuPet, L"Toggle desktop pet");
            AppendMenuW(menu, MF_STRING | (m_paused ? MF_CHECKED : 0), kMenuPause,
                        m_paused ? L"Resume animation" : L"Pause animation");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kMenuReload, L"Reload wallpaper");
            AppendMenuW(menu, MF_STRING, kMenuConfig, L"Open config folder");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kMenuQuit, L"Exit Live Shan Shui");

            // Required so the menu dismisses when focus moves elsewhere.
            SetForegroundWindow(m_nid.hWnd);
            UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                            pt.x, pt.y, 0, m_nid.hWnd, nullptr);
            DestroyMenu(menu);
            switch (cmd) {
                case kMenuPanel:  m_cb(TrayCommand::TogglePanel); break;
                case kMenuShuffle: m_cb(TrayCommand::Reshuffle); break;
                case kMenuPet:     m_cb(TrayCommand::TogglePet); break;
                case kMenuPause:  m_cb(TrayCommand::PauseToggle); break;
                case kMenuReload: m_cb(TrayCommand::Reload); break;
                case kMenuConfig: m_cb(TrayCommand::OpenConfig); break;
                case kMenuQuit:   m_cb(TrayCommand::Quit); break;
                default: break;
            }
            return true;
        }
        default:
            return true;
    }
}

} // namespace lp
