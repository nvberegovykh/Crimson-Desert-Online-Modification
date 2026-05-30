// SPDX-License-Identifier: MIT
//
// Admin HTTP API on (game port + 1). All mutating routes require the
// X-Admin-Token header to match config.adminToken.

import * as http from "http";
import { log } from "./logger";
import type { RoomManager } from "./manager";
import type { RuntimeConfig } from "./config";
import { PacketType, encode, type KickPayload, type ChatPayload } from "./protocol";

interface ParsedBody {
  [key: string]: unknown;
}

function sendJson(res: http.ServerResponse, code: number, body: unknown): void {
  const data = JSON.stringify(body);
  res.writeHead(code, {
    "Content-Type": "application/json",
    "Content-Length": Buffer.byteLength(data),
  });
  res.end(data);
}

function readBody(req: http.IncomingMessage): Promise<ParsedBody> {
  return new Promise((resolve) => {
    const chunks: Buffer[] = [];
    let size = 0;
    req.on("data", (c: Buffer) => {
      size += c.length;
      if (size > 1_000_000) {
        req.destroy();
        return;
      }
      chunks.push(c);
    });
    req.on("end", () => {
      if (chunks.length === 0) return resolve({});
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString("utf8")));
      } catch {
        resolve({});
      }
    });
    req.on("error", () => resolve({}));
  });
}

export function startAdminServer(
  manager: RoomManager,
  runtime: RuntimeConfig
): http.Server {
  const port = runtime.current.port + 1;

  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url ?? "/", `http://localhost:${port}`);
    const method = req.method ?? "GET";
    const path = url.pathname;

    const authed = () =>
      req.headers["x-admin-token"] === runtime.current.adminToken;

    try {
      // GET /status — no auth, basic health
      if (method === "GET" && path === "/status") {
        sendJson(res, 200, {
          ok: true,
          sessionName: runtime.current.sessionName,
          rooms: manager.listRooms().length,
          players: manager.totalPlayers(),
          uptime: process.uptime(),
        });
        return;
      }

      // GET /players — list all players across rooms (auth)
      if (method === "GET" && path === "/players") {
        if (!authed()) return sendJson(res, 401, { error: "unauthorized" });
        const out = manager.listRooms().flatMap((room) =>
          room.state.getPlayers().map((p) => ({
            id: p.id,
            name: p.name,
            room: room.inviteCode,
            isHost: p.isHost,
            health: p.health,
            ping: p.ping,
          }))
        );
        sendJson(res, 200, { players: out });
        return;
      }

      // POST /kick/:id (auth)
      if (method === "POST" && path.startsWith("/kick/")) {
        if (!authed()) return sendJson(res, 401, { error: "unauthorized" });
        const id = decodeURIComponent(path.slice("/kick/".length));
        const body = await readBody(req);
        const reason = typeof body.reason === "string" ? body.reason : "kicked by admin";
        // Notify the room first, then disconnect.
        const room = manager.getRoomForPlayer(id);
        if (room) {
          const payload: KickPayload = { playerId: id, reason };
          room.broadcast(encode(PacketType.KICK, payload));
        }
        const ok = manager.kickPlayer(id, reason);
        sendJson(res, ok ? 200 : 404, { ok });
        return;
      }

      // POST /config — patch runtime config (auth)
      if (method === "POST" && path === "/config") {
        if (!authed()) return sendJson(res, 401, { error: "unauthorized" });
        const body = await readBody(req);
        const updated = runtime.patch(body);
        log.info("Config updated via admin API");
        sendJson(res, 200, { config: updated });
        return;
      }

      // POST /message — broadcast a server message to all rooms (auth)
      if (method === "POST" && path === "/message") {
        if (!authed()) return sendJson(res, 401, { error: "unauthorized" });
        const body = await readBody(req);
        const text = typeof body.text === "string" ? body.text : "";
        if (!text) return sendJson(res, 400, { error: "text required" });
        const chat: ChatPayload = { playerId: "server", name: "SERVER", text };
        for (const room of manager.listRooms()) {
          room.broadcast(encode(PacketType.CHAT, chat));
        }
        sendJson(res, 200, { ok: true });
        return;
      }

      sendJson(res, 404, { error: "not found" });
    } catch (err) {
      log.error("admin error:", err);
      sendJson(res, 500, { error: "internal error" });
    }
  });

  server.listen(port, () => {
    log.info(`Admin HTTP API listening on http://0.0.0.0:${port}`);
  });

  return server;
}
