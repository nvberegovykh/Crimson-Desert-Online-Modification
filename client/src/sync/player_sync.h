// SPDX-License-Identifier: MIT
//
// Reads the local player's transform/animation state and pushes it to the
// server at 20Hz; applies remote players' state into game memory so the
// BLACKSPACE engine plays the correct locomotion/combat clips (no T-poses).

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "../game/memory.h"
#include <nlohmann/json.hpp>

namespace cdmp {

class WsClient;

struct AnimStateData {
    int32_t anim_id = 0;
    float anim_time = 0.f;
    uint32_t move_flags = 0;
    uint32_t combat_flags = 0;
    float blend_weight = 1.f;
};

struct RemotePlayer {
    std::string id;
    std::string name;
    Vec3 currentPos;
    Vec3 targetPos;
    Vec3 velocity;
    float currentHealth = 100.f;
    float maxHealth = 100.f;
    AnimStateData lastAnim;
    double lastUpdateTime = 0.0; // ms
    uintptr_t entityBase = 0;    // resolved remote entity slot, if any
    bool isHost = false;
    int ping = 0;
};

class PlayerSync {
public:
    void SetClient(WsClient* client) { client_ = client; }
    void SetLocalId(const std::string& id) { localId_ = id; }

    // Called every frame; internally rate-limits to 20Hz.
    void Tick(double nowMs);

    // Apply a remote STATE_UPDATE / ANIMATION_UPDATE payload.
    void ApplyStateUpdate(const nlohmann::json& players);
    void ApplyAnimationUpdate(const std::string& id, const nlohmann::json& anim);

    void AddRemotePlayer(const std::string& id, const std::string& name);
    void RemoveRemotePlayer(const std::string& id);
    void Clear();

    // Snapshot for the in-session UI.
    std::vector<RemotePlayer> GetRemotePlayers();

private:
    void SendLocalState(double nowMs);
    void ApplyRemoteState(double nowMs);

    WsClient* client_ = nullptr;
    std::string localId_;
    double lastSendMs_ = 0.0;

    std::mutex mutex_;
    std::map<std::string, RemotePlayer> remotes_;

    // Last sent values for change detection.
    Vec3 lastPos_;
    float lastHealth_ = -1.f;
    AnimStateData lastAnim_;
};

} // namespace cdmp
