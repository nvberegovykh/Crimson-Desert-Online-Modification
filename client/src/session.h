// SPDX-License-Identifier: MIT
//
// Session controller: the glue between the UI, the network client, and the
// sync systems. Owns connection lifecycle and routes incoming packets.

#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "net/client.h"
#include "sync/player_sync.h"
#include "sync/combat_sync.h"

namespace cdmp {

enum class SessionStatus {
    Disconnected,
    Connecting,
    Connected, // handshaked, not yet in a room
    InSession,
    Failed,
};

class Session {
public:
    static Session& Get();

    void Init();
    void Shutdown();

    // UI actions.
    void HostSession(const std::string& host, uint16_t port,
                     const std::string& sessionName, const std::string& password);
    void JoinSession(const std::string& host, uint16_t port,
                     const std::string& inviteCode, const std::string& password);
    void LeaveSession();

    // Per-frame pump for the sync systems (called from the render hook).
    void Tick();

    SessionStatus Status() const { return status_.load(); }
    bool IsHost() const { return isHost_.load(); }
    std::string InviteCode();
    std::string ServerName();
    std::string LastError();
    bool FriendlyFire() const { return friendlyFire_.load(); }

    // Local player id (server-assigned uuid). Thread-safe.
    std::string LocalId();

    // True while the local player is inside a cutscene (state sync suspended).
    bool CutsceneActive() const { return cutsceneActive_.load(); }
    void SetCutsceneActive(bool active) { cutsceneActive_.store(active); }

    // Underlying network client (used by the sync subsystems). May be null
    // before Init().
    WsClient* Client() { return client_.get(); }

    PlayerSync& Players() { return playerSync_; }

private:
    Session() = default;
    void OnConnected();
    void OnDisconnected();
    void OnPacket(PacketType type, const nlohmann::json& data);
    void SendPing();

    std::unique_ptr<WsClient> client_;
    PlayerSync playerSync_;

    std::atomic<SessionStatus> status_{SessionStatus::Disconnected};
    std::atomic<bool> isHost_{false};
    std::atomic<bool> friendlyFire_{false};
    std::atomic<bool> wantCreate_{false};
    std::atomic<bool> cutsceneActive_{false};

    std::mutex stateMutex_;
    std::string localId_;
    std::string playerName_ = "Player";
    std::string pendingSessionName_;
    std::string pendingPassword_;
    std::string pendingInviteCode_;
    std::string inviteCode_;
    std::string serverName_;
    std::string lastError_;

    double lastPingMs_ = 0.0;
};

} // namespace cdmp
