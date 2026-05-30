// SPDX-License-Identifier: MIT
//
// Lightweight logger that writes to %APPDATA%/CDMultiplayer/client.log and
// (in debug builds) to the debugger via OutputDebugString.

#pragma once

#include <string>

namespace cdmp {

void LogInit();
void LogShutdown();
void LogLine(const char* level, const std::string& msg);

// Path to the per-user config/cache directory; created if missing.
std::wstring GetAppDataDir();

} // namespace cdmp

#define CDMP_LOG_INFO(msg) ::cdmp::LogLine("INFO", (msg))
#define CDMP_LOG_WARN(msg) ::cdmp::LogLine("WARN", (msg))
#define CDMP_LOG_ERROR(msg) ::cdmp::LogLine("ERROR", (msg))
