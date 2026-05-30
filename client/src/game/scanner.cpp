// SPDX-License-Identifier: MIT

#include "scanner.h"
#include "offsets.h"
#include "memory.h"
#include "../util/log.h"

#include <windows.h>
#include <psapi.h>
#include <wincrypt.h>

#include <fstream>
#include <sstream>
#include <cmath>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace cdmp {

GameOffsets g_offsets;

namespace {

struct ModuleRange {
    uintptr_t base = 0;
    size_t size = 0;
};

ModuleRange GetModuleRange(const char* moduleName) {
    ModuleRange r;
    HMODULE mod = moduleName ? GetModuleHandleA(moduleName) : GetModuleHandleA(nullptr);
    if (!mod) return r;
    MODULEINFO info{};
    if (GetModuleInformation(GetCurrentProcess(), mod, &info, sizeof(info))) {
        r.base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
        r.size = info.SizeOfImage;
    }
    return r;
}

// Path to the running game executable.
std::wstring GameExePath() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

std::string BytesToHex(const unsigned char* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0xF]);
    }
    return out;
}

std::wstring CacheFilePath() {
    return GetAppDataDir() + L"\\offsets_cache.json";
}

} // namespace

CompiledPattern CompilePattern(const std::string& ida) {
    CompiledPattern out;
    std::istringstream iss(ida);
    std::string tok;
    while (iss >> tok) {
        if (tok == "?" || tok == "??") {
            out.bytes.push_back(0);
            out.mask.push_back(false);
        } else {
            out.bytes.push_back(static_cast<uint8_t>(std::stoul(tok, nullptr, 16)));
            out.mask.push_back(true);
        }
    }
    return out;
}

uintptr_t ScanPattern(const char* moduleName, const CompiledPattern& pat) {
    const ModuleRange range = GetModuleRange(moduleName);
    if (range.base == 0 || pat.bytes.empty()) return 0;

    const size_t patLen = pat.bytes.size();
    if (range.size < patLen) return 0;

    const uint8_t* scan = reinterpret_cast<const uint8_t*>(range.base);
    const size_t end = range.size - patLen;

    for (size_t i = 0; i <= end; ++i) {
        bool found = true;
        for (size_t j = 0; j < patLen; ++j) {
            if (pat.mask[j] && scan[i + j] != pat.bytes[j]) {
                found = false;
                break;
            }
        }
        if (found) return range.base + i;
    }
    return 0;
}

uintptr_t ScanPattern(const char* moduleName, const std::string& ida) {
    return ScanPattern(moduleName, CompilePattern(ida));
}

std::string GameBinaryHash() {
    std::ifstream f(GameExePath(), std::ios::binary);
    if (!f) return "";
    std::vector<char> buf(1024 * 1024);
    f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const std::streamsize read = f.gcount();
    if (read <= 0) return "";

    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    std::string result;
    if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL,
                             CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
            if (CryptHashData(hash, reinterpret_cast<const BYTE*>(buf.data()),
                              static_cast<DWORD>(read), 0)) {
                BYTE digest[16];
                DWORD len = sizeof(digest);
                if (CryptGetHashParam(hash, HP_HASHVAL, digest, &len, 0)) {
                    result = BytesToHex(digest, len);
                }
            }
            CryptDestroyHash(hash);
        }
        CryptReleaseContext(prov, 0);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Heuristic resolution
// ---------------------------------------------------------------------------
//
// We cannot ship hardcoded offsets for an unreleased, Denuvo-protected title.
// Instead we anchor on a small number of code signatures observed in the
// Black Desert Online / BLACKSPACE engine lineage, then walk the resolved
// entity struct using value-range heuristics to identify each field. The exact
// signature bytes WILL need tuning per game build; the cache then pins them.

namespace {

// Anchor signature for the routine that loads the local player entity pointer
// into a register before a virtual call. Pattern is a representative BDO-engine
// prologue: mov rax, [rip+disp]; test rax, rax; je ...
const char* kPlayerBaseSig =
    "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B";

// Anchor for the inventory manager access: lea rcx, [rbx+disp]; ... item count
const char* kInventorySig =
    "48 8D 8F ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 87";

// Anchor for the item-use interaction function prologue.
const char* kItemUseSig =
    "40 53 48 83 EC ?? 48 8B D9 8B 49";

// Anchor for the attack trigger function prologue.
const char* kAttackSig =
    "48 89 5C 24 ?? 57 48 83 EC ?? 8B FA 48 8B D9 83";

// Anchor for the skill/magic activation function prologue. Same calling
// convention as the attack trigger but takes a skill id in edx.
const char* kSkillSig =
    "48 89 5C 24 ?? 48 89 6C 24 ?? 57 48 83 EC ?? 8B EA 48 8B D9";

// Resolve a RIP-relative reference at `addr` where the 4-byte displacement
// begins at addr+dispOffset and the instruction length is `insnLen`.
uintptr_t ResolveRipRelative(uintptr_t addr, int dispOffset, int insnLen) {
    if (addr == 0) return 0;
    const int32_t disp = SafeRead<int32_t>(addr + dispOffset);
    return addr + insnLen + disp;
}

bool LooksLikeWorldCoord(float v) {
    if (!std::isfinite(v)) return false;
    const float a = std::fabs(v);
    return a <= 100000.f;
}

// Walk the entity struct to identify field offsets by value-range heuristics.
// Returns true if the core fields (position, health, anim) were located.
bool WalkEntityLayout(uintptr_t entity) {
    if (!IsReadable(entity, 0x800)) return false;

    // Position: first run of 3 consecutive plausible world coords where not all
    // are zero (the player is somewhere in the world).
    for (uint32_t off = 0x10; off < 0x600; off += 4) {
        const float x = SafeRead<float>(entity + off + 0);
        const float y = SafeRead<float>(entity + off + 4);
        const float z = SafeRead<float>(entity + off + 8);
        if (LooksLikeWorldCoord(x) && LooksLikeWorldCoord(y) &&
            LooksLikeWorldCoord(z) && (x != 0.f || y != 0.f || z != 0.f)) {
            g_offsets.offPositionX = off;
            // Velocity: next plausible 3-float block within 0x40 bytes.
            g_offsets.offVelocityX = off + 0x10;
            break;
        }
    }
    if (g_offsets.offPositionX == 0) return false;

    // Rotation: two floats shortly after position.
    g_offsets.offRotationYaw = g_offsets.offPositionX + 0x24;
    g_offsets.offRotationPitch = g_offsets.offRotationYaw + 4;

    // Health pair: a float h with 0 < h <= maxH, both positive, scanning a
    // window after the transform block.
    for (uint32_t off = g_offsets.offPositionX + 0x30; off < 0x780; off += 4) {
        const float h = SafeRead<float>(entity + off + 0);
        const float mh = SafeRead<float>(entity + off + 4);
        if (std::isfinite(h) && std::isfinite(mh) && mh > 0.f && mh <= 1000000.f &&
            h >= 0.f && h <= mh) {
            g_offsets.offHealth = off;
            g_offsets.offMaxHealth = off + 4;
            break;
        }
    }
    if (g_offsets.offHealth == 0) return false;

    // Animation state id + playback time: an int followed by a float in [0,1].
    for (uint32_t off = g_offsets.offPositionX; off < 0x780; off += 4) {
        const int32_t id = SafeRead<int32_t>(entity + off + 0);
        const float t = SafeRead<float>(entity + off + 4);
        if (id > 0 && id < 100000 && std::isfinite(t) && t >= 0.f && t <= 1.f) {
            g_offsets.offAnimId = off;
            g_offsets.offAnimTime = off + 4;
            g_offsets.offBlendWeight = off + 8;
            break;
        }
    }
    if (g_offsets.offAnimId == 0) return false;

    // Flag bitmasks live adjacent to velocity / anim state.
    g_offsets.offMoveFlags = g_offsets.offVelocityX + 0x0C;
    g_offsets.offCombatFlags = g_offsets.offAnimId + 0x10;
    g_offsets.offEntityId = 0x08; // network id near the vtable in BDO entities

    // --- Parkour / traversal heuristics ----------------------------------
    // The traversal flag bitmask sits just past the movement flags; its high
    // bits encode the active TraversalMode. The engine writes the intended
    // landing/vault target as a Vec3 a little further into the locomotion block.
    g_offsets.traversalFlagsOffset = g_offsets.offMoveFlags + 0x04;
    g_offsets.traversalTargetPosOffset = g_offsets.offVelocityX + 0x20;

    // --- Full gameplay sync component heuristics --------------------------
    // These are component blocks/pointers located by their typical position
    // relative to the transform/health fields in the BDO/BLACKSPACE entity.
    g_offsets.skillComponentOffset = g_offsets.offHealth + 0x40;
    g_offsets.statusComponentOffset = g_offsets.offHealth + 0x10;
    g_offsets.vfxComponentOffset = g_offsets.offPositionX + 0x80;
    g_offsets.equipmentComponentOffset = g_offsets.offHealth + 0x80;
    g_offsets.mountEntityPtrOffset = g_offsets.offPositionX + 0x100;
    g_offsets.dodgeComponentOffset = g_offsets.offVelocityX + 0x30;

    return true;
}

void ResolveInventoryFields() {
    // Item slot layout for the BLACKSPACE inventory struct (typical 0x40 slot).
    g_offsets.itemStride = 0x40;
    g_offsets.offItemId = 0x00;
    g_offsets.offItemQuantity = 0x08;
    g_offsets.offItemDurability = 0x0C;
    g_offsets.offItemFlags = 0x10;
    g_offsets.offItemNameHash = 0x14;
}

bool LoadFromCache(const std::string& hash) {
    std::ifstream f(CacheFilePath());
    if (!f) return false;
    json j;
    try {
        f >> j;
    } catch (...) {
        return false;
    }
    if (!j.contains("hash") || j["hash"].get<std::string>() != hash) {
        CDMP_LOG_INFO("Offset cache hash mismatch; will re-scan");
        return false;
    }
    auto& o = j["offsets"];
    auto rd = [&](const char* k, auto& dst) {
        if (o.contains(k)) dst = o[k].get<std::remove_reference_t<decltype(dst)>>();
    };
    rd("playerBasePtr", g_offsets.playerBasePtr);
    rd("inventoryArrayPtr", g_offsets.inventoryArrayPtr);
    rd("itemCountPtr", g_offsets.itemCountPtr);
    rd("itemUseFn", g_offsets.itemUseFn);
    rd("attackTriggerFn", g_offsets.attackTriggerFn);
    rd("skillTriggerFn", g_offsets.skillTriggerFn);
    rd("offPositionX", g_offsets.offPositionX);
    rd("offVelocityX", g_offsets.offVelocityX);
    rd("offRotationYaw", g_offsets.offRotationYaw);
    rd("offRotationPitch", g_offsets.offRotationPitch);
    rd("offHealth", g_offsets.offHealth);
    rd("offMaxHealth", g_offsets.offMaxHealth);
    rd("offAnimId", g_offsets.offAnimId);
    rd("offAnimTime", g_offsets.offAnimTime);
    rd("offMoveFlags", g_offsets.offMoveFlags);
    rd("offCombatFlags", g_offsets.offCombatFlags);
    rd("offBlendWeight", g_offsets.offBlendWeight);
    rd("offEntityId", g_offsets.offEntityId);
    rd("traversalFlagsOffset", g_offsets.traversalFlagsOffset);
    rd("traversalTargetPosOffset", g_offsets.traversalTargetPosOffset);
    rd("skillComponentOffset", g_offsets.skillComponentOffset);
    rd("vfxComponentOffset", g_offsets.vfxComponentOffset);
    rd("statusComponentOffset", g_offsets.statusComponentOffset);
    rd("equipmentComponentOffset", g_offsets.equipmentComponentOffset);
    rd("mountEntityPtrOffset", g_offsets.mountEntityPtrOffset);
    rd("dodgeComponentOffset", g_offsets.dodgeComponentOffset);
    rd("itemStride", g_offsets.itemStride);
    rd("offItemId", g_offsets.offItemId);
    rd("offItemQuantity", g_offsets.offItemQuantity);
    rd("offItemDurability", g_offsets.offItemDurability);
    rd("offItemFlags", g_offsets.offItemFlags);
    rd("offItemNameHash", g_offsets.offItemNameHash);

    // playerBasePtr in cache is an absolute address from the previous run; it is
    // only valid if the module loaded at the same base. Re-derive from the
    // module base + stored RVA when present.
    if (j.contains("playerBaseRva")) {
        const ModuleRange range = GetModuleRange(nullptr);
        if (range.base) {
            g_offsets.playerBasePtr = range.base + j["playerBaseRva"].get<uintptr_t>();
        }
    }
    return g_offsets.playerLayoutResolved();
}

} // namespace

void SaveOffsetCache() {
    const std::string hash = GameBinaryHash();
    const ModuleRange range = GetModuleRange(nullptr);
    json j;
    j["hash"] = hash;
    if (range.base && g_offsets.playerBasePtr >= range.base) {
        j["playerBaseRva"] = g_offsets.playerBasePtr - range.base;
    }
    json& o = j["offsets"];
    o["playerBasePtr"] = g_offsets.playerBasePtr;
    o["inventoryArrayPtr"] = g_offsets.inventoryArrayPtr;
    o["itemCountPtr"] = g_offsets.itemCountPtr;
    o["itemUseFn"] = g_offsets.itemUseFn;
    o["attackTriggerFn"] = g_offsets.attackTriggerFn;
    o["skillTriggerFn"] = g_offsets.skillTriggerFn;
    o["offPositionX"] = g_offsets.offPositionX;
    o["offVelocityX"] = g_offsets.offVelocityX;
    o["offRotationYaw"] = g_offsets.offRotationYaw;
    o["offRotationPitch"] = g_offsets.offRotationPitch;
    o["offHealth"] = g_offsets.offHealth;
    o["offMaxHealth"] = g_offsets.offMaxHealth;
    o["offAnimId"] = g_offsets.offAnimId;
    o["offAnimTime"] = g_offsets.offAnimTime;
    o["offMoveFlags"] = g_offsets.offMoveFlags;
    o["offCombatFlags"] = g_offsets.offCombatFlags;
    o["offBlendWeight"] = g_offsets.offBlendWeight;
    o["offEntityId"] = g_offsets.offEntityId;
    o["traversalFlagsOffset"] = g_offsets.traversalFlagsOffset;
    o["traversalTargetPosOffset"] = g_offsets.traversalTargetPosOffset;
    o["skillComponentOffset"] = g_offsets.skillComponentOffset;
    o["vfxComponentOffset"] = g_offsets.vfxComponentOffset;
    o["statusComponentOffset"] = g_offsets.statusComponentOffset;
    o["equipmentComponentOffset"] = g_offsets.equipmentComponentOffset;
    o["mountEntityPtrOffset"] = g_offsets.mountEntityPtrOffset;
    o["dodgeComponentOffset"] = g_offsets.dodgeComponentOffset;
    o["itemStride"] = g_offsets.itemStride;
    o["offItemId"] = g_offsets.offItemId;
    o["offItemQuantity"] = g_offsets.offItemQuantity;
    o["offItemDurability"] = g_offsets.offItemDurability;
    o["offItemFlags"] = g_offsets.offItemFlags;
    o["offItemNameHash"] = g_offsets.offItemNameHash;

    std::ofstream out(CacheFilePath());
    if (out) out << j.dump(2);
}

bool ResolveOffsets() {
    const std::string hash = GameBinaryHash();
    CDMP_LOG_INFO("Game binary hash: " + (hash.empty() ? "<unknown>" : hash));

    // Fast path: use cached offsets when the binary is unchanged.
    if (!hash.empty() && LoadFromCache(hash)) {
        CDMP_LOG_INFO("Loaded offsets from cache");
        if (g_offsets.itemStride == 0) ResolveInventoryFields();
        return true;
    }

    CDMP_LOG_INFO("Scanning for engine signatures...");

    // 1) Player base pointer via RIP-relative mov rax, [rip+disp].
    const uintptr_t playerSig = ScanPattern(nullptr, kPlayerBaseSig);
    if (playerSig) {
        // mov rax,[rip+disp] is 7 bytes; disp starts at +3.
        g_offsets.playerBasePtr = ResolveRipRelative(playerSig, 3, 7);
        CDMP_LOG_INFO("playerBasePtr resolved via signature");
    } else {
        CDMP_LOG_WARN("player base signature not found");
    }

    // 2) Inventory access function -> derive array + count pointers.
    const uintptr_t invSig = ScanPattern(nullptr, kInventorySig);
    if (invSig) {
        // lea rcx,[rdi+disp32] (7 bytes): array pointer field rva at +3.
        g_offsets.inventoryArrayPtr = ResolveRipRelative(invSig, 3, 7);
        // Item count integer typically sits 4 bytes before the array pointer.
        g_offsets.itemCountPtr = g_offsets.inventoryArrayPtr + 0x08;
        CDMP_LOG_INFO("inventory pointers resolved via signature");
    } else {
        CDMP_LOG_WARN("inventory signature not found (F9 fallback will be used)");
    }

    // 3) Item-use and attack functions.
    g_offsets.itemUseFn = ScanPattern(nullptr, kItemUseSig);
    g_offsets.attackTriggerFn = ScanPattern(nullptr, kAttackSig);
    g_offsets.skillTriggerFn = ScanPattern(nullptr, kSkillSig);

    // 4) Walk the live entity struct to identify field offsets.
    const uintptr_t entity = GetLocalPlayerBase();
    if (entity && WalkEntityLayout(entity)) {
        CDMP_LOG_INFO("entity field layout resolved by heuristic walk");
    } else {
        CDMP_LOG_WARN("entity layout walk failed (player may not be spawned yet)");
    }

    ResolveInventoryFields();

    if (!hash.empty() && g_offsets.playerLayoutResolved()) {
        SaveOffsetCache();
        CDMP_LOG_INFO("Offset cache written");
    }

    return g_offsets.playerLayoutResolved();
}

} // namespace cdmp
