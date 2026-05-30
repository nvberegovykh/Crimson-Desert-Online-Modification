// SPDX-License-Identifier: MIT
//
// Authoritative state model and the 20Hz tick/diff loop.

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export interface Rotation {
  yaw: number;
  pitch: number;
}

export interface AnimState {
  /** Engine animation clip / state machine id. */
  anim_id: number;
  /** Normalized playback time (0..1) for the active clip. */
  anim_time: number;
  /** Bitmask of movement intent (walk/run/jump/crouch/...). */
  move_flags: number;
  /** Bitmask of combat intent (attacking/blocking/dodging/...). */
  combat_flags: number;
  /** Locomotion blend weight, used to avoid snapping between clips. */
  blend_weight: number;
}

export interface PlayerState {
  id: string;
  name: string;
  position: Vec3;
  rotation: Rotation;
  velocity: Vec3;
  health: number;
  maxHealth: number;
  animState: AnimState;
  isHost: boolean;
  ping: number;
}

export interface EnemyState {
  id: string;
  health: number;
  maxHealth: number;
  position: Vec3;
  animState: AnimState;
}

export function defaultAnimState(): AnimState {
  return {
    anim_id: 0,
    anim_time: 0,
    move_flags: 0,
    combat_flags: 0,
    blend_weight: 1,
  };
}

export function createPlayerState(id: string, name: string): PlayerState {
  return {
    id,
    name,
    position: { x: 0, y: 0, z: 0 },
    rotation: { yaw: 0, pitch: 0 },
    velocity: { x: 0, y: 0, z: 0 },
    health: 100,
    maxHealth: 100,
    animState: defaultAnimState(),
    isHost: false,
    ping: 0,
  };
}

const POS_EPSILON = 0.01;
const VEL_EPSILON = 0.05;
const ROT_EPSILON = 0.001;
const ANIM_TIME_EPSILON = 0.01;

function vecChanged(a: Vec3, b: Vec3, eps: number): boolean {
  return (
    Math.abs(a.x - b.x) > eps ||
    Math.abs(a.y - b.y) > eps ||
    Math.abs(a.z - b.z) > eps
  );
}

/** True when `next` differs from `prev` enough to be worth broadcasting. */
export function playerChanged(prev: PlayerState, next: PlayerState): boolean {
  if (vecChanged(prev.position, next.position, POS_EPSILON)) return true;
  if (vecChanged(prev.velocity, next.velocity, VEL_EPSILON)) return true;
  if (
    Math.abs(prev.rotation.yaw - next.rotation.yaw) > ROT_EPSILON ||
    Math.abs(prev.rotation.pitch - next.rotation.pitch) > ROT_EPSILON
  ) {
    return true;
  }
  if (prev.health !== next.health || prev.maxHealth !== next.maxHealth) {
    return true;
  }
  const a = prev.animState;
  const b = next.animState;
  if (
    a.anim_id !== b.anim_id ||
    a.move_flags !== b.move_flags ||
    a.combat_flags !== b.combat_flags ||
    Math.abs(a.anim_time - b.anim_time) > ANIM_TIME_EPSILON
  ) {
    return true;
  }
  return false;
}

function clonePlayer(p: PlayerState): PlayerState {
  return {
    ...p,
    position: { ...p.position },
    rotation: { ...p.rotation },
    velocity: { ...p.velocity },
    animState: { ...p.animState },
  };
}

/**
 * Holds the live state for one room and runs the diffing tick loop. The loop
 * itself does not send anything — it invokes `onDiff` with the set of players
 * that changed since the last tick so the Room can decide how to broadcast.
 */
export class ServerState {
  private players = new Map<string, PlayerState>();
  private lastBroadcast = new Map<string, PlayerState>();
  private enemies = new Map<string, EnemyState>();
  private timer: NodeJS.Timeout | null = null;

  constructor(
    private readonly tickRate: number,
    private readonly onDiff: (
      changedPlayers: PlayerState[],
      enemies: EnemyState[]
    ) => void
  ) {}

  start(): void {
    if (this.timer) return;
    const intervalMs = Math.max(1, Math.round(1000 / this.tickRate));
    this.timer = setInterval(() => this.tick(), intervalMs);
  }

  stop(): void {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  }

  addPlayer(state: PlayerState): void {
    this.players.set(state.id, state);
  }

  removePlayer(id: string): void {
    this.players.delete(id);
    this.lastBroadcast.delete(id);
  }

  getPlayer(id: string): PlayerState | undefined {
    return this.players.get(id);
  }

  getPlayers(): PlayerState[] {
    return [...this.players.values()];
  }

  /** Merge an incoming partial player update into the authoritative state. */
  updatePlayer(id: string, patch: Partial<PlayerState>): void {
    const cur = this.players.get(id);
    if (!cur) return;
    if (patch.position) cur.position = patch.position;
    if (patch.rotation) cur.rotation = patch.rotation;
    if (patch.velocity) cur.velocity = patch.velocity;
    if (typeof patch.health === "number") cur.health = patch.health;
    if (typeof patch.maxHealth === "number") cur.maxHealth = patch.maxHealth;
    if (patch.animState) cur.animState = patch.animState;
    if (typeof patch.ping === "number") cur.ping = patch.ping;
  }

  setHost(id: string): void {
    for (const p of this.players.values()) {
      p.isHost = p.id === id;
    }
  }

  /** Host-submitted enemy snapshot. Replaces the tracked enemy set. */
  setEnemies(enemies: EnemyState[]): void {
    this.enemies.clear();
    for (const e of enemies) this.enemies.set(e.id, e);
  }

  getEnemies(): EnemyState[] {
    return [...this.enemies.values()];
  }

  private tick(): void {
    const changed: PlayerState[] = [];
    for (const [id, p] of this.players) {
      const prev = this.lastBroadcast.get(id);
      if (!prev || playerChanged(prev, p)) {
        changed.push(clonePlayer(p));
        this.lastBroadcast.set(id, clonePlayer(p));
      }
    }
    if (changed.length > 0 || this.enemies.size > 0) {
      this.onDiff(changed, this.getEnemies());
    }
  }
}
