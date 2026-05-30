# Setup Guide

This walks through building and running both halves of the project from scratch.

## Prerequisites

| Component | Requirement |
| --- | --- |
| Server | Node.js 18+ and npm |
| Client | Windows 10/11 x64, Visual Studio 2022 (Desktop C++ workload) or Build Tools, CMake 3.20+ |
| Game | A legitimately owned copy of Crimson Desert (Steam App 3321460) |
| Network (remote play) | TCP port 7777 reachable on the host machine |

## 1. Clone

```bash
git clone https://github.com/nvberegovykh/Crimson-Desert-Online-Modification.git
cd Crimson-Desert-Online-Modification
```

## 2. Build & run the server

```bash
cd server
npm install
cp config.example.json config.json
```

Edit `config.json` as needed:

```json
{
  "port": 7777,
  "maxPlayers": 8,
  "password": "",
  "friendlyFire": false,
  "tickRate": 20,
  "sessionName": "My CD Server",
  "adminToken": "change-me-please"
}
```

Run it:

```bash
npm run dev            # ts-node + auto-reload
# or for production:
npm run build && npm start
```

You should see:

```
[..] [INFO] CD Multiplayer session server listening on ws://0.0.0.0:7777
[..] [INFO] Admin HTTP API listening on http://0.0.0.0:7778
```

Verify the admin API:

```bash
curl -s localhost:7778/status
```

## 3. Build the client `dxgi.dll`

```bash
cd client
cmake -B build -A x64
cmake --build build --config Release
```

CMake fetches ImGui 1.90.4, MinHook 1.3.3, and nlohmann/json 3.11.3 on the first
configure. The output binary is `client/build/Release/dxgi.dll`.

You can have CMake copy the DLL straight into your game folder:

```bash
cmake -B build -A x64 -DOUTPUT_DIR="C:/Path/To/CrimsonDesert"
cmake --build build --config Release
```

## 4. Install the DLL

Option A — automatic (run from the repo root):

```bat
Install_DLL.bat
```

It reads the install location from
`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 3321460`.

Option B — manual: copy `client/build/Release/dxgi.dll` into the folder that
contains the Crimson Desert executable.

## 5. Launch and connect

1. Start the server (`Launch_CDMultiplayer.bat` from the repo root will install
   server deps if needed and start it in a new window).
2. Launch Crimson Desert from Steam.
3. In-game, **use the Multiplayer Beacon** item, or press **F9**.
4. Host a session and share the 6-character invite code; guests enter it to join.

## 6. Logs & caches

- Client log: `%APPDATA%\CDMultiplayer\client.log`
- Offset cache: `%APPDATA%\CDMultiplayer\offsets_cache.json`
  (delete it to force a re-scan, e.g. after a game patch).

## Remote / internet play

For players outside your LAN, forward TCP `7777` on the host's router to the host
machine, and have guests enter the host's public IP in the *Server* field of the
Join screen. Always set a `password` in `config.json` for internet-exposed
servers.
