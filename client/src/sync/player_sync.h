// SPDX-License-Identifier: MIT
//
// Reads the local player's transform/animation state and pushes it to the
// server at 20Hz; applies remote players' state into game memory so the
// BLACKSPACE engine plays the correct locomotion/combat clips (no T-poses).

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "../game/memory.h"
#include "../net/protocol.h"
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
    bool inCutscene = false;     // true while this remote is in a cutscene

    // Parkour / traversal + full gameplay state from the latest packet.
    PlayerStatePacket gameplay;
    bool prevDead = false; // tracks isDead edge for respawn teleport

    // Fallback traversal heuristic: how many of the last updates changed the
    // animation id. Used only when traversal offsets weren't resolved.
    int32_t prevAnimId = 0;
    int animChangeStreak = 0;
};

class PlayerSync {
public:
    void SetClient(WsClient* client) { client_ = client; }
    void SetLocalId(const std::string& id) { localId_ = id; }

    // While suspended, the local 20Hz STATE_UPDATE/ANIMATION_UPDATE send is
    // skipped (e.g. during a cutscene). Remote application keeps running.
    void SetSuspended(bool suspended) { suspended_.store(suspended); }
    bool IsSuspended() const { return suspended_.load(); }

    // Called every frame; internally rate-limits to 20Hz.
    void Tick(double nowMs);

    // Apply a remote STATE_UPDATE / ANIMATION_UPDATE payload.
    void ApplyStateUpdate(const nlohmann::json& players);
    void ApplyAnimationUpdate(const std::string& id, const nlohmann::json& anim);

    void AddRemotePlayer(const std::string& id, const std::string& name);
    void RemoveRemotePlayer(const std::string& id);
    void SetRemoteCutscene(const std::string& id, bool inCutscene);
    void Clear();

    // Snapshot for the in-session UI.
    std::vector<RemotePlayer> GetRemotePlayers();

    // Resolved remote entity slot for a player id, or 0 if unknown/unresolved.
    uintptr_t GetRemoteEntityBase(const std::string& id);

private:
    void SendLocalState(double nowMs);
    void ApplyRemoteState(double nowMs);

    WsClient* client_ = nullptr;
    std::string localId_;
    double lastSendMs_ = 0.0;
    std::atomic<bool> suspended_{false};

    std::mutex mutex_;
    std::map<std::string, RemotePlayer> remotes_;

    // Build the local traversal + gameplay state by reading scanner offsets.
    PlayerStatePacket ReadLocalGameplay(uintptr_t base);
    // Serialize gameplay fields into a player JSON entry (only present ones).
    void EmitGameplayJson(nlohmann::json& player, const PlayerStatePacket& gp);
    // Parse gameplay fields out of an incoming player JSON entry.
    void ParseGameplay(const nlohmann::json& p, PlayerStatePacket& gp);
    // Apply per-mode traversal interpolation + gameplay writes for one remote.
    void ApplyTraversal(RemotePlayer& rp, double nowMs, float baseT, int mode);
    void ApplyGameplayWrites(RemotePlayer& rp);

    // Last sent values for change detection.
    Vec3 lastPos_;
    float lastHealth_ = -1.f;
    AnimStateData lastAnim_;
};

} // namespace cdmp
