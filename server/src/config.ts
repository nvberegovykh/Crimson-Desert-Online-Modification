// SPDX-License-Identifier: MIT
//
// Configuration loading. Reads config.json from the server directory, falling
// back to config.example.json and then to hard defaults.

import * as fs from "fs";
import * as path from "path";
import { log } from "./logger";

export interface ServerConfig {
  port: number;
  maxPlayers: number;
  password: string;
  friendlyFire: boolean;
  tickRate: number;
  sessionName: string;
  adminToken: string;
}

const DEFAULTS: ServerConfig = {
  port: 7777,
  maxPlayers: 8,
  password: "",
  friendlyFire: false,
  tickRate: 20,
  sessionName: "My CD Server",
  adminToken: "changeme",
};

function coerce(raw: Partial<ServerConfig>): ServerConfig {
  const cfg: ServerConfig = { ...DEFAULTS };
  if (typeof raw.port === "number") cfg.port = raw.port;
  if (typeof raw.maxPlayers === "number")
    cfg.maxPlayers = Math.min(Math.max(1, raw.maxPlayers), 8);
  if (typeof raw.password === "string") cfg.password = raw.password;
  if (typeof raw.friendlyFire === "boolean") cfg.friendlyFire = raw.friendlyFire;
  if (typeof raw.tickRate === "number")
    cfg.tickRate = Math.min(Math.max(1, raw.tickRate), 60);
  if (typeof raw.sessionName === "string") cfg.sessionName = raw.sessionName;
  if (typeof raw.adminToken === "string") cfg.adminToken = raw.adminToken;
  return cfg;
}

export function loadConfig(dir = process.cwd()): ServerConfig {
  const candidates = [
    path.join(dir, "config.json"),
    path.join(dir, "config.example.json"),
  ];
  for (const file of candidates) {
    if (fs.existsSync(file)) {
      try {
        const raw = JSON.parse(fs.readFileSync(file, "utf8"));
        log.info(`Loaded config from ${file}`);
        return coerce(raw);
      } catch (err) {
        log.warn(`Failed to parse ${file}:`, err);
      }
    }
  }
  log.warn("No config file found, using defaults");
  return { ...DEFAULTS };
}

/** Mutable runtime config holder so admin /config can adjust live settings. */
export class RuntimeConfig {
  constructor(public current: ServerConfig) {}

  patch(patch: Partial<ServerConfig>): ServerConfig {
    this.current = coerce({ ...this.current, ...patch });
    return this.current;
  }
}
