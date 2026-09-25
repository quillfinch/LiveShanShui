#include "Log.h"
#include <windows.h>
#include <cstdarg>
#include <mutex>

namespace lp {

namespace {
    LogLevel g_level = LogLevel::Off;
    FILE* g_file = nullptr;
    std::mutex g_mtx;
    wchar_t g_lastWhat[256] = L"";
    long g_lastHr = 0;
    bool g_haveLast = false;

    const wchar_t* LevelTag(LogLevel l) {
        switch (l) {
            case LogLevel::Error: return L"ERR ";
            case LogLevel::Warn:  return L"WARN";
            case LogLevel::Info:  return L"INFO";
            case LogLevel::Debug: return L"DBG ";
            default:              return L"    ";
        }
    }
}

void LogInit(LogLevel level, const std::wstring& filePath) {
    std::lock_guard<std::mutex> lock(g_mtx);
    // A debugger always wants diagnostics; otherwise honour the requested level.
    if (level == LogLevel::Off && IsDebuggerPresent()) level = LogLevel::Debug;
    g_level = level;
    if (level == LogLevel::Off) return;

    if (!filePath.empty()) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, filePath.c_str(), L"a, ccs=UTF-8") == 0 && f) {
            g_file = f;
            SYSTEMTIME st; GetLocalTime(&st);
            fwprintf(g_file, L"\n===== LivePaper %04d-%02d-%02d %02d:%02d:%02d =====\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            fflush(g_file);
        }
    }
}

void LogShutdown() {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (g_file) { fclose(g_file); g_file = nullptr; }
}

void LogSetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_level = level;
}

LogLevel LogGetLevel() { return g_level; }

void LogWrite(LogLevel level, const wchar_t* fmt, ...) {
    wchar_t msg[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(msg, _countof(msg), _TRUNCATE, fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_mtx);
    if (g_file) {
        SYSTEMTIME st; GetLocalTime(&st);
        // %ls, not %s: with the MSVC/UCRT printf a bare %s takes a narrow string and
        // would stop at the first NUL byte, truncating every wide message to one char.
        fwprintf(g_file, L"[%02d:%02d:%02d.%03d] %ls %ls\n",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, LevelTag(level), msg);
        fflush(g_file);
    }
    OutputDebugStringW(L"[LivePaper] ");
    OutputDebugStringW(msg);
    OutputDebugStringW(L"\n");
}

void RecordFailure(const wchar_t* what, long hr) {
    std::lock_guard<std::mutex> lock(g_mtx);
    wcsncpy_s(g_lastWhat, what ? what : L"", _TRUNCATE);
    g_lastHr = hr;
    g_haveLast = true;
}

std::wstring GetLastErrorText() {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (!g_haveLast) return L"";
    wchar_t buf[512];
    swprintf_s(buf, L"%ls (0x%08X)", g_lastWhat, (unsigned)g_lastHr);
    return buf;
}

} // namespace lp
