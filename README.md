# Crimson Desert Online Modification

A self-hosted, GTA5-RP style multiplayer mod for **Crimson Desert** (Pearl Abyss,
BLACKSPACE engine, Steam App `3321460`). It pairs a small Node.js/TypeScript
session server with a C++ proxy `dxgi.dll` client plugin that renders an
Elden-Ring-style host/join overlay inside the game.

> **This is a research/hobby project.** It requires that every participant owns
> a legitimate copy of the game. Read [LEGAL.md](LEGAL.md) before using or
> distributing it.

---

## Features

- **8-player sessions** with 6-character invite codes (Elden Ring style).
- **Host-authoritative** simulation: combat, enemy state, and loot are owned by
  the host; guests relay their inputs.
- **20 Hz state relay** with delta diffing so only changed players are broadcast.
- **Full animation sync** — position, rotation, velocity, health, animation
  state id + playback time, movement flags, and combat flags are written into
  remote player entity memory so the engine plays the correct clips
  (**no T-poses**).
- **Proxy DLL injection** — drops in as `dxgi.dll` next to the game executable
  (the same technique ReShade uses), so it is **Denuvo-compatible** and never
  modifies the protected binary.
- **Runtime signature scanner** with heuristic BLACKSPACE-engine patterns and a
  per-build **offset cache** keyed by the game binary's hash.
- **Inventory beacon**: a synthetic "Multiplayer Beacon" item is *appended* to
  your inventory (never overwriting an existing slot). Using it opens the
  multiplayer menu. If scanning fails, **F9** opens the same menu.
- **Admin HTTP API** for status, player listing, kicking, live config, and
  server-wide messages.

---

## Architecture

```
                          +--------------------------------------------+
                          |              SESSION SERVER                |
                          |      (Node.js + TypeScript, port 7777)     |
                          |                                            |
   +----------+  ws       |   +---------+   20Hz    +--------------+   |   http 7778
   | Player A |<--------->|   |  Rooms  |--tick---->| ServerState  |   |<------------ admin
   | (HOST)   |           |   | (<=8 ea)|  diffing  |  + EnemyState|   |
   +----------+           |   +---------+           +--------------+   |
   +----------+  ws       |        ^  relay packets        |           |
   | Player B |<--------->|--------+                       |           |
   +----------+           +--------------------------------------------+
        ^
        | in-process
        v
   +----------------------------- CLIENT PLUGIN (dxgi.dll) ----------------------------+
   |                                                                                   |
   |  proxy/    -> forwards real dxgi exports to System32\dxgi.dll                     |
   |  game/     -> signature scanner + offset cache + safe memory r/w                  |
   |  hooks/    -> IDXGISwapChain::Present vtable hook  -> ImGui overlay               |
   |  ui/       -> Elden-Ring style modal (host / join / in-session)                   |
   |  net/      -> RFC6455 WebSocket client (reconnect, send queue)                    |
   |  sync/     -> player_sync (transform+anim) . combat_sync (attacks/enemies/loot)   |
   |  inventory/-> appends Multiplayer Beacon + hooks item-use (F9 fallback)           |
   +-----------------------------------------------------------------------------------+
```

See [docs/architecture.md](docs/architecture.md) for the full six-layer
description and data-flow diagram.

---

## Quick start

### 1. Server

```bash
cd server
npm install
cp config.example.json config.json   # edit if you like
npm run dev                           # or: npm run build && npm start
```

The WebSocket server listens on `7777` and the admin API on `7778`.

### 2. Client (`dxgi.dll`)

Requires Visual Studio 2022 (or Build Tools) with the C++ workload and CMake
3.20+.

```bash
cd client
cmake -B build -A x64
cmake --build build --config Release
```

The output is `client/build/Release/dxgi.dll`. Copy it into your Crimson Desert
game folder (the directory containing the game executable). On Windows you can
just run `Install_DLL.bat` from the repo root, which reads the install path from
the registry.

### 3. Play

1. Launch the server (`Launch_CDMultiplayer.bat` or `npm run dev`).
2. Launch Crimson Desert from Steam.
3. In-game, **use the Multiplayer Beacon** item (or press **F9**).
4. **Host:** click *Host Session* -> *Start Hosting*. A 6-character invite code
   appears; copy it and share it.
5. **Guests:** click *Join Session*, enter the code (and password if set), then
   *Connect*.

---

## Invite code flow

```
HOST                          SERVER                         GUEST
 |  HANDSHAKE --------------->  |                              |
 |  <----------- HANDSHAKE_ACK  |                              |
 |  JOIN_REQUEST{create:true}-> |  (creates room, code=ABC123) |
 |  <------------ JOIN_ACK{ABC123, isHost:true}               |
 |   shows "ABC123" in overlay  |                              |
 |                              |  <---------------- HANDSHAKE |
 |                              |  HANDSHAKE_ACK ------------> |
 |                              |  < JOIN_REQUEST{code:ABC123} |
 |                              |  JOIN_ACK{isHost:false} ---> |
 |  < PLAYER_JOIN(guest) ------ | --------- PLAYER_JOIN(host)> |
 |  STATE_UPDATE (20Hz) <------>|<-------- STATE_UPDATE (20Hz) |
```

Full packet reference: [docs/protocol.md](docs/protocol.md).

---

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| Overlay never appears | Confirm `dxgi.dll` is in the game folder. Check `%APPDATA%\CDMultiplayer\client.log`. |
| Beacon item not in inventory | The scanner could not resolve the inventory. Press **F9** to open the menu instead. |
| "room not found" | Codes are case-insensitive but must be exact. Confirm the host is still connected. |
| Can't connect | Verify the server host/port in the overlay, and that port `7777` is reachable (firewall / port-forward for remote play). |
| Remote players T-pose | Animation offsets were not resolved. Delete `%APPDATA%\CDMultiplayer\offsets_cache.json` to force a re-scan after a game update. |

Set the `CD_DEBUG=1` environment variable before launching the server for verbose logs.

---

## Repository layout

```
server/    Node.js + TypeScript session server
client/    C++ proxy dxgi.dll plugin (CMake)
docs/      architecture, protocol, setup
.github/   CI workflow + issue templates
```

## License

[MIT](LICENSE). Third-party dependencies (ImGui, MinHook, nlohmann/json) retain
their own licenses.
