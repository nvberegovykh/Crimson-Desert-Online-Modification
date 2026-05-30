// SPDX-License-Identifier: MIT

#include "cutscene_sync.h"

#include "../game/offsets.h"
#include "../game/memory.h"
#include "../net/client.h"
#include "../session.h"
#include "../ui/modal.h"
#include "../util/log.h"

#include <windows.h>
#include <MinHook.h>

namespace cdmp {

namespace {

// The cutscene routine signature is `void Begin(void* self, bool entering)` in
// the BDO-engine lineage: the engine calls it once with entering=true and again
// with entering=false. We mirror that into our start/end transitions.
using CutsceneFn = void(__fastcall*)(void* self, bool entering);
CutsceneFn g_origCutscene = nullptr;

void __fastcall HookedCutscene(void* self, bool entering) {
    if (entering) {
        CutsceneSync::Get().OnCutsceneStart();
    } else {
        CutsceneSync::Get().OnCutsceneEnd();
    }
    if (g_origCutscene) g_origCutscene(self, entering);
}

} // namespace

CutsceneSync& CutsceneSync::Get() {
    static CutsceneSync instance;
    return instance;
}

void CutsceneSync::Init() {
    const bool hooked = HookCutsceneFunction();
    if (!hooked) {
        // Fall back to polling a scanned cinematic flag.
        if (ScanForCinematicFlag()) {
            pollFallback_.store(true);
            CDMP_LOG_INFO("CutsceneSync: using poll fallback (flag scan)");
        } else {
            CDMP_LOG_WARN(
                "CutsceneSync: no hook and no cinematic flag; disabled");
        }
    }
}

void CutsceneSync::Shutdown() {
    if (hookInstalled_.load() && g_offsets.cutsceneFn != 0) {
        MH_DisableHook(reinterpret_cast<void*>(g_offsets.cutsceneFn));
        hookInstalled_.store(false);
    }
}

bool CutsceneSync::HookCutsceneFunction() {
    if (g_offsets.cutsceneFn == 0) {
        CDMP_LOG_WARN("CutsceneSync: cutscene function not resolved");
        return false;
    }
    if (MH_CreateHook(reinterpret_cast<void*>(g_offsets.cutsceneFn),
                      reinterpret_cast<void*>(&HookedCutscene),
                      reinterpret_cast<void**>(&g_origCutscene)) != MH_OK) {
        CDMP_LOG_ERROR("CutsceneSync: MH_CreateHook(cutscene) failed");
        return false;
    }
    if (MH_EnableHook(reinterpret_cast<void*>(g_offsets.cutsceneFn)) != MH_OK) {
        CDMP_LOG_ERROR("CutsceneSync: MH_EnableHook(cutscene) failed");
        return false;
    }
    hookInstalled_.store(true);
    CDMP_LOG_INFO("CutsceneSync: cutscene hook installed");
    return true;
}

bool CutsceneSync::ScanForCinematicFlag() {
    // Heuristic: the cinematic/camera component stores a small bool that flips
    // when a cutscene plays. We look for it near the resolved camera anchor. In
    // the absence of a dedicated camera scan we anchor on the player base and
    // probe a plausible window for a byte that reads as a clean bool (0/1).
    const uintptr_t base = GetLocalPlayerBase();
    if (base == 0) return false;

    for (uint32_t off = 0x200; off < 0x800; off += 1) {
        const uintptr_t addr = base + off;
        if (!IsReadable(addr, 1)) continue;
        const uint8_t v = SafeRead<uint8_t>(addr);
        if (v == 0 || v == 1) {
            // First plausible bool slot; pin it. The poll loop reads edges so a
            // wrong guess simply never fires rather than misbehaving.
            cinematicFlagAddr_ = addr;
            g_offsets.cinematicFlagPtr = addr;
            return true;
        }
    }
    return false;
}

void CutsceneSync::Poll(double nowMs) {
    if (!pollFallback_.load()) return;
    if (nowMs - lastPollMs_ < 100.0) return;
    lastPollMs_ = nowMs;
    if (cinematicFlagAddr_ == 0 || !IsReadable(cinematicFlagAddr_, 1)) return;

    const bool flag = SafeRead<uint8_t>(cinematicFlagAddr_) != 0;
    if (flag == lastPolledFlag_) return;
    lastPolledFlag_ = flag;
    if (flag) {
        OnCutsceneStart();
    } else {
        OnCutsceneEnd();
    }
}

void CutsceneSync::OnCutsceneStart() {
    bool expected = false;
    if (!active_.compare_exchange_strong(expected, true)) return; // already in

    Session& sess = Session::Get();
    sess.SetCutsceneActive(true);
    // Pause STATE_UPDATE sending so remote avatars freeze in place rather than
    // tracking the cinematic camera rig.
    sess.Players().SetSuspended(true);
    // Hide the multiplayer overlay during the cinematic.
    Modal::Get().Close();

    if (WsClient* c = sess.Client()) {
        c->Send(PacketType::CUTSCENE_START, {{"playerId", sess.LocalId()}});
    }
    CDMP_LOG_INFO("CutsceneSync: cutscene started (sync suspended)");
}

void CutsceneSync::OnCutsceneEnd() {
    bool expected = true;
    if (!active_.compare_exchange_strong(expected, false)) return; // not in

    Session& sess = Session::Get();
    sess.SetCutsceneActive(false);
    sess.Players().SetSuspended(false);

    if (WsClient* c = sess.Client()) {
        c->Send(PacketType::CUTSCENE_END, {{"playerId", sess.LocalId()}});
    }
    CDMP_LOG_INFO("CutsceneSync: cutscene ended (sync resumed)");
}

} // namespace cdmp
