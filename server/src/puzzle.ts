// SPDX-License-Identifier: MIT
//
// Cooperative puzzle control rotation. While a puzzle is active exactly one
// player holds the "control token"; only that player's puzzle input is allowed
// through on the client. The token rotates through a queue either automatically
// (when the per-turn timer expires) or manually (when the controller passes).

import { Room } from "./room";
import { PacketType, encode, type PuzzleTokenPayload } from "./protocol";

export interface PuzzleSession {
  puzzleId: string;
  controlQueue: string[];
  currentController: string;
  tokenDuration: number;
  tokenTimer: NodeJS.Timeout | null;
  /** Wall-clock ms when the current token was granted (for remainingMs). */
  tokenGrantedAt: number;
}

export class PuzzleManager {
  private puzzles = new Map<string, PuzzleSession>();

  startPuzzle(
    puzzleId: string,
    allPlayerIds: string[],
    room: Room,
    tokenDuration = 30000
  ): void {
    // Restarting an existing puzzle resets its rotation cleanly.
    const existing = this.puzzles.get(puzzleId);
    if (existing) this.clearTimer(existing);

    const queue = [...allPlayerIds];
    if (queue.length === 0) return;

    const session: PuzzleSession = {
      puzzleId,
      controlQueue: queue,
      currentController: queue[0],
      tokenDuration,
      tokenTimer: null,
      tokenGrantedAt: Date.now(),
    };
    this.puzzles.set(puzzleId, session);

    // Announce the puzzle, then grant the first token.
    room.broadcast(encode(PacketType.PUZZLE_START, { puzzleId }));
    this.grantToken(session, room);
  }

  nextController(puzzleId: string, room: Room): void {
    const session = this.puzzles.get(puzzleId);
    if (!session) return;
    if (session.controlQueue.length === 0) {
      this.endPuzzle(puzzleId, room);
      return;
    }

    // Rotate: move the current head to the back, advance to the new head.
    const prev = session.controlQueue.shift();
    if (prev !== undefined) session.controlQueue.push(prev);
    session.currentController = session.controlQueue[0];
    session.tokenGrantedAt = Date.now();
    this.grantToken(session, room);
  }

  endPuzzle(puzzleId: string, room: Room): void {
    const session = this.puzzles.get(puzzleId);
    if (!session) return;
    this.clearTimer(session);
    this.puzzles.delete(puzzleId);
    room.broadcast(encode(PacketType.PUZZLE_END, { puzzleId }));
  }

  onPlayerDisconnect(playerId: string, room: Room): void {
    for (const session of [...this.puzzles.values()]) {
      const idx = session.controlQueue.indexOf(playerId);
      if (idx === -1) continue;

      const wasController = session.currentController === playerId;
      session.controlQueue.splice(idx, 1);

      if (session.controlQueue.length === 0) {
        this.endPuzzle(session.puzzleId, room);
        continue;
      }
      if (wasController) {
        // The controller dropped: hand the token to the new head.
        session.currentController = session.controlQueue[0];
        session.tokenGrantedAt = Date.now();
        this.grantToken(session, room);
      } else {
        // Queue changed but controller unchanged: refresh listeners' view.
        this.broadcastToken(session, room);
      }
    }
  }

  handlePuzzlePass(puzzleId: string, playerId: string, room: Room): void {
    const session = this.puzzles.get(puzzleId);
    if (!session) return;
    // Only the current controller may pass the token.
    if (session.currentController !== playerId) return;
    this.nextController(puzzleId, room);
  }

  /** Grant/refresh the token for the current controller and arm the timer. */
  private grantToken(session: PuzzleSession, room: Room): void {
    this.clearTimer(session);
    this.broadcastToken(session, room);
    session.tokenTimer = setTimeout(() => {
      this.nextController(session.puzzleId, room);
    }, session.tokenDuration);
  }

  private broadcastToken(session: PuzzleSession, room: Room): void {
    const elapsed = Date.now() - session.tokenGrantedAt;
    const remainingMs = Math.max(0, session.tokenDuration - elapsed);
    const payload: PuzzleTokenPayload = {
      puzzleId: session.puzzleId,
      controllerId: session.currentController,
      remainingMs,
      queue: [...session.controlQueue],
    };
    room.broadcast(encode(PacketType.PUZZLE_TOKEN, payload));
  }

  private clearTimer(session: PuzzleSession): void {
    if (session.tokenTimer) {
      clearTimeout(session.tokenTimer);
      session.tokenTimer = null;
    }
  }
}
