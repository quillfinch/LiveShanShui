// LivePaper - application entry point and engine loop.
//
// One executable, three roles (see App.h). The engine role owns a single worker-free
// design: everything happens on one thread with a PeekMessage loop, so there is no
// cross-thread synchronisation, no shared state to lock, and no extra stacks.
#include "App.h"
#include "WallpaperHost.h"
#include "IconAnchors.h"
#include "../core/Config.h"
#include "../core/Log.h"
#include "../core/Math.h"
#include "../core/Win32Compat.h"
#include "../gfx/GfxDevice.h"
#include "../gfx/Scene.h"
#include "../ui/Panel.h"
#include "../ui/Tray.h"
#include "../ipc/Ipc.h"
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <memory>

using namespace lp::abi;

namespace lp {

namespace {

constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT kPollIntervalMs = 400;       // power/session/desktop watch
constexpr UINT_PTR kWatchTimer = 2;
constexpr int  kHotkeyTogglePanel = 1;
constexpr int  kHotkeyNextScene = 2;
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"LivePaper";

// ---------------------------------------------------------------------------
// command line
// ---------------------------------------------------------------------------

struct Options {
    enum class Mode { Engine, Help, Version, List, Selftest, Capture, Status,
                      SetScene, Next, Prev, Pause, Resume, Toggle, Quit, Reload,
                      Install, Uninstall, SetFps, SetQuality, Render, Diag, Screencap,
                      Reshuffle, SetCycle };
    Mode mode = Mode::Engine;
    SceneId scene = SceneId::Lines;
    int fps = -1;
    Quality quality = Quality::Balanced;
    bool verbose = false;
    bool noPanel = false;
    int cycleMinutes = -1;
    std::wstring capturePath;
    bool badArg = false;
    std::wstring badArgText;

    // --render: draw a wallpaper to a PNG without needing the desktop host.
    SceneId renderScene = SceneId::Lines;
    std::wstring renderPath = L"wallpaper.png";
    uint32_t renderWidth = 0, renderHeight = 0;
    bool renderUseEngine = true;   // prefer whatever the running engine is showing
};

// Writes one line to the console.
//
// The console is attached lazily and then kept for the process lifetime. An earlier
// version attached and freed the console around every line, which made the output
// arrive interleaved and truncated (recursive output redirect in particular could not
// keep up with the repeated handle churn).
void Print(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list a; va_start(a, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, a);
    va_end(a);

    static bool consoleReady = false;
    if (!consoleReady) {
        // Reuse an inherited console if the shell already gave us one, otherwise
        // attach to the parent, and only create a new one as a last resort. The
        // handle is intentionally never released: the process exits shortly after.
        if (!GetConsoleWindow()) {
            if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        }
        consoleReady = true;
    }

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || !out) return;

    char utf8[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8) - 2, nullptr, nullptr);
    if (n > 0) {
        utf8[n - 1] = '\n';
        DWORD written = 0;
        WriteFile(out, utf8, (DWORD)n, &written, nullptr);
    }
}

void PrintHelp() {
    Print(LR"HELP(LivePaper - a lightweight live wallpaper engine for Windows

USAGE
  LivePaper                       Start the engine (tray icon + control panel)
  LivePaper --scene <name>        Switch wallpaper on the running engine
  LivePaper --next | --prev       Cycle wallpapers
  LivePaper --reshuffle           Re-roll the current wallpaper's layout
  LivePaper --pause | --resume    Pause / resume animation
  LivePaper --toggle              Toggle pause (global hotkey: Ctrl+Alt+P)
  LivePaper --fps <n|max>         Set the frame-rate cap
  LivePaper --quality <low|balanced|high>
  LivePaper --cycle <min|off>     Auto-switch wallpaper every N minutes
  LivePaper --reload              Re-attach to the desktop after an Explorer restart
  LivePaper --quit                Stop the engine
  LivePaper --status              Report whether an engine is running
  LivePaper --list                List the available wallpapers
  LivePaper --install             Start with Windows
  LivePaper --uninstall           Remove the startup entry

DIAGNOSTICS
  LivePaper --selftest            Render every wallpaper and validate the result
  LivePaper --diag                Explain what the engine sees (use when nothing moves)
  LivePaper --screencap [file.png]  Capture the real desktop at physical resolution
  LivePaper --render [scene] [file.png]
                                  Render one wallpaper to a PNG, no desktop needed
  LivePaper --capture [file.png]  Save the current wallpaper to a PNG
  LivePaper --verbose             Write a log to %LOCALAPPDATA%\LivePaper\livepaper.log

WALLPAPERS
  lines, cyberpunk, nature, beach, anime, aurora, nebula, alpine,
  fireflies, mesh, ocean, matrix, koi, lava, snow, rain, desert,
  storm, fireworks, galaxy, crystal, gears, balloons, lighthouse,
  savanna, silk, blackhole, kaleido, flow, plasma
  (run --list for descriptions)
)HELP");
}

bool ParseArgs(int argc, wchar_t** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto need = [&](const wchar_t* name) -> const wchar_t* {
            if (i + 1 >= argc) { opt.badArg = true; opt.badArgText = name; return nullptr; }
            return argv[++i];
        };

        if (a == L"--verbose" || a == L"-v") opt.verbose = true;
        else if (a == L"--no-panel") opt.noPanel = true;
        else if (a == L"--help" || a == L"-h" || a == L"/?") opt.mode = Options::Mode::Help;
        else if (a == L"--version") opt.mode = Options::Mode::Version;
        else if (a == L"--list") opt.mode = Options::Mode::List;
        else if (a == L"--selftest") opt.mode = Options::Mode::Selftest;
        else if (a == L"--diag" || a == L"--diagnose") opt.mode = Options::Mode::Diag;
        else if (a == L"--screencap") {
            opt.mode = Options::Mode::Screencap;
            if (i + 1 < argc && argv[i + 1][0] != L'-') opt.capturePath = argv[++i];
            else opt.capturePath = L"screencap.png";
        }
        else if (a == L"--render") {
            opt.mode = Options::Mode::Render;
            // Optional scene name, then an optional output path.
            if (i + 1 < argc && argv[i + 1][0] != L'-') {
                bool found = false;
                SceneId s = SceneFromKey(argv[i + 1], &found);
                if (found) { opt.renderScene = s; opt.renderUseEngine = false; ++i; }
            }
            if (i + 1 < argc && argv[i + 1][0] != L'-') {
                opt.renderPath = argv[++i];
            } else {
                opt.renderPath = std::wstring(L"wallpaper-") + SceneKey(opt.renderScene) + L".png";
            }
        }
        else if (a == L"--status") opt.mode = Options::Mode::Status;
        else if (a == L"--next") opt.mode = Options::Mode::Next;
        else if (a == L"--prev") opt.mode = Options::Mode::Prev;
        else if (a == L"--reshuffle" || a == L"--shuffle") opt.mode = Options::Mode::Reshuffle;
        else if (a == L"--cycle") {
            const wchar_t* v = need(L"--cycle");
            if (!v) return false;
            opt.cycleMinutes = (_wcsicmp(v, L"off") == 0 || _wcsicmp(v, L"0") == 0) ? 0 : _wtoi(v);
            opt.mode = Options::Mode::SetCycle;
        }
        else if (a == L"--pause") opt.mode = Options::Mode::Pause;
        else if (a == L"--resume") opt.mode = Options::Mode::Resume;
        else if (a == L"--toggle") opt.mode = Options::Mode::Toggle;
        else if (a == L"--quit" || a == L"--exit") opt.mode = Options::Mode::Quit;
        else if (a == L"--reload") opt.mode = Options::Mode::Reload;
        else if (a == L"--install") opt.mode = Options::Mode::Install;
        else if (a == L"--uninstall") opt.mode = Options::Mode::Uninstall;
        else if (a == L"--capture") {
            opt.mode = Options::Mode::Capture;
            // Optional filename.
            if (i + 1 < argc && argv[i + 1][0] != L'-') opt.capturePath = argv[++i];
        }
        else if (a == L"--scene") {
            const wchar_t* v = need(L"--scene");
            if (!v) return false;
            bool found = false;
            opt.scene = SceneFromKey(v, &found);
            opt.mode = Options::Mode::SetScene;
            if (!found) { opt.badArg = true; opt.badArgText = v; return false; }
        }
        else if (a == L"--fps") {
            const wchar_t* v = need(L"--fps");
            if (!v) return false;
            opt.fps = (_wcsicmp(v, L"max") == 0 || _wcsicmp(v, L"0") == 0) ? 0 : _wtoi(v);
            opt.mode = Options::Mode::SetFps;
        }
        else if (a == L"--quality") {
            const wchar_t* v = need(L"--quality");
            if (!v) return false;
            if (_wcsicmp(v, L"low") == 0) opt.quality = Quality::Low;
            else if (_wcsicmp(v, L"high") == 0) opt.quality = Quality::High;
            else opt.quality = Quality::Balanced;
            opt.mode = Options::Mode::SetQuality;
        }
        else {
            // A bare scene name is a convenience: `LivePaper nature`.
            bool found = false;
            SceneId s = SceneFromKey(a.c_str(), &found);
            if (found) { opt.scene = s; opt.mode = Options::Mode::SetScene; }
            else { opt.badArg = true; opt.badArgText = a; return false; }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// autostart
// ---------------------------------------------------------------------------

std::wstring ExecutablePath() {
    wchar_t buf[MAX_PATH * 2]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, _countof(buf));
    return (n && n < _countof(buf)) ? std::wstring(buf, n) : std::wstring();
}

bool SetAutostart(bool enable) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        LP_LOGE(L"autostart: cannot open Run key");
        return false;
    }
    bool ok = false;
    if (enable) {
        std::wstring cmd = L"\"" + ExecutablePath() + L"\"";
        // REG_SZ so the shell can invoke it directly.
        ok = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                            (const BYTE*)cmd.c_str(),
                            (DWORD)((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        LSTATUS st = RegDeleteValueW(key, kRunValue);
        ok = (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(key);
    LP_LOGI(L"autostart: %ls -> %d", enable ? L"enable" : L"disable", ok ? 1 : 0);
    return ok;
}

bool IsAutostartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    wchar_t buf[MAX_PATH * 2]{};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    bool present = RegQueryValueExW(key, kRunValue, nullptr, &type, (BYTE*)buf, &size) == ERROR_SUCCESS;
    RegCloseKey(key);
    return present;
}

// ---------------------------------------------------------------------------
// scene accents (shared with the tray icon)
// ---------------------------------------------------------------------------

COLORREF SceneAccent(SceneId id) {
    switch (id) {
        case SceneId::Lines:     return RGB(0x4d, 0xd0, 0xe1);
        case SceneId::Cyberpunk: return RGB(0xff, 0x2d, 0x95);
        case SceneId::Nature:    return RGB(0x7e, 0xc8, 0x50);
        case SceneId::Beach:     return RGB(0x59, 0xd3, 0xe8);
        case SceneId::Anime:     return RGB(0xff, 0xb7, 0xd5);
        case SceneId::Aurora:    return RGB(0x38, 0xef, 0x7d);
        case SceneId::Nebula:    return RGB(0xff, 0x5e, 0xcf);
        case SceneId::Alpine:    return RGB(0xdf, 0xe8, 0xff);
        case SceneId::Fireflies: return RGB(0xff, 0xd8, 0x73);
        case SceneId::Mesh:      return RGB(0x6a, 0x7b, 0xff);
        case SceneId::Ocean:     return RGB(0x2f, 0xbc, 0xdf);
        case SceneId::Matrix:    return RGB(0x39, 0xff, 0x6e);
        case SceneId::Koi:       return RGB(0xff, 0xa0, 0x4d);
        case SceneId::Lava:      return RGB(0xff, 0x5a, 0x1f);
        case SceneId::Snow:      return RGB(0x9f, 0xd4, 0xff);
        case SceneId::Rain:      return RGB(0x6e, 0xa8, 0xff);
        case SceneId::Desert: return RGB(0xec, 0xc9, 0x4b);
        case SceneId::Storm: return RGB(0x9d, 0xb4, 0xff);
        case SceneId::Fireworks: return RGB(0xff, 0x5c, 0x5c);
        case SceneId::Galaxy: return RGB(0xc9, 0xa2, 0xff);
        case SceneId::Crystal: return RGB(0x8f, 0xf2, 0xe0);
        case SceneId::Gears: return RGB(0xa9, 0x71, 0x3c);
        case SceneId::Balloons: return RGB(0xff, 0x6f, 0x91);
        case SceneId::Lighthouse: return RGB(0xff, 0xf1, 0xb8);
        case SceneId::Savanna: return RGB(0xd9, 0x6c, 0x4f);
        case SceneId::Silk: return RGB(0x66, 0xd9, 0xc2);
        case SceneId::BlackHole: return RGB(0xff, 0xb0, 0x5e);
        case SceneId::Kaleido: return RGB(0xff, 0x9d, 0xe2);
        case SceneId::Flow: return RGB(0x4d, 0xe8, 0xb0);
        case SceneId::Plasma: return RGB(0xa8, 0x8f, 0xff);
        case SceneId::Bloom: return RGB(0xff, 0x8a, 0xd4);
        case SceneId::Strata: return RGB(0x5a, 0xc8, 0xff);
        case SceneId::Shards: return RGB(0x8f, 0x7b, 0xff);
        case SceneId::Halos: return RGB(0x7d, 0xe0, 0xff);
        case SceneId::Hive: return RGB(0xa8, 0xe0, 0x5f);
        case SceneId::Weave: return RGB(0xd0, 0x8a, 0x5f);
        default:                 return RGB(0x4d, 0xd0, 0xe1);
    }
}

float QualitySceneScale(Quality q) {
    switch (q) {
        case Quality::Low:  return 0.72f;
        case Quality::High: return 1.0f;
        default:            return 0.85f;
    }
}

float QualityDensity(Quality q) {
    switch (q) {
        case Quality::Low:  return 0.55f;
        case Quality::High: return 1.30f;
        default:            return 1.0f;
    }
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

class Engine {
public:
    int Run(const Options& opt);

private:
    static LRESULT CALLBACK EngineProc(HWND, UINT, WPARAM, LPARAM);

    bool Startup(const Options& opt);
    void Shutdown();

    bool EnsureWallpaper();          // attach + resize the device
    void ApplyConfig(bool save);
    void SwitchScene(SceneId id);
    void NextScene(int delta);
    void Reshuffle();

    SceneCtx BaseCtx();              // fills the context every call site shares

    void TickFrame();
    void TickWatch();

    bool ShouldPause();
    void UpdateTray();
    void DumpStateToFile();

    // --- state -------------------------------------------------------------
    HWND m_window = nullptr;
    WallpaperHost m_host;
    IconAnchors m_icons;
    gfx::Device m_device;
    std::unique_ptr<Panel> m_panel;
    Tray m_tray;

    std::unique_ptr<Scene> m_scene;
    SceneId m_sceneId = SceneId::Lines;
    Config m_config;
    unsigned m_appliedVariation = 0;   // variation the live scene was built with
    float m_cycleSeconds = 0;          // auto-cycle accumulator
    bool m_paused = false;
    bool m_userPaused = false;
    bool m_suspended = false;
    bool m_occluded = false;         // desktop fully covered by other windows (polled)
    const wchar_t* m_pauseReason = L"";

    float m_sceneTime = 0;
    float m_lastFrameTime = 0;
    unsigned m_iconRevision = 0;
    std::wstring m_captureRequest;
    bool m_verbose = false;
    bool m_running = true;
    int  m_framesDrawn = 0;

    // Adaptive quality: if we cannot hold the frame budget, drop render scale.
    float m_adaptiveScale = 1.0f;
    int   m_slowFrames = 0;
    int   m_fastFrames = 0;
};

Engine* g_engine = nullptr;   // single instance by construction

LRESULT CALLBACK Engine::EngineProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Engine* e = g_engine;
    if (!e) return DefWindowProcW(hwnd, msg, wp, lp);
    // m_window is assigned in Startup() by the creator. It deliberately is NOT set
    // from a WM_CREATE branch here: CreateEngineWindow creates the window with
    // DefWindowProcW and only then swaps in this procedure, so WM_CREATE has already
    // been delivered by the time we could handle it.

    // --- tray ---
    {
        LRESULT r = 0;
        if (e->m_tray.HandleMessage(msg, wp, lp, &r)) return r;
    }

    // --- panel ---
    if (e->m_panel) {
        LRESULT r = 0;
        if (e->m_panel->HandleMessage(msg, wp, lp, &r)) return r;
    }

    // --- IPC commands ---
    if (msg >= WM_APP + 0x100 && msg <= WM_APP + 0x120) {
        auto cmd = (ipc::Command)(msg - (WM_APP + 0x100));
        switch (cmd) {
            case ipc::Command::ShowPanel:
                if (e->m_panel->Window() && !e->m_panel->IsVisible()) e->m_panel->Show();
                break;
            case ipc::Command::TogglePause:
                e->m_userPaused = !e->m_userPaused;
                e->UpdateTray();
                break;
            case ipc::Command::NextScene: e->NextScene(1); break;
            case ipc::Command::PrevScene: e->NextScene(-1); break;
            case ipc::Command::Reshuffle: e->Reshuffle(); break;
            case ipc::Command::SetScene:
                e->SwitchScene((SceneId)lp);
                e->m_config.Save();
                break;
            case ipc::Command::ApplyConfig:
                e->m_config = Config::Load();
                e->ApplyConfig(true);
                break;
            case ipc::Command::Reload: {
                e->m_host.Detach();
                e->m_icons.Refresh(nullptr, 0, 0, 0, 0);
                e->EnsureWallpaper();
                break;
            }
            case ipc::Command::Capture:
                if (e->m_captureRequest.empty()) {
                    wchar_t path[MAX_PATH]{};
                    wchar_t* local = nullptr;
                    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &local)) && local) {
                        swprintf_s(path, L"%ls\\LivePaper-%ls.png", local, SceneKey(e->m_sceneId));
                        CoTaskMemFree(local);
                    } else {
                        swprintf_s(path, L"LivePaper-%ls.png", SceneKey(e->m_sceneId));
                    }
                    e->m_captureRequest = path;
                }
                break;
            case ipc::Command::Quit:
                e->m_running = false;
                PostQuitMessage(0);
                break;
            case ipc::Command::ReportState:
            case ipc::Command::Poll:
                // Reply with the live scene so a CLI caller can report the truth
                // rather than guessing from the config file.
                return (LRESULT)(unsigned)e->m_sceneId + 1;
            case ipc::Command::DumpState:
                // Written to a file rather than a shared buffer: the CLI runs in a
                // different process and this keeps the hand-off trivially reliable.
                e->DumpStateToFile();
                return 1;
            default: break;
        }
        return 0;
    }

    switch (msg) {
        case WM_TIMER:
            if (wp == kFrameTimer) { e->TickFrame(); return 0; }
            if (wp == kWatchTimer) { e->TickWatch(); return 0; }
            break;
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            // Monitors changed: rebuild the host window and the device.
            PostMessageW(hwnd, WM_APP + 0x100 + (UINT)ipc::Command::Reload, 0, 0);
            return 0;
        case WM_ENDSESSION:
        case WM_QUERYENDSESSION:
            e->m_running = false;
            return TRUE;
        case WM_DESTROY:
            e->m_running = false;
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Engine::Startup(const Options& opt) {
    m_verbose = opt.verbose;
    m_config = Config::Load();
    m_config.Normalize();
    m_userPaused = false;

    // --- message-only control window ---------------------------------------
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!ipc::RegisterWindowClass(inst)) return false;
    m_window = ipc::CreateEngineWindow(inst, EngineProc, nullptr);
    if (!m_window) return false;
    // EngineProc is installed on m_window by CreateEngineWindow, so every later
    // message (timers, tray, IPC, hotkeys) reaches this instance.
    g_engine = this;

    // --- tray + hotkeys -----------------------------------------------------
    m_tray.Create(m_window, [this](TrayCommand c) {
        switch (c) {
            case TrayCommand::TogglePanel:
                if (!m_panel) break;
                if (m_panel->IsVisible()) m_panel->Hide();
                else m_panel->Show();
                break;
            case TrayCommand::NextScene: NextScene(1); break;
            case TrayCommand::Reshuffle: Reshuffle(); break;
            case TrayCommand::PauseToggle:
                m_userPaused = !m_userPaused;
                UpdateTray();
                break;
            case TrayCommand::Reload:
                m_host.Detach();
                EnsureWallpaper();
                break;
            case TrayCommand::OpenConfig: {
                std::wstring p = Config::FilePath();
                std::wstring dir = p.substr(0, p.find_last_of(L'\\'));
                ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            }
            case TrayCommand::Quit:
                m_running = false;
                PostQuitMessage(0);
                break;
        }
    });
    m_tray.SetTooltip(L"LivePaper");
    // Ctrl+Alt+P toggles pause, Ctrl+Alt+N advances the wallpaper.
    RegisterHotKey(m_window, kHotkeyTogglePanel, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'P');
    RegisterHotKey(m_window, kHotkeyNextScene, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'N');

    // --- control panel (lazy: only when first shown) ------------------------
    m_panel.reset(new Panel());
    Panel::Callbacks cb;
    cb.onScene = [this](SceneId s) { SwitchScene(s); m_config.Save(); UpdateTray(); };
    cb.onConfig = [this](const Config& c) {
        m_config = c;
        // startWithWindows is a side effect of the toggle, not render state.
        if (m_config.startWithWindows != IsAutostartEnabled())
            SetAutostart(m_config.startWithWindows);
        ApplyConfig(true);
    };
    cb.onShuffle = [this] { Reshuffle(); };
    cb.onClose = [] {};
    if (!m_panel->Create(inst, cb)) {
        LP_LOGW(L"engine: control panel unavailable");
        m_panel.reset();
    }

    // --- wallpaper ----------------------------------------------------------
    m_icons.SetCachePath(L"");
    if (!EnsureWallpaper()) return false;

    m_sceneId = m_config.scene;
    SwitchScene(m_sceneId);

    SetTimer(m_window, kWatchTimer, kPollIntervalMs, nullptr);
    UpdateTray();

    LP_LOGI(L"engine: started, scene=%ls fps=%d quality=%d", SceneKey(m_sceneId),
            m_config.targetFps, (int)m_config.quality);
    return true;
}

void Engine::Shutdown() {
    if (m_window) {
        KillTimer(m_window, kFrameTimer);
        KillTimer(m_window, kWatchTimer);
        UnregisterHotKey(m_window, kHotkeyTogglePanel);
        UnregisterHotKey(m_window, kHotkeyNextScene);
    }
    if (m_panel) m_panel->Destroy();
    m_tray.Destroy();
    m_scene.reset();
    m_device.Destroy();
    m_host.Detach();
    if (m_window) {
        DestroyWindow(m_window);
        m_window = nullptr;
    }
    ipc::UnregisterWindowClass(GetModuleHandleW(nullptr));
    LP_LOGI(L"engine: stopped");
}

bool Engine::EnsureWallpaper() {
    if (!m_host.Attach()) {
        LP_LOGE(L"engine: could not attach to the desktop");
        if (m_tray.Window()) m_tray.Balloon(L"LivePaper", L"Could not attach to the desktop.");
        return false;
    }

    // Attach() has already sized and positioned the window to the host's client area.
    // Use that same geometry for the swap chain so the backdrop matches exactly.
    DesktopRect r = m_host.HostClientRect();
    m_host.Layout(r);

    gfx::DeviceDesc desc;
    desc.hwnd = m_host.Window();
    desc.width = (uint32_t)r.width;
    desc.height = (uint32_t)r.height;
    desc.sceneScale = QualitySceneScale(m_config.quality) * m_adaptiveScale;
    desc.allowSoftware = true;

    if (!m_device.Create(desc)) {
        LP_LOGE(L"engine: render device creation failed");
        m_host.Detach();
        if (m_tray.Window())
            m_tray.Balloon(L"LivePaper", L"Could not create a Direct3D device.", NIIF_ERROR);
        return false;
    }

    // Icon anchors are gathered in wallpaper-local space, so the origin we subtract is
    // the wallpaper's own top-left rather than assumption about the desktop origin.
    m_icons.Refresh(m_host.Window(), r.x, r.y, r.width, r.height);
    m_iconRevision = m_icons.Revision();

    m_device.SetDim(m_config.dim);

    if (m_scene) m_scene->Configure(BaseCtx());
    return true;
}

// One place that knows what goes into a SceneCtx. Every call site (configure,
// apply, tick) used to duplicate this block; they could drift apart silently.
SceneCtx Engine::BaseCtx() {
    SceneCtx ctx{};
    ctx.device = &m_device;
    ctx.dc = m_device.Dc();
    ctx.white = m_device.White();
    ctx.width = (float)m_device.SceneWidth();
    ctx.height = (float)m_device.SceneHeight();
    ctx.sceneScale = m_device.SceneScale();
    ctx.icons = &m_icons;
    ctx.config = &m_config;
    ctx.density = QualityDensity(m_config.quality);
    ctx.variation = m_config.variation;
    ctx.useCustomColor = m_config.useCustomColor;
    ctx.customColor = Color::Hex(m_config.customColor);
    return ctx;
}

void Engine::ApplyConfig(bool save) {
    m_config.Normalize();
    if (save) m_config.Save();

    // A bumped variation (reshuffle) re-rolls the current scene in place.
    if (m_config.variation != m_appliedVariation) {
        m_appliedVariation = m_config.variation;
        m_scene.reset(CreateScene(m_sceneId));
        m_sceneTime = 0;
        if (m_scene && m_device.Dc()) m_scene->Configure(BaseCtx());
        if (m_panel) m_panel->SetState(m_config, m_paused, L"", L"");
    }

    float want = QualitySceneScale(m_config.quality) * m_adaptiveScale;
    if (std::abs(want - m_device.SceneScale()) > 0.01f) {
        m_device.SetSceneScale(want);
    }
    m_device.SetDim(m_config.dim);

    // A changed frame cap changes the timer period that drives the loop.
    if (m_window) {
        UINT periodMs = 1;
        if (m_config.targetFps > 0) {
            int interval = (int)(1000.0 / m_config.targetFps) / 2;
            periodMs = (UINT)std::max(1, std::min(interval, 16));
        }
        SetTimer(m_window, kFrameTimer, periodMs, nullptr);
    }

    if (m_scene) m_scene->Configure(BaseCtx());
}

void Engine::SwitchScene(SceneId id) {
    m_sceneId = id;
    m_config.scene = id;
    m_scene.reset(CreateScene(id));
    m_sceneTime = 0;

    m_appliedVariation = m_config.variation;
    if (m_scene && m_device.Dc()) m_scene->Configure(BaseCtx());
    if (m_panel) {
        // The panel shows the scene's own parameter names; copy them out before
        // anything can destroy the scene again.
        if (m_scene) {
            const wchar_t* names[4] = {};
            int n = std::min(m_scene->ParamCount(), 4);
            for (int i = 0; i < n; ++i) names[i] = m_scene->ParamName(i);
            m_panel->SetSceneParams(names, n, m_scene->SupportsCustomColor());
        }
        m_panel->SetState(m_config, m_paused, L"", L"");
    }
    LP_LOGI(L"engine: scene -> %ls", SceneKey(id));
}

void Engine::NextScene(int delta) {
    int count = (int)SceneId::Count;
    int next = ((int)m_sceneId + delta) % count;
    if (next < 0) next += count;
    SwitchScene((SceneId)next);
    m_config.Save();
    UpdateTray();
}

// Re-rolls the current scene's layout: bump the persisted variation seed and
// rebuild the scene, so every random-but-deterministic element gets new values.
void Engine::Reshuffle() {
    m_config.variation = (m_config.variation + 1) & 0x00FFFFFFu;
    SwitchScene(m_sceneId);
    m_config.Save();
    UpdateTray();
    LP_LOGI(L"engine: reshuffled -> variation %u", m_config.variation);
}

bool Engine::ShouldPause() {
    if (m_userPaused) { m_pauseReason = L"paused"; return true; }
    if (m_config.pauseWhenLocked && IsSessionLocked()) { m_pauseReason = L"session locked"; return true; }
    if (m_config.pauseOnBattery && IsOnBattery()) { m_pauseReason = L"on battery"; return true; }
    if (m_config.pauseWhenFullscreen && IsFullscreenAppInForeground()) {
        m_pauseReason = L"fullscreen app"; return true;
    }
    if (m_config.pauseWhenOccluded && m_occluded) { m_pauseReason = L"desktop covered"; return true; }
    return false;
}

void Engine::TickFrame() {
    if (!m_running) return;
    if (!m_host.IsAttached() || m_host.NeedsReattach()) {
        LP_LOGW(L"engine: desktop host lost, re-attaching");
        EnsureWallpaper();
        return;
    }

    bool shouldPause = ShouldPause();
    m_paused = shouldPause;

    if (shouldPause) {
        // Nothing to draw: return immediately, no swap chain wait. This is what keeps
        // the engine at ~0% CPU while a game is fullscreen or the session is locked.
        if (m_panel && m_panel->IsVisible()) {
            m_panel->Render();
        }
        return;
    }

    if (!m_device.Dc()) return;

    // A capture request is served without disturbing the running animation.
    if (!m_captureRequest.empty()) {
        bool ok = m_device.CapturePng(m_captureRequest);
        Print(L"%ls %ls", ok ? L"Saved" : L"Failed to save", m_captureRequest.c_str());
        m_captureRequest.clear();
    }

    // Frame pacing. The waitable swap chain object blocks us until the compositor
    // wants a new frame; when the target is below the refresh rate we sleep the
    // remainder. Either path is a real block, never a spin.
    int targetFps = m_config.targetFps;
    int refresh = m_device.RefreshHz();
    if (targetFps == 0 || refresh < targetFps) {
        m_device.BeginFrame(true);
    } else {
        m_device.BeginFrame(false);
        double want = 1.0 / (double)targetFps;
        double elapsed = m_device.LastFrameMs() / 1000.0;
        if (elapsed < want) {
            Sleep((DWORD)std::max(1.0, (want - elapsed) * 1000.0 - 1.0));
        }
    }

    LARGE_INTEGER now, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    float dt = 0;
    if (m_lastFrameTime > 0) {
        dt = (float)((double)(now.QuadPart) / freq.QuadPart) - m_lastFrameTime;
        // Clamp so a stall (sleep/resume, device reset) never teleports the scene.
        dt = Clamp(dt, 0.0f, 0.1f);
    }
    m_lastFrameTime = (float)((double)(now.QuadPart) / freq.QuadPart);
    m_sceneTime += dt;

    // Auto-cycle: we only get here while actually drawing, so the timer pauses
    // with everything else.
    if (m_config.cycleMinutes > 0) {
        m_cycleSeconds += dt;
        if (m_cycleSeconds >= m_config.cycleMinutes * 60.0f) {
            m_cycleSeconds = 0;
            NextScene(1);
            return;   // the scene was just rebuilt; draw it on the next tick
        }
    }

    SceneCtx ctx = BaseCtx();
    ctx.dt = dt;
    ctx.time = m_sceneTime;
    ctx.fps = dt > 0 ? 1.0f / dt : 0.0f;

    // Rebuild scene caches if the icon layout moved.
    if (m_icons.Revision() != m_iconRevision) {
        m_iconRevision = m_icons.Revision();
        if (m_scene) m_scene->OnIconsChanged(ctx);
    }

    if (m_scene) {
        m_scene->Update(ctx, dt);
        m_device.BeginScene();
        // Re-read the context: BeginScene may have rebound a new target.
        ctx.dc = m_device.Dc();
        ctx.width = (float)m_device.SceneWidth();
        ctx.height = (float)m_device.SceneHeight();
        m_scene->Draw(ctx);
        m_device.EndScene();
    }
    m_device.EndFrame();
    m_framesDrawn++;

    // --- adaptive quality ---------------------------------------------------
    // Hold the frame budget by nudging the internal render scale. Small, bounded,
    // and it never fights the user's explicit quality setting.
    float budget = targetFps > 0 ? (1000.0f / targetFps) * 0.55f : 8.0f;
    float avg = m_device.AverageFrameMs();
    if (avg > budget && m_adaptiveScale > 0.7f) {
        if (++m_slowFrames > 90) {
            m_adaptiveScale = std::max(0.7f, m_adaptiveScale - 0.06f);
            m_device.SetSceneScale(QualitySceneScale(m_config.quality) * m_adaptiveScale);
            m_slowFrames = 0;
            LP_LOGI(L"engine: adaptive scale -> %.2f (avg %.2fms, budget %.2fms)",
                    m_adaptiveScale, avg, budget);
        }
    } else if (avg < budget * 0.55f && m_adaptiveScale < 1.0f) {
        if (++m_fastFrames > 300) {
            m_adaptiveScale = std::min(1.0f, m_adaptiveScale + 0.06f);
            m_device.SetSceneScale(QualitySceneScale(m_config.quality) * m_adaptiveScale);
            m_fastFrames = 0;
        }
    } else {
        m_slowFrames = 0;
        m_fastFrames = 0;
    }

    if (m_panel && m_panel->IsVisible()) {
        wchar_t status[256];
        swprintf_s(status, L"%ls  -  %ls  -  %.0f fps  -  %.1f ms",
                   SceneKey(m_sceneId),
                   m_icons.SourceName(),
                   m_sceneTime > 0.5f ? (dt > 0 ? 1.0f / dt : 0.0f) : 0.0f,
                   m_device.AverageFrameMs());
        wchar_t adapter[256];
        swprintf_s(adapter, L"%ls  -  %ux%u @ %.0f%%  -  %d Hz",
                   m_device.AdapterName(), m_device.Width(), m_device.Height(),
                   m_device.SceneScale() * 100.0f, m_device.RefreshHz());
        m_panel->SetState(m_config, m_paused, status, adapter);
        m_panel->Render();
    }
}

void Engine::TickWatch() {
    if (!m_running) return;

    // Keep the wallpaper fitted to the desktop. Resolution changes, taskbar auto-hide,
    // monitor hotplug and DPI changes can all invalidate the geometry, and the window
    // must cover the host exactly or the wallpaper shows as a partial rectangle.
    DesktopRect r{};
    if (m_host.EnsureLayout(&r)) {
        if (m_device.Dc()) {
            m_device.Resize((uint32_t)r.width, (uint32_t)r.height,
                            QualitySceneScale(m_config.quality) * m_adaptiveScale);
        }
        if (m_panel) m_panel->SetState(m_config, m_paused, L"", L"");
    }

    // Re-read the icon layout occasionally, and after any geometry change (anchors are
    // expressed in wallpaper-local pixels, so a resize invalidates them). The shell
    // does not notify us and the query is cheap, so a slow poll is the pragmatic choice.
    m_icons.Refresh(m_host.Window(), r.x, r.y, r.width, r.height);

    // Occlusion is polled rather than per-frame: the probe walks all top-level
    // windows, and 400 ms is far below any human's threshold for noticing a pause.
    bool covered = IsDesktopCovered();
    if (covered != m_occluded) {
        m_occluded = covered;
        LP_LOGI(L"engine: desktop %ls by other windows", covered ? L"covered" : L"visible");
    }

    // Keep the tray tooltip informative without touching it every tick.
    static int counter = 0;
    if (++counter % 8 == 0) UpdateTray();
}

// Writes a one-line snapshot of the engine's live state for `--status`. This exists
// because "the wallpaper is not moving" has several causes that look identical from
// the outside; the snapshot names the actual one.
void Engine::DumpStateToFile() {
    std::wstring path = Config::FilePath();
    size_t slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) path = path.substr(0, slash + 1);
    path += L"state.txt";

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) return;
    fwprintf(f, L"scene=%ls\n", SceneKey(m_sceneId));
    fwprintf(f, L"sceneName=%ls\n", SceneName(m_sceneId));
    fwprintf(f, L"running=%d\n", m_running ? 1 : 0);
    fwprintf(f, L"paused=%d\n", m_paused ? 1 : 0);
    fwprintf(f, L"pauseReason=%ls\n", m_pauseReason ? m_pauseReason : L"");
    fwprintf(f, L"userPaused=%d\n", m_userPaused ? 1 : 0);
    fwprintf(f, L"framesDrawn=%d\n", m_framesDrawn);
    fwprintf(f, L"hostAttached=%d\n", m_host.IsAttached() ? 1 : 0);
    fwprintf(f, L"deviceReady=%d\n", m_device.Dc() ? 1 : 0);
    fwprintf(f, L"targetFps=%d\n", m_config.targetFps);
    fwprintf(f, L"quality=%d\n", (int)m_config.quality);
    fwprintf(f, L"avgFrameMs=%.2f\n", m_device.AverageFrameMs());
    fwprintf(f, L"adapter=%ls\n", m_device.AdapterName());
    fwprintf(f, L"output=%ux%u\n", m_device.Width(), m_device.Height());
    fwprintf(f, L"sceneSize=%ux%u\n", m_device.SceneWidth(), m_device.SceneHeight());
    fwprintf(f, L"refreshHz=%d\n", m_device.RefreshHz());
    fwprintf(f, L"adaptiveScale=%.2f\n", m_adaptiveScale);
    fwprintf(f, L"variation=%u\n", m_config.variation);
    fwprintf(f, L"dim=%.3f\n", m_config.dim);
    fwprintf(f, L"occluded=%d\n", m_occluded ? 1 : 0);
    fwprintf(f, L"iconSource=%ls\n", m_icons.SourceName());
    fwprintf(f, L"iconCount=%zu\n", m_icons.Anchors().size());
    fwprintf(f, L"onBattery=%d\n", abi::IsOnBattery() ? 1 : 0);
    fwprintf(f, L"sessionLocked=%d\n", abi::IsSessionLocked() ? 1 : 0);
    fwprintf(f, L"fullscreen=%d\n", abi::IsFullscreenAppInForeground() ? 1 : 0);
    fclose(f);
    LP_LOGI(L"engine: state dumped to %ls", path.c_str());
}

void Engine::UpdateTray() {    wchar_t tip[128];
    swprintf_s(tip, L"LivePaper - %ls%ls", SceneName(m_sceneId),
               m_userPaused ? L" (paused)" : (m_paused ? L" (idle)" : L""));
    m_tray.SetTooltip(tip);
    m_tray.SetAccent(SceneAccent(m_sceneId));
    m_tray.SetPaused(m_userPaused);
}

int Engine::Run(const Options& opt) {
    if (!ipc::AcquireSingleInstance()) {
        // Another engine owns the wallpaper. Opening its control panel makes a second
        // launch useful instead of a silent no-op (which also looks like a failure).
        HWND existing = ipc::FindEngine();
        if (existing && ipc::Send(ipc::Command::ShowPanel)) {
            Print(L"LivePaper is already running - opened its control panel.");
        } else {
            Print(L"LivePaper is already running.");
        }
        return (int)ExitCode::AlreadyRunning;
    }

    if (!Startup(opt)) {
        Shutdown();
        ipc::ReleaseSingleInstance();
        return (int)ExitCode::NoDesktop;
    }

    // The frame timer wakes the loop. It is set to the target frame interval rather
    // than 1 ms: a 1 ms timer would wake ~1000 times a second just to be told the
    // frame is not due yet, which is measurable idle cost for no benefit. The exact
    // pacing still comes from the waitable swap chain and the sleep in TickFrame.
    // It is clamped to >=1 ms so a paused engine keeps waking (and can un-pause).
    UINT periodMs = 1;
    if (m_config.targetFps > 0) {
        int interval = (int)(1000.0 / m_config.targetFps) / 2;
        periodMs = (UINT)std::max(1, std::min(interval, 16));
    }
    SetTimer(m_window, kFrameTimer, periodMs, nullptr);
    // Engine::Run is reached with the wallpaper already laid out; the panel stays
    // hidden until the user asks for it from the tray or Ctrl+Alt+P, because a
    // control panel appearing on every launch is intrusive.
    (void)opt.noPanel;

    MSG msg;
    while (m_running) {
        // Block until a message arrives. The frame timer guarantees we wake at least
        // every millisecond while running, so no busy loop is needed.
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got == 0 || got == -1) break;
        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == kHotkeyTogglePanel) {
                if (m_panel) {
                    if (m_panel->IsVisible()) m_panel->Hide();
                    else m_panel->Show();
                }
            } else if (msg.wParam == kHotkeyNextScene) {
                NextScene(1);
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shutdown();
    ipc::ReleaseSingleInstance();
    return (int)ExitCode::Ok;
}

// ---------------------------------------------------------------------------
// diagnostics
// ---------------------------------------------------------------------------

// Shared offscreen renderer.
//
// Renders `scene` into a hidden window at `width`x`height` for `frames` frames and
// optionally writes a PNG. This deliberately does NOT use the desktop host: the
// result is reproducible, works whether or not an engine is running, and cannot be
// occluded by other windows (a screen capture of a wallpaper child window can be).
struct RenderRequest {
    SceneId scene = SceneId::Lines;
    Config config = Config::Defaults();
    uint32_t width = 0;
    uint32_t height = 0;
    int frames = 45;
    std::wstring outPath;         // empty = no capture
    bool attachDesktop = false;   // true = use the real desktop host (self-test)
    HWND target = nullptr;        // filled in by the caller for the desktop case
};

// Returns average frame time in ms, or -1 on failure. Fills `worstMs` when non-null.
double RenderSceneOffscreen(const RenderRequest& req, float* worstMs, bool* captured) {
    if (worstMs) *worstMs = 0;
    if (captured) *captured = false;

    bool ownsWindow = false;
    HWND target = req.target;
    if (!target) {
        target = CreateWindowExW(0, L"STATIC", L"LivePaper render", WS_POPUP,
                                 0, 0, (int)req.width, (int)req.height,
                                 nullptr, nullptr, nullptr, nullptr);
        if (!target) return -1;
        ownsWindow = true;
    }

    gfx::Device device;
    gfx::DeviceDesc desc;
    desc.hwnd = target;
    desc.width = req.width;
    desc.height = req.height;
    // Full resolution here: the point of the diagnostic is to show exactly what the
    // scene produces, not what the adaptive scaler happens to have chosen.
    desc.sceneScale = 1.0f;
    desc.allowSoftware = true;
    if (!device.Create(desc)) {
        if (ownsWindow) DestroyWindow(target);
        return -1;
    }

    IconAnchors icons;
    icons.SetCachePath(L"");
    icons.Refresh(target, 0, 0, (int)req.width, (int)req.height);

    std::unique_ptr<Scene> scene(CreateScene(req.scene));
    if (!scene) { device.Destroy(); if (ownsWindow) DestroyWindow(target); return -1; }

    SceneCtx ctx{};
    ctx.device = &device;
    ctx.dc = device.Dc();
    ctx.white = device.White();
    ctx.width = (float)device.SceneWidth();
    ctx.height = (float)device.SceneHeight();
    ctx.sceneScale = device.SceneScale();
    ctx.icons = &icons;
    ctx.config = &req.config;
    ctx.density = 1.0f;
    ctx.variation = req.config.variation;
    scene->Configure(ctx);

    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    double total = 0, worst = 0;
    // Render enough frames for animations to settle into a representative pose.
    for (int k = 0; k < req.frames; ++k) {
        ctx.time = (float)k / 30.0f;
        ctx.dt = 1.0f / 30.0f;
        QueryPerformanceCounter(&t0);
        scene->Update(ctx, ctx.dt);
        device.BeginFrame(false);
        device.BeginScene();
        ctx.dc = device.Dc();
        scene->Draw(ctx);
        device.EndScene();
        device.EndFrame();
        QueryPerformanceCounter(&t1);
        double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
        total += ms;
        if (ms > worst) worst = ms;
    }

    bool ok = true;
    if (!req.outPath.empty()) ok = device.CapturePng(req.outPath);
    if (captured) *captured = ok;
    if (worstMs) *worstMs = (float)worst;

    device.Destroy();
    if (ownsWindow) DestroyWindow(target);
    return total / (double)req.frames;
}

// Renders one frame of every scene and reports timing plus a PNG per scene. This is
// the regression test for the renderer.
int RunSelfTest() {
    Print(L"LivePaper self-test");

    WallpaperHost host;
    bool attached = host.Attach();
    Print(L"  desktop attach      : %ls", attached ? L"ok" : L"FAILED (using a hidden window)");

    DesktopRect r{0, 0, 1920, 1200};
    HWND target = nullptr;
    if (attached) {
        r = WallpaperHost::VirtualDesktop();
        host.Layout(r);
        target = host.Window();
    }
    Print(L"  target              : %ux%u", r.width, r.height);

    int failures = 0;
    for (int i = 0; i < (int)SceneId::Count; ++i) {
        SceneId id = (SceneId)i;
        RenderRequest req;
        req.scene = id;
        req.config = Config::Defaults();
        req.config.quality = Quality::Balanced;
        req.width = (uint32_t)r.width;
        req.height = (uint32_t)r.height;
        req.frames = 30;
        req.target = attached ? target : nullptr;
        std::wstring out = L"selftest-";
        out += SceneKey(id);
        out += L".png";
        req.outPath = out;

        float worst = 0;
        bool captured = false;
        double avg = RenderSceneOffscreen(req, &worst, &captured);
        if (avg < 0) { Print(L"  %-18ls: FAILED", SceneKey(id)); ++failures; continue; }

        Print(L"  %-18ls: %5.2f ms avg, %5.2f ms worst%ls",
              SceneKey(id), avg, worst, captured ? L"" : L"  (capture failed)");
        if (avg > 16.0) Print(L"      ^ over the 16 ms budget at full resolution");
    }

    host.Detach();
    Print(L"  failures            : %d", failures);
    Print(failures == 0 ? L"PASS" : L"FAIL");
    return failures == 0 ? 0 : 1;
}

// Renders a scene to a PNG without needing the engine (or the desktop) to be live.
// Takes `Options` by non-const reference because it refines the scene selection from
// whatever a running engine reports.
int RunRender(Options& opt) {
    // Report what a running engine is actually showing when it differs from config.
    if (opt.renderUseEngine) {
        HWND w = ipc::FindEngine();
        if (w) {
            LRESULT reply = 0;
            if (ipc::SendSync(ipc::Command::ReportState, 0, 0, &reply) && reply > 0) {
                opt.renderScene = (SceneId)(reply - 1);
                Print(L"Using the running engine's scene: %ls", SceneKey(opt.renderScene));
            }
        }
    }

    Config cfg = Config::Load();
    cfg.scene = opt.renderScene;
    cfg.quality = Quality::High;   // diagnostics render at full detail

    uint32_t w = opt.renderWidth;
    uint32_t h = opt.renderHeight;
    if (!w || !h) {
        DesktopRect d = WallpaperHost::VirtualDesktop();
        w = (uint32_t)d.width;
        h = (uint32_t)d.height;
    }

    RenderRequest req;
    req.scene = opt.renderScene;
    req.config = cfg;
    req.width = w;
    req.height = h;
    req.frames = 45;
    req.outPath = opt.renderPath;

    float worst = 0;
    bool captured = false;
    double avg = RenderSceneOffscreen(req, &worst, &captured);
    if (avg < 0) { Print(L"Render failed."); return 1; }

    Print(L"Rendered %ls at %ux%u", SceneKey(opt.renderScene), w, h);
    Print(L"  frame time          : %.2f ms avg, %.2f ms worst", avg, worst);
    if (!captured) { Print(L"  could not write %ls", opt.renderPath.c_str()); return 1; }
    Print(L"  saved               : %ls", opt.renderPath.c_str());
    return 0;
}

// Explains the engine's own view of the world without needing the desktop, the tray,
// or another process to interpret it. This is the first thing to run when the
// wallpaper is not moving: it reports exactly which condition is holding it back.
int RunDiagnose() {
    Print(L"LivePaper diagnostics");

    // --- environment --------------------------------------------------------
    Print(L"  executable          : %ls", ExecutablePath().c_str());
    Print(L"  config              : %ls", Config::FilePath().c_str());

    Config cfg = Config::Load();
    Print(L"  scene / fps / qual  : %ls / %d / %d",
          SceneKey(cfg.scene), cfg.targetFps, (int)cfg.quality);
    Print(L"  render scale        : %.2f", QualitySceneScale(cfg.quality));

    Print(L"  on battery          : %ls", abi::IsOnBattery() ? L"YES" : L"no");
    Print(L"  session locked      : %ls", abi::IsSessionLocked() ? L"YES" : L"no");
    Print(L"  fullscreen app      : %ls", abi::IsFullscreenAppInForeground() ? L"YES" : L"no");
    Print(L"  desktop covered     : %ls", abi::IsDesktopCovered() ? L"YES" : L"no");
    Print(L"  pause cfg (batt/lock/full/occl): %d / %d / %d / %d",
          cfg.pauseOnBattery ? 1 : 0, cfg.pauseWhenLocked ? 1 : 0, cfg.pauseWhenFullscreen ? 1 : 0,
          cfg.pauseWhenOccluded ? 1 : 0);

    const wchar_t* reason = L"";
    bool paused = false;
    if (cfg.pauseWhenLocked && abi::IsSessionLocked()) { paused = true; reason = L"session locked"; }
    else if (cfg.pauseOnBattery && abi::IsOnBattery()) { paused = true; reason = L"on battery"; }
    else if (cfg.pauseWhenFullscreen && abi::IsFullscreenAppInForeground()) { paused = true; reason = L"fullscreen app"; }
    else if (cfg.pauseWhenOccluded && abi::IsDesktopCovered()) { paused = true; reason = L"desktop covered"; }
    Print(L"  would pause now     : %ls%ls", paused ? L"YES - " : L"no", reason);

    // --- desktop host -------------------------------------------------------
    WallpaperHost host;
    bool attached = host.Attach();
    Print(L"  desktop attach      : %ls", attached ? L"ok" : L"FAILED");
    if (attached) {
        DesktopRect r = WallpaperHost::VirtualDesktop();
        host.Layout(r);
        Print(L"  virtual desktop     : %d,%d %dx%d", r.x, r.y, r.width, r.height);
        Print(L"  host hwnd / parent  : 0x%p / 0x%p",
              (void*)host.Window(), (void*)GetParent(host.Window()));

        // --- icons ----------------------------------------------------------
        IconAnchors icons;
        icons.SetCachePath(L"");
        icons.Refresh(host.Window(), r.x, r.y, r.width, r.height);
        Print(L"  icon anchors        : %zu (%ls)", icons.Anchors().size(), icons.SourceName());

        // --- render loop timing ---------------------------------------------
        gfx::Device device;
        gfx::DeviceDesc desc;
        desc.hwnd = host.Window();
        desc.width = (uint32_t)r.width;
        desc.height = (uint32_t)r.height;
        desc.sceneScale = QualitySceneScale(cfg.quality);
        desc.allowSoftware = true;
        if (device.Create(desc)) {
            Print(L"  adapter             : %ls%ls", device.AdapterName(),
                  device.IsSoftware() ? L" (WARP SOFTWARE)" : L"");
            Print(L"  output / scene      : %ux%u / %ux%u @ %.0f%%",
                  device.Width(), device.Height(), device.SceneWidth(), device.SceneHeight(),
                  device.SceneScale() * 100.0f);
            Print(L"  refresh rate        : %d Hz", device.RefreshHz());

            std::unique_ptr<Scene> scene(CreateScene(cfg.scene));
            Print(L"  scene created       : %ls", scene ? L"yes" : L"NO");
            if (scene) {
                SceneCtx ctx{};
                ctx.device = &device;
                ctx.dc = device.Dc();
                ctx.white = device.White();
                ctx.width = (float)device.SceneWidth();
                ctx.height = (float)device.SceneHeight();
                ctx.sceneScale = device.SceneScale();
                ctx.icons = &icons;
                ctx.config = &cfg;
                ctx.density = QualityDensity(cfg.quality);
                ctx.variation = cfg.variation;
                scene->Configure(ctx);
                Print(L"  scene configured    : ok");

                LARGE_INTEGER f, t0, t1;
                QueryPerformanceFrequency(&f);
                // Measure the full frame path including the swap chain wait, and time
                // it against a real clock so a pacing mistake is obvious.
                LARGE_INTEGER wall0, wall1;
                QueryPerformanceCounter(&wall0);
                const int frames = 60;
                double total = 0, worst = 0;
                for (int i = 0; i < frames; ++i) {
                    QueryPerformanceCounter(&t0);
                    device.BeginFrame(true);
                    ctx.time = (float)i / 30.0f;
                    ctx.dt = 1.0f / 30.0f;
                    scene->Update(ctx, ctx.dt);
                    device.BeginScene();
                    ctx.dc = device.Dc();
                    scene->Draw(ctx);
                    device.EndScene();
                    if (!device.EndFrame()) { Print(L"  Present FAILED at frame %d", i); break; }
                    QueryPerformanceCounter(&t1);
                    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
                    total += ms; if (ms > worst) worst = ms;
                }
                QueryPerformanceCounter(&wall1);
                double wallS = (double)(wall1.QuadPart - wall0.QuadPart) / f.QuadPart;
                Print(L"  frame workload      : %.2f ms avg, %.2f ms worst (%d frames)",
                      total / frames, worst, frames);
                Print(L"  wall clock          : %.2f s for %d frames = %.1f fps actual",
                      wallS, frames, frames / (wallS > 0 ? wallS : 1));
                Print(L"  budget              : %.1f ms at %d fps",
                      cfg.targetFps > 0 ? 1000.0 / cfg.targetFps : 0.0, cfg.targetFps);

                // --- full-screen proof ---------------------------------------
                // Sample the far corners of the presented buffer. Every scene begins by
                // filling the whole target, so if the wallpaper truly covers the window
                // the corners must be non-black. A black corner means the content is
                // smaller than the window - the exact "partial screen" symptom.
                Print(L"  --- full-screen proof (corner pixels) ---");
                uint32_t tl=0, tr=0, bl=0, br=0, c=0;
                uint32_t W = device.Width(), H = device.Height();
                bool a = device.ReadbackPixel(2, 2, &tl);
                bool b = device.ReadbackPixel(W - 3, 2, &tr);
                bool ccc = device.ReadbackPixel(2, H - 3, &bl);
                bool d = device.ReadbackPixel(W - 3, H - 3, &br);
                bool e = device.ReadbackPixel(W / 2, H / 2, &c);
                Print(L"  top-left     : %ls  0x%06X", a ? L"filled" : L"N/A", tl);
                Print(L"  top-right    : %ls  0x%06X", b ? L"filled" : L"N/A", tr);
                Print(L"  bottom-left  : %ls  0x%06X", ccc ? L"filled" : L"N/A", bl);
                Print(L"  bottom-right : %ls  0x%06X", d ? L"filled" : L"N/A", br);
                Print(L"  center       : %ls  0x%06X", e ? L"filled" : L"N/A", c);
                bool allFilled = (tl != 0x000000) && (tr != 0x000000) &&
                                 (bl != 0x000000) && (br != 0x000000) && (c != 0x000000);
                Print(allFilled
                      ? L"  COVERAGE     : FULL (all corners carry scene content)"
                      : L"  COVERAGE     : PARTIAL (at least one corner is empty/black)");
            }
            device.Destroy();
        } else {
            Print(L"  device              : FAILED to create");
        }
    }

    host.Detach();
    Print(paused ? L"RESULT: the engine would be PAUSED, which is why nothing animates."
                 : L"RESULT: nothing is blocking rendering.");
    return 0;
}

// Captures the real composited desktop at true physical resolution and writes a PNG.
//
// This exists because a screenshot taken by a DPI-unaware tool (including most
// PowerShell/System.Drawing snippets) is virtualised and can misreport where the
// wallpaper ends, which makes "the wallpaper is not full screen" very hard to settle.
// Grabbing the bits from inside the engine, using physical metrics, is authoritative.
int RunScreencap(const Options& opt) {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    Print(L"LivePaper screencap");
    Print(L"  virtual desktop     : %d,%d %dx%d (physical pixels)", x, y, w, h);

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    // CAPTUREBLT so layered/alpha windows are included, not just the visible frame.
    BOOL ok = BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT);
    SelectObject(mem, old);
    ReleaseDC(nullptr, screen);
    if (!ok) {
        Print(L"  BitBlt failed");
        DeleteObject(bmp); DeleteDC(mem);
        return 1;
    }

    // Read the DIB back and hand it to WIC.
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> pixels((size_t)w * h * 4);
    HDC ref = GetDC(nullptr);
    int rows = GetDIBits(ref, bmp, 0, (UINT)h, pixels.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, ref);
    DeleteObject(bmp);
    DeleteDC(mem);
    if (rows != h) { Print(L"  GetDIBits failed"); return 1; }

    bool written = false;
    IWICImagingFactory* wic = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IWICImagingFactory), (void**)&wic)) && wic) {
        IWICStream* st = nullptr;
        IWICBitmapEncoder* enc = nullptr;
        if (SUCCEEDED(wic->CreateStream(&st)) && st &&
            SUCCEEDED(st->InitializeFromFilename(opt.capturePath.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
            SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache))) {
            IWICBitmapFrameEncode* fr = nullptr;
            IPropertyBag2* pb = nullptr;
            if (SUCCEEDED(enc->CreateNewFrame(&fr, &pb))) {
                fr->Initialize(pb);
                fr->SetSize((UINT)w, (UINT)h);
                WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
                fr->SetPixelFormat(&pf);
                if (SUCCEEDED(fr->WritePixels((UINT)h, (UINT)w * 4, (UINT)pixels.size(), pixels.data()))) {
                    fr->Commit();
                    enc->Commit();
                    written = true;
                }
                fr->Release();
                if (pb) pb->Release();
            }
        }
        if (enc) enc->Release();
        if (st) st->Release();
        wic->Release();
    }
    if (!written) { Print(L"  could not write PNG"); return 1; }
    Print(L"  saved               : %ls", opt.capturePath.c_str());
    return 0;
}

int PrintStatus() {
    HWND w = ipc::FindEngine();
    if (!w) {
        Print(L"LivePaper is not running.");
        return (int)ExitCode::NotRunning;
    }

    // Ask the engine to publish a live snapshot, then read it back. Reporting the
    // engine's own state matters: a paused engine looks identical to a broken one
    // from the outside, and "why is my wallpaper frozen" is the common question.
    std::wstring statePath = Config::FilePath();
    size_t slash = statePath.find_last_of(L'\\');
    if (slash != std::wstring::npos) statePath = statePath.substr(0, slash + 1);
    statePath += L"state.txt";
    DeleteFileW(statePath.c_str());

    if (!ipc::SendSync(ipc::Command::DumpState, 0, 0, nullptr, 800)) {
        Print(L"LivePaper is running (hwnd 0x%p) but is not responding.", (void*)w);
        return (int)ExitCode::IpcError;
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, statePath.c_str(), L"r, ccs=UTF-8") != 0 || !f) {
        Print(L"LivePaper is running but produced no state snapshot.");
        return (int)ExitCode::IpcError;
    }

    wchar_t line[512];
    std::wstring sceneName = L"?", sceneKey = L"?", reason;
    int paused = 0, frames = 0, attached = 0, deviceReady = 0, fps = 0;
    double avgMs = 0;
    while (fgetws(line, _countof(line), f)) {
        std::wstring s(line);
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back();
        size_t eq = s.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = s.substr(0, eq), v = s.substr(eq + 1);
        if (k == L"sceneName") sceneName = v;
        else if (k == L"scene") sceneKey = v;
        else if (k == L"pauseReason") reason = v;
        else if (k == L"paused") paused = _wtoi(v.c_str());
        else if (k == L"framesDrawn") frames = _wtoi(v.c_str());
        else if (k == L"hostAttached") attached = _wtoi(v.c_str());
        else if (k == L"deviceReady") deviceReady = _wtoi(v.c_str());
        else if (k == L"targetFps") fps = _wtoi(v.c_str());
        else if (k == L"avgFrameMs") avgMs = _wtof(v.c_str());
    }
    fclose(f);

    Print(L"LivePaper is running.");
    Print(L"  wallpaper           : %ls (%ls)", sceneName.c_str(), sceneKey.c_str());
    Print(L"  frames drawn        : %d", frames);
    Print(L"  target / actual     : %d fps / %.1f ms per frame",
          fps, avgMs);
    Print(L"  desktop attached    : %ls", attached ? L"yes" : L"NO");
    Print(L"  render device       : %ls", deviceReady ? L"ready" : L"NOT READY");
    if (paused) {
        Print(L"  STATE               : PAUSED (%ls)", reason.empty() ? L"unknown" : reason.c_str());
        if (frames == 0)
            Print(L"  note                : rendering has never started; the pause condition "
                  L"has been true since launch (battery, lock or fullscreen).");
        else
            Print(L"  note                : animation is intentionally halted; the wallpaper "
                  L"stays on its last frame.");
    } else if (frames == 0) {
        Print(L"  STATE               : not paused but no frame has been drawn yet.");
    } else {
        Print(L"  STATE               : animating");
    }

    // Geometry: the wallpaper must cover the whole desktop, so report the numbers that
    // decide it rather than leaving the user to eyeball a partial rectangle.
    Print(L"  --- geometry ---");
    Print(L"  virtual desktop     : %d,%d %dx%d",
          GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
          GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    Print(L"  monitors            : %d", WallpaperHost::MonitorCount());

    // Read the live window geometry straight from the running engine's host window.
    // The host is a CHILD of the desktop WorkerW, so FindWindowW (which only walks
    // top-level windows) cannot see it - enumerate the shell's children instead.
    HWND hostWnd = nullptr;
    {
        struct Ctx { HWND found; } ctx{ nullptr };
        EnumWindows([](HWND top, LPARAM lp) -> BOOL {
            wchar_t cls[64]{};
            GetClassNameW(top, cls, _countof(cls));
            if (_wcsicmp(cls, L"Progman") != 0 && _wcsicmp(cls, L"WorkerW") != 0) return TRUE;
            HWND child = FindWindowExW(top, nullptr, L"LivePaper.Host", nullptr);
            if (child) { ((Ctx*)lp)->found = child; return FALSE; }
            return TRUE;
        }, (LPARAM)&ctx);
        hostWnd = ctx.found;
    }
    if (hostWnd) {
        RECT wr{};
        GetWindowRect(hostWnd, &wr);
        HWND parent = GetParent(hostWnd);
        RECT pr{};
        if (parent) GetClientRect(parent, &pr);
        wchar_t cls[64]{};
        if (parent) GetClassNameW(parent, cls, _countof(cls));
        Print(L"  wallpaper window    : %dx%d at (%d,%d)",
              wr.right - wr.left, wr.bottom - wr.top, wr.left, wr.top);
        Print(L"  host parent         : %ls, client %dx%d",
              cls[0] ? cls : L"?", pr.right - pr.left, pr.bottom - pr.top);
        bool fits = (wr.right - wr.left) == (pr.right - pr.left) &&
                    (wr.bottom - wr.top) == (pr.bottom - pr.top);
        Print(L"  covers host exactly : %ls", fits ? L"yes" : L"NO");
    } else {
        Print(L"  wallpaper window    : not found (engine not attached?)");
    }
    return 0;
}

} // namespace

int App::Run(const wchar_t* cmdLine) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(cmdLine ? cmdLine : L"", &argc);
    if (!argv) return (int)ExitCode::BadArgs;

    Options opt;
    bool ok = ParseArgs(argc, argv, opt);
    LocalFree(argv);
    if (!ok || opt.badArg) {
        Print(L"Unknown or incomplete argument: %ls", opt.badArgText.c_str());
        Print(L"Run 'LivePaper --help' for usage.");
        return (int)ExitCode::BadArgs;
    }

    // Logging: only when asked, or when a debugger is attached.
    if (opt.verbose || IsDebuggerPresent()) {
        std::wstring logPath;
        wchar_t* local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) {
            logPath = std::wstring(local) + L"\\LivePaper";
            CreateDirectoryW(logPath.c_str(), nullptr);
            logPath += L"\\livepaper.log";
            CoTaskMemFree(local);
        }
        LogInit(opt.verbose ? LogLevel::Debug : LogLevel::Info, logPath);
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int result = 0;
    switch (opt.mode) {
        case Options::Mode::Help:
            PrintHelp();
            result = 0;
            break;
        case Options::Mode::Version:
            Print(L"LivePaper 1.4.0 (native Direct2D engine)");
            result = 0;
            break;
        case Options::Mode::List: {
            Print(L"Available wallpapers:");
            for (int i = 0; i < (int)SceneId::Count; ++i) {
                SceneId id = (SceneId)i;
                std::unique_ptr<Scene> s(CreateScene(id));
                Print(L"  %-10ls %ls", SceneKey(id), s ? s->Description() : L"");
            }
            result = 0;
            break;
        }
        case Options::Mode::Selftest:
            result = RunSelfTest();
            break;
        case Options::Mode::Diag:
            result = RunDiagnose();
            break;
        case Options::Mode::Screencap:
            result = RunScreencap(opt);
            break;
        case Options::Mode::Render:
            result = RunRender(opt);
            break;
        case Options::Mode::Status:
            result = PrintStatus();
            break;
        case Options::Mode::SetScene:
            result = ipc::Send(ipc::Command::SetScene, 0, (LPARAM)opt.scene)
                         ? 0 : (int)ExitCode::NotRunning;
            break;
        case Options::Mode::Next:
            result = ipc::Send(ipc::Command::NextScene) ? 0 : (int)ExitCode::NotRunning;
            break;
        case Options::Mode::Reshuffle:
            result = ipc::Send(ipc::Command::Reshuffle) ? 0 : (int)ExitCode::NotRunning;
            break;
        case Options::Mode::Prev:
            result = ipc::Send(ipc::Command::PrevScene) ? 0 : (int)ExitCode::NotRunning;
            break;
        case Options::Mode::Toggle:
            result = ipc::Send(ipc::Command::TogglePause) ? 0 : (int)ExitCode::NotRunning;
            break;
        case Options::Mode::Pause:
            result = ipc::Send(ipc::Command::TogglePause) ? 0 : (int)ExitCode::NotRunning;
            break;
        case Options::Mode::Resume:
            result = ipc::Send(ipc::Command::TogglePause) ? 0 : (int)ExitCode::NotRunning;
            break;
        case Options::Mode::Reload:
            result = ipc::Send(ipc::Command::Reload) ? 0 : (int)ExitCode::NotRunning;
            break;
        case Options::Mode::Quit:
            result = ipc::Send(ipc::Command::Quit) ? 0 : (int)ExitCode::NotRunning;
            break;
        case Options::Mode::Capture:
            result = ipc::Send(ipc::Command::Capture) ? 0 : (int)ExitCode::NotRunning;
            break;
        case Options::Mode::Install: {
            bool on = SetAutostart(true);
            Print(on ? L"LivePaper will start with Windows." : L"Could not set the startup entry.");
            result = on ? 0 : 1;
            break;
        }
        case Options::Mode::Uninstall: {
            bool off = SetAutostart(false);
            Print(off ? L"LivePaper will no longer start with Windows."
                      : L"Could not remove the startup entry.");
            result = off ? 0 : 1;
            break;
        }
        case Options::Mode::SetCycle: {
            Config c = Config::Load();
            c.cycleMinutes = std::max(0, std::min(opt.cycleMinutes, 240));
            c.Normalize();
            c.Save();
            if (c.cycleMinutes > 0)
                Print(L"Auto-cycle set to %d minutes.", c.cycleMinutes);
            else
                Print(L"Auto-cycle disabled.");
            if (ipc::FindEngine()) ipc::Send(ipc::Command::ApplyConfig);
            result = 0;
            break;
        }
        case Options::Mode::SetFps:
        case Options::Mode::SetQuality: {
            // These adjust persistent config, then ask a running engine to re-read it.
            Config c = Config::Load();
            if (opt.mode == Options::Mode::SetFps) {
                c.targetFps = opt.fps;
                Print(L"Frame cap set to %d.", opt.fps);
            } else {
                c.quality = opt.quality;
                Print(L"Quality set to %d.", (int)opt.quality);
            }
            c.Normalize();
            c.Save();
            if (ipc::FindEngine()) ipc::Send(ipc::Command::ApplyConfig);
            result = 0;
            break;
        }
        case Options::Mode::Engine: {
            Engine engine;
            result = engine.Run(opt);
            break;
        }
        default:
            result = (int)ExitCode::BadArgs;
            break;
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    LogShutdown();
    return result;
}

} // namespace lp
