// SPDX-License-Identifier: MIT
//
// Signature scanner with a persistent offset cache. The scanner resolves the
// BLACKSPACE engine memory layout heuristically and caches the result keyed by
// a hash of the game binary so subsequent launches take a fast path.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cdmp {

// Convert an IDA-style pattern "48 8B ?? 89" into bytes + a wildcard mask.
struct CompiledPattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask; // true = significant byte, false = wildcard
};

CompiledPattern CompilePattern(const std::string& ida);

// Scan a loaded module's executable region for the given pattern. Returns the
// absolute address of the first match, or 0 on failure.
uintptr_t ScanPattern(const char* moduleName, const CompiledPattern& pat);
uintptr_t ScanPattern(const char* moduleName, const std::string& ida);

// Hash of the first 1 MiB of the main game executable (hex string). Used as the
// cache key so the cache invalidates automatically when the game updates.
std::string GameBinaryHash();

// Resolve every offset in g_offsets. Tries the on-disk cache first; on a hash
// mismatch or missing cache it re-scans and rewrites the cache. Returns true if
// at least the core player layout was resolved.
bool ResolveOffsets();

// Persist the current g_offsets to %APPDATA%/CDMultiplayer/offsets_cache.json.
void SaveOffsetCache();

} // namespace cdmp
