// SPDX-License-Identifier: MIT
//
// Detects when the local player enters or leaves an in-engine cutscene and
// suspends outgoing state sync for the duration so remote avatars do not jitter
// to the cinematic camera rig. Detection is hook-driven with a polling
// fallback; if neither can be resolved the system silently does nothing.

#pragma once

#include <atomic>
#include <cstdint>

namespace cdmp {

class CutsceneSync {
public:
    static CutsceneSync& Get();

    // Resolve detection primitives and install hooks. Safe to call once after
    // the scanner has run. Never throws; logs and continues on any failure.
    void Init();
    void Shutdown();

    // Called periodically (every ~100ms) from the session tick as a fallback
    // when the function hook could not be installed: reads the cinematic flag
    // directly and synthesizes start/end transitions.
    void Poll(double nowMs);

    bool Active() const { return active_.load(); }

    // Transition handlers. Invoked by the hook trampoline or the poll fallback.
    void OnCutsceneStart();
    void OnCutsceneEnd();

private:
    CutsceneSync() = default;

    // Scan for a bool sitting next to the camera component (BLACKSPACE
    // heuristic). Populates cinematicFlagAddr_. Returns true on success.
    bool ScanForCinematicFlag();

    // Hook the engine cutscene start/end routine via MinHook. Returns true on
    // success; on failure the poll fallback covers detection.
    bool HookCutsceneFunction();

    std::atomic<bool> active_{false};
    std::atomic<bool> hookInstalled_{false};
    std::atomic<bool> pollFallback_{false};

    uintptr_t cinematicFlagAddr_ = 0;
    double lastPollMs_ = 0.0;
    bool lastPolledFlag_ = false;
};

} // namespace cdmp
