// SPDX-License-Identifier: MIT
//
// Tiny timestamped logger shared across the server.

type Level = "INFO" | "WARN" | "ERROR" | "DEBUG";

function ts(): string {
  return new Date().toISOString();
}

function emit(level: Level, args: unknown[]): void {
  const prefix = `[${ts()}] [${level}]`;
  if (level === "ERROR") {
    console.error(prefix, ...args);
  } else if (level === "WARN") {
    console.warn(prefix, ...args);
  } else {
    console.log(prefix, ...args);
  }
}

export const log = {
  info: (...args: unknown[]) => emit("INFO", args),
  warn: (...args: unknown[]) => emit("WARN", args),
  error: (...args: unknown[]) => emit("ERROR", args),
  debug: (...args: unknown[]) => {
    if (process.env.CD_DEBUG) emit("DEBUG", args);
  },
};
