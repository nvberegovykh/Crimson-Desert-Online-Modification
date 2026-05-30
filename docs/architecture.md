# Architecture

The project is organized as **six cooperating layers** split across two
processes: the **session server** (Node.js/TypeScript) and the **client plugin**
(`dxgi.dll`, C++). The server is authoritative for room membership and relays
state; the *host client* is authoritative for combat, enemies, and loot.

## The six layers

### 1. Transport layer (`net/`, server `index.ts`)
RFC 6455 WebSocket. The server uses the `ws` library; the client implements a
minimal masked WebSocket directly on Winsock with a worker thread, a
mutex-guarded send queue, and exponential-backoff reconnect. All frames are JSON
envelopes `{ "t": <PacketType>, "d": <payload> }`.

### 2. Protocol layer (`protocol.ts` / `net/protocol.*`)
A single `PacketType` enum is mirrored on both sides. Encoding/decoding and
shallow validation live here. Adding a packet means editing both mirrors.

### 3. Session/room layer (server `room.ts`, `manager.ts`, client `session.*`)
`RoomManager` owns every `Room`. A room holds up to 8 connections, generates the
6-char invite code, validates passwords, and handles host reassignment when the
host leaves. The client `Session` controller drives the connection lifecycle and
routes incoming packets to the sync systems.

### 4. State layer (server `state.ts`)
`ServerState` runs the **20 Hz tick loop** and performs **delta diffing**: each
tick it compares the live player states against the last broadcast snapshot and
emits `STATE_UPDATE` only for players that changed beyond per-field epsilons.
Enemy snapshots submitted by the host are relayed to guests.

### 5. Sync layer (client `sync/`)
- **`player_sync`** reads the local transform + animation block at 20 Hz and
  sends it; for each remote player it interpolates position (100 ms buffer) and
  writes position, velocity, health, `anim_id`, `anim_time`, `move_flags`,
  `combat_flags`, and `blend_weight` into that player's entity memory so the
  engine's own animation system plays the correct clips.
- **`combat_sync`** hooks the attack-trigger function to emit attack RPCs,
  applies host-validated damage, mirrors host enemy state into memory, and
  (host only) broadcasts `LOOT_TAKEN`.

### 6. Presentation + integration layer (client `hooks/`, `ui/`, `inventory/`, `proxy/`)
- **`proxy`** forwards the real dxgi exports so the DLL can masquerade as the
  system `dxgi.dll`.
- **`hooks/render_hook`** detours `IDXGISwapChain::Present` (vtable index 8) to
  initialize and draw the ImGui overlay each frame.
- **`ui/modal`** is the Elden-Ring-style host/join/in-session menu.
- **`inventory/inventory_hook`** appends the Multiplayer Beacon item and hooks
  item-use; F9 is the fallback entry point.
- **`game/scanner` + `game/memory`** underpin layers 5–6 with the signature
  scanner, offset cache, and SEH-guarded memory access.

## Data flow

```
   LOCAL CLIENT (any player)                 SERVER                 REMOTE CLIENTS
   ---------------------------               ------                 --------------
   player_sync.SendLocalState()
     read entity memory @20Hz
     -> STATE_UPDATE / ANIMATION_UPDATE  ->  ServerState.updatePlayer
                                              tick loop (20Hz):
                                              diff vs last snapshot
                                              -> STATE_UPDATE (changed only) ->  player_sync.ApplyStateUpdate
                                                                                  interpolate + write to
                                                                                  remote entity memory
                                                                                  (drives anim, no T-pose)

   combat_sync (attack hook)
     -> RPC_CALL{attack}              ->  relay to HOST
                                          HOST combat_sync.HandleAttackRpc
                                          validate + compute damage
                                          -> RPC_CALL{damage}            ->  guests apply damage to memory

   HOST combat_sync (enemy tick)
     -> RPC_CALL{enemy_update}        ->  ServerState.setEnemies
                                          tick relay to guests           ->  guests write enemy memory

   HOST loot interaction
     -> LOOT_TAKEN                    ->  broadcast to guests            ->  guests hide world object
```

## Authority model

| Concern | Authority | Rationale |
| --- | --- | --- |
| Room membership, invite codes | Server | Single source of truth, survives host churn |
| Player transform/animation | Owning client | Lowest latency for one's own avatar |
| Combat damage | Host | Prevents client-side damage spoofing |
| Enemy state | Host | One simulation avoids divergence |
| Loot | Host | Prevents duplication; guests only get notified |

## Failure handling

- **Scanner miss** → fields stay `0`; memory helpers no-op; the F9 fallback
  still lets you open the menu, and the offset cache is re-scanned on the next
  game build.
- **Network drop** → the WebSocket client reconnects with backoff and
  re-handshakes; `Session` re-issues the join.
- **Host leaves** → server promotes the next connection and broadcasts
  `HOST_REASSIGN`; the new host installs combat hooks.
