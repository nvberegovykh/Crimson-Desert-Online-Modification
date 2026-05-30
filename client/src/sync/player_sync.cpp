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

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

bool VecDiff(const Vec3& a, const Vec3& b, float eps) {
    return std::fabs(a.x - b.x) > eps || std::fabs(a.y - b.y) > eps ||
           std::fabs(a.z - b.z) > eps;
}
} // namespace

void PlayerSync::Tick(double nowMs) {
    SendLocalState(nowMs);
    ApplyRemoteState(nowMs);
}

void PlayerSync::SendLocalState(double nowMs) {
    if (!client_ || !client_->IsConnected()) return;
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

    if (!moved && !healthChanged && !animChanged) return;

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

    client_->Send(PacketType::STATE_UPDATE, {{"players", {player}}});
    if (animChanged) {
        client_->Send(PacketType::ANIMATION_UPDATE,
                      {{"playerId", localId_}, {"animState", animJson}});
    }

    lastPos_ = pos;
    lastHealth_ = health;
    lastAnim_ = anim;
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
        if (p.contains("animState")) {
            const auto& a = p["animState"];
            rp.lastAnim.anim_id = a.value("anim_id", 0);
            rp.lastAnim.anim_time = a.value("anim_time", 0.f);
            rp.lastAnim.move_flags = a.value("move_flags", 0u);
            rp.lastAnim.combat_flags = a.value("combat_flags", 0u);
            rp.lastAnim.blend_weight = a.value("blend_weight", 1.f);
        }
        rp.lastUpdateTime = now;
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
        // Position interpolation toward the latest target with a fixed buffer.
        const double elapsed = nowMs - rp.lastUpdateTime;
        const float t = elapsed <= 0 ? 1.f
                                      : static_cast<float>(std::min(
                                            1.0, elapsed / kInterpBufferMs));
        rp.currentPos.x = Lerp(rp.currentPos.x, rp.targetPos.x, t);
        rp.currentPos.y = Lerp(rp.currentPos.y, rp.targetPos.y, t);
        rp.currentPos.z = Lerp(rp.currentPos.z, rp.targetPos.z, t);

        // If we have a resolved entity slot for this remote, write the state so
        // the engine animates the avatar. Velocity drives the locomotion blend
        // tree; anim_id/anim_time drive the active clip; combat_flags drive the
        // combat layer. This is what prevents T-poses.
        if (rp.entityBase != 0 && IsReadable(rp.entityBase, 0x800)) {
            WriteVec3(rp.entityBase, g_offsets.offPositionX, rp.currentPos);
            WriteVec3(rp.entityBase, g_offsets.offVelocityX, rp.velocity);
            WriteFloat(rp.entityBase, g_offsets.offHealth, rp.currentHealth);
            WriteFloat(rp.entityBase, g_offsets.offMaxHealth, rp.maxHealth);
            WriteInt(rp.entityBase, g_offsets.offAnimId, rp.lastAnim.anim_id);
            WriteFloat(rp.entityBase, g_offsets.offAnimTime, rp.lastAnim.anim_time);
            WriteU32(rp.entityBase, g_offsets.offMoveFlags, rp.lastAnim.move_flags);
            WriteU32(rp.entityBase, g_offsets.offCombatFlags,
                     rp.lastAnim.combat_flags);
            WriteFloat(rp.entityBase, g_offsets.offBlendWeight,
                       rp.lastAnim.blend_weight);
        }
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

void PlayerSync::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    remotes_.clear();
    lastHealth_ = -1.f;
}

std::vector<RemotePlayer> PlayerSync::GetRemotePlayers() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RemotePlayer> out;
    out.reserve(remotes_.size());
    for (auto& [id, rp] : remotes_) out.push_back(rp);
    return out;
}

} // namespace cdmp
