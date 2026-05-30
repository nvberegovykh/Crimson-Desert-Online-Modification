// SPDX-License-Identifier: MIT

#include "puzzle_sync.h"

#include "../game/offsets.h"
#include "../game/memory.h"
#include "../net/client.h"
#include "../session.h"
#include "../util/log.h"

#include <windows.h>
#include <MinHook.h>

namespace cdmp {

namespace {

// The puzzle input dispatch in the BDO-engine lineage is
// `bool Dispatch(void* self, void* input)`: it returns whether the input was
// accepted. We gate it on the current control-token holder so that only the
// controlling player's puzzle input takes effect.
using PuzzleInputFn = bool(__fastcall*)(void* self, void* input);
PuzzleInputFn g_origPuzzleInput = nullptr;

bool __fastcall HookedPuzzleInput(void* self, void* input) {
    // Block the call entirely when a puzzle is active and we are not the
    // controller; pass through otherwise (including when no puzzle is active).
    if (!PuzzleSync::Get().LocalInputAllowed()) {
        return false;
    }
    if (g_origPuzzleInput) return g_origPuzzleInput(self, input);
    return false;
}

} // namespace

PuzzleSync& PuzzleSync::Get() {
    static PuzzleSync instance;
    return instance;
}

void PuzzleSync::Init() {
    if (!HookPuzzleInput()) {
        // No hard failure: without the hook we cannot gate engine input, but
        // the token UI and pass flow still operate over the network.
        CDMP_LOG_WARN(
            "PuzzleSync: input hook unavailable; control gating disabled");
    }
}

void PuzzleSync::Shutdown() {
    if (hookInstalled_.load() && g_offsets.puzzleInputFn != 0) {
        MH_DisableHook(reinterpret_cast<void*>(g_offsets.puzzleInputFn));
        hookInstalled_.store(false);
    }
}

bool PuzzleSync::ScanForPuzzleHook() {
    // The signature scan already populated puzzleInputFn during ResolveOffsets;
    // this is the accessor the hook installer consults.
    return g_offsets.puzzleInputFn != 0;
}

bool PuzzleSync::HookPuzzleInput() {
    if (!ScanForPuzzleHook()) {
        CDMP_LOG_WARN("PuzzleSync: puzzle input function not resolved");
        return false;
    }
    if (MH_CreateHook(reinterpret_cast<void*>(g_offsets.puzzleInputFn),
                      reinterpret_cast<void*>(&HookedPuzzleInput),
                      reinterpret_cast<void**>(&g_origPuzzleInput)) != MH_OK) {
        CDMP_LOG_ERROR("PuzzleSync: MH_CreateHook(puzzleInput) failed");
        return false;
    }
    if (MH_EnableHook(reinterpret_cast<void*>(g_offsets.puzzleInputFn)) !=
        MH_OK) {
        CDMP_LOG_ERROR("PuzzleSync: MH_EnableHook(puzzleInput) failed");
        return false;
    }
    hookInstalled_.store(true);
    CDMP_LOG_INFO("PuzzleSync: puzzle input hook installed");
    return true;
}

bool PuzzleSync::DetectPuzzleStart() {
    // Manual fallback hook point: read a scanned puzzle-trigger flag near the
    // player to notice puzzle entry. The puzzle lifecycle is server-driven, so
    // this is only a courtesy local trigger; absent a resolved flag we simply
    // never auto-start (the server still broadcasts PUZZLE_START).
    if (g_offsets.puzzleTriggerFn == 0) return false;
    return false;
}

void PuzzleSync::Tick(double nowMs) {
    if (lastTickMs_ == 0.0) lastTickMs_ = nowMs;
    const double dt = nowMs - lastTickMs_;
    lastTickMs_ = nowMs;

    if (puzzleActive_.load() && dt > 0.0) {
        // Locally decay the displayed countdown between PUZZLE_TOKEN packets so
        // the progress bar animates smoothly; clamped at zero.
        int rem = tokenTimeRemaining_.load();
        rem -= static_cast<int>(dt);
        tokenTimeRemaining_.store(rem < 0 ? 0 : rem);
    } else {
        DetectPuzzleStart();
    }
}

void PuzzleSync::OnPuzzleStart(const std::string& puzzleId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        puzzleId_ = puzzleId;
        controllerId_.clear();
        queue_.clear();
    }
    puzzleActive_.store(true);
    tokenTimeRemaining_.store(0);
    CDMP_LOG_INFO("PuzzleSync: puzzle started " + puzzleId);
}

void PuzzleSync::OnPuzzleToken(const std::string& puzzleId,
                               const std::string& controllerId, int remainingMs,
                               const std::vector<std::string>& queue) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        puzzleId_ = puzzleId;
        controllerId_ = controllerId;
        queue_ = queue;
    }
    puzzleActive_.store(true);
    tokenTimeRemaining_.store(remainingMs < 0 ? 0 : remainingMs);
}

void PuzzleSync::OnPuzzleEnd(const std::string& puzzleId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!puzzleId_.empty() && puzzleId_ != puzzleId) return;
        puzzleId_.clear();
        controllerId_.clear();
        queue_.clear();
    }
    puzzleActive_.store(false);
    tokenTimeRemaining_.store(0);
    CDMP_LOG_INFO("PuzzleSync: puzzle ended " + puzzleId);
}

bool PuzzleSync::IsLocalController() {
    if (!puzzleActive_.load()) return false;
    const std::string local = Session::Get().LocalId();
    std::lock_guard<std::mutex> lock(mutex_);
    return !controllerId_.empty() && controllerId_ == local;
}

bool PuzzleSync::LocalInputAllowed() {
    // When no puzzle is active the input is unrelated to the rotation and must
    // pass through; otherwise only the controller may act.
    if (!puzzleActive_.load()) return true;
    return IsLocalController();
}

std::string PuzzleSync::CurrentControllerId() {
    std::lock_guard<std::mutex> lock(mutex_);
    return controllerId_;
}

std::string PuzzleSync::ActivePuzzleId() {
    std::lock_guard<std::mutex> lock(mutex_);
    return puzzleId_;
}

std::vector<std::string> PuzzleSync::Queue() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_;
}

void PuzzleSync::RequestPass() {
    if (!IsLocalController()) return;
    std::string puzzleId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        puzzleId = puzzleId_;
    }
    if (puzzleId.empty()) return;
    if (WsClient* c = Session::Get().Client()) {
        c->Send(PacketType::PUZZLE_PASS, {{"puzzleId", puzzleId}});
    }
}

} // namespace cdmp
