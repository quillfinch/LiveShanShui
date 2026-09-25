// LivePaper - lightweight live wallpaper engine for Windows.
// Logging: opt-in, allocation-free in the common path, never on the render hot path.
#pragma once
#include <string>
#include <cstdio>

namespace lp {

enum class LogLevel { Off = 0, Error, Warn, Info, Debug };

// Logging is disabled unless --verbose/--log is passed or a debugger is attached,
// so the shipping engine performs zero I/O while rendering.
void LogInit(LogLevel level, const std::wstring& filePath);
void LogShutdown();
void LogSetLevel(LogLevel level);
LogLevel LogGetLevel();

void LogWrite(LogLevel level, const wchar_t* fmt, ...);

// Cheap guard: avoids formatting work entirely when the level is filtered out.
inline bool LogEnabled(LogLevel level) { return (int)level <= (int)LogGetLevel(); }

#define LP_LOGE(...) do { if (::lp::LogEnabled(::lp::LogLevel::Error)) ::lp::LogWrite(::lp::LogLevel::Error, __VA_ARGS__); } while (0)
#define LP_LOGW(...) do { if (::lp::LogEnabled(::lp::LogLevel::Warn))  ::lp::LogWrite(::lp::LogLevel::Warn,  __VA_ARGS__); } while (0)
#define LP_LOGI(...) do { if (::lp::LogEnabled(::lp::LogLevel::Info))  ::lp::LogWrite(::lp::LogLevel::Info,  __VA_ARGS__); } while (0)
#define LP_LOGD(...) do { if (::lp::LogEnabled(::lp::LogLevel::Debug)) ::lp::LogWrite(::lp::LogLevel::Debug, __VA_ARGS__); } while (0)

// Records the last failure for user-visible diagnostics (tray balloon / console verb).
// Named RecordFailure rather than SetLastError so it never collides with the Win32
// function of that name once <windows.h> is in scope.
void RecordFailure(const wchar_t* what, long hr);
std::wstring GetLastErrorText();

enum class ExitCode : int {
    Ok = 0,
    AlreadyRunning = 10,
    NoDesktop = 11,
    NoDevice = 12,
    BadArgs = 13,
    ConfigError = 14,
    IpcError = 15,
    NotRunning = 16,
};

} // namespace lp
