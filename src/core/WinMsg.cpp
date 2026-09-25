#include "WinMsg.h"
#include "Log.h"
#include <cstdlib>
#include <cstring>

namespace lp {

LRESULT SendMessageTimeoutW_Int(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT timeoutMs) {
    if (!hwnd || !IsWindow(hwnd)) return 0;
    DWORD_PTR result = 0;
    // ABORTIFHUNG: a stuck Explorer must never stall the engine.
    if (!SendMessageTimeoutW(hwnd, msg, wp, lp, SMTO_ABORTIFHUNG | SMTO_NORMAL, timeoutMs, &result)) {
        LP_LOGD(L"winmsg: msg 0x%04X failed (%lu)", msg, GetLastError());
        return 0;
    }
    return (LRESULT)result;
}

RemoteBuffer::RemoteBuffer(HWND target, size_t bytes) : m_target(target), m_bytes(bytes) {
    if (!target || !bytes) return;
    GetWindowThreadProcessId(target, &m_pid);
    if (!m_pid) return;

    if (m_pid == GetCurrentProcessId()) {
        m_heap = std::calloc(1, bytes);
        if (!m_heap) return;
        m_addr = m_heap;
        m_local = true;
        return;
    }

    // PROCESS_VM_WRITE lets us seed the buffer; PROCESS_VM_READ reads results back
    // when the target does not share our mapping.
    m_proc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, m_pid);
    if (!m_proc) {
        LP_LOGD(L"winmsg: OpenProcess(pid=%lu) failed (%lu)", m_pid, GetLastError());
        return;
    }
    m_addr = VirtualAllocEx(m_proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!m_addr) {
        LP_LOGD(L"winmsg: VirtualAllocEx failed (%lu)", GetLastError());
        CloseHandle(m_proc);
        m_proc = nullptr;
    }
}

void RemoteBuffer::Reset() {
    if (m_local) {
        if (m_heap) { std::free(m_heap); m_heap = nullptr; }
        m_addr = nullptr;
        return;
    }
    if (m_proc) {
        if (m_addr) VirtualFreeEx(m_proc, m_addr, 0, MEM_RELEASE);
        CloseHandle(m_proc);
        m_proc = nullptr;
    }
    m_addr = nullptr;
}

RemoteBuffer::~RemoteBuffer() { Reset(); }

RemoteBuffer::RemoteBuffer(RemoteBuffer&& o) noexcept {
    *this = std::move(o);
}

RemoteBuffer& RemoteBuffer::operator=(RemoteBuffer&& o) noexcept {
    if (this == &o) return *this;
    Reset();
    m_target = o.m_target; m_pid = o.m_pid; m_addr = o.m_addr; m_heap = o.m_heap;
    m_bytes = o.m_bytes; m_local = o.m_local; m_proc = o.m_proc;
    o.m_addr = nullptr; o.m_heap = nullptr; o.m_proc = nullptr;
    return *this;
}

void RemoteBuffer::Zero() {
    if (!m_addr || !m_bytes) return;
    if (m_local) {
        std::memset(m_heap, 0, m_bytes);
        return;
    }
    // Write zeros into the target so a partial/failed write is detectable.
    BYTE zeros[64] = {};
    size_t done = 0;
    while (done < m_bytes) {
        size_t chunk = (m_bytes - done) < sizeof(zeros) ? (m_bytes - done) : sizeof(zeros);
        SIZE_T written = 0;
        if (!WriteProcessMemory(m_proc, (BYTE*)m_addr + done, zeros, chunk, &written) || written != chunk)
            break;
        done += chunk;
    }
}

bool RemoteBuffer::Read(void* out, size_t bytes) const {
    if (!m_addr || bytes > m_bytes) return false;
    if (m_local) {
        std::memcpy(out, m_heap, bytes);
        return true;
    }
    SIZE_T got = 0;
    if (!ReadProcessMemory(m_proc, m_addr, out, bytes, &got) || got != bytes) return false;
    return true;
}

} // namespace lp
