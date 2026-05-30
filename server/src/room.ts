// SPDX-License-Identifier: MIT
//
// Room: a single multiplayer session of up to 8 players. Owns the player
// connections, the authoritative ServerState, and host-reassignment logic.

import type { WebSocket } from "ws";
import { log } from "./logger";
import {
  PacketType,
  encode,
  type HostReassignPayload,
  type PlayerJoinPayload,
  type PlayerLeavePayload,
  type StateUpdatePayload,
  type EnemyUpdatePayload,
  type CutscenePayload,
} from "./protocol";
import {
  ServerState,
  createPlayerState,
  type PlayerState,
  type EnemyState,
} from "./state";
import { PuzzleManager } from "./puzzle";

const MAX_PLAYERS_HARD = 8;
const INVITE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no 0/O/1/I
const INVITE_LEN = 6;

export interface Connection {
  playerId: string;
  name: string;
  socket: WebSocket;
  lastPingMs: number;
  alive: boolean;
}

function genInviteCode(): string {
  let out = "";
  for (let i = 0; i < INVITE_LEN; i++) {
    out += INVITE_ALPHABET[Math.floor(Math.random() * INVITE_ALPHABET.length)];
  }
  return out;
}

export class Room {
  readonly inviteCode: string;
  readonly sessionName: string;
  readonly maxPlayers: number;
  readonly friendlyFire: boolean;
  private readonly password: string;
  private hostId: string;
  private readonly connections = new Map<string, Connection>();
  readonly state: ServerState;
  readonly puzzleManager = new PuzzleManager();

  constructor(opts: {
    hostId: string;
    sessionName: string;
    password?: string;
    friendlyFire?: boolean;
    maxPlayers: number;
    tickRate: number;
    inviteCode?: string;
  }) {
    this.inviteCode = opts.inviteCode ?? genInviteCode();
    this.sessionName = opts.sessionName;
    this.password = opts.password ?? "";
    this.friendlyFire = opts.friendlyFire ?? false;
    this.maxPlayers = Math.min(opts.maxPlayers, MAX_PLAYERS_HARD);
    this.hostId = opts.hostId;
    this.state = new ServerState(opts.tickRate, (players, enemies) =>
      this.broadcastDiff(players, enemies)
    );
    this.state.start();
  }

  get hostPlayerId(): string {
    return this.hostId;
  }

  isHost(playerId: string): boolean {
    return this.hostId === playerId;
  }

  isFull(): boolean {
    return this.connections.size >= this.maxPlayers;
  }

  isEmpty(): boolean {
    return this.connections.size === 0;
  }

  size(): number {
    return this.connections.size;
  }

  checkPassword(candidate: string | undefined): boolean {
    if (!this.password) return true;
    return candidate === this.password;
  }

  getConnections(): Connection[] {
    return [...this.connections.values()];
  }

  getConnection(playerId: string): Connection | undefined {
    return this.connections.get(playerId);
  }

  getPlayerIds(): string[] {
    return [...this.connections.keys()];
  }

  /**
   * Adds a player to the room. Caller is responsible for password / capacity
   * checks. Returns the freshly created PlayerState.
   */
  addPlayer(conn: Connection): PlayerState {
    const ps = createPlayerState(conn.playerId, conn.name);
    ps.isHost = conn.playerId === this.hostId;
    this.connections.set(conn.playerId, conn);
    this.state.addPlayer(ps);

    // Announce the newcomer to everyone already present.
    const payload: PlayerJoinPayload = { player: ps };
    this.broadcastExcept(conn.playerId, encode(PacketType.PLAYER_JOIN, payload));
    log.info(
      `Room ${this.inviteCode}: ${conn.name} (${conn.playerId}) joined ` +
        `(${this.connections.size}/${this.maxPlayers})`
    );
    return ps;
  }

  /**
   * Removes a player. If the host left and players remain, reassigns the host
   * and broadcasts HOST_REASSIGN. Returns true when the room is now empty.
   */
  removePlayer(playerId: string): boolean {
    const conn = this.connections.get(playerId);
    if (!conn) return this.isEmpty();
    this.connections.delete(playerId);
    this.state.removePlayer(playerId);
    this.puzzleManager.onPlayerDisconnect(playerId, this);

    const leavePayload: PlayerLeavePayload = { playerId };
    this.broadcast(encode(PacketType.PLAYER_LEAVE, leavePayload));
    log.info(`Room ${this.inviteCode}: ${conn.name} left`);

    if (playerId === this.hostId && this.connections.size > 0) {
      this.reassignHost();
    }
    if (this.isEmpty()) {
      this.state.stop();
      return true;
    }
    return false;
  }

  /** Promote the next remaining connection to host. */
  reassignHost(): void {
    const next = this.connections.values().next().value as
      | Connection
      | undefined;
    if (!next) return;
    this.hostId = next.playerId;
    this.state.setHost(this.hostId);
    const payload: HostReassignPayload = { newHostId: this.hostId };
    this.broadcast(encode(PacketType.HOST_REASSIGN, payload));
    log.info(`Room ${this.inviteCode}: host reassigned to ${next.name}`);
  }

  /** Replace the room's enemy snapshot (host-authoritative). */
  submitEnemies(enemies: EnemyState[]): void {
    this.state.setEnemies(enemies);
  }

  /**
   * A player entered (active=true) or left (active=false) a cutscene. Update the
   * authoritative state and notify everyone else so their UI / suspend logic can
   * react. Returns false when the player is unknown.
   */
  setCutscene(playerId: string, active: boolean): boolean {
    if (!this.connections.has(playerId)) return false;
    this.state.setCutscene(playerId, active);
    const type = active ? PacketType.CUTSCENE_START : PacketType.CUTSCENE_END;
    const payload: CutscenePayload = { playerId };
    this.broadcastExcept(playerId, encode(type, payload));
    log.info(
      `Room ${this.inviteCode}: ${playerId} cutscene ` +
        (active ? "started" : "ended")
    );
    return true;
  }

  broadcast(raw: string): void {
    for (const conn of this.connections.values()) {
      this.safeSend(conn, raw);
    }
  }

  broadcastExcept(exceptId: string, raw: string): void {
    for (const conn of this.connections.values()) {
      if (conn.playerId === exceptId) continue;
      this.safeSend(conn, raw);
    }
  }

  sendTo(playerId: string, raw: string): void {
    const conn = this.connections.get(playerId);
    if (conn) this.safeSend(conn, raw);
  }

  private safeSend(conn: Connection, raw: string): void {
    try {
      if (conn.socket.readyState === conn.socket.OPEN) {
        conn.socket.send(raw);
      }
    } catch (err) {
      log.warn(`send failed to ${conn.playerId}:`, err);
    }
  }

  private broadcastDiff(players: PlayerState[], enemies: EnemyState[]): void {
    if (players.length > 0) {
      const payload: StateUpdatePayload = { players };
      this.broadcast(encode(PacketType.STATE_UPDATE, payload));
    }
    if (enemies.length > 0) {
      const payload: EnemyUpdatePayload = { enemies };
      // Enemy state is host-authoritative: send only to guests.
      this.broadcastExcept(this.hostId, encode(PacketType.RPC_CALL, {
        type: "enemy_update",
        ...payload,
      }));
    }
  }
}
