// Live Shan Shui - configuration. Stored as a small key=value file under %APPDATA%iveshanshui.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace lp {

// Quality drives particle counts and internal render scale. The engine keeps the
// frame budget, so higher quality trades detail for nothing else.
enum class Quality : int { Low = 0, Balanced, High };

struct Config {
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

    // Shan shui painting tunables, wired to the panel's sliders:
    //   p0: scroll speed   p1: mist amount   p2: tree/vegetation density   p3: ink strength
    float sceneParam[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };

    // Extra darkening applied to the whole frame, 0..0.6.
    float dim = 0.0f;

    // World seed. Incremented by `--reshuffle` (and the tray menu); the whole
    // landscape is a deterministic function of it.
    unsigned variation = 0;

    // One global custom color (0xRRGGBB) that tints the ink wash and paper.
    bool useCustomColor = false;
    unsigned customColor = 0x4DD0E1;

    // Desktop pet (tamagotchi companion above the taskbar).
    bool petEnabled = true;
    bool petCard = false;       // dark card behind the pet (off = transparent)
    std::wstring petKind = L"cat";   // PetKind key; unknown values fall back to "cat"

    static Config Defaults();
    static std::wstring FilePath();
    static Config Load();
    bool Save() const;

    // Applies quality-dependent ceilings (clamped on load and on change).
    void Normalize();
};

} // namespace lp
