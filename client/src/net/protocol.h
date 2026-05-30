// SPDX-License-Identifier: MIT
//
// C++ mirror of server/src/protocol.ts. Packets are JSON envelopes of the form
// { "t": <PacketType string>, "d": <payload object> }.

#pragma once

#include <string>
#include <utility>
#include <nlohmann/json.hpp>

namespace cdmp {

constexpr int PROTOCOL_VERSION = 1;

enum class PacketType {
    HANDSHAKE,
    HANDSHAKE_ACK,
    JOIN_REQUEST,
    JOIN_ACK,
    JOIN_DENIED,
    PLAYER_JOIN,
    PLAYER_LEAVE,
    STATE_UPDATE,
    ANIMATION_UPDATE,
    RPC_CALL,
    RPC_RESULT,
    CHAT,
    HOST_REASSIGN,
    PING,
    PONG,
    KICK,
    SERVER_INFO,
    LOOT_TAKEN,
    UNKNOWN,
};

const char* PacketTypeToString(PacketType t);
PacketType PacketTypeFromString(const std::string& s);

// Serialize a packet to a JSON string ready to send over the socket.
std::string SerializePacket(PacketType type, const nlohmann::json& payload);

// Parse a raw frame. On failure returns {UNKNOWN, null}.
std::pair<PacketType, nlohmann::json> DeserializePacket(const std::string& raw);

} // namespace cdmp
