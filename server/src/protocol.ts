// SPDX-License-Identifier: MIT
//
// Wire protocol shared by the session server and the C++ client plugin.
// All packets are JSON objects of the shape { t: PacketType, d: <payload> }.

import type { PlayerState, EnemyState, AnimState } from "./state";

export enum PacketType {
  HANDSHAKE = "HANDSHAKE",
  HANDSHAKE_ACK = "HANDSHAKE_ACK",
  JOIN_REQUEST = "JOIN_REQUEST",
  JOIN_ACK = "JOIN_ACK",
  JOIN_DENIED = "JOIN_DENIED",
  PLAYER_JOIN = "PLAYER_JOIN",
  PLAYER_LEAVE = "PLAYER_LEAVE",
  STATE_UPDATE = "STATE_UPDATE",
  ANIMATION_UPDATE = "ANIMATION_UPDATE",
  RPC_CALL = "RPC_CALL",
  RPC_RESULT = "RPC_RESULT",
  CHAT = "CHAT",
  HOST_REASSIGN = "HOST_REASSIGN",
  PING = "PING",
  PONG = "PONG",
  KICK = "KICK",
  SERVER_INFO = "SERVER_INFO",
  LOOT_TAKEN = "LOOT_TAKEN",
}

/** Generic envelope every packet uses on the wire. */
export interface Packet<T = unknown> {
  t: PacketType;
  d: T;
}

// ---------------------------------------------------------------------------
// Payload interfaces
// ---------------------------------------------------------------------------

export interface HandshakePayload {
  /** Protocol version the client speaks. */
  protocol: number;
  /** Display name chosen by the player. */
  name: string;
  /** Mod/client build string, informational only. */
  clientVersion?: string;
}

export interface HandshakeAckPayload {
  /** Server-assigned player id (uuid). */
  playerId: string;
  protocol: number;
  serverName: string;
}

export interface JoinRequestPayload {
  /**
   * When `create` is true the player wants to host a brand new room and
   * `inviteCode` is ignored. Otherwise it is the room they want to join.
   */
  create: boolean;
  inviteCode?: string;
  password?: string;
  friendlyFire?: boolean;
  sessionName?: string;
}

export interface JoinAckPayload {
  inviteCode: string;
  isHost: boolean;
  friendlyFire: boolean;
  sessionName: string;
  /** Snapshot of everyone already in the room (excluding the joiner). */
  players: PlayerState[];
}

export interface JoinDeniedPayload {
  reason: string;
}

export interface PlayerJoinPayload {
  player: PlayerState;
}

export interface PlayerLeavePayload {
  playerId: string;
}

export interface StateUpdatePayload {
  /** One or more player states (server batches per tick). */
  players: PlayerState[];
}

export interface AnimationUpdatePayload {
  playerId: string;
  animState: AnimState;
}

export interface EnemyUpdatePayload {
  enemies: EnemyState[];
}

export interface RpcCallPayload {
  /** e.g. "attack", "enemy_update", "interact". */
  type: string;
  /** Who triggered the RPC (server fills this in on relay). */
  sourceId?: string;
  // Free-form RPC arguments. Validated by the host.
  [key: string]: unknown;
}

export interface RpcResultPayload {
  type: string;
  accepted: boolean;
  // Free-form result fields.
  [key: string]: unknown;
}

export interface ChatPayload {
  playerId: string;
  name: string;
  text: string;
}

export interface HostReassignPayload {
  newHostId: string;
}

export interface PingPayload {
  /** Client clock in ms; echoed back in PONG for RTT calculation. */
  clientTime: number;
}

export interface PongPayload {
  clientTime: number;
  serverTime: number;
}

export interface KickPayload {
  playerId: string;
  reason: string;
}

export interface ServerInfoPayload {
  sessionName: string;
  playerCount: number;
  maxPlayers: number;
  tickRate: number;
}

export interface LootTakenPayload {
  /** Stable identifier of the looted world object. */
  lootId: string;
  /** Player who took the loot. */
  playerId: string;
}

export const PROTOCOL_VERSION = 1;

// ---------------------------------------------------------------------------
// Serialization helpers
// ---------------------------------------------------------------------------

export function encode<T>(type: PacketType, payload: T): string {
  const packet: Packet<T> = { t: type, d: payload };
  return JSON.stringify(packet);
}

/**
 * Parse and shallow-validate a raw frame. Returns null when the frame is not a
 * well-formed packet so callers can simply drop it.
 */
export function decode(raw: string): Packet | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch {
    return null;
  }
  if (typeof parsed !== "object" || parsed === null) return null;
  const obj = parsed as Record<string, unknown>;
  const type = obj.t;
  if (typeof type !== "string" || !(type in PacketType)) return null;
  if (!("d" in obj)) return null;
  return { t: type as PacketType, d: obj.d };
}
