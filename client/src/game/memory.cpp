// SPDX-License-Identifier: MIT

#include "memory.h"
#include "offsets.h"

#include <windows.h>

namespace cdmp {

bool IsReadable(uintptr_t addr, size_t size) {
    if (addr == 0) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0) {
        return false;
    }
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & readable) == 0) return false;
    if (mbi.Protect & PAGE_GUARD) return false;
    // Ensure the whole range is inside the queried region.
    const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    return addr + size <= regionBase + mbi.RegionSize;
}

bool SafeReadRaw(uintptr_t addr, void* out, size_t size) {
    if (!IsReadable(addr, size)) return false;
    __try {
        memcpy(out, reinterpret_cast<const void*>(addr), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeWriteRaw(uintptr_t addr, const void* in, size_t size) {
    if (addr == 0) return false;
    // Make sure the page is writable; restore protection afterwards.
    DWORD oldProtect = 0;
    const bool changed = VirtualProtect(reinterpret_cast<LPVOID>(addr), size,
                                        PAGE_EXECUTE_READWRITE, &oldProtect) != 0;
    bool ok = false;
    __try {
        memcpy(reinterpret_cast<void*>(addr), in, size);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (changed) {
        DWORD tmp = 0;
        VirtualProtect(reinterpret_cast<LPVOID>(addr), size, oldProtect, &tmp);
    }
    return ok;
}

uintptr_t GetLocalPlayerBase() {
    if (g_offsets.playerBasePtr == 0) return 0;
    return SafeRead<uintptr_t>(g_offsets.playerBasePtr);
}

Vec3 ReadVec3(uintptr_t base, uint32_t offset) {
    Vec3 v;
    if (base == 0 || offset == 0) return v;
    v.x = SafeRead<float>(base + offset + 0);
    v.y = SafeRead<float>(base + offset + 4);
    v.z = SafeRead<float>(base + offset + 8);
    return v;
}

void WriteVec3(uintptr_t base, uint32_t offset, const Vec3& v) {
    if (base == 0 || offset == 0) return;
    SafeWrite<float>(base + offset + 0, v.x);
    SafeWrite<float>(base + offset + 4, v.y);
    SafeWrite<float>(base + offset + 8, v.z);
}

float ReadFloat(uintptr_t base, uint32_t offset) {
    if (base == 0 || offset == 0) return 0.f;
    return SafeRead<float>(base + offset);
}

void WriteFloat(uintptr_t base, uint32_t offset, float value) {
    if (base == 0 || offset == 0) return;
    SafeWrite<float>(base + offset, value);
}

int32_t ReadInt(uintptr_t base, uint32_t offset) {
    if (base == 0 || offset == 0) return 0;
    return SafeRead<int32_t>(base + offset);
}

void WriteInt(uintptr_t base, uint32_t offset, int32_t value) {
    if (base == 0 || offset == 0) return;
    SafeWrite<int32_t>(base + offset, value);
}

uint32_t ReadU32(uintptr_t base, uint32_t offset) {
    if (base == 0 || offset == 0) return 0;
    return SafeRead<uint32_t>(base + offset);
}

void WriteU32(uintptr_t base, uint32_t offset, uint32_t value) {
    if (base == 0 || offset == 0) return;
    SafeWrite<uint32_t>(base + offset, value);
}

uint64_t ReadU64(uintptr_t base, uint32_t offset) {
    if (base == 0 || offset == 0) return 0;
    return SafeRead<uint64_t>(base + offset);
}

} // namespace cdmp
