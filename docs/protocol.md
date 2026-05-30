# Wire Protocol

All messages are JSON text frames over WebSocket with the envelope:

```json
{ "t": "<PacketType>", "d": { ...payload } }
```

`PacketType` is the string name of the enum (e.g. `"STATE_UPDATE"`). Protocol
version is **1** (`HANDSHAKE.protocol`). Unknown or malformed frames are
silently dropped.

## Handshake & join

### HANDSHAKE (client -> server)
```json
{ "t": "HANDSHAKE", "d": { "protocol": 1, "name": "Alice", "clientVersion": "cdmp-0.1.0" } }
```

### HANDSHAKE_ACK (server -> client)
```json
{ "t": "HANDSHAKE_ACK", "d": { "playerId": "uuid", "protocol": 1, "serverName": "My CD Server" } }
```

### JOIN_REQUEST (client -> server)
Create a room:
```json
{ "t": "JOIN_REQUEST", "d": { "create": true, "sessionName": "My Game", "password": "", "friendlyFire": false } }
```
Join an existing room:
```json
{ "t": "JOIN_REQUEST", "d": { "create": false, "inviteCode": "ABC123", "password": "" } }
```

### JOIN_ACK (server -> client)
```json
{
  "t": "JOIN_ACK",
  "d": {
    "inviteCode": "ABC123",
    "isHost": true,
    "friendlyFire": false,
    "sessionName": "My Game",
    "players": [ /* existing PlayerState[] */ ]
  }
}
```

### JOIN_DENIED (server -> client)
```json
{ "t": "JOIN_DENIED", "d": { "reason": "room is full" } }
```

## Presence

### PLAYER_JOIN (server -> clients)
```json
{ "t": "PLAYER_JOIN", "d": { "player": { /* PlayerState */ } } }
```

### PLAYER_LEAVE (server -> clients)
```json
{ "t": "PLAYER_LEAVE", "d": { "playerId": "uuid" } }
```

### HOST_REASSIGN (server -> clients)
```json
{ "t": "HOST_REASSIGN", "d": { "newHostId": "uuid" } }
```

## State

### STATE_UPDATE (both directions)
A client sends only its own state; the server batches changed players back out.
```json
{
  "t": "STATE_UPDATE",
  "d": {
    "players": [
      {
        "id": "uuid",
        "name": "Alice",
        "position": { "x": 100.0, "y": 5.0, "z": -42.0 },
        "rotation": { "yaw": 1.57, "pitch": 0.0 },
        "velocity": { "x": 1.2, "y": 0.0, "z": 0.0 },
        "health": 95.0,
        "maxHealth": 100.0,
        "animState": {
          "anim_id": 1042,
          "anim_time": 0.37,
          "move_flags": 3,
          "combat_flags": 0,
          "blend_weight": 1.0
        },
        "isHost": true,
        "ping": 24
      }
    ]
  }
}
```

### ANIMATION_UPDATE (client -> server -> clients)
```json
{
  "t": "ANIMATION_UPDATE",
  "d": {
    "playerId": "uuid",
    "animState": { "anim_id": 1042, "anim_time": 0.37, "move_flags": 3, "combat_flags": 0, "blend_weight": 1.0 }
  }
}
```

## RPCs (combat & enemies)

### RPC_CALL — attack (guest -> server -> host)
```json
{ "t": "RPC_CALL", "d": { "type": "attack", "anim_id": 2201, "direction": { "x": 0, "y": 0, "z": 1 }, "targetEntityId": 0 } }
```
The server appends `"sourceId"` before relaying to the host.

### RPC_CALL — damage (host -> server -> guests)
```json
{ "t": "RPC_CALL", "d": { "type": "damage", "sourceId": "uuid", "targetEntityId": 12345, "damage": 14.0, "anim_id": 2201 } }
```

### RPC_CALL — enemy_update (host -> server -> guests)
```json
{
  "t": "RPC_CALL",
  "d": {
    "type": "enemy_update",
    "enemies": [
      { "id": "enemy-7", "health": 220.0, "maxHealth": 400.0,
        "position": { "x": 10, "y": 0, "z": 8 },
        "animState": { "anim_id": 88, "anim_time": 0.1, "move_flags": 1, "combat_flags": 2, "blend_weight": 1.0 } }
    ]
  }
}
```

## Loot

### LOOT_TAKEN (host -> server -> guests)
```json
{ "t": "LOOT_TAKEN", "d": { "lootId": "chest-1042", "playerId": "uuid" } }
```

## Liveness

### PING (client -> server) / PONG (server -> client)
```json
{ "t": "PING", "d": { "clientTime": 1717029384000 } }
{ "t": "PONG", "d": { "clientTime": 1717029384000, "serverTime": 1717029384012 } }
```

## Misc

### CHAT (any -> server -> room)
```json
{ "t": "CHAT", "d": { "playerId": "uuid", "name": "Alice", "text": "hello" } }
```

### KICK (server -> clients)
```json
{ "t": "KICK", "d": { "playerId": "uuid", "reason": "kicked by admin" } }
```

### SERVER_INFO (server -> client)
```json
{ "t": "SERVER_INFO", "d": { "sessionName": "My Game", "playerCount": 2, "maxPlayers": 8, "tickRate": 20 } }
```

## Admin HTTP API (port = WS port + 1)

| Method | Path | Auth | Description |
| --- | --- | --- | --- |
| GET | `/status` | none | Health: rooms, players, uptime |
| GET | `/players` | `X-Admin-Token` | All players across rooms |
| POST | `/kick/:id` | `X-Admin-Token` | Kick a player; body `{ "reason": "..." }` |
| POST | `/config` | `X-Admin-Token` | Patch runtime config (JSON body) |
| POST | `/message` | `X-Admin-Token` | Broadcast `{ "text": "..." }` to all rooms |

Example:
```bash
curl -s localhost:7778/status
curl -s -H "X-Admin-Token: changeme" localhost:7778/players
curl -s -X POST -H "X-Admin-Token: changeme" -d '{"reason":"afk"}' localhost:7778/kick/<uuid>
```
