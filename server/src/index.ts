// SPDX-License-Identifier: MIT
//
// Entry point: boots the WebSocket session server and the admin HTTP API.

import { WebSocketServer, type WebSocket } from "ws";
import { v4 as uuidv4 } from "uuid";
import { log } from "./logger";
import { loadConfig, RuntimeConfig } from "./config";
import { RoomManager } from "./manager";
import { startAdminServer } from "./admin";
import type { Connection } from "./room";
import {
  PacketType,
  PROTOCOL_VERSION,
  decode,
  encode,
  type HandshakePayload,
  type HandshakeAckPayload,
  type JoinRequestPayload,
  type JoinAckPayload,
  type JoinDeniedPayload,
  type StateUpdatePayload,
  type AnimationUpdatePayload,
  type RpcCallPayload,
  type ChatPayload,
  type PingPayload,
  type PongPayload,
  type ServerInfoPayload,
  type LootTakenPayload,
} from "./protocol";
import type { PlayerState, EnemyState } from "./state";

interface Session {
  socket: WebSocket;
  playerId: string;
  name: string;
  handshaked: boolean;
  inRoom: boolean;
}

function main(): void {
  const config = loadConfig();
  const runtime = new RuntimeConfig(config);
  const manager = new RoomManager(runtime.current);

  const wss = new WebSocketServer({ port: runtime.current.port });
  log.info(
    `CD Multiplayer session server listening on ws://0.0.0.0:${runtime.current.port}`
  );

  wss.on("connection", (socket) => {
    const session: Session = {
      socket,
      playerId: "",
      name: "",
      handshaked: false,
      inRoom: false,
    };

    socket.on("message", (data) => {
      const raw = typeof data === "string" ? data : data.toString("utf8");
      const packet = decode(raw);
      if (!packet) {
        log.debug("dropped malformed packet");
        return;
      }
      try {
        handlePacket(session, packet.t, packet.d, manager, runtime);
      } catch (err) {
        log.error("handler error:", err);
      }
    });

    socket.on("close", () => {
      if (session.playerId) {
        manager.removePlayer(session.playerId);
        log.info(`Connection closed: ${session.name || session.playerId}`);
      }
    });

    socket.on("error", (err) => {
      log.warn("socket error:", err.message);
    });
  });

  const admin = startAdminServer(manager, runtime);

  const shutdown = () => {
    log.info("Shutting down...");
    for (const room of manager.listRooms()) room.state.stop();
    wss.close();
    admin.close();
    setTimeout(() => process.exit(0), 250);
  };
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
}

function handlePacket(
  session: Session,
  type: PacketType,
  payload: unknown,
  manager: RoomManager,
  runtime: RuntimeConfig
): void {
  switch (type) {
    case PacketType.HANDSHAKE:
      onHandshake(session, payload as HandshakePayload, runtime);
      break;
    case PacketType.JOIN_REQUEST:
      onJoinRequest(session, payload as JoinRequestPayload, manager, runtime);
      break;
    case PacketType.STATE_UPDATE:
      onStateUpdate(session, payload as StateUpdatePayload, manager);
      break;
    case PacketType.ANIMATION_UPDATE:
      onAnimationUpdate(session, payload as AnimationUpdatePayload, manager);
      break;
    case PacketType.RPC_CALL:
      onRpcCall(session, payload as RpcCallPayload, manager);
      break;
    case PacketType.CHAT:
      onChat(session, payload as ChatPayload, manager);
      break;
    case PacketType.LOOT_TAKEN:
      onLootTaken(session, payload as LootTakenPayload, manager);
      break;
    case PacketType.CUTSCENE_START:
      onCutscene(session, manager, true);
      break;
    case PacketType.CUTSCENE_END:
      onCutscene(session, manager, false);
      break;
    case PacketType.PING:
      onPing(session, payload as PingPayload, manager);
      break;
    default:
      log.debug(`unhandled packet type ${type}`);
  }
}

function onHandshake(
  session: Session,
  payload: HandshakePayload,
  runtime: RuntimeConfig
): void {
  if (session.handshaked) return;
  if (payload.protocol !== PROTOCOL_VERSION) {
    const denied: JoinDeniedPayload = {
      reason: `protocol mismatch (server ${PROTOCOL_VERSION}, client ${payload.protocol})`,
    };
    send(session.socket, PacketType.JOIN_DENIED, denied);
    session.socket.close();
    return;
  }
  session.playerId = uuidv4();
  session.name = (payload.name || "Player").slice(0, 32);
  session.handshaked = true;
  const ack: HandshakeAckPayload = {
    playerId: session.playerId,
    protocol: PROTOCOL_VERSION,
    serverName: runtime.current.sessionName,
  };
  send(session.socket, PacketType.HANDSHAKE_ACK, ack);
  log.info(`Handshake from ${session.name} -> ${session.playerId}`);
}

function onJoinRequest(
  session: Session,
  payload: JoinRequestPayload,
  manager: RoomManager,
  runtime: RuntimeConfig
): void {
  if (!session.handshaked) {
    deny(session, "handshake required");
    return;
  }
  if (session.inRoom) {
    deny(session, "already in a room");
    return;
  }

  const conn: Connection = {
    playerId: session.playerId,
    name: session.name,
    socket: session.socket,
    lastPingMs: 0,
    alive: true,
  };

  let room;
  if (payload.create) {
    room = manager.createRoom(session.playerId, {
      sessionName: payload.sessionName,
      password: payload.password,
      friendlyFire: payload.friendlyFire,
    });
  } else {
    if (!payload.inviteCode) {
      deny(session, "invite code required");
      return;
    }
    room = manager.getRoom(payload.inviteCode);
    if (!room) {
      deny(session, "room not found");
      return;
    }
    if (room.isFull()) {
      deny(session, "room is full");
      return;
    }
    if (!room.checkPassword(payload.password)) {
      deny(session, "incorrect password");
      return;
    }
  }

  // Snapshot existing players BEFORE adding the joiner.
  const existing: PlayerState[] = room.state.getPlayers();
  manager.bindPlayer(session.playerId, room, conn);
  session.inRoom = true;

  const ack: JoinAckPayload = {
    inviteCode: room.inviteCode,
    isHost: room.isHost(session.playerId),
    friendlyFire: room.friendlyFire,
    sessionName: room.sessionName,
    players: existing,
  };
  send(session.socket, PacketType.JOIN_ACK, ack);

  const info: ServerInfoPayload = {
    sessionName: room.sessionName,
    playerCount: room.size(),
    maxPlayers: room.maxPlayers,
    tickRate: runtime.current.tickRate,
  };
  send(session.socket, PacketType.SERVER_INFO, info);
}

function onStateUpdate(
  session: Session,
  payload: StateUpdatePayload,
  manager: RoomManager
): void {
  const room = manager.getRoomForPlayer(session.playerId);
  if (!room || !Array.isArray(payload.players)) return;
  for (const p of payload.players) {
    // A client may only update its own state.
    if (p.id !== session.playerId) continue;
    room.state.updatePlayer(session.playerId, {
      position: p.position,
      rotation: p.rotation,
      velocity: p.velocity,
      health: p.health,
      maxHealth: p.maxHealth,
      animState: p.animState,
      traversalState: p.traversalState,
      skillId: p.skillId,
      skillPhase: p.skillPhase,
      skillAnimId: p.skillAnimId,
      skillTargetPos: p.skillTargetPos,
      skillTargetEntity: p.skillTargetEntity,
      activeVfx: p.activeVfx,
      statusFlags: p.statusFlags,
      buffIds: p.buffIds,
      weaponId: p.weaponId,
      weaponStance: p.weaponStance,
      offHandId: p.offHandId,
      isMounted: p.isMounted,
      mountEntityId: p.mountEntityId,
      mountAnimId: p.mountAnimId,
      mountAnimTime: p.mountAnimTime,
      mountPosition: p.mountPosition,
      dodgeDirection: p.dodgeDirection,
      isDead: p.isDead,
      respawnPosition: p.respawnPosition,
      interactionType: p.interactionType,
      interactionEntityId: p.interactionEntityId,
    });
  }
}

function onAnimationUpdate(
  session: Session,
  payload: AnimationUpdatePayload,
  manager: RoomManager
): void {
  const room = manager.getRoomForPlayer(session.playerId);
  if (!room) return;
  if (payload.playerId !== session.playerId) return;
  room.state.updatePlayer(session.playerId, { animState: payload.animState });
}

function onRpcCall(
  session: Session,
  payload: RpcCallPayload,
  manager: RoomManager
): void {
  const room = manager.getRoomForPlayer(session.playerId);
  if (!room) return;
  const withSource: RpcCallPayload = { ...payload, sourceId: session.playerId };

  if (payload.type === "enemy_update") {
    // Only the host may submit enemy state.
    if (!room.isHost(session.playerId)) return;
    const enemies = (payload.enemies as EnemyState[]) ?? [];
    room.submitEnemies(enemies);
    return; // relayed by the tick loop
  }

  if (payload.type === "attack") {
    // Attacks are validated by the host. Relay to the host for adjudication;
    // the host will emit RPC_RESULT / damage application back to the room.
    room.sendTo(room.hostPlayerId, encode(PacketType.RPC_CALL, withSource));
    return;
  }

  if (payload.type === "damage") {
    // Host-adjudicated damage result -> broadcast to everyone but the host.
    if (!room.isHost(session.playerId)) return;
    room.broadcastExcept(
      room.hostPlayerId,
      encode(PacketType.RPC_CALL, withSource)
    );
    return;
  }

  // Generic relay for any other RPC.
  room.broadcastExcept(session.playerId, encode(PacketType.RPC_CALL, withSource));
}

function onChat(
  session: Session,
  payload: ChatPayload,
  manager: RoomManager
): void {
  const room = manager.getRoomForPlayer(session.playerId);
  if (!room) return;
  const msg: ChatPayload = {
    playerId: session.playerId,
    name: session.name,
    text: (payload.text || "").slice(0, 256),
  };
  room.broadcast(encode(PacketType.CHAT, msg));
}

function onLootTaken(
  session: Session,
  payload: LootTakenPayload,
  manager: RoomManager
): void {
  const room = manager.getRoomForPlayer(session.playerId);
  if (!room) return;
  // Loot is host-authoritative; only the host announces a take.
  if (!room.isHost(session.playerId)) return;
  const msg: LootTakenPayload = {
    lootId: payload.lootId,
    playerId: payload.playerId,
  };
  room.broadcastExcept(room.hostPlayerId, encode(PacketType.LOOT_TAKEN, msg));
}

function onCutscene(
  session: Session,
  manager: RoomManager,
  active: boolean
): void {
  const room = manager.getRoomForPlayer(session.playerId);
  if (!room) return;
  // A client may only toggle its own cutscene state; the room broadcasts the
  // change to everyone else.
  room.setCutscene(session.playerId, active);
}

function onPing(
  session: Session,
  payload: PingPayload,
  manager: RoomManager
): void {
  const pong: PongPayload = {
    clientTime: payload.clientTime,
    serverTime: Date.now(),
  };
  send(session.socket, PacketType.PONG, pong);
  const room = manager.getRoomForPlayer(session.playerId);
  if (room) {
    const rtt = Math.max(0, Date.now() - payload.clientTime);
    room.state.updatePlayer(session.playerId, { ping: rtt });
  }
}

function deny(session: Session, reason: string): void {
  const denied: JoinDeniedPayload = { reason };
  send(session.socket, PacketType.JOIN_DENIED, denied);
  log.info(`Join denied for ${session.name}: ${reason}`);
}

function send<T>(socket: WebSocket, type: PacketType, payload: T): void {
  try {
    if (socket.readyState === socket.OPEN) socket.send(encode(type, payload));
  } catch (err) {
    log.warn("send failed:", err);
  }
}

main();
