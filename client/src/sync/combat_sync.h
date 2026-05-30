// SPDX-License-Identifier: MIT
//
// Combat + enemy synchronization. Hooks the attack trigger to emit attack RPCs,
// applies host-adjudicated damage to entity memory, mirrors enemy state from
// the host, and (host-only) broadcasts loot takes.

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "../game/memory.h"
#include <nlohmann/json.hpp>

namespace cdmp {

class WsClient;

struct EnemyMirror {
    std::string id;
    float health = 0.f;
    float maxHealth = 0.f;
    Vec3 position;
    int32_t anim_id = 0;
    uintptr_t entityBase = 0;
};

class CombatSync {
public:
    void SetClient(WsClient* client) { client_ = client; }
    void SetHost(bool isHost) { isHost_ = isHost; }
    void SetLocalId(const std::string& id) { localId_ = id; }

    // Install the attack-trigger hook (no-op if the function was not resolved).
    bool InstallHooks();
    void RemoveHooks();

    // Called by the attack hook trampoline: emit an attack RPC.
    void OnLocalAttack(int32_t animId, const Vec3& direction,
                       uint64_t targetEntityId);

    // Host: validate an incoming attack RPC and broadcast a damage result.
    void HandleAttackRpc(const nlohmann::json& rpc);

    // Guest: apply a damage result from the host.
    void HandleDamageRpc(const nlohmann::json& rpc);

    // Guest: write host-authoritative enemy snapshot into memory.
    void HandleEnemyUpdate(const nlohmann::json& rpc);

    // Host: detect a loot interaction and broadcast LOOT_TAKEN.
    void NotifyLootTaken(const std::string& lootId);

    // Guest: hide/disable a looted world object on notification.
    void HandleLootTaken(const nlohmann::json& payload);

private:
    WsClient* client_ = nullptr;
    bool isHost_ = false;
    bool hooksInstalled_ = false;
    std::string localId_;

    std::mutex mutex_;
    std::map<std::string, EnemyMirror> enemies_;
};

// Global accessor so the C-style hook trampoline can reach the instance.
CombatSync& GetCombatSync();

} // namespace cdmp
