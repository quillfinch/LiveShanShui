# LivePaper — Engineering Handover

**Audience:** whoever picks this up next. This document assumes you have
the source in front of you and explains how it actually works, what is fragile, what was
learned the hard way, and what to do when it breaks.

For a user-facing overview (features, CLI reference, screenshots) read `README.md`
first. This file is the internals.

---

## Contents

1. [Current state at a glance](#1-current-state-at-a-glance)
2. [Build, run, verify in 60 seconds](#2-build-run-verify-in-60-seconds)
3. [Project map](#3-project-map)
4. [How a frame reaches the screen](#4-how-a-frame-reaches-the-screen)
5. [How the wallpaper gets behind the icons](#5-how-the-wallpaper-gets-behind-the-icons)
6. [The six things that will bite you](#6-the-six-things-that-will-bite-you)
7. [Invariants — do not break these](#7-invariants--do-not-break-these)
8. [How to add a wallpaper scene](#8-how-to-add-a-wallpaper-scene)
9. [Systems reference](#9-systems-reference)
10. [Diagnostics and debugging playbook](#10-diagnostics-and-debugging-playbook)
11. [Resource budget](#11-resource-budget)
12. [Testing checklist](#12-testing-checklist)
13. [Known limitations and suggested next steps](#13-known-limitations-and-suggested-next-steps)
14. [Environment specifics](#14-environment-specifics)

---

## 1. Current state at a glance

LivePaper is a Windows live-wallpaper engine: it renders an animated wallpaper into the
desktop layer, *behind* the icons, where the static wallpaper normally sits.

| Fact | Value |
|---|---|
| Language / std | C++20, no exceptions, no RTTI |
| Toolchain | MSYS2 UCRT64 `g++` 16.1.0 (`-municode -mwindows`) |
| Build system | `build.ps1` (no CMake, no NuGet, offline) |
| Dependencies | **none** — Windows system libraries only |
| Output | one self-contained exe, ~678 KB |
| Source | 53 files, ~12,500 lines |
| Wallpapers | 30 procedural scenes |
| Steady-state cost | ~0.5% of one core at 30 fps, ~55 MB working set |
| State | Feature-complete for its scope; works live on the dev machine |

There is **no git repository**. See §13 for why initialising one is the first thing you
should do.

It is a single process that owns everything: the wallpaper window, the tray icon, the
hotkeys, and a hidden message-only window that acts as the control endpoint. There is no
service, no injected DLL, no shell extension, and no second process.

---

## 2. Build, run, verify in 60 seconds

```powershell
cd D:\Projects\LivePaper

# Build (stops a running engine first, then compiles + links + embeds resources)
.\build.ps1

# Render every wallpaper, time it, write a PNG per scene, report PASS/FAIL
.\build\LivePaper.exe --selftest

# Explain the environment, the render path, and prove the frame covers the screen
.\build\LivePaper.exe --diag

# Run it for real
.\build\LivePaper.exe
```

Expected `--selftest` tail:

```
  lines             :  6.84 ms avg, 17.60 ms worst
  ...
  failures            : 0
PASS
```

Expected `--diag` tail:

```
  COVERAGE     : FULL (all corners carry scene content)
RESULT: nothing is blocking rendering.
```

If `--selftest` fails to link with an opaque `ld returned 1 exit status`, a running
engine is holding `build\LivePaper.exe` open. `build.ps1` already stops it, but a
hand-run `g++` will not.

---

## 3. Project map

```
build.ps1                  build script; stops running engine, compiles, links, embeds res/
README.md                  user-facing docs
HANDOVER.md                this file
res/
  livepaper.manifest       system DPI awareness, Win10/11 compat, asInvoker
  livepaper.rc             icon + manifest + version info
  livepaper.ico            4 sizes, generated programmatically (no external art)

src/
  main.cpp                 wWinMain -> lp::App::Run()
  core/
    Log.{h,cpp}            opt-in file/debugger logging; RecordFailure for diagnostics
    Config.{h,cpp}         %APPDATA%\LivePaper\config.ini; SceneId enum + registry
    Math.h                 Vec2, Color, hash noise, Rng, Lerp/Clamp/Smoothstep
    Win32Compat.{h,cpp}    *** ABI-exact D2D mirrors, bitmap helpers, power/session probes
    WinMsg.{h,cpp}         cross-process messages + RemoteBuffer (buffer inside target)
  gfx/
    GfxDevice.{h,cpp}      *** D3D11 device, flip swap chain, D2D context, upscale, capture
    Scene.{h,cpp}          Scene interface, SceneCtx, shared draw:: primitives, factory
  engine/
    App.{h,cpp}            *** engine loop, CLI parsing, all diagnostics, autostart
    WallpaperHost.{h,cpp}  *** Progman/WorkerW discovery, desktop child window, layout
    IconAnchors.{h,cpp}    desktop icon positions with cache + grid fallback
  scenes/
    Scenes.h               factory declarations
    LinesScene.cpp         the icon-graph scene (reads real desktop icons)
    CyberpunkScene.cpp     neon skyline + rain
    NatureScene.cpp        ridges, procedural trees, leaves
    BeachScene.cpp         sunset surf
    AnimeScene.cpp         sakura / pastel
    AuroraScene.cpp        northern lights
    NebulaScene.cpp        nebula clouds + star field
    AlpineScene.cpp        snow-lit mountain ridges
    FirefliesScene.cpp     drifting glowing fireflies
    MeshScene.cpp          additive colour-field gradient
    OceanScene.cpp         god rays, bubble streams, fish silhouettes
    MatrixScene.cpp        falling glyph streams (Digital Rain)
    KoiScene.cpp           koi + lily pads + ripples (rotation transforms)
    LavaScene.cpp          pulsing molten crack networks + embers
    SnowScene.cpp          parallax snowfall over a winter treeline
    RainScene.cpp          droplets on glass over city bokeh
    DesertScene.cpp        dune ridges, wind-blown sand, mirage band
    StormScene.cpp         midpoint-displaced lightning + flash + rain sheets
    FireworksScene.cpp     rockets, burst shells (peony/ring/willow), gravity debris
    GalaxyScene.cpp        rotating spiral-arm star disc with dust lanes
    CrystalScene.cpp       faceted crystals, glow pools, sparkles, light shafts
    GearsScene.cpp         brass gear train (cached tooth polygons under transforms)
    BalloonsScene.cpp      striped balloon canopies via axis-aligned clip stripes
    LighthouseScene.cpp    rotating beam wedges, swell ridges, lamp pulse
    SavannaScene.cpp       banded sun, acacia silhouettes, swaying grass, birds
    SilkScene.cpp          undulating translucent ribbon bands (sine + noise)
    BlackHoleScene.cpp     accretion-disk motes, near-half arc bands, lensed flares
    KaleidoScene.cpp       mirrored wedge shapes under per-segment rotations
    FlowScene.cpp          noise-field advection with ring-buffer particle trails
    PlasmaScene.cpp        midpoint-displaced filament bolts inside a glass orb
  ui/
    Panel.{h,cpp}          control panel: one D2D surface, scrollable, custom hit-testing
    Tray.{h,cpp}           notification icon, generated at runtime with GDI
  ipc/
    Ipc.{h,cpp}            message-only window, command protocol, single-instance mutex
```

`***` = files carrying the hard-won platform knowledge. Read those before changing
anything structural.

---

## 4. How a frame reaches the screen

```
Engine::TickFrame()                    engine/App.cpp
  ├─ ShouldPause()?  -> battery / locked / fullscreen  -> return (0% CPU)
  ├─ m_host.EnsureLayout()             re-fit window if the desktop geometry changed
  ├─ frame pacing                      waitable swap chain + Sleep
  ├─ m_scene->Update(ctx, dt)
  ├─ device.BeginScene()               D2D BeginDraw, targets the scene surface
  ├─ m_scene->Draw(ctx)                scene fills the whole scene surface
  ├─ device.EndScene()                 EndDraw
  └─ device.EndFrame()                 upscale scene -> back buffer, then Present1
```

### The device (`gfx/GfxDevice.cpp`)

- `D3D11CreateDevice` (hardware; WARP fallback behind `allowSoftware`).
- `ID2D1Factory1` → `ID2D1Device` → `ID2D1DeviceContext`.
- Swap chain: `CreateSwapChainForHwnd` with `FLIP_DISCARD`, 2 buffers,
  `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, BGRA8.
- The back buffer is wrapped as an `ID2D1Bitmap1` with
  `TARGET | CANNOT_DRAW` (it cannot be sampled, only drawn into).

**Initialisation order matters and is not negotiable:**

```
D3D11 device  ->  D2D device  ->  D2D device context  ->  swap chain  ->  target bitmap
```

Creating the target bitmap before the device context exists was an actual crash during
development (a null `g_dc` dereference).

### Render scaling (quality)

At anything below High quality the scene is drawn into a smaller offscreen surface and
upscaled:

| Quality | `sceneScale` | `density` |
|---|---|---|
| Low | 0.72 | 0.55 |
| Balanced | 0.85 | 1.00 |
| High | 1.00 | 1.30 |

`sceneScale == 1.0` collapses the offscreen surface away entirely (`scene = nullptr`)
and scenes draw straight into the back buffer.

`SceneCtx::width/height` are the **scene surface** size, not the output size. Scenes must
always draw relative to those and fill `0..width, 0..height`.

### Frame pacing

Two mechanisms, deliberately:

1. **Waitable swap chain** (`WaitForSingleObjectEx(frameWait, ...)`) blocks until the
   compositor wants a frame. Zero CPU spin. This is the single biggest reason idle cost
   is ~0.5% of a core instead of ~4.5%.
2. **Sleep** for the remainder when `targetFps` is below the refresh rate.

The `WM_TIMER` driving the loop is set to roughly half the frame interval (clamped 1–16 ms),
**not 1 ms**. A 1 ms timer wakes ~1000×/s only to find the frame is not due yet.

### Adaptive quality

`TickFrame` compares `device.AverageFrameMs()` against `55%` of the frame budget and
nudges `m_adaptiveScale` down (min 0.7) after 90 slow frames, back up after 300 fast
ones. It scales *within* the user's chosen quality; it never overrides it.

---

## 5. How the wallpaper gets behind the icons

Explorer hosts desktop icons inside a `SHELLDLL_DefView` window. Behind it sits either
`Progman` itself or a sibling `WorkerW` window. `WallpaperHost` creates its own child
window under that host.

Discovery order (`WallpaperHost::Attach`), each tried in turn:

1. Send `0x052C` to `Progman` → Explorer spawns the wallpaper `WorkerW`; find the top-level
   window containing `SHELLDLL_DefView` and take its `WorkerW` sibling.
2. That `DefView` host itself.
3. A top-level `WorkerW` that directly contains `SHELLDLL_DefView` (Windows 11 24H2 layout).
4. `Progman` directly.

**The host is a CHILD window.** Child coordinates are relative to the *parent's client
area*, not the screen. `HostClientRect()` measures that client area and the child is
placed at `(0,0)` inside it. Passing screen coordinates straight through is the classic
cause of a wallpaper that covers only part of the display — and it is exactly the bug
that was hit here.

`EnsureLayout()` runs from the watch timer, compares the window's current size against
the host client area, and re-fits (returning `true` so the caller can resize the swap
chain). This is what makes resolution changes, taskbar auto-hide, monitor hotplug and DPI
changes self-heal without a restart.

If Explorer restarts, `NeedsReattach()` notices the parent window changed and the engine
re-attaches.

---

## 6. The six things that will bite you

These all cost real debugging time. They are not theoretical.

### 6.1 `%s` is a NARROW string in a wide printf

In `wprintf`/`fwprintf`/`swprintf_s`, `%s` takes a `char*` and **stops at the first NUL
byte**. Passing a `wchar_t*` silently truncates to one character.

This bug appeared in **four separate places**, each with a different symptom:

| Location | Symptom |
|---|---|
| `Log.cpp` | every log line was one character |
| `Config.cpp` Save | `scene=nebula` written as `scene=n`; on reload the scene reset to default |
| `App.cpp` status/tray strings | panel footer and tray tooltip showed a single letter |
| `App.cpp` capture path | truncated filenames |

**Rule: wide arguments require `%ls`.** When adding any formatted output, use `%ls` for
`wchar_t*`, `%s` only for `char*`. Grep for `%s` before committing:

```powershell
Select-String -Path src\*\*.cpp -Pattern 'swprintf_s?\s*\([^)]*%s|fwprintf\s*\([^)]*%s|Print\(L[^)]*%s'
```

### 6.2 `DrawImage` does not stretch — use `DrawBitmap`

`ID2D1DeviceContext::DrawImage` draws an image at its **intrinsic size**, derived from the
bitmap's DPI. It has no destination-size parameter.

The upscale originally used `DrawImage(scene, nullptr, nullptr, ...)`. Since the scene
surface is smaller than the output, the wallpaper was drawn at ~85% size in the corner
and the rest of the screen showed the static desktop. Geometry checks all passed — the
*window* was the right size, the *content* was not.

The fix is `DrawBitmap` with explicit source and destination rects
(`GfxDevice::EndFrame`), plus creating the scene surface at a constant 96 DPI so no size
is ever implied.

**Because this class of bug is invisible to geometry checks, coverage is verified by
sampling pixels** — see `--diag` in §10.

### 6.3 `D2D1_BITMAP_PROPERTIES1` is ABI-hostile under MinGW

It is passed **by value** and the struct is not laid out identically by every compiler.
Measured on this toolchain:

```
sizeof(D2D1_BITMAP_PROPERTIES1) = 32
offsets: pixelFormat 0, dpiX 8, dpiY 12, bitmapOptions 16, colorContext 24
```

`bitmapOptions` must be **set explicitly**. If it is left zeroed,
`CreateBitmapFromDxgiSurface` returns `E_INVALIDARG` and — worse — an earlier form of the
call crashed outright.

`src/core/Win32Compat.h` declares `BitmapProps1Abi` with compile-time `static_assert`s on
every offset, plus `abi::MakeBitmapProps` / `abi::CreateBitmapFromDxgiSurface` /
`abi::CreateBitmap1` wrappers. **Always go through those wrappers** rather than calling
the MinGW interface methods directly.

Also note MinGW only declares the pointer forms of `CreateLinearGradientBrush` /
`CreateRadialGradientBrush`, so the two-argument convenience overloads are unavailable —
pass the stop collection directly.

### 6.4 A message-only window delivered `WM_CREATE` before my procedure existed

`ipc::CreateEngineWindow` creates the window with `DefWindowProcW` and only *then*
installs the real `WNDPROC`. So a `WM_CREATE` branch inside the engine procedure never
runs.

Consequence: `Engine::m_window` stayed `NULL`, `SetTimer` silently failed, and **the engine
never rendered a single frame** while reporting itself healthy. It looked exactly like a
broken wallpaper.

**Rule: never initialise state from `WM_CREATE` in that procedure.** `m_window` is assigned
by `Startup()` from the return value of `CreateEngineWindow`. `EngineProc` reads
`g_engine`, which is set in `Startup()` before any message can be dispatched.

### 6.5 `near` and `far` are Win32 macros

`windows.h` (via `windef.h`) defines `near` and `far` as empty macros. Naming a local
variable `near` or `far` produces bizarre syntax errors. Use `nearTone` / `farTone` etc.
This bit twice in the nature/alpine scenes.

### 6.6 Screenshots from DPI-unaware tools lie

The desktop is 1920×1200 physical with 125% scaling. PowerShell and `System.Drawing`
`CopyFromScreen` are DPI-unaware, so they silently return **1536×960 logical** pixels —
a full-screen wallpaper then *looks* like it only covers two thirds of the screen.

Do not diagnose layout from such a screenshot. Use:

```powershell
.\build\LivePaper.exe --status     # exact window vs host geometry
.\build\LivePaper.exe --diag       # corner-pixel coverage proof
.\build\LivePaper.exe --screencap shot.png   # DPI-correct full-resolution capture
```

---

## 7. Invariants — do not break these

1. **Initialisation order** in `GfxDevice::Create`: device → D2D device → context → swap
   chain → target bitmap.
2. **Scenes fill `0..SceneWidth × 0..SceneHeight`.** They never touch DXGI, never assume
   output size, and must not allocate per frame (precompute in `Configure`).
3. **All D2D by-value structs go through `abi::` wrappers.**
4. **`%ls` for wide strings in formatted output.**
5. **The host window is positioned in parent-client coordinates**, always via
   `HostClientRect()` / `EnsureLayout()`.
6. **The upscale uses explicit rects** (`DrawBitmap` with source + destination).
7. **One process, one tray icon.** A second launch must send `ShowPanel` to the existing
   engine and exit with code 10, never start a second engine.
8. **`SceneId` order is load-bearing.** The enum, `kScenes[]` in `Config.cpp`, the panel
   swatch array, and `SceneAccent()` are all indexed positionally. Adding a scene means
   updating all four (a `static_assert` catches the `kScenes` count mismatch, but not the
   swatch/accent arrays — those will silently mis-colour).
9. **Attribute any scene's per-frame work.** Every scene has hard caps on particles,
   edges, rain, pulses, etc. derived from the quality setting.

---

## 8. How to add a wallpaper scene

Follow this exactly; steps 4–6 are the easy ones to miss.

1. **`src/scenes/Scenes.h`** — declare `Scene* CreateYourThing();`
2. **New file `src/scenes/YourThingScene.cpp`** — subclass `lp::Scene`. Copy an existing
   scene as a template; `MeshScene.cpp` is the smallest, `LinesScene.cpp` the most complete.
3. **`src/core/Config.h`** — add `YourThing` to `SceneId` **before `Count`**.
4. **`src/core/Config.cpp`** — add the row to `kScenes[]` in the *same position* as the enum:
   `{ SceneId::YourThing, L"yourthing", L"Your Thing" }`.
5. **`src/gfx/Scene.cpp`** — add the `case` to `CreateScene()`.
6. **`src/ui/Panel.cpp`** — add a swatch to `kSceneSwatches[]` (positional; the
   `static_assert` catches a count mismatch). The panel's four parameter sliders are
   driven by `ParamName()`, so nothing else is needed for them.
7. **`src/engine/App.cpp`** — add a `case` to `SceneAccent()` (positional, for the tray icon).

Also fold `ctx.variation` into every `Rng` seed (`Rng rng(base + ctx.variation * 7919u)`)
so `--reshuffle` produces a new deterministic layout; 0 must stay the canonical layout
the self-test renders.

### Scene contract

```cpp
class YourThing final : public Scene {
    const wchar_t* Name() const override;          // display name
    const wchar_t* Description() const override;   // one line for --list
    int ParamCount() const override;               // 0..4
    const wchar_t* ParamName(int i) const override;

    void Configure(const SceneCtx& ctx) override;        // build caches; called on activate + resize
    void Update(const SceneCtx& ctx, float dt) override; // advance state
    void Draw(const SceneCtx& ctx) override;             // fill the target
    void OnIconsChanged(const SceneCtx& ctx) override;   // only if you use icon anchors
};
```

Rules:

- Read tunables from `ctx.config->sceneParam[0..7]` in `Configure`/`Update`.
- `ctx.width/height` are the **scene** dimensions; `ctx.dt` is seconds and already clamped
  to ≤0.1 s so a stall cannot teleport the animation.
- Never allocate in `Draw`. Build vectors in `Configure`, resize conservatively.
- Prefer the shared helpers in `gfx/Scene.h` (`draw::VerticalGradient`, `RadialGlow`,
  `Circle`, `Line`, `SoftCurve`, `RoundedRect`, `StarField`). `RadialGlow` is concentric
  ellipses, not a blur effect — no intermediate surfaces, no effect graph.
- Use a fixed `Rng` seed so output is deterministic and screenshots are comparable.

Available parameter slots and how existing scenes use them:

| Scene | p0 | p1 (speed) | p2 | p3 |
|---|---|---|---|---|
| Lines | Density | Speed | Glow | Hue |
| Cyberpunk | Rain | Neon | Glow | Traffic |
| Nature | Leaves | Wind | Haze | Palette |
| Beach | Surf | Wind | Sun glow | Warmth |
| Anime | Petals | Drift | Glow | Sky tint |
| Aurora | Curtains | Motion | Brightness | Hue |
| Nebula | Density | Motion | Brightness | Hue |
| Alpine | Peaks | Haze | Moon | Tint |
| Fireflies | Count | Drift | Glow | Warmth |
| Mesh | Speed | Saturation | Glow | Hue |

`p1` is wired to the panel's "Animation speed" slider for every scene — keep it as the
scene's overall speed knob.

**Verify with:** `--selftest` (timing + non-blank), then `--render yourthing out.png`, then
`--diag` with `scene=yourthing` for the coverage proof.

---

## 9. Systems reference

### 9.1 Icon anchors (`engine/IconAnchors.cpp`)

The "Lines & Connections" scene wants the real desktop icon positions. Reading them is
**unreliable in practice**: on Windows 10 19045, `LVM_GETITEMCOUNT` and
`LVM_GETITEMSPACING` answer fine but `LVM_GETITEMPOSITION` returns without filling the
caller's buffer, and the call times out cross-process.

Three-tier strategy, best first:

1. **Shell** — `LVM_GETITEMPOSITION` with the `POINT` staged *inside Explorer's address
   space* via `RemoteBuffer` (`core/WinMsg.h`). Requires **most** items to answer, or the
   partial result is discarded. On success the layout is written to a cache.
2. **Cache** — `%LOCALAPPDATA%\LivePaper\icons.cache`, from the last successful (1).
   Rejected if it falls outside the current desktop bounds.
3. **Grid** — derived from the live icon **cell metrics** (`LVM_GETITEMSPACING`) plus the
   shell's placement mode. This is approximate but metric-accurate, and looks intentional.

`--status` / `--diag` report which source was used (`shell` / `cache` / `grid`).
Note that a real query has been observed to succeed on the dev machine — the fallback is
not dead code, but (1) is genuinely hit.

### 9.2 IPC (`ipc/Ipc.cpp`)

A **message-only window** (class `LivePaper.Ipc`, title `LivePaper.Engine`, parent
`HWND_MESSAGE`) is the control endpoint. Commands are posted as `WM_APP + 0x100 + n`
where `n` is the `ipc::Command` value.

- Fire-and-forget: `ipc::Send()` → `PostMessage`, never blocks.
- Request/reply: `ipc::SendSync()` → `SendMessageTimeoutW(..., ABORTIFHUNG, 400ms)`.

| Command | Value | Purpose |
|---|---|---|
| ShowPanel / HidePanel | 1 / 2 | toggle the control panel |
| TogglePause | 3 | pause/resume |
| NextScene / PrevScene | 4 / 5 | cycle |
| SetScene | 6 | `lParam` = `SceneId` |
| ApplyConfig | 7 | re-read `config.ini` and apply |
| Capture | 8 | write a PNG of the current frame |
| Quit | 9 | shut down |
| Reshuffle | 10 | re-roll the current scene's layout (bumps the persisted variation seed) |
| Reload | 11 | re-attach to the desktop |
| ReportState | 12 | returns current scene + 1 in the message result |
| Poll | 13 | liveness probe |
| DumpState | 14 | write `state.txt` next to the config for `--status` |

Single instance: named mutex `Global\LivePaper.SingleInstance` with a session-local
fallback. The tray icon is separately gated by `Local\LivePaper.TrayIcon`, so only one
process can ever own a notification-area icon.

### 9.3 Config (`core/Config.cpp`)

`%APPDATA%\LivePaper\config.ini`, written whenever a setting changes.

```ini
scene=lines
fps=30                 ; 0 = uncapped
quality=1              ; 0 low, 1 balanced, 2 high
dim=0.000              ; extra darkening 0..0.6
pauseFullscreen=1
pauseBattery=1
pauseLocked=1
pauseOccluded=1       ; pause when non-shell windows cover the desktop (polled)
spanAll=1              ; one surface across the virtual desktop
startWithWindows=0
variation=0            ; scene layout seed, bumped by --reshuffle, folded into Rng seeds
p0..p7=0.500           ; per-scene parameters; the panel labels all four via ParamName()
```

`dim` (0..0.6) is applied by `Device::EndFrame` as one black overlay on the composited
frame — it works identically on the upscale path and the full-res path.

Sibling files: `state.txt` (live snapshot written on demand for `--status`),
`icons.cache` (icon layout, under `%LOCALAPPDATA%`).

Autostart is a `HKCU\...\CurrentVersion\Run` value named `LivePaper`.

Parsing is forgiving: unknown keys are ignored, malformed values fall back to defaults,
and `Normalize()` clamps everything on load.

### 9.4 Pause conditions (`abi::` in `Win32Compat.cpp`)

`Engine::ShouldPause()` checks, in order: user pause, session locked, on battery,
fullscreen app in foreground, desktop fully covered by other windows (`pauseOccluded`,
polled by `abi::IsDesktopCovered()` in `TickWatch` every 400 ms — an area-coverage
`EnumWindows` walk that ignores shell windows, cloaked windows and the engine's own
windows). When paused, `TickFrame` returns immediately and **no swap chain wait occurs**
— this is what makes the engine drop to 0% CPU.

> **This is the #1 cause of "it's broken".** A laptop on battery with `pauseBattery=1`
> (the default) renders nothing. `--diag` states it explicitly under "would pause now",
> and `--status` shows `STATE: PAUSED (on battery)`.

`IsSessionLocked` works by opening the input desktop and checking it is named `Default`;
`IsFullscreenAppInForeground` compares the foreground window rect against the monitor and
excludes shell classes.

---

## 10. Diagnostics and debugging playbook

### The tools

| Command | What it answers |
|---|---|
| `--selftest` | Does every scene render, how long does each take, does it stay in budget? Writes `selftest-<scene>.png` |
| `--diag` | Full environment + render path + **corner-pixel coverage proof** + "would pause now" |
| `--status` | Live engine state: scene, frames drawn, fps, geometry, pause reason |
| `--screencap [f.png]` | DPI-correct capture of the real desktop |
| `--render [scene] [f.png]` | Render one scene to a PNG with no desktop/engine involved |
| `--verbose` | Log to `%LOCALAPPDATA%\LivePaper\livepaper.log` |

`--diag` and `--status` are the first thing to run for any "it doesn't work" report.
`--diag` needs no running engine, attaches its own host, and reports the pause reason,
the adapter, the render scale, the icon source, and whether the rendered frame actually
reaches the corners.

### Symptom → cause

| Symptom | Likely cause |
|---|---|
| Wallpaper covers only part of the screen | Content smaller than window → upscale bug (§6.2). Check `--diag` COVERAGE. Also check child-coordinate layout (§6.5/§5). |
| Nothing animates, 0% CPU | Pause condition. Run `--status`, read `STATE`. On battery is the usual one; a maximised window now triggers `desktop covered` when `pauseOccluded=1`. |
| `--status` prints nothing | Console attachment, or the engine is not running. `--status` returns exit 10/16 when absent. |
| Config resets / scene reverts on restart | `%s` truncation in `Config::Save` (§6.1). Inspect `config.ini` — a one-character `scene=` is the tell. |
| Link fails with `ld returned 1 exit status` | Running engine holds the exe. `build.ps1` stops it; a manual `g++` does not. |
| Scene renders nothing / crashed on activate | Check the `--selftest` row; run `--render <scene>` and inspect. |
| Icons not aligned with anchors | Icon source is `grid` not `shell` — see §9.1. `--status` reports `iconSource`. |
| Tray icon duplicated | Should be impossible (session lock). Ghosts from an old build persist until the notification area refreshes. |

### Verifying coverage without trusting your eyes

`--diag` renders the active scene and samples five points of the presented back buffer
(the values below are a real run of the **Nebula** scene — yours will differ):

```
top-left     : filled  0x020206
top-right    : filled  0x020206
bottom-left  : filled  0x020105
bottom-right : filled  0x020105
center       : filled  0x231532
COVERAGE     : FULL (all corners carry scene content)
```

Every scene begins by filling its whole target, so a **black corner means content smaller
than the window**. Before the §6.2 fix these read `0x000000`. This check is the reason the
upscale bug was found at all — geometry checks were all green.

Note the corners legitimately differ *between* scenes; what matters is that none are zero,
and that changing scene changes the values (it is also a cheap check that the right scene
actually loaded).

---

## 11. Resource budget

Measured live at 1920×1200 output / 1536×960 visible, 30 fps target, Lines scene:

| Metric | Value |
|---|---|
| CPU (sustained) | ~0.5% of one core (~0.14 ms CPU per frame) |
| Working set | 53–65 MB |
| Executable | 567 KB |
| Frame workload | 3–16 ms depending on scene (budget 33 ms at 30 fps) |

Per-scene `--selftest` averages are the regression baseline. If a scene exceeds ~16 ms avg
at full resolution the self-test prints a budget warning. Keep new scenes under that.

Where the savings come from, in order of impact:

1. Waitable swap chain instead of polling (~4.5% → ~0.5% of a core on its own).
2. Timer set to the frame interval, not 1 ms.
3. Pausing on battery/lock/fullscreen — genuinely 0% while paused.
4. Cached static geometry (halos, skylines, ridges, canopies, star fields).
5. Bounded per-frame work (hard caps on particles/rain/edges/pulses).
6. Reduced-resolution scene rendering + adaptive scale.
7. Optimal-path scene blitting — icon halos are pre-rendered once and blitted, not redrawn.

---

## 12. Testing checklist

There is no automated test framework. Verification is the CLI diagnostics plus a manual
acceptance pass. Run this before handing anything on:

```powershell
# 1. Builds clean (no warnings expected)
.\build.ps1

# 2. All scenes render, in budget
.\build\LivePaper.exe --selftest        # expect: failures: 0 / PASS

# 3. Render path + coverage proof
.\build\LivePaper.exe --diag            # expect: COVERAGE: FULL

# 4. Live engine
.\build\LivePaper.exe
.\build\LivePaper.exe --status          # expect: attached yes, device ready, animating
                                        #         wallpaper window == host client size
```

Manual acceptance (all verified working on the dev machine previously — re-confirm after
structural changes):

- [ ] Wallpaper visible behind real desktop icons, full screen, no gaps
- [ ] Icons remain readable (scenes keep contrast low at the edges)
- [ ] `--scene <name>` / `--next` / `--prev` switch live
- [ ] `--reshuffle` visibly re-rolls the scene and bumps `variation` in config.ini
- [ ] `--pause` stops drawing (frame counter freezes), `--resume` restarts it
- [ ] Tray icon responds; control panel opens and its controls apply
- [ ] A **second launch** opens the panel and exits 10 — not a second engine
- [ ] `--install` writes the Run key, `--uninstall` removes it
- [ ] `--quit` exits cleanly and removes the tray icon
- [ ] Pause conditions behave (battery / lock / fullscreen)

---

## 13. Known limitations and suggested next steps

Ordered roughly by value.

1. **Initialise a git repository.** There is none. `build/`, `selftest-*.png`,
   `samples/` output and the `_aotprobe/` scratch dir (already deleted) should be ignored.
   Also remove the stray `selftest-*.png` files currently in the project root — they are
   self-test by-products.
2. **Per-monitor wallpapers.** `spanAll=1` renders one surface across the virtual desktop.
   True per-monitor scenes need one swap chain + scene instance per monitor. This is the
   largest remaining architectural gap, and the reason `spanAllMonitors` exists but is
   effectively always on.
3. **Video wallpapers.** Media Foundation ships with Windows and needs no new dependency.
   Only procedural scenes exist today.
4. ~~**`Reshuffle` unimplemented**~~ **Done** — `--reshuffle`, the tray menu item and
   `Ipc::Command::Reshuffle` bump the persisted `variation` seed, folded into every
   scene's RNG seeds.
5. ~~**Only animation speed in the panel**~~ **Done** — four sliders per scene, labelled
   by `ParamName()`; the panel content scrolls.
6. ~~**`pauseWhenOccluded` never evaluated**~~ **Done** — see §9.4.
7. ~~**`dim` not applied**~~ **Done** — applied in `Device::EndFrame`.
8. **Code signing.** The binary is unsigned; SmartScreen warns on first run.
9. **Accessibility / multi-DPI.** The manifest declares *system* DPI awareness. PMv2 was
   evaluated and **rejected**: switching to it broke discovery of the shell's icon
   ListView on the dev machine. If you revisit this, re-test icon discovery first.
10. **`e->m_config.Save()` on scene switch** writes the config on every `--next`. Minor,
    but it means the config file churns.

Also worth knowing:

- **Dead or stubbed state.** `Engine::m_suspended` is declared and never used.
  (`Reshuffle`, `pauseWhenOccluded` and `dim` were stubs once — all three are wired
  now; see §13 items 4–7.)
- `Engine::m_pauseReason` and `m_framesDrawn` are diagnostic-only, surfaced through
  `state.txt` and `--status`.
- **`ID2D1DeviceContextAbi` in `Win32Compat.h` declares a deliberately incomplete
  vtable.** Only the slots actually called are needed, but the ones declared must stay in
  the correct order — inserting a method mid-vtable without matching the real ABI will
  silently corrupt every subsequent call. Prefer adding a free wrapper function
  (`abi::CreateBitmapFromDxgiSurface` is the model) over adding vtable entries.

---

## 14. Environment specifics

The machine this was developed and verified on:

| | |
|---|---|
| OS | Windows 10 Home, build 19045 |
| CPU | 16 logical cores |
| GPU | AMD Radeon 780M (iGPU) + NVIDIA RTX 5050 Laptop (hybrid) |
| Display | 1920×1200 physical @ 125% scaling (1536×960 logical) |
| Desktop | icons in a left column, taskbar bottom |
| Toolchain | MSYS2 UCRT64, `g++` 16.1.0, `windres` |

Notes that follow from this:

- The engine selects the **AMD iGPU** by default (first adapter). On hybrid systems this
  is the power-friendly choice for a wallpaper; if you want the discrete GPU you would
  need to enumerate adapters explicitly.
- Battery is often the case on this laptop, so the default `pauseBattery=1` frequently
  makes the wallpaper look frozen. `pauseBattery=0` is set in the current config.
- 125% scaling is why DPI-unaware screenshots are misleading (§6.6).

The toolchain path is hard-coded in `build.ps1`
(`C:\Program Files (x86)\MSYS2\ucrt64\bin\`). To build elsewhere, either install MSYS2
UCRT64 or parameterise those two paths at the top of the script.

---

## Appendix: quick file-to-symptom index

| If you need to change… | Go to |
|---|---|
| Frame loop, pacing, pause logic | `engine/App.cpp` — `Engine::TickFrame`, `ShouldPause` |
| CLI verbs, diagnostics | `engine/App.cpp` — `ParseArgs`, `RunDiagnose`, `PrintStatus`, `RunSelfTest`, `RunRender`, `RunScreencap` |
| Swap chain, upscale, capture, readback | `gfx/GfxDevice.cpp` |
| D2D/D3D ABI helpers | `core/Win32Compat.h` |
| Desktop attachment, geometry self-heal | `engine/WallpaperHost.cpp` |
| Icon positions | `engine/IconAnchors.cpp`, `core/WinMsg.cpp` |
| Scene list / metadata | `core/Config.h` + `core/Config.cpp` (`kScenes`) |
| Scene factory | `gfx/Scene.cpp` (`CreateScene`) |
| Shared drawing primitives | `gfx/Scene.cpp` (`draw::`) |
| Control panel layout & hit-testing | `ui/Panel.cpp` (`RebuildLayout`, `HitTest`) |
| Tray icon art and menu | `ui/Tray.cpp` |
| Config persistence | `core/Config.cpp` (`Load`/`Save`) |
| Autostart | `engine/App.cpp` (`SetAutostart`) |
| Build flags, libraries, resources | `build.ps1`, `res/` |
