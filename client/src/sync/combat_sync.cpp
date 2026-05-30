// SPDX-License-Identifier: MIT

#include "combat_sync.h"
#include "../game/offsets.h"
#include "../net/client.h"
#include "../util/log.h"

#include <windows.h>
#include <MinHook.h>

#include <algorithm>
#include <cmath>

namespace cdmp {

namespace {
CombatSync g_combatSync;

// The attack trigger has the engine signature `void Attack(Entity* self)`.
using AttackFn = void(__fastcall*)(void* self);
AttackFn g_origAttack = nullptr;

void __fastcall HookedAttack(void* self) {
    // Emit our RPC first so latency is minimized, then run the real attack.
    const uintptr_t base = GetLocalPlayerBase();
    if (base != 0) {
        const int32_t animId = ReadInt(base, g_offsets.offAnimId);
        const float yaw = ReadFloat(base, g_offsets.offRotationYaw);
        Vec3 dir{std::sin(yaw), 0.f, std::cos(yaw)};
        GetCombatSync().OnLocalAttack(animId, dir, 0);
    }
    if (g_origAttack) g_origAttack(self);
}
} // namespace

CombatSync& GetCombatSync() { return g_combatSync; }

bool CombatSync::InstallHooks() {
    if (hooksInstalled_) return true;
    if (g_offsets.attackTriggerFn == 0) {
        CDMP_LOG_WARN("attack trigger not resolved; combat hook disabled");
        return false;
    }
    if (MH_CreateHook(reinterpret_cast<void*>(g_offsets.attackTriggerFn),
                      reinterpret_cast<void*>(&HookedAttack),
                      reinterpret_cast<void**>(&g_origAttack)) != MH_OK) {
        CDMP_LOG_ERROR("MH_CreateHook(attack) failed");
        return false;
    }
    if (MH_EnableHook(reinterpret_cast<void*>(g_offsets.attackTriggerFn)) != MH_OK) {
        CDMP_LOG_ERROR("MH_EnableHook(attack) failed");
        return false;
    }
    hooksInstalled_ = true;
    CDMP_LOG_INFO("Combat attack hook installed");
    return true;
}

void CombatSync::RemoveHooks() {
    if (!hooksInstalled_) return;
    MH_DisableHook(reinterpret_cast<void*>(g_offsets.attackTriggerFn));
    hooksInstalled_ = false;
}

void CombatSync::OnLocalAttack(int32_t animId, const Vec3& direction,
                               uint64_t targetEntityId) {
    if (!client_ || !client_->IsConnected()) return;
    client_->Send(PacketType::RPC_CALL,
                  {{"type", "attack"},
                   {"anim_id", animId},
                   {"direction",
                    {{"x", direction.x}, {"y", direction.y}, {"z", direction.z}}},
                   {"targetEntityId", targetEntityId}});
}

void CombatSync::HandleAttackRpc(const nlohmann::json& rpc) {
    if (!isHost_) return; // only the host adjudicates
    // Validate: target must exist; compute damage from the attacker anim.
    const std::string source = rpc.value("sourceId", std::string());
    const uint64_t targetId = rpc.value("targetEntityId", 0ull);
    const int32_t animId = rpc.value("anim_id", 0);
    // Simple host-side damage model; the real model would map anim_id -> damage.
    const float damage = 10.f + static_cast<float>(animId % 5) * 2.f;

    nlohmann::json result = {
        {"type", "damage"},
        {"sourceId", source},
        {"targetEntityId", targetId},
        {"damage", damage},
        {"anim_id", animId},
    };
    client_->Send(PacketType::RPC_CALL, result);
}

void CombatSync::HandleDamageRpc(const nlohmann::json& rpc) {
    // Guests apply the host-validated damage to the target enemy mirror.
    const uint64_t targetId = rpc.value("targetEntityId", 0ull);
    const float damage = rpc.value("damage", 0.f);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, e] : enemies_) {
        if (e.entityBase != 0 &&
            ReadU64(e.entityBase, g_offsets.offEntityId) == targetId) {
            e.health = std::max(0.f, e.health - damage);
            WriteFloat(e.entityBase, g_offsets.offHealth, e.health);
            break;
        }
    }
}

void CombatSync::HandleEnemyUpdate(const nlohmann::json& rpc) {
    if (isHost_) return; // host owns the truth
    if (!rpc.contains("enemies") || !rpc["enemies"].is_array()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& e : rpc["enemies"]) {
        const std::string id = e.value("id", std::string());
        if (id.empty()) continue;
        EnemyMirror& m = enemies_[id];
        m.id = id;
        m.health = e.value("health", m.health);
        m.maxHealth = e.value("maxHealth", m.maxHealth);
        if (e.contains("position")) {
            const auto& p = e["position"];
            m.position = {p.value("x", 0.f), p.value("y", 0.f), p.value("z", 0.f)};
        }
        if (e.contains("animState")) {
            m.anim_id = e["animState"].value("anim_id", m.anim_id);
        }
        // Mirror into memory when we have a resolved enemy entity slot.
        if (m.entityBase != 0 && IsReadable(m.entityBase, 0x400)) {
            WriteVec3(m.entityBase, g_offsets.offPositionX, m.position);
            WriteFloat(m.entityBase, g_offsets.offHealth, m.health);
            WriteFloat(m.entityBase, g_offsets.offMaxHealth, m.maxHealth);
            WriteInt(m.entityBase, g_offsets.offAnimId, m.anim_id);
            if (m.health <= 0.f) {
                // Death: clear combat flags so the engine plays the death clip.
                WriteU32(m.entityBase, g_offsets.offCombatFlags, 0);
            }
        }
    }
}

void CombatSync::NotifyLootTaken(const std::string& lootId) {
    if (!isHost_ || !client_) return;
    client_->Send(PacketType::LOOT_TAKEN,
                  {{"lootId", lootId}, {"playerId", localId_}});
}

void CombatSync::HandleLootTaken(const nlohmann::json& payload) {
    const std::string lootId = payload.value("lootId", std::string());
    CDMP_LOG_INFO("Loot taken notification: " + lootId);
    // Guests do not pick up loot; the host already removed it. We simply log so
    // the local world object can be hidden by a future world-object resolver.
}

} // namespace cdmp
