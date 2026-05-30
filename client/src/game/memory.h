// SPDX-License-Identifier: MIT
//
// Safe (SEH-guarded) memory read/write helpers and typed accessors used by the
// sync and inventory systems.

#pragma once

#include <cstdint>
#include <cstring>

namespace cdmp {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

// SEH-guarded raw copies. Defined in memory.cpp so the __try/__except blocks
// live in a translation unit compiled with /EHsc semantics that tolerate SEH.
bool SafeReadRaw(uintptr_t addr, void* out, size_t size);
bool SafeWriteRaw(uintptr_t addr, const void* in, size_t size);

template <typename T>
T SafeRead(uintptr_t addr) {
    T value{};
    SafeReadRaw(addr, &value, sizeof(T));
    return value;
}

template <typename T>
bool SafeWrite(uintptr_t addr, const T& value) {
    return SafeWriteRaw(addr, &value, sizeof(T));
}

// Resolve the local player entity base by dereferencing the scanned pointer.
uintptr_t GetLocalPlayerBase();

Vec3 ReadVec3(uintptr_t base, uint32_t offset);
void WriteVec3(uintptr_t base, uint32_t offset, const Vec3& v);

float ReadFloat(uintptr_t base, uint32_t offset);
void WriteFloat(uintptr_t base, uint32_t offset, float value);

int32_t ReadInt(uintptr_t base, uint32_t offset);
void WriteInt(uintptr_t base, uint32_t offset, int32_t value);

uint32_t ReadU32(uintptr_t base, uint32_t offset);
void WriteU32(uintptr_t base, uint32_t offset, uint32_t value);

uint64_t ReadU64(uintptr_t base, uint32_t offset);

// True if `addr` looks like a committed, readable user-space pointer.
bool IsReadable(uintptr_t addr, size_t size = 8);

} // namespace cdmp
