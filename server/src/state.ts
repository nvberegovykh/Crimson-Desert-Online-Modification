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

/**
 * Parkour / traversal state. `mode` selects the per-mode interpolation strategy
 * applied on remote clients (see client player_sync.cpp ApplyRemoteState).
 */
export interface TraversalState {
  /** 0=NONE 1=CLIMBING 2=VAULTING 3=LEDGE_GRAB 4=WALL_RUN 5=SLIDE 6=ROLL 7=JUMP 8=FALL 9=LAND */
  mode: number;
  animId: number;
  animTime: number;
  targetPos: Vec3;
  /** 0.0-1.0 progress through a one-shot traversal (vault/roll/slide). */
  progress: number;
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
  traversalState?: TraversalState;

  // --- Full gameplay sync (magic / VFX / buffs / weapons / mounts / death) ---
  skillId?: number;
  skillPhase?: number; // 0=none 1=charge 2=cast 3=recovery
  skillAnimId?: number;
  skillTargetPos?: Vec3;
  skillTargetEntity?: number;
  activeVfx?: number[]; // up to 8
  statusFlags?: number; // uint32 bitmask
  buffIds?: number[]; // up to 8
  weaponId?: number;
  weaponStance?: number; // 0=sheathed 1=drawn 2=two-hand
  offHandId?: number;
  isMounted?: boolean;
  mountEntityId?: number;
  mountAnimId?: number;
  mountAnimTime?: number;
  mountPosition?: Vec3;
  dodgeDirection?: { x: number; y: number };
  isDead?: boolean;
  respawnPosition?: Vec3;
  interactionType?: number; // 0=none 1=npc 2=chest 3=object
  interactionEntityId?: number;
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
  if (traversalChanged(prev.traversalState, next.traversalState)) return true;
  if (gameplayChanged(prev, next)) return true;
  return false;
}

function traversalChanged(
  a: TraversalState | undefined,
  b: TraversalState | undefined
): boolean {
  if (!a && !b) return false;
  if (!a || !b) return true;
  return (
    a.mode !== b.mode ||
    a.animId !== b.animId ||
    Math.abs(a.animTime - b.animTime) > ANIM_TIME_EPSILON ||
    Math.abs(a.progress - b.progress) > 0.02 ||
    vecChanged(a.targetPos, b.targetPos, POS_EPSILON)
  );
}

function arrChanged(a?: number[], b?: number[]): boolean {
  if (!a && !b) return false;
  if (!a || !b || a.length !== b.length) return true;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return true;
  return false;
}

function gameplayChanged(prev: PlayerState, next: PlayerState): boolean {
  if (prev.skillId !== next.skillId) return true;
  if (prev.skillPhase !== next.skillPhase) return true;
  if (prev.skillAnimId !== next.skillAnimId) return true;
  if (prev.skillTargetEntity !== next.skillTargetEntity) return true;
  if (arrChanged(prev.activeVfx, next.activeVfx)) return true;
  if (prev.statusFlags !== next.statusFlags) return true;
  if (arrChanged(prev.buffIds, next.buffIds)) return true;
  if (prev.weaponId !== next.weaponId) return true;
  if (prev.weaponStance !== next.weaponStance) return true;
  if (prev.offHandId !== next.offHandId) return true;
  if (prev.isMounted !== next.isMounted) return true;
  if (prev.mountEntityId !== next.mountEntityId) return true;
  if (prev.mountAnimId !== next.mountAnimId) return true;
  if (prev.isDead !== next.isDead) return true;
  if (prev.interactionType !== next.interactionType) return true;
  if (prev.interactionEntityId !== next.interactionEntityId) return true;
  return false;
}

function clonePlayer(p: PlayerState): PlayerState {
  return {
    ...p,
    position: { ...p.position },
    rotation: { ...p.rotation },
    velocity: { ...p.velocity },
    animState: { ...p.animState },
    traversalState: p.traversalState
      ? { ...p.traversalState, targetPos: { ...p.traversalState.targetPos } }
      : undefined,
    skillTargetPos: p.skillTargetPos ? { ...p.skillTargetPos } : undefined,
    activeVfx: p.activeVfx ? [...p.activeVfx] : undefined,
    buffIds: p.buffIds ? [...p.buffIds] : undefined,
    mountPosition: p.mountPosition ? { ...p.mountPosition } : undefined,
    dodgeDirection: p.dodgeDirection ? { ...p.dodgeDirection } : undefined,
    respawnPosition: p.respawnPosition ? { ...p.respawnPosition } : undefined,
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

    // Optional gameplay-sync fields. Only overwrite when present in the patch so
    // a partial update doesn't clear state the client didn't resend.
    if (patch.traversalState !== undefined)
      cur.traversalState = patch.traversalState;
    if (typeof patch.skillId === "number") cur.skillId = patch.skillId;
    if (typeof patch.skillPhase === "number") cur.skillPhase = patch.skillPhase;
    if (typeof patch.skillAnimId === "number")
      cur.skillAnimId = patch.skillAnimId;
    if (patch.skillTargetPos !== undefined)
      cur.skillTargetPos = patch.skillTargetPos;
    if (typeof patch.skillTargetEntity === "number")
      cur.skillTargetEntity = patch.skillTargetEntity;
    if (patch.activeVfx !== undefined) cur.activeVfx = patch.activeVfx;
    if (typeof patch.statusFlags === "number")
      cur.statusFlags = patch.statusFlags;
    if (patch.buffIds !== undefined) cur.buffIds = patch.buffIds;
    if (typeof patch.weaponId === "number") cur.weaponId = patch.weaponId;
    if (typeof patch.weaponStance === "number")
      cur.weaponStance = patch.weaponStance;
    if (typeof patch.offHandId === "number") cur.offHandId = patch.offHandId;
    if (typeof patch.isMounted === "boolean") cur.isMounted = patch.isMounted;
    if (typeof patch.mountEntityId === "number")
      cur.mountEntityId = patch.mountEntityId;
    if (typeof patch.mountAnimId === "number")
      cur.mountAnimId = patch.mountAnimId;
    if (typeof patch.mountAnimTime === "number")
      cur.mountAnimTime = patch.mountAnimTime;
    if (patch.mountPosition !== undefined)
      cur.mountPosition = patch.mountPosition;
    if (patch.dodgeDirection !== undefined)
      cur.dodgeDirection = patch.dodgeDirection;
    if (typeof patch.isDead === "boolean") cur.isDead = patch.isDead;
    if (patch.respawnPosition !== undefined)
      cur.respawnPosition = patch.respawnPosition;
    if (typeof patch.interactionType === "number")
      cur.interactionType = patch.interactionType;
    if (typeof patch.interactionEntityId === "number")
      cur.interactionEntityId = patch.interactionEntityId;
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
