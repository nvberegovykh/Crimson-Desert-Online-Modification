// SPDX-License-Identifier: MIT
//
// Cooperative puzzle control rotation (client side). While a puzzle is active
// the server hands a single "control token" to one player at a time; only the
// holder's puzzle input is allowed through. We gate the engine's puzzle input
// handler on the current controller and surface token/queue state to the UI.
// Detection of a puzzle starting is hook-driven with a manual fallback; if the
// hook cannot be installed the gating is skipped and the UI still works.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace cdmp {

class PuzzleSync {
public:
    static PuzzleSync& Get();

    // Resolve detection primitives and install the input hook. Safe to call
    // once after the scanner has run. Never throws; logs and continues.
    void Init();
    void Shutdown();

    // Incoming server packets.
    void OnPuzzleStart(const std::string& puzzleId);
    void OnPuzzleToken(const std::string& puzzleId,
                       const std::string& controllerId, int remainingMs,
                       const std::vector<std::string>& queue);
    void OnPuzzleEnd(const std::string& puzzleId);

    // Called every frame from the session tick: decays the displayed countdown
    // and runs the manual start-detection fallback when the hook is absent.
    void Tick(double nowMs);

    // UI accessors (thread-safe snapshots).
    bool PuzzleActive() const { return puzzleActive_.load(); }
    bool IsLocalController();
    std::string CurrentControllerId();
    std::string ActivePuzzleId();
    int TokenTimeRemaining() const { return tokenTimeRemaining_.load(); }
    std::vector<std::string> Queue();

    // Ask the server to hand the token to the next player. No-op unless the
    // local player currently holds it.
    void RequestPass();

    // True when the engine puzzle-input hook should let this input through.
    // Consulted by the hook trampoline.
    bool LocalInputAllowed();

private:
    PuzzleSync() = default;

    bool ScanForPuzzleHook();
    bool HookPuzzleInput();

    // Manual fallback: probe a scanned puzzle-trigger flag to notice that the
    // local player walked into a puzzle even when no hook is installed.
    bool DetectPuzzleStart();

    std::atomic<bool> puzzleActive_{false};
    std::atomic<bool> hookInstalled_{false};
    std::atomic<int> tokenTimeRemaining_{0};

    std::mutex mutex_;
    std::string puzzleId_;
    std::string controllerId_;
    std::vector<std::string> queue_;
    double lastTickMs_ = 0.0;
};

} // namespace cdmp
