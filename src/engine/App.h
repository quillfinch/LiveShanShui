// Live Shan Shui - application entry point.
//
// One executable serves three roles:
//   * engine      : owns the wallpaper window and renders           (default)
//   * control     : sends a command to a running engine and exits    (--scene, --fps, ...)
//   * diagnostics : --selftest / --capture / --list                  (no engine state)
#pragma once
#include <windows.h>

namespace lp {

class App {
public:
    // `cmdLine` is the raw GetCommandLineW() string.
    static int Run(const wchar_t* cmdLine);
};

} // namespace lp
