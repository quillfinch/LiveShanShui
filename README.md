# Live Shan Shui

An endlessly scrolling, procedurally-generated Chinese ink-wash landscape for your
Windows desktop, in the spirit of classical 山水 scrolls. Built directly on the
framework components that ship with the operating system: one ~700 KB executable, no
installer, no runtime, no third-party libraries, and no image assets — the painting is
drawn by code.

The landscape is infinite: the scroll drifts sideways forever, generating new mountains,
rivers, pavilions, boats and wandering figures as it goes, all deterministic from a
world seed (`--reshuffle` paints a brand-new one). It is a native C++ port of
LingDong's **[shan-shui-inf](https://github.com/LingDong-/shan-shui-inf)** (MIT), with
Direct2D in place of SVG.

Measured while animating on the reference machine:

| Metric | Value |
|---|---|
| CPU while animating | **~1% of one core** at 30 fps |
| Working set | **~60 MB** |
| Disk footprint | **~700 KB** (single file, nothing else) |
| Startup | instant (no runtime to load) |

---

## Quick start

```powershell
# Build (needs the MSYS2 UCRT64 toolchain; no CMake, no NuGet, no network)
.\build.ps1

# Run it — the wallpaper starts and a tray icon appears
.\build\LiveShanShui.exe

# Paint a brand-new landscape
.\build\LiveShanShui.exe --reshuffle

# Stop it
.\build\LiveShanShui.exe --quit
```

New to it? If anything looks wrong, run the diagnostic first — it explains what the
engine sees and which condition is holding it back:

```powershell
.\build\LiveShanShui.exe --diag
.\build\LiveShanShui.exe --status
```

---

## The painting

Every element is drawn procedurally — layered translucent ink strokes on procedural
paper, no textures:

* **Mountains** with tapered brush texture, pale rim trees, foothill rocks
* **Distant ridges** in washed-out gray for depth
* **Flat-topped plateau mounts** with groves, rocks and an occasional pavilion
* **Architecture**: thatched huts, tiered buildings, pagodas, arched bridge railings,
  boats with anglers, and (rarely, on purpose) transmission towers
* **Figures**: stick men in conical hats with walking sticks
* **Water** ripples at the mountain bases, **mist** bands across the flanks
* A **sun** and wandering **bird flocks**, plus the collector's red **seal** in the
  corner

The panel's **SCENE SETTINGS** sliders (labels come from the painting itself):

| Slider | What it does |
|---|---|
| Scroll speed | how fast the scroll drifts sideways |
| Mist | the soft paper-colored washes across the mountains |
| Vegetation | tree density on the mountains and plateaus |
| Ink | overall ink strength |

**COLOR** tints the paper and ink (a rose tint gives the warm pink wash shown in the
screenshots; the classic look is the default off state), **Shuffle** re-rolls the world
seed, and the **COMPANION** section picks the desktop pet.

### Desktop pet

A small tamagotchi companion floats just above the taskbar on a fully transparent
window — six species (cat, shiba, axolotl, chick, ghost, panda), each with its own
stats, animations and moods. Click to pet, cookie to feed, ball to play; XP earns
levels. See `--pet list`. It hides itself behind fullscreen apps.

---

## Command line

```
LiveShanShui                       Start the engine (tray icon appears)
LiveShanShui --reshuffle           Paint a new landscape (re-roll the world seed)
LiveShanShui --pause | --resume    Pause / resume animation
LiveShanShui --toggle              Toggle pause           (global hotkey: Ctrl+Alt+P)
LiveShanShui --fps <n|max>         Set the frame-rate cap
LiveShanShui --quality <low|balanced|high>
LiveShanShui --pet <key|list>      Choose the desktop pet companion
LiveShanShui --reload              Re-attach after an Explorer restart
LiveShanShui --status              What the engine is doing right now
LiveShanShui --quit                Stop the engine
LiveShanShui --install             Start with Windows
LiveShanShui --uninstall           Remove the startup entry

LiveShanShui --selftest            Render the painting and the pets, validate and time it
LiveShanShui --diag                Explain the engine's environment and frame timing
LiveShanShui --screencap [file.png]
                                   Capture the real desktop at physical resolution
LiveShanShui --render [file.png]   Render one frame of the scroll to a PNG
LiveShanShui --verbose             Write a log to %LOCALAPPDATA%\LiveShanShui\liveshanshui.log
```

---

## Resource behaviour

The point of the project is to be invisible in Task Manager. That is achieved by:

* **A flip-model swap chain with a waitable frame-latency object.** The render loop
  blocks on that handle instead of polling or spinning, so an idle engine consumes
  essentially no CPU.
* **A frame timer set to the frame interval, not 1 ms.** A 1 ms timer wakes the loop
  ~1000 times a second just to be told the frame is not due yet.
* **Automatic pausing.** Rendering stops entirely when the machine is on battery, the
  session is locked, a fullscreen app is in front, or other windows cover the desktop.
  Each is configurable.
* **Baked tiles.** The painting is rendered into screen-sized tiles (paper + shapes)
  once as the camera advances; a normal frame replays at most two bitmaps. A new tile
  costs a few tens of milliseconds about once every half minute at the default scroll
  speed, amortised between frames.
* **Adaptive quality.** If the measured frame cost exceeds the budget for a sustained
  period, the internal render scale is nudged down (and back up when there is room).

---

## Architecture

```
src/
  main.cpp                entry point (wWinMain)
  core/
    Log.*                 opt-in file/debugger logging
    Config.*              %APPDATA%\LiveShanShui\config.ini
    Math.h                vectors, colour, hashes, noise, deterministic RNG
    Win32Compat.*         ABI-exact COM mirrors + D3D/D2D helpers + power/session
    WinMsg.*              cross-process message + remote staging buffer
  gfx/
    GfxDevice.*           D3D11 device, flip swap chain, D2D device context
    Scene.*               scene interface + shared drawing primitives
  shanshui/
    ShanShuiLib.*         the painting engine (port of shan-shui-inf): PRNG, Perlin
                          noise, brush primitives, mountain/tree/architecture/figure
                          generators, chunk planner and streaming world
    ShanShuiScene.*       the wallpaper scene: tile baker, camera, panel parameters
  engine/
    App.*                 engine loop, CLI, diagnostics, autostart
    WallpaperHost.*       Progman/WorkerW discovery and desktop child window
    IconAnchors.*         desktop icon positions with cache and grid fallback
  ui/
    Panel.*               control panel (single Direct2D surface, scrollable)
    Pet.*                 desktop pet window above the taskbar
    Tray.*                notification-area icon, generated at runtime
  ipc/
    Ipc.*                 message-only window + commands
res/                      manifest, icon, version info
```

### How the painting is made

The original shan-shui-inf is vanilla JavaScript that emits SVG `<polyline>` strings
from a seeded Blum-Blum-Shub PRNG and table-based Perlin noise. The port keeps the
algorithms and swaps the SVG emitter for `Shape` records (polygon + fill + stroke) that
are rasterised into Direct2D path geometries. Where the original relied on SVG
multiply-blending against the paper (its "white" masks), the port paints those masks in
the paper colour under normal source-over, which is visually equivalent for translucent
gray inks.

The world streams in 512-unit chunks, planned by the same noise-field local-maxima
search as the original: a chunk is generated strictly left-to-right through one global
RNG stream, which makes the whole infinite scroll reproducible from a single seed.
Features spill beyond their anchor, so a tile is only baked once the world has been
generated ~1700 units past its right edge — after that its item set is final and tiles
join seamlessly.

Not part of the original: the sun, bird flocks, mist bands and the collector's seal,
added here in the same spirit.

### How the wallpaper gets behind the icons

Explorer hosts desktop icons inside a `SHELLDLL_DefView` window. Behind it sits
`Progman` or a sibling `WorkerW`. Live Shan Shui creates its own child window under
that host, which puts it *behind the icons but in front of the wallpaper*. Four
discovery strategies are tried in turn, so the classic Windows 7–10 layout and the
Windows 11 24H2 layout both work. If Explorer restarts, the engine notices the parent
window changed and re-attaches itself.

To confirm the fit, ask the engine rather than a screenshot tool:

```powershell
.\build\LiveShanShui.exe --status     # reports "covers host exactly: yes/no"
.\build\LiveShanShui.exe --screencap shot.png
```

---

## Building

Requirements: **MSYS2 with the UCRT64 toolchain** (`g++` and `windres`), and Windows 10
or later. Nothing is downloaded; the build links only against libraries that ship with
Windows (`d2d1`, `d3d11`, `dxgi`, `dwrite`, `windowscodecs`, `shell32`, ...).

```powershell
.\build.ps1                 # Release
.\build.ps1 -Config Debug   # -O0 -g
.\build.ps1 -Clean          # rebuild from scratch
.\build.ps1 -Test           # build then run --selftest
```

The build script stops a running engine before linking and embeds the icon, version
info and DPI manifest from `res/`. `--selftest` renders the painting across a tile
seam at full resolution plus one frame of every pet species, and fails on blank output,
NaN-colored shapes or a blown 16 ms frame budget.

Toolchain notes (MSYS2 + MinGW, found the hard way, documented in
`src/core/Win32Compat.h`):

* `D2D1_BITMAP_PROPERTIES1` under MinGW lays `bitmapOptions` out differently from
  MSVC; set it explicitly (`abi::MakeBitmapProps` exists for exactly this). A bitmap
  that is to be used as a render target needs `D2D1_BITMAP_OPTIONS_TARGET`, and the
  `D2D1_BITMAP_PROPERTIES1` overload of `CreateBitmap`.
* `%s` in a wide `printf` takes a **narrow** string; wide arguments need `%ls`.
* D2D matrices are row-vector: in `A * B`, **A applies first** (`Scale * Translation`
  scales then translates — the reverse of the usual column-vector intuition).

---

## Configuration

`%APPDATA%\LiveShanShui\config.ini` (a first run migrates `config.ini` and `pet.ini`
from an old `%APPDATA%\LivePaper` install):

```ini
fps=30
quality=1              ; 0 low, 1 balanced, 2 high
dim=0.000              ; extra darkening, 0..0.6 (applied over the whole frame)
variation=0            ; world seed, bumped by --reshuffle
useCustomColor=0       ; tint paper + ink from customColor
customColor=D845B2
pauseFullscreen=1
pauseBattery=1
pauseLocked=1
pauseOccluded=1        ; pause when other windows cover the desktop
spanAll=1              ; one painting across all monitors
startWithWindows=0
petEnabled=1
petKind=ghost          ; cat, shiba, axolotl, chick, ghost, panda
p0=0.250               ; scroll speed (the shipped default is a slow drift)
p1=0.500               ; mist
p2=0.500               ; vegetation
p3=0.500               ; ink
```

Each pet species keeps its own stats block in `pet.ini` (hunger, happiness, xp, age),
so switching pets switches creatures, not costumes.

---

## Status and known limitations

Working and verified live:

* the scroll renders behind the real desktop icons and stays there; it exactly covers
  the desktop host (verified by `--status` and by `--diag`, which samples the far
  corners of the rendered frame)
* tiles join seamlessly across chunk boundaries (the self-test walks the camera across
  a seam at full resolution and inspects the result)
* deterministic: the same world seed paints byte-identical frames
* reshuffle, pause/resume, tray menu, control panel with the four painting sliders,
  color tinting, desktop pet, `dim`, battery/lock/fullscreen/covered pausing,
  Explorer-restart re-attach, adaptive quality

Known limitations:

* baking a new tile takes a few tens of milliseconds about once every half minute at
  the default scroll speed; a single frame hitches when it happens
* the painting is memory-held per generated chunk and trimmed behind the camera; very
  long sessions at high scroll speed trade RAM for scenery
* per-monitor paintings (`spanAll` renders one surface across the virtual desktop)
* code signing: the binary is unsigned, so SmartScreen will warn on first run

## Credits and licence

Based on [shan-shui-inf](https://github.com/LingDong-/shan-shui-inf) by LingDong
(MIT license) — the generator algorithms, planning and chunk streaming are a port of
that work; the Windows engine, Direct2D rasterisation, tile baking, the sun, birds,
mist and seal are new here. The engine itself is provided as-is for personal use; no
third-party art, fonts or media are bundled.
