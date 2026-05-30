// SPDX-License-Identifier: MIT

#include "protocol.h"

#include <array>
#include <cstring>

namespace cdmp {

namespace {
struct Entry {
    PacketType type;
    const char* name;
};
constexpr std::array<Entry, 24> kTable = {{
    {PacketType::HANDSHAKE, "HANDSHAKE"},
    {PacketType::HANDSHAKE_ACK, "HANDSHAKE_ACK"},
    {PacketType::JOIN_REQUEST, "JOIN_REQUEST"},
    {PacketType::JOIN_ACK, "JOIN_ACK"},
    {PacketType::JOIN_DENIED, "JOIN_DENIED"},
    {PacketType::PLAYER_JOIN, "PLAYER_JOIN"},
    {PacketType::PLAYER_LEAVE, "PLAYER_LEAVE"},
    {PacketType::STATE_UPDATE, "STATE_UPDATE"},
    {PacketType::ANIMATION_UPDATE, "ANIMATION_UPDATE"},
    {PacketType::RPC_CALL, "RPC_CALL"},
    {PacketType::RPC_RESULT, "RPC_RESULT"},
    {PacketType::CHAT, "CHAT"},
    {PacketType::HOST_REASSIGN, "HOST_REASSIGN"},
    {PacketType::PING, "PING"},
    {PacketType::PONG, "PONG"},
    {PacketType::KICK, "KICK"},
    {PacketType::SERVER_INFO, "SERVER_INFO"},
    {PacketType::LOOT_TAKEN, "LOOT_TAKEN"},
    {PacketType::CUTSCENE_START, "CUTSCENE_START"},
    {PacketType::CUTSCENE_END, "CUTSCENE_END"},
    {PacketType::PUZZLE_START, "PUZZLE_START"},
    {PacketType::PUZZLE_TOKEN, "PUZZLE_TOKEN"},
    {PacketType::PUZZLE_PASS, "PUZZLE_PASS"},
    {PacketType::PUZZLE_END, "PUZZLE_END"},
}};
} // namespace

const char* PacketTypeToString(PacketType t) {
    for (const auto& e : kTable) {
        if (e.type == t) return e.name;
    }
    return "UNKNOWN";
}

PacketType PacketTypeFromString(const std::string& s) {
    for (const auto& e : kTable) {
        if (s == e.name) return e.type;
    }
    return PacketType::UNKNOWN;
}

std::string SerializePacket(PacketType type, const nlohmann::json& payload) {
    nlohmann::json packet;
    packet["t"] = PacketTypeToString(type);
    packet["d"] = payload;
    return packet.dump();
}

std::pair<PacketType, nlohmann::json> DeserializePacket(const std::string& raw) {
    try {
        auto j = nlohmann::json::parse(raw);
        if (!j.is_object() || !j.contains("t")) {
            return {PacketType::UNKNOWN, nullptr};
        }
        const PacketType t = PacketTypeFromString(j["t"].get<std::string>());
        nlohmann::json d = j.contains("d") ? j["d"] : nlohmann::json::object();
        return {t, std::move(d)};
    } catch (...) {
        return {PacketType::UNKNOWN, nullptr};
    }
}

} // namespace cdmp
