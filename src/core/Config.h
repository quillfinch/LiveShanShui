// LivePaper - configuration. Stored as a small key=value file under %APPDATA%\LivePaper.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace lp {

// Scenes are identified by a stable short name so configs stay valid across builds.
enum class SceneId : int {
    Lines = 0,
    Cyberpunk,
    Nature,
    Beach,
    Anime,
    Aurora,
    Nebula,
    Alpine,
    Fireflies,
    Mesh,
    Ocean,
    Matrix,
    Koi,
    Lava,
    Snow,
    Rain,
    Desert,
    Storm,
    Fireworks,
    Galaxy,
    Crystal,
    Gears,
    Balloons,
    Lighthouse,
    Savanna,
    Silk,
    BlackHole,
    Kaleido,
    Flow,
    Plasma,
    Bloom,
    Strata,
    Shards,
    Halos,
    Hive,
    Weave,
    Count
};

const wchar_t* SceneName(SceneId id);
const wchar_t* SceneKey(SceneId id);
SceneId SceneFromKey(const wchar_t* key, bool* found = nullptr);

// Quality drives particle counts and internal render scale. The engine keeps the
// frame budget, so higher quality trades detail for nothing else.
enum class Quality : int { Low = 0, Balanced, High };

struct Config {
    SceneId scene = SceneId::Lines;

    // Frame pacing. 0 = uncapped (follows the compositor), otherwise the waitable
    // swap chain paces us and we sleep off the remainder.
    int  targetFps = 30;
    bool pauseWhenFullscreen = true;   // stop drawing behind exclusive-fullscreen apps
    bool pauseOnBattery = true;        // stop drawing on battery to save power
    bool pauseWhenLocked = true;       // stop drawing while the session is locked
    bool pauseWhenOccluded = true;     // stop when a fullscreen window covers the desktop
    bool spanAllMonitors = true;       // one wallpaper across the whole virtual desktop
    bool startWithWindows = false;
    Quality quality = Quality::Balanced;

    // Per-scene tunables. Scenes read the ones they care about; unused entries are inert.
    // Lines: density, speed, glow, hue
    // Cyberpunk: rain, neon, glow, speed
    // Nature: petals, wind, glow, hue
    // Beach: waves, wind, glow, hue
    // Anime: petals, speed, glow, hue
    float sceneParam[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };

    // Wallpaper the engine should show before the first frame settles.
    float dim = 0.0f;                  // extra darkening applied to the whole scene 0..0.6

    // Scene layout variation. Incremented by `--reshuffle` (and the tray menu);
    // scenes fold it into their RNG seeds, so each value is a new deterministic look.
    unsigned variation = 0;

    // One global custom color (0xRRGGBB). Scenes that report SupportsCustomColor()
    // derive their whole palette from it when enabled; otherwise palettes come
    // from the variation seed.
    bool useCustomColor = false;
    unsigned customColor = 0x4DD0E1;

    // Auto-cycle: advance to the next wallpaper every N minutes. 0 = off.
    int cycleMinutes = 0;

    static Config Defaults();
    static std::wstring FilePath();
    static Config Load();
    bool Save() const;

    // Applies quality-dependent ceilings (clamped on load and on change).
    void Normalize();
};

} // namespace lp
