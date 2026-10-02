// Live Shan Shui - desktop icon anchors.
//
// The "Lines & Connections" scene decorates the real desktop icons, so it wants their
// true positions. Reading them out of the shell's icon ListView is unreliable in
// practice: LVM_GETITEMPOSITION returns without filling the caller's buffer on some
// systems (verified on affected Windows builds), while LVM_GETITEMCOUNT and
// LVM_GETITEMSPACING answer fine.
//
// Strategy, best source first:
//   1. LVM_GETITEMPOSITION, with the buffer staged inside Explorer  (exact)
//   2. a cached layout from the last time (1) succeeded             (exact, stale)
//   3. a grid derived from the live ListView cell metrics           (approximate)
//
// (3) is genuinely close: the cell size comes from the shell, so the anchors land on
// the same lattice the icons occupy. Anything is better than nothing - a scene that
// silently draws no anchors looks broken.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace lp {

struct IconAnchor {
    float x = 0, y = 0;      // wallpaper-local pixels
    float size = 48.0f;      // nominal icon cell extent
    bool  real = false;      // true when read from the shell (or its cache)
};

enum class AnchorSource { None, Shell, Cache, Grid };

class IconAnchors {
public:
    // `originX/originY` is the wallpaper window's top-left in virtual-screen space.
    bool Refresh(HWND wallpaperWindow, int originX, int originY, int width, int height);

    const std::vector<IconAnchor>& Anchors() const { return m_anchors; }
    AnchorSource Source() const { return m_source; }
    const wchar_t* SourceName() const;
    // Bumped when the anchor set changes, so scenes can rebuild connectivity.
    unsigned Revision() const { return m_revision; }

    // Persisted layout cache, keyed by nothing in particular - one desktop, one cache.
    void SetCachePath(const std::wstring& path) { m_cachePath = path; }
    bool LoadCache();
    bool SaveCache() const;

private:
    std::vector<IconAnchor> m_anchors;
    std::wstring m_cachePath;
    AnchorSource m_source = AnchorSource::None;
    unsigned m_revision = 0;
    size_t m_lastCount = 0;
    float m_cellW = 0, m_cellH = 0;

    bool QueryShell(HWND listView, int originX, int originY);
    void BuildGrid(HWND listView, int width, int height);
    void Touch();
};

} // namespace lp
