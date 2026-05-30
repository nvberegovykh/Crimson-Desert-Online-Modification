// SPDX-License-Identifier: MIT
//
// Entry point for the proxy dxgi.dll. On DLL_PROCESS_ATTACH we spawn an init
// thread (DllMain must not block or call most APIs). The init thread sets up
// the dxgi forwarders, runs the signature scanner, installs hooks, brings up
// the network client, and registers the inventory beacon.

#include <windows.h>

#include <MinHook.h>

#include "proxy/dxgi_proxy.h"
#include "game/scanner.h"
#include "game/offsets.h"
#include "hooks/render_hook.h"
#include "inventory/inventory_hook.h"
#include "sync/combat_sync.h"
#include "sync/cutscene_sync.h"
#include "session.h"
#include "util/log.h"

namespace {

HMODULE g_self = nullptr;
volatile bool g_initialized = false;

DWORD WINAPI InitThread(LPVOID) {
    cdmp::LogInit();
    CDMP_LOG_INFO("CD Multiplayer client starting");

    // 1) Load the genuine dxgi so the game's rendering works.
    cdmp::InitDxgiProxy();

    // 2) Initialize MinHook before any detours.
    if (MH_Initialize() != MH_OK) {
        CDMP_LOG_ERROR("MH_Initialize failed");
        return 1;
    }

    // 3) Resolve the engine memory layout (cache fast-path or fresh scan).
    //    Retry a few times since the local player may not be spawned at launch.
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (cdmp::ResolveOffsets()) break;
        Sleep(1000);
    }
    if (!cdmp::g_offsets.playerLayoutResolved()) {
        CDMP_LOG_WARN("Offsets unresolved after retries; sync limited until F9 use");
    }

    // 4) Bring up the session controller + network client.
    cdmp::Session::Get().Init();

    // 5) Install the ImGui overlay (Present hook).
    cdmp::InstallRenderHook();

    // 6) Inventory beacon + use hook (with F9 fallback).
    cdmp::InventoryHook::Get().Init();

    // 7) Combat hooks (host installs attack adjudication on JOIN_ACK; this
    //    pre-creates the hook objects when the function is known).
    cdmp::GetCombatSync().InstallHooks();

    // 8) Cutscene detection (function hook + poll fallback).
    cdmp::CutsceneSync::Get().Init();

    g_initialized = true;
    CDMP_LOG_INFO("CD Multiplayer client ready");
    return 0;
}

void Shutdown() {
    if (!g_initialized) return;
    cdmp::CutsceneSync::Get().Shutdown();
    cdmp::InventoryHook::Get().Shutdown();
    cdmp::RemoveRenderHook();
    cdmp::Session::Get().Shutdown();
    MH_Uninitialize();
    cdmp::ShutdownDxgiProxy();
    cdmp::LogShutdown();
    g_initialized = false;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_self = module;
            DisableThreadLibraryCalls(module);
            // Load the genuine dxgi early so forwarders resolve even if the
            // init thread is delayed.
            cdmp::InitDxgiProxy();
            CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
            break;
        case DLL_PROCESS_DETACH:
            if (reserved == nullptr) {
                // Dynamic unload (not process teardown): clean up.
                Shutdown();
            }
            break;
        default:
            break;
    }
    return TRUE;
}
