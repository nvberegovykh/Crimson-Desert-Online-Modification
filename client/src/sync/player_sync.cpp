// SPDX-License-Identifier: MIT

#include "player_sync.h"
#include "../game/offsets.h"
#include "../net/client.h"
#include "../util/log.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cdmp {

namespace {
constexpr double kSendIntervalMs = 50.0; // 20Hz
constexpr double kInterpBufferMs = 100.0;
// Reduced buffer + faster convergence used by the no-offset traversal fallback.
constexpr double kFallbackBufferMs = 50.0;

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

bool VecDiff(const Vec3& a, const Vec3& b, float eps) {
    return std::fabs(a.x - b.x) > eps || std::fabs(a.y - b.y) > eps ||
           std::fabs(a.z - b.z) > eps;
}

// Decode the engine traversal bitmask into a TraversalMode. The high nibble of
// the flags word carries the active traversal action in the BDO/BLACKSPACE
// locomotion state; 0 means ordinary grounded locomotion.
int TraversalModeFromFlags(uint32_t flags) {
    const uint32_t mode = (flags >> 24) & 0x0F;
    if (mode > TM_LAND) return TM_NONE;
    return static_cast<int>(mode);
}
} // namespace

void PlayerSync::Tick(double nowMs) {
    SendLocalState(nowMs);
    ApplyRemoteState(nowMs);
}

void PlayerSync::SendLocalState(double nowMs) {
    if (!client_ || !client_->IsConnected()) return;
    if (suspended_.load()) return; // e.g. local player is in a cutscene
    if (nowMs - lastSendMs_ < kSendIntervalMs) return;
    lastSendMs_ = nowMs;

    const uintptr_t base = GetLocalPlayerBase();
    if (base == 0 || !g_offsets.playerLayoutResolved()) return;

    const Vec3 pos = ReadVec3(base, g_offsets.offPositionX);
    const Vec3 vel = ReadVec3(base, g_offsets.offVelocityX);
    const float yaw = ReadFloat(base, g_offsets.offRotationYaw);
    const float pitch = ReadFloat(base, g_offsets.offRotationPitch);
    const float health = ReadFloat(base, g_offsets.offHealth);
    const float maxHealth = ReadFloat(base, g_offsets.offMaxHealth);

    AnimStateData anim;
    anim.anim_id = ReadInt(base, g_offsets.offAnimId);
    anim.anim_time = ReadFloat(base, g_offsets.offAnimTime);
    anim.move_flags = ReadU32(base, g_offsets.offMoveFlags);
    anim.combat_flags = ReadU32(base, g_offsets.offCombatFlags);
    anim.blend_weight = ReadFloat(base, g_offsets.offBlendWeight);

    const bool moved = VecDiff(pos, lastPos_, 0.01f);
    const bool healthChanged = std::fabs(health - lastHealth_) > 0.5f;
    const bool animChanged = anim.anim_id != lastAnim_.anim_id ||
                             anim.move_flags != lastAnim_.move_flags ||
                             anim.combat_flags != lastAnim_.combat_flags ||
                             std::fabs(anim.anim_time - lastAnim_.anim_time) > 0.02f;

    // Read traversal + full gameplay state every send; these always ride along
    // with the state update so remotes see magic/buffs/weapons/mounts/death.
    const PlayerStatePacket gp = ReadLocalGameplay(base);
    const bool inTraversal = gp.traversal.present &&
                             gp.traversal.mode != TM_NONE;

    if (!moved && !healthChanged && !animChanged && !inTraversal) return;

    nlohmann::json animJson = {
        {"anim_id", anim.anim_id},      {"anim_time", anim.anim_time},
        {"move_flags", anim.move_flags},{"combat_flags", anim.combat_flags},
        {"blend_weight", anim.blend_weight},
    };

    nlohmann::json player = {
        {"id", localId_},
        {"name", ""},
        {"position", {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}}},
        {"rotation", {{"yaw", yaw}, {"pitch", pitch}}},
        {"velocity", {{"x", vel.x}, {"y", vel.y}, {"z", vel.z}}},
        {"health", health},
        {"maxHealth", maxHealth},
        {"animState", animJson},
        {"isHost", false},
        {"ping", 0},
    };
    EmitGameplayJson(player, gp);

    client_->Send(PacketType::STATE_UPDATE, {{"players", {player}}});
    if (animChanged) {
        client_->Send(PacketType::ANIMATION_UPDATE,
                      {{"playerId", localId_}, {"animState", animJson}});
    }

    lastPos_ = pos;
    lastHealth_ = health;
    lastAnim_ = anim;
}

PlayerStatePacket PlayerSync::ReadLocalGameplay(uintptr_t base) {
    PlayerStatePacket gp;

    // --- Traversal -------------------------------------------------------
    if (g_offsets.traversalFlagsOffset != 0) {
        const uint32_t flags = ReadU32(base, g_offsets.traversalFlagsOffset);
        gp.traversal.mode = TraversalModeFromFlags(flags);
        gp.traversal.anim_id = ReadInt(base, g_offsets.offAnimId);
        gp.traversal.anim_time = ReadFloat(base, g_offsets.offAnimTime);
        gp.traversal.present = true;
        if (g_offsets.traversalTargetPosOffset != 0) {
            const Vec3 tp = ReadVec3(base, g_offsets.traversalTargetPosOffset);
            gp.traversal.target_x = tp.x;
            gp.traversal.target_y = tp.y;
            gp.traversal.target_z = tp.z;
        }
        // Progress is derived from the active clip's normalized playback time
        // for one-shot actions (vault/roll/slide); engine resets it per action.
        gp.traversal.progress = gp.traversal.anim_time;
    }

    // --- Skill / magic ---------------------------------------------------
    if (g_offsets.skillComponentOffset != 0) {
        const uintptr_t sc = base + g_offsets.skillComponentOffset;
        gp.skill_id = ReadInt(sc, 0x00);
        gp.skill_phase = ReadInt(sc, 0x04);
        gp.skill_anim_id = ReadInt(sc, 0x08);
        gp.skill_target_entity = ReadInt(sc, 0x0C);
        const Vec3 stp = ReadVec3(sc, 0x10);
        gp.skill_target_x = stp.x;
        gp.skill_target_y = stp.y;
        gp.skill_target_z = stp.z;
        gp.skill_present = true;
    }

    // --- Active VFX ------------------------------------------------------
    if (g_offsets.vfxComponentOffset != 0) {
        const uintptr_t vc = base + g_offsets.vfxComponentOffset;
        for (int i = 0; i < 8; ++i) {
            const int id = ReadInt(vc, static_cast<uint32_t>(i * 4));
            if (id != 0) gp.active_vfx.push_back(id);
        }
        gp.vfx_present = true;
    }

    // --- Status flags / buffs --------------------------------------------
    if (g_offsets.statusComponentOffset != 0) {
        const uintptr_t stc = base + g_offsets.statusComponentOffset;
        gp.status_flags = ReadU32(stc, 0x00);
        for (int i = 0; i < 8; ++i) {
            const int id = ReadInt(stc, static_cast<uint32_t>(0x04 + i * 4));
            if (id != 0) gp.buff_ids.push_back(id);
        }
        gp.status_present = true;
    }

    // --- Equipment / weapons ---------------------------------------------
    if (g_offsets.equipmentComponentOffset != 0) {
        const uintptr_t ec = base + g_offsets.equipmentComponentOffset;
        gp.weapon_id = ReadInt(ec, 0x00);
        gp.weapon_stance = ReadInt(ec, 0x04);
        gp.off_hand_id = ReadInt(ec, 0x08);
        gp.equipment_present = true;
    }

    // --- Mount -----------------------------------------------------------
    if (g_offsets.mountEntityPtrOffset != 0) {
        const uintptr_t mountPtr =
            SafeRead<uintptr_t>(base + g_offsets.mountEntityPtrOffset);
        if (mountPtr != 0 && IsReadable(mountPtr, 0x400)) {
            gp.is_mounted = true;
            gp.mount_entity_id =
                static_cast<int>(ReadU64(mountPtr, g_offsets.offEntityId));
            gp.mount_anim_id = ReadInt(mountPtr, g_offsets.offAnimId);
            gp.mount_anim_time = ReadFloat(mountPtr, g_offsets.offAnimTime);
            const Vec3 mp = ReadVec3(mountPtr, g_offsets.offPositionX);
            gp.mount_x = mp.x;
            gp.mount_y = mp.y;
            gp.mount_z = mp.z;
        }
        gp.mount_present = true;
    }

    // --- Dodge -----------------------------------------------------------
    if (g_offsets.dodgeComponentOffset != 0) {
        const uintptr_t dc = base + g_offsets.dodgeComponentOffset;
        gp.dodge_x = ReadFloat(dc, 0x00);
        gp.dodge_y = ReadFloat(dc, 0x04);
        gp.dodge_present = true;
    }

    // --- Death -----------------------------------------------------------
    // Death is derived from current health; respawn position rides the
    // transform once health is restored (handled on the remote side).
    if (g_offsets.offHealth != 0) {
        gp.is_dead = ReadFloat(base, g_offsets.offHealth) <= 0.f;
        gp.death_present = true;
        const Vec3 rp = ReadVec3(base, g_offsets.offPositionX);
        gp.respawn_x = rp.x;
        gp.respawn_y = rp.y;
        gp.respawn_z = rp.z;
        gp.respawn_present = true;
    }

    // --- Interaction -----------------------------------------------------
    if (g_offsets.skillComponentOffset != 0) {
        // Interaction type/entity live just past the skill block in the combat
        // component for BDO entities.
        const uintptr_t ic = base + g_offsets.skillComponentOffset;
        gp.interaction_type = ReadInt(ic, 0x1C);
        gp.interaction_entity_id = ReadInt(ic, 0x20);
        gp.interaction_present = true;
    }

    return gp;
}

void PlayerSync::EmitGameplayJson(nlohmann::json& player,
                                  const PlayerStatePacket& gp) {
    if (gp.traversal.present) {
        player["traversalState"] = {
            {"mode", gp.traversal.mode},
            {"animId", gp.traversal.anim_id},
            {"animTime", gp.traversal.anim_time},
            {"targetPos",
             {{"x", gp.traversal.target_x},
              {"y", gp.traversal.target_y},
              {"z", gp.traversal.target_z}}},
            {"progress", gp.traversal.progress},
        };
    }
    if (gp.skill_present) {
        player["skillId"] = gp.skill_id;
        player["skillPhase"] = gp.skill_phase;
        player["skillAnimId"] = gp.skill_anim_id;
        player["skillTargetEntity"] = gp.skill_target_entity;
        player["skillTargetPos"] = {{"x", gp.skill_target_x},
                                    {"y", gp.skill_target_y},
                                    {"z", gp.skill_target_z}};
    }
    if (gp.vfx_present) player["activeVfx"] = gp.active_vfx;
    if (gp.status_present) {
        player["statusFlags"] = gp.status_flags;
        player["buffIds"] = gp.buff_ids;
    }
    if (gp.equipment_present) {
        player["weaponId"] = gp.weapon_id;
        player["weaponStance"] = gp.weapon_stance;
        player["offHandId"] = gp.off_hand_id;
    }
    if (gp.mount_present) {
        player["isMounted"] = gp.is_mounted;
        player["mountEntityId"] = gp.mount_entity_id;
        player["mountAnimId"] = gp.mount_anim_id;
        player["mountAnimTime"] = gp.mount_anim_time;
        player["mountPosition"] = {
            {"x", gp.mount_x}, {"y", gp.mount_y}, {"z", gp.mount_z}};
    }
    if (gp.dodge_present)
        player["dodgeDirection"] = {{"x", gp.dodge_x}, {"y", gp.dodge_y}};
    if (gp.death_present) player["isDead"] = gp.is_dead;
    if (gp.respawn_present)
        player["respawnPosition"] = {
            {"x", gp.respawn_x}, {"y", gp.respawn_y}, {"z", gp.respawn_z}};
    if (gp.interaction_present) {
        player["interactionType"] = gp.interaction_type;
        player["interactionEntityId"] = gp.interaction_entity_id;
    }
}

void PlayerSync::ApplyStateUpdate(const nlohmann::json& players) {
    if (!players.is_array()) return;
    const double now = static_cast<double>(GetTickCount64());
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : players) {
        if (!p.contains("id")) continue;
        const std::string id = p["id"].get<std::string>();
        if (id == localId_) continue;
        RemotePlayer& rp = remotes_[id];
        rp.id = id;
        if (p.contains("name") && p["name"].is_string())
            rp.name = p["name"].get<std::string>();
        if (p.contains("position")) {
            const auto& pos = p["position"];
            rp.targetPos = {pos.value("x", 0.f), pos.value("y", 0.f),
                            pos.value("z", 0.f)};
            if (rp.lastUpdateTime == 0.0) rp.currentPos = rp.targetPos;
        }
        if (p.contains("velocity")) {
            const auto& v = p["velocity"];
            rp.velocity = {v.value("x", 0.f), v.value("y", 0.f),
                           v.value("z", 0.f)};
        }
        if (p.contains("health")) rp.currentHealth = p["health"].get<float>();
        if (p.contains("maxHealth")) rp.maxHealth = p["maxHealth"].get<float>();
        if (p.contains("isHost")) rp.isHost = p["isHost"].get<bool>();
        if (p.contains("ping")) rp.ping = p["ping"].get<int>();
        if (p.contains("inCutscene") && p["inCutscene"].is_boolean())
            rp.inCutscene = p["inCutscene"].get<bool>();
        if (p.contains("animState")) {
            const auto& a = p["animState"];
            const int32_t newAnim = a.value("anim_id", rp.lastAnim.anim_id);
            // Track recent anim changes for the traversal fallback heuristic.
            if (newAnim != rp.prevAnimId) {
                rp.animChangeStreak = 3; // "changed within last 3 ticks"
                rp.prevAnimId = newAnim;
            } else if (rp.animChangeStreak > 0) {
                rp.animChangeStreak--;
            }
            rp.lastAnim.anim_id = newAnim;
            rp.lastAnim.anim_time = a.value("anim_time", 0.f);
            rp.lastAnim.move_flags = a.value("move_flags", 0u);
            rp.lastAnim.combat_flags = a.value("combat_flags", 0u);
            rp.lastAnim.blend_weight = a.value("blend_weight", 1.f);
        }
        ParseGameplay(p, rp.gameplay);
        rp.lastUpdateTime = now;
    }
}

void PlayerSync::ParseGameplay(const nlohmann::json& p, PlayerStatePacket& gp) {
    if (p.contains("traversalState") && p["traversalState"].is_object()) {
        const auto& t = p["traversalState"];
        gp.traversal.mode = t.value("mode", 0);
        gp.traversal.anim_id = t.value("animId", 0);
        gp.traversal.anim_time = t.value("animTime", 0.f);
        gp.traversal.progress = t.value("progress", 0.f);
        if (t.contains("targetPos")) {
            const auto& tp = t["targetPos"];
            gp.traversal.target_x = tp.value("x", 0.f);
            gp.traversal.target_y = tp.value("y", 0.f);
            gp.traversal.target_z = tp.value("z", 0.f);
        }
        gp.traversal.present = true;
    } else {
        gp.traversal.present = false;
    }

    if (p.contains("skillId")) {
        gp.skill_id = p.value("skillId", 0);
        gp.skill_phase = p.value("skillPhase", 0);
        gp.skill_anim_id = p.value("skillAnimId", 0);
        gp.skill_target_entity = p.value("skillTargetEntity", 0);
        if (p.contains("skillTargetPos")) {
            const auto& s = p["skillTargetPos"];
            gp.skill_target_x = s.value("x", 0.f);
            gp.skill_target_y = s.value("y", 0.f);
            gp.skill_target_z = s.value("z", 0.f);
        }
        gp.skill_present = true;
    } else {
        gp.skill_present = false;
    }

    if (p.contains("activeVfx") && p["activeVfx"].is_array()) {
        gp.active_vfx = p["activeVfx"].get<std::vector<int>>();
        gp.vfx_present = true;
    } else {
        gp.vfx_present = false;
    }

    if (p.contains("statusFlags")) {
        gp.status_flags = p.value("statusFlags", 0u);
        gp.buff_ids = p.contains("buffIds") && p["buffIds"].is_array()
                          ? p["buffIds"].get<std::vector<int>>()
                          : std::vector<int>{};
        gp.status_present = true;
    } else {
        gp.status_present = false;
    }

    if (p.contains("weaponId")) {
        gp.weapon_id = p.value("weaponId", 0);
        gp.weapon_stance = p.value("weaponStance", 0);
        gp.off_hand_id = p.value("offHandId", 0);
        gp.equipment_present = true;
    } else {
        gp.equipment_present = false;
    }

    if (p.contains("isMounted")) {
        gp.is_mounted = p.value("isMounted", false);
        gp.mount_entity_id = p.value("mountEntityId", 0);
        gp.mount_anim_id = p.value("mountAnimId", 0);
        gp.mount_anim_time = p.value("mountAnimTime", 0.f);
        if (p.contains("mountPosition")) {
            const auto& m = p["mountPosition"];
            gp.mount_x = m.value("x", 0.f);
            gp.mount_y = m.value("y", 0.f);
            gp.mount_z = m.value("z", 0.f);
        }
        gp.mount_present = true;
    } else {
        gp.mount_present = false;
    }

    if (p.contains("dodgeDirection")) {
        const auto& d = p["dodgeDirection"];
        gp.dodge_x = d.value("x", 0.f);
        gp.dodge_y = d.value("y", 0.f);
        gp.dodge_present = true;
    } else {
        gp.dodge_present = false;
    }

    if (p.contains("isDead")) {
        gp.is_dead = p.value("isDead", false);
        gp.death_present = true;
    } else {
        gp.death_present = false;
    }

    if (p.contains("respawnPosition")) {
        const auto& r = p["respawnPosition"];
        gp.respawn_x = r.value("x", 0.f);
        gp.respawn_y = r.value("y", 0.f);
        gp.respawn_z = r.value("z", 0.f);
        gp.respawn_present = true;
    } else {
        gp.respawn_present = false;
    }

    if (p.contains("interactionType")) {
        gp.interaction_type = p.value("interactionType", 0);
        gp.interaction_entity_id = p.value("interactionEntityId", 0);
        gp.interaction_present = true;
    } else {
        gp.interaction_present = false;
    }
}

void PlayerSync::ApplyAnimationUpdate(const std::string& id,
                                      const nlohmann::json& anim) {
    if (id == localId_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = remotes_.find(id);
    if (it == remotes_.end()) return;
    RemotePlayer& rp = it->second;
    rp.lastAnim.anim_id = anim.value("anim_id", rp.lastAnim.anim_id);
    rp.lastAnim.anim_time = anim.value("anim_time", rp.lastAnim.anim_time);
    rp.lastAnim.move_flags = anim.value("move_flags", rp.lastAnim.move_flags);
    rp.lastAnim.combat_flags =
        anim.value("combat_flags", rp.lastAnim.combat_flags);
    rp.lastAnim.blend_weight =
        anim.value("blend_weight", rp.lastAnim.blend_weight);
}

void PlayerSync::ApplyRemoteState(double nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, rp] : remotes_) {
        const double elapsed = nowMs - rp.lastUpdateTime;

        // Resolve effective traversal mode. If the local sender couldn't read a
        // traversal offset, fall back to a velocity/anim-change heuristic.
        int mode = TM_NONE;
        bool fallbackTraversal = false;
        if (rp.gameplay.traversal.present) {
            mode = rp.gameplay.traversal.mode;
        } else if (std::fabs(rp.velocity.y) > 5.0f ||
                   rp.animChangeStreak > 0) {
            // No traversal data: a strong vertical velocity or a recent anim
            // change (within the last 3 ticks) flags an in-progress traversal.
            // Drive the jump/fall arc when airborne, otherwise stay on the plain
            // lerp path but with the tighter fallback buffer + convergence.
            fallbackTraversal = true;
            mode = (rp.velocity.y > 0.f)   ? TM_JUMP
                   : (rp.velocity.y < 0.f) ? TM_FALL
                                           : TM_NONE;
        }

        // Convergence factor for plain lerp. The fallback path uses a reduced
        // buffer (50ms) and doubled convergence to keep traversal tight.
        const double buffer = fallbackTraversal ? kFallbackBufferMs
                                                 : kInterpBufferMs;
        float baseT = elapsed <= 0
                          ? 1.f
                          : static_cast<float>(std::min(1.0, elapsed / buffer));
        if (fallbackTraversal)
            baseT = std::min(1.f, baseT * 2.f); // 2x convergence on fallback

        if (rp.entityBase == 0 || !IsReadable(rp.entityBase, 0x800)) {
            // Still advance currentPos so the UI/interp stays sane even when we
            // have no memory slot to write into.
            rp.currentPos.x = Lerp(rp.currentPos.x, rp.targetPos.x, baseT);
            rp.currentPos.y = Lerp(rp.currentPos.y, rp.targetPos.y, baseT);
            rp.currentPos.z = Lerp(rp.currentPos.z, rp.targetPos.z, baseT);
            continue;
        }

        ApplyTraversal(rp, nowMs, baseT, mode);
        ApplyGameplayWrites(rp);
    }
}

// Per-mode position/animation strategy. Always writes velocity + flags so the
// engine blend tree stays driven; the position write differs by mode.
void PlayerSync::ApplyTraversal(RemotePlayer& rp, double nowMs, float baseT,
                                int mode) {
    const TraversalState& tv = rp.gameplay.traversal;
    const Vec3 target = rp.targetPos;
    const Vec3 traversalTarget{tv.target_x, tv.target_y, tv.target_z};

    auto writeAnimExact = [&](int animId, float animTime) {
        WriteInt(rp.entityBase, g_offsets.offAnimId, animId);
        WriteFloat(rp.entityBase, g_offsets.offAnimTime, animTime);
    };
    auto writeCommon = [&]() {
        WriteVec3(rp.entityBase, g_offsets.offVelocityX, rp.velocity);
        WriteFloat(rp.entityBase, g_offsets.offHealth, rp.currentHealth);
        WriteFloat(rp.entityBase, g_offsets.offMaxHealth, rp.maxHealth);
        WriteU32(rp.entityBase, g_offsets.offMoveFlags, rp.lastAnim.move_flags);
        WriteU32(rp.entityBase, g_offsets.offCombatFlags,
                 rp.lastAnim.combat_flags);
        WriteFloat(rp.entityBase, g_offsets.offBlendWeight,
                   rp.lastAnim.blend_weight);
    };

    switch (mode) {
    case TM_VAULTING:
    case TM_ROLL:
    case TM_SLIDE: {
        // Drive position toward the traversal target by progress; write the
        // action animation exactly (no lerp on the clip).
        const float pr = std::min(1.f, std::max(0.f, tv.progress));
        rp.currentPos.x = Lerp(rp.currentPos.x, traversalTarget.x, pr);
        rp.currentPos.y = Lerp(rp.currentPos.y, traversalTarget.y, pr);
        rp.currentPos.z = Lerp(rp.currentPos.z, traversalTarget.z, pr);
        WriteVec3(rp.entityBase, g_offsets.offPositionX, rp.currentPos);
        writeAnimExact(tv.anim_id, tv.anim_time);
        writeCommon();
        break;
    }
    case TM_CLIMBING:
    case TM_LEDGE_GRAB: {
        // No interpolation buffer: snap to target every tick, exact anim.
        rp.currentPos = target;
        WriteVec3(rp.entityBase, g_offsets.offPositionX, rp.currentPos);
        writeAnimExact(tv.anim_id, tv.anim_time);
        writeCommon();
        break;
    }
    case TM_WALL_RUN: {
        const float t = std::min(1.f, baseT * 3.f); // 3x normal lerp speed
        rp.currentPos.x = Lerp(rp.currentPos.x, target.x, t);
        rp.currentPos.y = Lerp(rp.currentPos.y, target.y, t);
        rp.currentPos.z = Lerp(rp.currentPos.z, target.z, t);
        WriteVec3(rp.entityBase, g_offsets.offPositionX, rp.currentPos);
        writeAnimExact(tv.anim_id, tv.anim_time);
        writeCommon();
        break;
    }
    case TM_JUMP:
    case TM_FALL: {
        // Lerp toward target plus dead reckoning along velocity between packets
        // for a smooth ballistic arc.
        rp.currentPos.x = Lerp(rp.currentPos.x, target.x, baseT);
        rp.currentPos.y = Lerp(rp.currentPos.y, target.y, baseT);
        rp.currentPos.z = Lerp(rp.currentPos.z, target.z, baseT);
        const float dt =
            static_cast<float>((nowMs - rp.lastUpdateTime) / 1000.0);
        rp.currentPos.x += rp.velocity.x * dt;
        rp.currentPos.y += rp.velocity.y * dt;
        rp.currentPos.z += rp.velocity.z * dt;
        WriteVec3(rp.entityBase, g_offsets.offPositionX, rp.currentPos);
        if (tv.present)
            writeAnimExact(tv.anim_id, tv.anim_time);
        else
            writeAnimExact(rp.lastAnim.anim_id, rp.lastAnim.anim_time);
        writeCommon();
        break;
    }
    case TM_LAND: {
        // Snap to landing position immediately, play the land clip.
        rp.currentPos = target;
        WriteVec3(rp.entityBase, g_offsets.offPositionX, rp.currentPos);
        writeAnimExact(tv.anim_id, tv.anim_time);
        writeCommon();
        break;
    }
    case TM_NONE:
    default: {
        // Existing behaviour: plain lerp + full anim/velocity write.
        rp.currentPos.x = Lerp(rp.currentPos.x, target.x, baseT);
        rp.currentPos.y = Lerp(rp.currentPos.y, target.y, baseT);
        rp.currentPos.z = Lerp(rp.currentPos.z, target.z, baseT);
        WriteVec3(rp.entityBase, g_offsets.offPositionX, rp.currentPos);
        WriteFloat(rp.entityBase, g_offsets.offHealth, rp.currentHealth);
        WriteFloat(rp.entityBase, g_offsets.offMaxHealth, rp.maxHealth);
        WriteVec3(rp.entityBase, g_offsets.offVelocityX, rp.velocity);
        WriteInt(rp.entityBase, g_offsets.offAnimId, rp.lastAnim.anim_id);
        WriteFloat(rp.entityBase, g_offsets.offAnimTime, rp.lastAnim.anim_time);
        WriteU32(rp.entityBase, g_offsets.offMoveFlags, rp.lastAnim.move_flags);
        WriteU32(rp.entityBase, g_offsets.offCombatFlags,
                 rp.lastAnim.combat_flags);
        WriteFloat(rp.entityBase, g_offsets.offBlendWeight,
                   rp.lastAnim.blend_weight);
        break;
    }
    }
}

// Write all the full-gameplay-sync fields onto the remote entity so the engine
// renders magic/VFX/buffs/weapons/mounts/death automatically. Each block is
// gated on both the wire `present` flag and a resolved scanner offset.
void PlayerSync::ApplyGameplayWrites(RemotePlayer& rp) {
    const PlayerStatePacket& gp = rp.gameplay;
    const uintptr_t base = rp.entityBase;

    if (gp.skill_present && g_offsets.skillComponentOffset != 0) {
        const uintptr_t sc = base + g_offsets.skillComponentOffset;
        WriteInt(sc, 0x00, gp.skill_id);
        WriteInt(sc, 0x04, gp.skill_phase);
        WriteInt(sc, 0x08, gp.skill_anim_id);
        WriteInt(sc, 0x0C, gp.skill_target_entity);
        WriteVec3(sc, 0x10,
                  Vec3{gp.skill_target_x, gp.skill_target_y, gp.skill_target_z});
    }

    if (gp.vfx_present && g_offsets.vfxComponentOffset != 0) {
        const uintptr_t vc = base + g_offsets.vfxComponentOffset;
        for (int i = 0; i < 8; ++i) {
            const int id = i < static_cast<int>(gp.active_vfx.size())
                               ? gp.active_vfx[i]
                               : 0;
            WriteInt(vc, static_cast<uint32_t>(i * 4), id);
        }
    }

    if (gp.status_present && g_offsets.statusComponentOffset != 0) {
        const uintptr_t stc = base + g_offsets.statusComponentOffset;
        WriteU32(stc, 0x00, gp.status_flags);
        for (int i = 0; i < 8; ++i) {
            const int id =
                i < static_cast<int>(gp.buff_ids.size()) ? gp.buff_ids[i] : 0;
            WriteInt(stc, static_cast<uint32_t>(0x04 + i * 4), id);
        }
    }

    if (gp.equipment_present && g_offsets.equipmentComponentOffset != 0) {
        const uintptr_t ec = base + g_offsets.equipmentComponentOffset;
        WriteInt(ec, 0x00, gp.weapon_id);
        WriteInt(ec, 0x04, gp.weapon_stance);
        WriteInt(ec, 0x08, gp.off_hand_id);
    }

    if (gp.mount_present && gp.is_mounted &&
        g_offsets.mountEntityPtrOffset != 0) {
        const uintptr_t mountPtr =
            SafeRead<uintptr_t>(base + g_offsets.mountEntityPtrOffset);
        if (mountPtr != 0 && IsReadable(mountPtr, 0x400)) {
            WriteVec3(mountPtr, g_offsets.offPositionX,
                      Vec3{gp.mount_x, gp.mount_y, gp.mount_z});
            WriteInt(mountPtr, g_offsets.offAnimId, gp.mount_anim_id);
            WriteFloat(mountPtr, g_offsets.offAnimTime, gp.mount_anim_time);
        }
    }

    if (gp.dodge_present && g_offsets.dodgeComponentOffset != 0) {
        const uintptr_t dc = base + g_offsets.dodgeComponentOffset;
        WriteFloat(dc, 0x00, gp.dodge_x);
        WriteFloat(dc, 0x04, gp.dodge_y);
    }

    if (gp.death_present && g_offsets.statusComponentOffset != 0) {
        // Death flag lives at the tail of the status component for BDO entities.
        WriteInt(base + g_offsets.statusComponentOffset, 0x24,
                 gp.is_dead ? 1 : 0);
        // Respawn edge: dead -> alive with a respawn position teleports.
        if (rp.prevDead && !gp.is_dead && gp.respawn_present) {
            const Vec3 r{gp.respawn_x, gp.respawn_y, gp.respawn_z};
            WriteVec3(base, g_offsets.offPositionX, r);
            rp.currentPos = r;
            rp.targetPos = r;
        }
        rp.prevDead = gp.is_dead;
    }

    if (gp.interaction_present && g_offsets.skillComponentOffset != 0) {
        const uintptr_t ic = base + g_offsets.skillComponentOffset;
        WriteInt(ic, 0x1C, gp.interaction_type);
        WriteInt(ic, 0x20, gp.interaction_entity_id);
    }
}

void PlayerSync::AddRemotePlayer(const std::string& id, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    RemotePlayer& rp = remotes_[id];
    rp.id = id;
    rp.name = name;
}

void PlayerSync::RemoveRemotePlayer(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    remotes_.erase(id);
}

void PlayerSync::SetRemoteCutscene(const std::string& id, bool inCutscene) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = remotes_.find(id);
    if (it != remotes_.end()) it->second.inCutscene = inCutscene;
}

void PlayerSync::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    remotes_.clear();
    lastHealth_ = -1.f;
}

uintptr_t PlayerSync::GetRemoteEntityBase(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = remotes_.find(id);
    return it == remotes_.end() ? 0 : it->second.entityBase;
}

std::vector<RemotePlayer> PlayerSync::GetRemotePlayers() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RemotePlayer> out;
    out.reserve(remotes_.size());
    for (auto& [id, rp] : remotes_) out.push_back(rp);
    return out;
}

} // namespace cdmp
