#include "Config.h"
#include "Log.h"
#include "Math.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cstdlib>

namespace lp {

namespace {

struct SceneInfo { SceneId id; const wchar_t* key; const wchar_t* name; };

// Order matters: this array is indexed by (int)SceneId, so it must stay in sync
// with the enum. SceneName/SceneKey assert that at runtime in debug builds.
const SceneInfo kScenes[] = {
    { SceneId::Lines,     L"lines",     L"Lines & Connections" },
    { SceneId::Cyberpunk, L"cyberpunk", L"Cyberpunk City" },
    { SceneId::Nature,    L"nature",    L"Nature" },
    { SceneId::Beach,     L"beach",     L"Beach" },
    { SceneId::Anime,     L"anime",     L"Anime Sakura" },
    { SceneId::Aurora,    L"aurora",    L"Aurora" },
    { SceneId::Nebula,    L"nebula",    L"Nebula" },
    { SceneId::Alpine,    L"alpine",    L"Alpine Peaks" },
    { SceneId::Fireflies, L"fireflies", L"Fireflies" },
    { SceneId::Mesh,      L"mesh",      L"Flow Mesh" },
    { SceneId::Ocean,     L"ocean",     L"Deep Ocean" },
    { SceneId::Matrix,    L"matrix",    L"Digital Rain" },
    { SceneId::Koi,       L"koi",       L"Koi Pond" },
    { SceneId::Lava,      L"lava",      L"Lava Flow" },
    { SceneId::Snow,      L"snow",      L"Snowfall" },
    { SceneId::Rain,      L"rain",      L"Rain on Glass" },
    { SceneId::Desert, L"desert", L"Golden Dunes" },
    { SceneId::Storm, L"storm", L"Thunderstorm" },
    { SceneId::Fireworks, L"fireworks", L"Fireworks" },
    { SceneId::Galaxy, L"galaxy", L"Spiral Galaxy" },
    { SceneId::Crystal, L"crystal", L"Crystal Cave" },
    { SceneId::Gears, L"gears", L"Clockwork" },
    { SceneId::Balloons, L"balloons", L"Balloons" },
    { SceneId::Lighthouse, L"lighthouse", L"Lighthouse" },
    { SceneId::Savanna, L"savanna", L"Savanna Dusk" },
    { SceneId::Silk, L"silk", L"Satin Flow" },
    { SceneId::BlackHole, L"blackhole", L"Black Hole" },
    { SceneId::Kaleido, L"kaleido", L"Kaleidoscope" },
    { SceneId::Flow, L"flow", L"Flow Field" },
    { SceneId::Plasma, L"plasma", L"Plasma Ball" },
    { SceneId::Bloom, L"bloom", L"Bloom" },
    { SceneId::Strata, L"strata", L"Strata" },
    { SceneId::Shards, L"shards", L"Shards" },
    { SceneId::Halos, L"halos", L"Halos" },
    { SceneId::Hive, L"hive", L"Hive" },
    { SceneId::Weave, L"weave", L"Weave" },
};
static_assert(sizeof(kScenes) / sizeof(kScenes[0]) == (size_t)SceneId::Count,
              "kScenes must have exactly one entry per SceneId");

std::wstring Trim(const std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return L"";
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

const wchar_t* SceneName(SceneId id) {
    int i = (int)id;
    if (i < 0 || i >= (int)SceneId::Count) i = 0;
    return kScenes[i].name;
}

const wchar_t* SceneKey(SceneId id) {
    int i = (int)id;
    if (i < 0 || i >= (int)SceneId::Count) i = 0;
    return kScenes[i].key;
}

SceneId SceneFromKey(const wchar_t* key, bool* found) {
    if (key) {
        for (const auto& s : kScenes) {
            if (_wcsicmp(s.key, key) == 0) { if (found) *found = true; return s.id; }
            if (_wcsicmp(s.name, key) == 0) { if (found) *found = true; return s.id; }
        }
    }
    if (found) *found = false;
    return SceneId::Lines;
}

Config Config::Defaults() {
    Config c;
    c.scene = SceneId::Lines;
    c.targetFps = 30;
    c.quality = Quality::Balanced;
    return c;
}

std::wstring Config::FilePath() {
    wchar_t* appdata = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata)) && appdata) {
        dir = appdata;
        CoTaskMemFree(appdata);
    } else {
        wchar_t buf[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        dir = (n && n < MAX_PATH) ? buf : L".";
    }
    dir += L"\\LivePaper";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\config.ini";
}

void Config::Normalize() {
    if ((int)scene < 0 || (int)scene >= (int)SceneId::Count) scene = SceneId::Lines;
    if ((int)quality < 0 || (int)quality > (int)Quality::High) quality = Quality::Balanced;
    if (targetFps != 0) targetFps = (int)Clamp((float)targetFps, 5.0f, 240.0f);
    dim = Clamp(dim, 0.0f, 0.6f);
    variation &= 0x00FFFFFFu;   // plenty of reshuffles, and never a sign bit surprise
    customColor &= 0x00FFFFFFu;
    if (cycleMinutes < 0) cycleMinutes = 0;
    if (cycleMinutes > 240) cycleMinutes = 240;
    for (float& p : sceneParam) p = Clamp(p, 0.0f, 1.0f);
}

Config Config::Load() {
    Config c = Defaults();
    std::wstring path = FilePath();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f) {
        LP_LOGI(L"config: no file at %ls, using defaults", path.c_str());
        return c;
    }

    wchar_t line[512];
    while (fgetws(line, _countof(line), f)) {
        std::wstring s = Trim(line);
        if (s.empty() || s[0] == L'#' || s[0] == L';' || s[0] == L'[') continue;
        size_t eq = s.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = Trim(s.substr(0, eq));
        std::wstring v = Trim(s.substr(eq + 1));

        if (k == L"scene")       { bool found = false; c.scene = SceneFromKey(v.c_str(), &found); }
        else if (k == L"fps")          c.targetFps = _wtoi(v.c_str());
        else if (k == L"quality")      c.quality = (Quality)_wtoi(v.c_str());
        else if (k == L"dim")          c.dim = (float)_wtof(v.c_str());
        else if (k == L"variation")    c.variation = (unsigned)_wtol(v.c_str());
        else if (k == L"useCustomColor") c.useCustomColor = (_wtoi(v.c_str()) != 0);
        else if (k == L"customColor")  c.customColor = (unsigned)wcstoul(v.c_str(), nullptr, 16) & 0xFFFFFFu;
        else if (k == L"cycleMinutes") c.cycleMinutes = _wtoi(v.c_str());
        else if (k == L"pauseFullscreen") c.pauseWhenFullscreen = (_wtoi(v.c_str()) != 0);
        else if (k == L"pauseBattery")    c.pauseOnBattery = (_wtoi(v.c_str()) != 0);
        else if (k == L"pauseLocked")     c.pauseWhenLocked = (_wtoi(v.c_str()) != 0);
        else if (k == L"pauseOccluded")   c.pauseWhenOccluded = (_wtoi(v.c_str()) != 0);
        else if (k == L"spanAll")         c.spanAllMonitors = (_wtoi(v.c_str()) != 0);
        else if (k == L"startWithWindows")c.startWithWindows = (_wtoi(v.c_str()) != 0);
        else if (k.rfind(L"p", 0) == 0 && k.size() >= 2 && iswdigit(k[1])) {
            int idx = _wtoi(k.c_str() + 1);
            if (idx >= 0 && idx < 8) c.sceneParam[idx] = (float)_wtof(v.c_str());
        }
    }
    fclose(f);
    c.Normalize();
    LP_LOGI(L"config: loaded scene=%ls fps=%d quality=%d", SceneKey(c.scene), c.targetFps, (int)c.quality);
    return c;
}

bool Config::Save() const {
    std::wstring path = FilePath();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) {
        LP_LOGE(L"config: cannot write %ls", path.c_str());
        return false;
    }
    fwprintf(f, L"# LivePaper configuration\n");
    // %ls (not %s): with a wide printf, %s takes a NARROW string and stops at the
    // first NUL byte, so a wide key would be truncated to a single character.
    fwprintf(f, L"scene=%ls\n", SceneKey(scene));
    fwprintf(f, L"fps=%d\n", targetFps);
    fwprintf(f, L"quality=%d\n", (int)quality);
    fwprintf(f, L"dim=%.3f\n", dim);
    fwprintf(f, L"variation=%u\n", variation);
    fwprintf(f, L"pauseFullscreen=%d\n", pauseWhenFullscreen ? 1 : 0);
    fwprintf(f, L"pauseBattery=%d\n", pauseOnBattery ? 1 : 0);
    fwprintf(f, L"pauseLocked=%d\n", pauseWhenLocked ? 1 : 0);
    fwprintf(f, L"pauseOccluded=%d\n", pauseWhenOccluded ? 1 : 0);
    fwprintf(f, L"spanAll=%d\n", spanAllMonitors ? 1 : 0);
    fwprintf(f, L"startWithWindows=%d\n", startWithWindows ? 1 : 0);
    for (int i = 0; i < 8; ++i) fwprintf(f, L"p%d=%.3f\n", i, sceneParam[i]);
    fclose(f);
    LP_LOGI(L"config: saved %ls", path.c_str());
    return true;
}

} // namespace lp
