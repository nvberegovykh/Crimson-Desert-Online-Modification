// SPDX-License-Identifier: MIT
//
// C++ mirror of server/src/protocol.ts. Packets are JSON envelopes of the form
// { "t": <PacketType string>, "d": <payload object> }.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace cdmp {

constexpr int PROTOCOL_VERSION = 1;

// Traversal interpolation modes. Mirrors server TraversalState.mode.
enum TraversalMode {
    TM_NONE = 0,
    TM_CLIMBING = 1,
    TM_VAULTING = 2,
    TM_LEDGE_GRAB = 3,
    TM_WALL_RUN = 4,
    TM_SLIDE = 5,
    TM_ROLL = 6,
    TM_JUMP = 7,
    TM_FALL = 8,
    TM_LAND = 9,
};

struct TraversalState {
    int mode = TM_NONE;
    int anim_id = 0;
    float anim_time = 0.f;
    float target_x = 0.f, target_y = 0.f, target_z = 0.f;
    float progress = 0.f;
    bool present = false; // false => field absent from the wire packet
};

// Full gameplay state attached to a STATE_UPDATE player entry. Optional fields
// carry a `*_present` flag so the remote side only writes what was actually
// sent (skips gracefully when the local scanner couldn't resolve an offset).
struct PlayerStatePacket {
    TraversalState traversal;

    int skill_id = 0;
    int skill_phase = 0;
    int skill_anim_id = 0;
    float skill_target_x = 0.f, skill_target_y = 0.f, skill_target_z = 0.f;
    int skill_target_entity = 0;
    bool skill_present = false;

    std::vector<int> active_vfx; // up to 8
    bool vfx_present = false;

    uint32_t status_flags = 0;
    std::vector<int> buff_ids; // up to 8
    bool status_present = false;

    int weapon_id = 0;
    int weapon_stance = 0;
    int off_hand_id = 0;
    bool equipment_present = false;

    bool is_mounted = false;
    int mount_entity_id = 0;
    int mount_anim_id = 0;
    float mount_anim_time = 0.f;
    float mount_x = 0.f, mount_y = 0.f, mount_z = 0.f;
    bool mount_present = false;

    float dodge_x = 0.f, dodge_y = 0.f;
    bool dodge_present = false;

    bool is_dead = false;
    float respawn_x = 0.f, respawn_y = 0.f, respawn_z = 0.f;
    bool respawn_present = false;
    bool death_present = false;

    int interaction_type = 0;
    int interaction_entity_id = 0;
    bool interaction_present = false;
};

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
    CUTSCENE_START,
    CUTSCENE_END,
    PUZZLE_START,
    PUZZLE_TOKEN,
    PUZZLE_PASS,
    PUZZLE_END,
    UNKNOWN,
};

const char* PacketTypeToString(PacketType t);
PacketType PacketTypeFromString(const std::string& s);

// Serialize a packet to a JSON string ready to send over the socket.
std::string SerializePacket(PacketType type, const nlohmann::json& payload);

// Parse a raw frame. On failure returns {UNKNOWN, null}.
std::pair<PacketType, nlohmann::json> DeserializePacket(const std::string& raw);

} // namespace cdmp
