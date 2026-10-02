// Live Shan Shui - cross-process window message + shared-memory helpers.
//
// Some shell queries write their results through a pointer (LVM_GETITEMPOSITION),
// and USER32 does not marshal those buffers across process boundaries. We therefore
// stage the buffer inside the target process. When that is not permitted (a higher
// integrity level, or a shell that simply refuses), callers must have a fallback -
// every user of this API is written to degrade rather than draw garbage.
#pragma once
#include <windows.h>
#include <cstddef>

namespace lp {

LRESULT SendMessageTimeoutW_Int(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT timeoutMs = 200);

// RAII staging buffer living in `target`'s address space.
//
//   RemoteBuffer buf(target, sizeof(POINT));
//   if (buf) {
//       buf.Zero();
//       SendMessageTimeoutW_Int(target, LVM_GETITEMPOSITION, 0, (LPARAM)buf.Remote());
//       POINT p = buf.Read<POINT>();
//   }
//
// Same-process targets transparently use a plain local buffer.
class RemoteBuffer {
public:
    RemoteBuffer() = default;
    RemoteBuffer(HWND target, size_t bytes);
    ~RemoteBuffer();
    RemoteBuffer(RemoteBuffer&& o) noexcept;
    RemoteBuffer& operator=(RemoteBuffer&& o) noexcept;
    RemoteBuffer(const RemoteBuffer&) = delete;
    RemoteBuffer& operator=(const RemoteBuffer&) = delete;

    explicit operator bool() const { return m_addr != nullptr; }
    PVOID Remote() const { return m_addr; }
    bool IsLocal() const { return m_local; }

    void Zero();
    // Reads `bytes` from the staging area into `out`.
    bool Read(void* out, size_t bytes) const;

    template <typename T>
    bool Read(T& out) const { return Read(&out, sizeof(T)); }

private:
    void Reset();

    HWND m_target = nullptr;
    DWORD m_pid = 0;
    PVOID m_addr = nullptr;      // address valid in the target (or ours when local)
    void* m_heap = nullptr;      // local backing allocation for the local case
    size_t m_bytes = 0;
    bool m_local = false;
    HANDLE m_proc = nullptr;
};

} // namespace lp
