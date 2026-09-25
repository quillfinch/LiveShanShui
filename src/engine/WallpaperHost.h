// LivePaper - attaches the wallpaper surface to the desktop.
//
// Explorer hosts desktop icons in a SHELLDLL_DefView window. Behind it sits either
// the Progman window itself or a sibling WorkerW window. Parenting our own child
// window to that host puts us *behind the icons* but in front of the wallpaper,
// which is exactly the layer a live wallpaper belongs on.
//
// Discovery order (try each until one works):
//   1. Progman's DefView -> sibling WorkerW        (classic Windows 7-10)
//   2. Progman's DefView -> Progman itself         (some single-monitor setups)
//   3. Top-level WorkerW containing DefView        (Windows 11 24H2+)
//   4. Progman directly                            (last resort)
#pragma once
#include <windows.h>
#include <cstdint>

namespace lp {

struct DesktopRect { int x = 0, y = 0, width = 0, height = 0; };

class WallpaperHost {
public:
    WallpaperHost();
    ~WallpaperHost();
    WallpaperHost(const WallpaperHost&) = delete;
    WallpaperHost& operator=(const WallpaperHost&) = delete;

    // Creates the host window as a child of the desktop. Returns false if Explorer
    // is not available (e.g. a shell replacement is running).
    bool Attach();
    void Detach();
    bool IsAttached() const;

    // Repositions to cover the whole virtual desktop (all monitors).
    void Layout(const DesktopRect& r);
    // Repositions to cover a single monitor.
    void LayoutMonitor(HMONITOR mon);

    // Re-checks that the wallpaper still exactly covers the desktop host and corrects
    // it if not (resolution change, taskbar auto-hide, monitor hotplug, DPI change).
    // Returns true when the geometry changed; `actual` receives the current desired
    // size. Callers should resize their swap chain when this returns true.
    bool EnsureLayout(DesktopRect* actual = nullptr);

    // Size of the host's client area - the coordinate space a child window lives in.
    DesktopRect HostClientRect() const;

    HWND Window() const;

    // True when the desktop host went away (Explorer restarted) and we should re-attach.
    bool NeedsReattach() const;

    static DesktopRect VirtualDesktop();
    static HMONITOR PrimaryMonitor();
    static int MonitorCount();

private:
    struct Impl;
    Impl* m_impl;
};

} // namespace lp
