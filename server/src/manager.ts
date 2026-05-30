// SPDX-License-Identifier: MIT
//
// RoomManager owns every active Room and maps players -> rooms. Shared between
// the WebSocket server (index.ts) and the admin HTTP API (admin.ts).

import { log } from "./logger";
import { Room, type Connection } from "./room";
import type { ServerConfig } from "./config";

export class RoomManager {
  private rooms = new Map<string, Room>(); // inviteCode -> Room
  private playerRoom = new Map<string, string>(); // playerId -> inviteCode

  constructor(private readonly config: ServerConfig) {}

  createRoom(
    hostId: string,
    opts: {
      sessionName?: string;
      password?: string;
      friendlyFire?: boolean;
    }
  ): Room {
    const room = new Room({
      hostId,
      sessionName: opts.sessionName?.trim() || this.config.sessionName,
      password: opts.password ?? this.config.password,
      friendlyFire: opts.friendlyFire ?? this.config.friendlyFire,
      maxPlayers: this.config.maxPlayers,
      tickRate: this.config.tickRate,
    });
    this.rooms.set(room.inviteCode, room);
    log.info(`Created room ${room.inviteCode} ("${room.sessionName}")`);
    return room;
  }

  getRoom(inviteCode: string): Room | undefined {
    return this.rooms.get(inviteCode.toUpperCase());
  }

  getRoomForPlayer(playerId: string): Room | undefined {
    const code = this.playerRoom.get(playerId);
    return code ? this.rooms.get(code) : undefined;
  }

  bindPlayer(playerId: string, room: Room, conn: Connection): void {
    this.playerRoom.set(playerId, room.inviteCode);
    room.addPlayer(conn);
  }

  /** Remove a player from whatever room they are in; clean up empty rooms. */
  removePlayer(playerId: string): void {
    const code = this.playerRoom.get(playerId);
    if (!code) return;
    this.playerRoom.delete(playerId);
    const room = this.rooms.get(code);
    if (!room) return;
    const empty = room.removePlayer(playerId);
    if (empty) {
      this.rooms.delete(code);
      log.info(`Room ${code} is empty, destroyed`);
    }
  }

  /** Force-disconnect a player (admin kick). Returns true if found. */
  kickPlayer(playerId: string, reason: string): boolean {
    const room = this.getRoomForPlayer(playerId);
    if (!room) return false;
    const conn = room.getConnection(playerId);
    if (conn) {
      try {
        conn.socket.close(4000, reason);
      } catch {
        /* ignore */
      }
    }
    this.removePlayer(playerId);
    return true;
  }

  listRooms(): Room[] {
    return [...this.rooms.values()];
  }

  totalPlayers(): number {
    return this.playerRoom.size;
  }
}
