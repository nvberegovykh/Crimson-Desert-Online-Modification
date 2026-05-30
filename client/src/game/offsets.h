// SPDX-License-Identifier: MIT
//
// Runtime-discovered game memory layout. Every field here is filled in by the
// signature scanner at startup (see scanner.cpp). A value of 0 means "not
// found" and callers must fail closed.

#pragma once

#include <cstdint>

namespace cdmp {

// Field offsets are byte offsets from the local player entity base pointer.
struct GameOffsets {
    // --- Absolute / module-relative bases (resolved by scanner) -----------
    uintptr_t playerBasePtr = 0;   // address of pointer to local player entity
    uintptr_t entityListPtr = 0;   // entity component manager / list base
    uintptr_t inventoryArrayPtr = 0; // address of pointer to item array
    uintptr_t itemCountPtr = 0;    // address of the item-count integer
    uintptr_t itemUseFn = 0;       // item interaction function
    uintptr_t attackTriggerFn = 0; // melee/attack trigger function
    uintptr_t skillTriggerFn = 0;  // skill/magic activation function
    uintptr_t cutsceneFn = 0;      // cutscene start/end routine (hook target)
    uintptr_t cinematicFlagPtr = 0;// bool flag near the camera component
    uintptr_t puzzleInputFn = 0;   // puzzle/interaction input handler
    uintptr_t puzzleTriggerFn = 0; // puzzle object interaction trigger

    // --- Field offsets within the player entity struct --------------------
    uint32_t offPositionX = 0;     // float[3] world position
    uint32_t offVelocityX = 0;     // float[3] velocity
    uint32_t offRotationYaw = 0;   // float yaw
    uint32_t offRotationPitch = 0; // float pitch
    uint32_t offHealth = 0;        // float current health
    uint32_t offMaxHealth = 0;     // float max health
    uint32_t offAnimId = 0;        // int active animation state id
    uint32_t offAnimTime = 0;      // float normalized playback time
    uint32_t offMoveFlags = 0;     // uint32 movement bitmask
    uint32_t offCombatFlags = 0;   // uint32 combat bitmask
    uint32_t offBlendWeight = 0;   // float locomotion blend weight
    uint32_t offEntityId = 0;      // uint64 entity network id

    // --- Parkour / traversal (Task 1) -------------------------------------
    uint32_t traversalFlagsOffset = 0;   // bitmask near velocity; high bits=mode
    uint32_t traversalTargetPosOffset = 0; // Vec3 written by engine on vault/climb

    // --- Full gameplay sync component offsets (Task 2) --------------------
    // Each is a byte offset from the entity base to a component or field. A
    // value of 0 means the scanner couldn't resolve it; callers must skip.
    uint32_t skillComponentOffset = 0;     // combat/skill component near health
    uint32_t vfxComponentOffset = 0;       // pointer near entity transform
    uint32_t statusComponentOffset = 0;    // status bitmask near health
    uint32_t equipmentComponentOffset = 0; // equipment block near entity base
    uint32_t mountEntityPtrOffset = 0;     // pointer to mount entity in player
    uint32_t dodgeComponentOffset = 0;     // movement/dodge block near velocity

    // Item struct size and field offsets used when appending the beacon.
    uint32_t itemStride = 0;       // bytes per item slot
    uint32_t offItemId = 0;        // int item id within a slot
    uint32_t offItemQuantity = 0;  // int quantity
    uint32_t offItemDurability = 0;// int durability
    uint32_t offItemFlags = 0;     // int flags
    uint32_t offItemNameHash = 0;  // uint32 name hash

    bool playerLayoutResolved() const {
        return playerBasePtr != 0 && offPositionX != 0 && offHealth != 0 &&
               offAnimId != 0;
    }
    bool inventoryResolved() const {
        return inventoryArrayPtr != 0 && itemCountPtr != 0 && itemStride != 0;
    }
};

// Single global instance, populated once during init.
extern GameOffsets g_offsets;

// Identifiers for the synthetic "Multiplayer Beacon" item.
constexpr int32_t MULTIPLAYER_BEACON_ID = 0xCD4D50; // "CDMP"
constexpr uint32_t MULTIPLAYER_BEACON_NAME_HASH = 0xB3AC0411u;

} // namespace cdmp
