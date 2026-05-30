// SPDX-License-Identifier: MIT

#include "session.h"
#include "util/log.h"

#include <windows.h>

namespace cdmp {

Session& Session::Get() {
    static Session instance;
    return instance;
}

void Session::Init() {
    client_ = std::make_unique<WsClient>();
    client_->SetOnConnected([this] { OnConnected(); });
    client_->SetOnDisconnected([this] { OnDisconnected(); });
    client_->SetOnPacket(
        [this](PacketType t, const nlohmann::json& d) { OnPacket(t, d); });

    playerSync_.SetClient(client_.get());
    GetCombatSync().SetClient(client_.get());
    GetCombatSync().SetRemoteEntityResolver(
        [this](const std::string& id) {
            return playerSync_.GetRemoteEntityBase(id);
        });
    CDMP_LOG_INFO("Session controller initialized");
}

void Session::Shutdown() {
    if (client_) client_->Disconnect();
    GetCombatSync().RemoveHooks();
    status_.store(SessionStatus::Disconnected);
}

void Session::HostSession(const std::string& host, uint16_t port,
                          const std::string& sessionName,
                          const std::string& password) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        pendingSessionName_ = sessionName;
        pendingPassword_ = password;
        pendingInviteCode_.clear();
        lastError_.clear();
    }
    wantCreate_.store(true);
    status_.store(SessionStatus::Connecting);
    client_->Connect(host, port);
}

void Session::JoinSession(const std::string& host, uint16_t port,
                          const std::string& inviteCode,
                          const std::string& password) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        pendingInviteCode_ = inviteCode;
        pendingPassword_ = password;
        pendingSessionName_.clear();
        lastError_.clear();
    }
    wantCreate_.store(false);
    status_.store(SessionStatus::Connecting);
    client_->Connect(host, port);
}

void Session::LeaveSession() {
    if (client_) client_->Disconnect();
    playerSync_.Clear();
    status_.store(SessionStatus::Disconnected);
    isHost_.store(false);
    CDMP_LOG_INFO("Left session");
}

void Session::OnConnected() {
    // Send handshake immediately on connect.
    std::string name;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        name = playerName_;
    }
    client_->Send(PacketType::HANDSHAKE,
                  {{"protocol", PROTOCOL_VERSION},
                   {"name", name},
                   {"clientVersion", "cdmp-0.1.0"}});
}

void Session::OnDisconnected() {
    if (status_.load() == SessionStatus::InSession) {
        // Unexpected drop; the WsClient will attempt to reconnect and we will
        // re-handshake + re-join on the next OnConnected.
        CDMP_LOG_WARN("Disconnected from session (will retry)");
    }
}

void Session::OnPacket(PacketType type, const nlohmann::json& d) {
    switch (type) {
        case PacketType::HANDSHAKE_ACK: {
            const std::string id = d.value("playerId", std::string());
            playerSync_.SetLocalId(id);
            GetCombatSync().SetLocalId(id);
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                localId_ = id;
                serverName_ = d.value("serverName", std::string());
            }
            status_.store(SessionStatus::Connected);

            // Now send the JOIN_REQUEST queued by Host/Join.
            const bool create = wantCreate_.load();
            std::string sn, pw, code;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                sn = pendingSessionName_;
                pw = pendingPassword_;
                code = pendingInviteCode_;
            }
            nlohmann::json req = {{"create", create}};
            if (create) {
                req["sessionName"] = sn;
                req["password"] = pw;
                req["friendlyFire"] = false;
            } else {
                req["inviteCode"] = code;
                req["password"] = pw;
            }
            client_->Send(PacketType::JOIN_REQUEST, req);
            break;
        }
        case PacketType::JOIN_ACK: {
            isHost_.store(d.value("isHost", false));
            friendlyFire_.store(d.value("friendlyFire", false));
            GetCombatSync().SetHost(isHost_.load());
            if (isHost_.load()) GetCombatSync().InstallHooks();
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                inviteCode_ = d.value("inviteCode", std::string());
                serverName_ = d.value("sessionName", serverName_);
            }
            if (d.contains("players")) playerSync_.ApplyStateUpdate(d["players"]);
            status_.store(SessionStatus::InSession);
            CDMP_LOG_INFO("Joined session " + InviteCode());
            break;
        }
        case PacketType::JOIN_DENIED: {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = d.value("reason", std::string("join denied"));
            status_.store(SessionStatus::Failed);
            CDMP_LOG_WARN("Join denied: " + lastError_);
            break;
        }
        case PacketType::PLAYER_JOIN: {
            if (d.contains("player")) {
                const auto& p = d["player"];
                playerSync_.AddRemotePlayer(p.value("id", std::string()),
                                            p.value("name", std::string()));
                playerSync_.ApplyStateUpdate(nlohmann::json::array({p}));
            }
            break;
        }
        case PacketType::PLAYER_LEAVE:
            playerSync_.RemoveRemotePlayer(d.value("playerId", std::string()));
            break;
        case PacketType::STATE_UPDATE:
            if (d.contains("players")) playerSync_.ApplyStateUpdate(d["players"]);
            break;
        case PacketType::ANIMATION_UPDATE:
            playerSync_.ApplyAnimationUpdate(
                d.value("playerId", std::string()),
                d.contains("animState") ? d["animState"] : nlohmann::json::object());
            break;
        case PacketType::RPC_CALL: {
            const std::string rpcType = d.value("type", std::string());
            if (rpcType == "attack") {
                GetCombatSync().HandleAttackRpc(d);
            } else if (rpcType == "damage") {
                GetCombatSync().HandleDamageRpc(d);
            } else if (rpcType == "enemy_update") {
                GetCombatSync().HandleEnemyUpdate(d);
            } else if (rpcType == "skill") {
                GetCombatSync().HandleSkillRpc(d);
            }
            break;
        }
        case PacketType::HOST_REASSIGN: {
            const std::string newHost = d.value("newHostId", std::string());
            std::string localId;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                localId = localId_;
            }
            const bool nowHost = (newHost == localId);
            isHost_.store(nowHost);
            GetCombatSync().SetHost(nowHost);
            if (nowHost) GetCombatSync().InstallHooks();
            CDMP_LOG_INFO("Host reassigned to " + newHost +
                          (nowHost ? " (that's us)" : ""));
            break;
        }
        case PacketType::LOOT_TAKEN:
            GetCombatSync().HandleLootTaken(d);
            break;
        case PacketType::KICK: {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "Kicked: " + d.value("reason", std::string());
            status_.store(SessionStatus::Failed);
            break;
        }
        case PacketType::PONG:
            break;
        case PacketType::SERVER_INFO: {
            std::lock_guard<std::mutex> lock(stateMutex_);
            serverName_ = d.value("sessionName", serverName_);
            break;
        }
        default:
            break;
    }
}

void Session::Tick() {
    const double now = static_cast<double>(GetTickCount64());
    if (status_.load() == SessionStatus::InSession) {
        playerSync_.Tick(now);
        if (now - lastPingMs_ > 2000.0) {
            lastPingMs_ = now;
            SendPing();
        }
    }
}

void Session::SendPing() {
    if (client_ && client_->IsConnected()) {
        client_->Send(PacketType::PING,
                      {{"clientTime", static_cast<double>(GetTickCount64())}});
    }
}

std::string Session::InviteCode() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return inviteCode_;
}
std::string Session::ServerName() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return serverName_;
}
std::string Session::LastError() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return lastError_;
}

} // namespace cdmp
