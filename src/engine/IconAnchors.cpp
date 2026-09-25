#include "IconAnchors.h"
#include "../core/WinMsg.h"
#include "../core/Log.h"
#include <commctrl.h>
#include <shlobj.h>
#include <cstdio>

namespace lp {

namespace {

// Finds the desktop icon ListView by walking the shell hierarchy (read-only).
HWND FindDesktopListView() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (progman) {
        HWND defView = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
        if (defView) {
            HWND lv = FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
            if (lv) return lv;
        }
    }
    // Windows 11 and some 10 builds host the icon view inside a WorkerW.
    struct Ctx { HWND list; } ctx{ nullptr };
    EnumWindows([](HWND top, LPARAM lp) -> BOOL {
        HWND defView = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
        if (defView) {
            HWND lv = FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
            if (lv) { ((Ctx*)lp)->list = lv; return FALSE; }
        }
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.list;
}

std::wstring DefaultCachePath() {
    wchar_t* appdata = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata)) && appdata) {
        dir = appdata;
        CoTaskMemFree(appdata);
    } else {
        dir = L".";
    }
    dir += L"\\LivePaper";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\icons.cache";
}

} // namespace

const wchar_t* IconAnchors::SourceName() const {
    switch (m_source) {
        case AnchorSource::Shell: return L"shell";
        case AnchorSource::Cache: return L"cache";
        case AnchorSource::Grid:  return L"grid";
        default:                  return L"none";
    }
}

void IconAnchors::Touch() {
    if (m_anchors.size() != m_lastCount) {
        m_lastCount = m_anchors.size();
        ++m_revision;
    }
}

bool IconAnchors::Refresh(HWND wallpaperWindow, int originX, int originY, int width, int height) {
    m_anchors.clear();
    m_source = AnchorSource::None;
    if (m_cachePath.empty()) m_cachePath = DefaultCachePath();

    HWND list = FindDesktopListView();
    if (list) {
        // These two queries are dependable and tell us the icon cell size.
        int count = (int)SendMessageTimeoutW_Int(list, LVM_GETITEMCOUNT, 0, 0, 150);
        DWORD spacing = (DWORD)SendMessageTimeoutW_Int(list, LVM_GETITEMSPACING, FALSE, 0, 150);
        m_cellW = (float)LOWORD(spacing);
        m_cellH = (float)HIWORD(spacing);
        if (m_cellW < 16 || m_cellW > 512) m_cellW = 0;
        if (m_cellH < 16 || m_cellH > 512) m_cellH = 0;

        if (count > 0 && count < 1000) {
            if (QueryShell(list, originX, originY)) {
                m_source = AnchorSource::Shell;
                SaveCache();   // so a later failure still has exact positions
                LP_LOGI(L"icons: %zu exact anchors from the shell", m_anchors.size());
                Touch();
                return true;
            }
        }
    }

    // No live positions: reuse the last exact layout when it still fits this desktop.
    if (LoadCache()) {
        // Reject a cache from a differently sized desktop.
        bool fits = true;
        for (const auto& a : m_anchors) {
            if (a.x < -64 || a.y < -64 || a.x > width + 64 || a.y > height + 64) { fits = false; break; }
        }
        if (fits && !m_anchors.empty()) {
            m_source = AnchorSource::Cache;
            LP_LOGI(L"icons: %zu anchors from cache", m_anchors.size());
            Touch();
            return true;
        }
        m_anchors.clear();
    }

    BuildGrid(list, width, height);
    m_source = AnchorSource::Grid;
    Touch();
    return !m_anchors.empty();
}

// Reads positions with the POINT staged inside the shell process, since USER32 does
// not marshal that buffer for us.
bool IconAnchors::QueryShell(HWND listView, int originX, int originY) {
    int count = (int)SendMessageTimeoutW_Int(listView, LVM_GETITEMCOUNT, 0, 0, 150);
    if (count <= 0 || count > 1000) return false;

    POINT listOrigin{0, 0};
    if (!ClientToScreen(listView, &listOrigin)) return false;

    RemoteBuffer buf(listView, sizeof(POINT));
    if (!buf) {
        LP_LOGD(L"icons: cannot stage a buffer in the shell process");
        return false;
    }

    int good = 0;
    for (int i = 0; i < count; ++i) {
        buf.Zero();
        DWORD_PTR ret = 0;
        if (!SendMessageTimeoutW(listView, LVM_GETITEMPOSITION, (WPARAM)i, (LPARAM)buf.Remote(),
                                 SMTO_ABORTIFHUNG | SMTO_NORMAL, 150, &ret)) {
            break;
        }
        POINT pt{};
        if (!buf.Read(pt)) break;
        // A zeroed buffer means the shell ignored the request - abort, don't fabricate.
        if (pt.x == 0 && pt.y == 0) break;

        IconAnchor a;
        a.x = (float)(listOrigin.x + pt.x - originX);
        a.y = (float)(listOrigin.y + pt.y - originY);
        a.size = (m_cellW > 0 ? m_cellW : 96.0f);
        a.real = true;
        m_anchors.push_back(a);
        ++good;
    }

    // Require most items to have answered, otherwise the result is a partial lie.
    if (good < count / 2 || good == 0) {
        m_anchors.clear();
        if (good) LP_LOGW(L"icons: only %d/%d positions readable, discarding", good, count);
        return false;
    }
    return true;
}

// Approximates the desktop lattice using the shell's own cell metrics so the anchors
// line up with where the icons actually sit.
void IconAnchors::BuildGrid(HWND listView, int width, int height) {
    float cellW = m_cellW > 0 ? m_cellW : 96.0f;
    float cellH = m_cellH > 0 ? m_cellH : 108.0f;
    // Icons begin at the top of the work area, which is below any top-docked appbar.
    float top = 8.0f;
    float left = 8.0f;

    if (listView) {
        RECT rc{};
        if (GetWindowRect(listView, &rc)) {
            // The ListView covers the desktop; its client origin already includes the
            // shell's inset for the icon area.
            HWND desktop = GetDesktopWindow();
            RECT dr{};
            if (GetWindowRect(desktop, &dr)) {
                left = 8.0f;
                top = 8.0f;
            }
        }
    }

    int rows = (int)((height - top - 8.0f) / cellH);
    if (rows < 1) rows = 1;
    if (rows > 64) rows = 64;

    for (int r = 0; r < rows; ++r) {
        IconAnchor a;
        a.x = left + cellW * 0.5f;
        a.y = top + cellH * 0.5f + r * cellH;
        if (a.y > height - 4.0f) break;
        a.size = cellW;
        a.real = false;
        m_anchors.push_back(a);
    }
    LP_LOGI(L"icons: %zu grid anchors (cell %.0fx%.0f)", m_anchors.size(), cellW, cellH);
}

bool IconAnchors::LoadCache() {
    if (m_cachePath.empty()) return false;
    FILE* f = nullptr;
    if (_wfopen_s(&f, m_cachePath.c_str(), L"r, ccs=UTF-8") != 0 || !f) return false;

    wchar_t line[256];
    bool headerSeen = false;
    int declared = 0;
    int read = 0;
    std::vector<IconAnchor> loaded;
    while (fgetws(line, _countof(line), f)) {
        if (!headerSeen) {
            // "LivePaperIcons v1 <count>"
            if (swscanf_s(line, L"LivePaperIcons v1 %d", &declared) == 1) headerSeen = true;
            continue;
        }
        float x, y, size;
        if (swscanf_s(line, L"%f %f %f", &x, &y, &size) == 3) {
            IconAnchor a;
            a.x = x; a.y = y; a.size = size; a.real = true;
            loaded.push_back(a);
            ++read;
        }
    }
    fclose(f);

    if (!headerSeen || read == 0 || (declared > 0 && read != declared)) {
        LP_LOGD(L"icons: cache malformed or truncated, ignoring");
        return false;
    }
    m_anchors = std::move(loaded);
    return true;
}

bool IconAnchors::SaveCache() const {
    if (m_cachePath.empty() || m_anchors.empty()) return false;
    FILE* f = nullptr;
    if (_wfopen_s(&f, m_cachePath.c_str(), L"w, ccs=UTF-8") != 0 || !f) return false;
    fwprintf(f, L"LivePaperIcons v1 %zu\n", m_anchors.size());
    for (const auto& a : m_anchors) {
        fwprintf(f, L"%.1f %.1f %.1f\n", a.x, a.y, a.size);
    }
    fclose(f);
    return true;
}

} // namespace lp
