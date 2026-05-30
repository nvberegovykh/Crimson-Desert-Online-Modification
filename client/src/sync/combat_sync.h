// SPDX-License-Identifier: MIT
//
// Combat + enemy synchronization. Hooks the attack trigger to emit attack RPCs,
// applies host-adjudicated damage to entity memory, mirrors enemy state from
// the host, and (host-only) broadcasts loot takes.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>

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

    // Called by the skill hook trampoline: emit a skill RPC.
    void OnLocalSkill(int32_t skillId, int32_t phase, const Vec3& targetPos,
                      int32_t targetEntity);

    // Apply an incoming skill RPC by writing into the source entity's combat
    // component so the engine plays the skill VFX + animation.
    void HandleSkillRpc(const nlohmann::json& rpc);

    // Resolve the remote entity slot for a player id (used by skill writes).
    void SetRemoteEntityResolver(
        std::function<uintptr_t(const std::string&)> resolver) {
        remoteResolver_ = std::move(resolver);
    }

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
    std::function<uintptr_t(const std::string&)> remoteResolver_;
};

// Global accessor so the C-style hook trampoline can reach the instance.
CombatSync& GetCombatSync();

} // namespace cdmp
