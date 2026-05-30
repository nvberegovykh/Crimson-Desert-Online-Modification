// SPDX-License-Identifier: MIT

#include "inventory_hook.h"
#include "../game/offsets.h"
#include "../game/memory.h"
#include "../ui/modal.h"
#include "../util/log.h"

#include <windows.h>
#include <MinHook.h>

#include <string>

namespace cdmp {

namespace {

// Item-use signature: void __fastcall UseItem(void* inventory, int slotIndex).
using UseItemFn = void(__fastcall*)(void* inventory, int slotIndex);
UseItemFn g_origUseItem = nullptr;

// Read the item id stored in the slot the player is using so we can intercept
// the beacon. The inventory array pointer is read fresh each call.
int32_t ReadSlotItemId(int slotIndex) {
    if (g_offsets.inventoryArrayPtr == 0 || g_offsets.itemStride == 0) return 0;
    const uintptr_t arrayBase = SafeRead<uintptr_t>(g_offsets.inventoryArrayPtr);
    if (arrayBase == 0) return 0;
    const uintptr_t slot = arrayBase + static_cast<uintptr_t>(slotIndex) *
                                           g_offsets.itemStride;
    return ReadInt(slot, g_offsets.offItemId);
}

void __fastcall HookedUseItem(void* inventory, int slotIndex) {
    if (ReadSlotItemId(slotIndex) == MULTIPLAYER_BEACON_ID) {
        // Beacon used -> open the modal, swallow the use.
        Modal::Get().Open();
        return;
    }
    if (g_origUseItem) g_origUseItem(inventory, slotIndex);
}

// A hidden message-only window pumps WM_HOTKEY for the F9 fallback.
HWND g_hotkeyWnd = nullptr;
constexpr int kHotkeyId = 0xCD;

LRESULT CALLBACK HotkeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_HOTKEY && wParam == kHotkeyId) {
        Modal::Get().Toggle();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

DWORD WINAPI HotkeyThread(LPVOID) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HotkeyWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CDMP_Hotkey";
    RegisterClassExW(&wc);
    g_hotkeyWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!g_hotkeyWnd) return 1;

    if (!RegisterHotKey(g_hotkeyWnd, kHotkeyId, 0, VK_F9)) {
        CDMP_LOG_WARN("RegisterHotKey(F9) failed");
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

} // namespace

InventoryHook& InventoryHook::Get() {
    static InventoryHook instance;
    return instance;
}

void InventoryHook::Init() {
    const bool invOk = g_offsets.inventoryResolved();
    const bool useHookOk = invOk && InstallUseHook();

    if (invOk) {
        AppendBeaconItem();
    } else {
        CDMP_LOG_WARN("Inventory not resolved; beacon append skipped");
    }

    // F9 fallback when either the inventory append or the use hook is missing.
    if (!invOk || !useHookOk) {
        fallback_ = true;
        RegisterFallbackHotkey();
        CDMP_LOG_INFO("F9 fallback enabled (press F9 to open multiplayer)");
    }
}

bool InventoryHook::InstallUseHook() {
    if (g_offsets.itemUseFn == 0) {
        CDMP_LOG_WARN("item-use function not resolved");
        return false;
    }
    if (MH_CreateHook(reinterpret_cast<void*>(g_offsets.itemUseFn),
                      reinterpret_cast<void*>(&HookedUseItem),
                      reinterpret_cast<void**>(&g_origUseItem)) != MH_OK) {
        CDMP_LOG_ERROR("MH_CreateHook(UseItem) failed");
        return false;
    }
    if (MH_EnableHook(reinterpret_cast<void*>(g_offsets.itemUseFn)) != MH_OK) {
        CDMP_LOG_ERROR("MH_EnableHook(UseItem) failed");
        return false;
    }
    CDMP_LOG_INFO("Item-use hook installed");
    return true;
}

bool InventoryHook::AppendBeaconItem() {
    if (beaconAppended_) return true;
    if (!g_offsets.inventoryResolved()) return false;

    const uintptr_t arrayBase = SafeRead<uintptr_t>(g_offsets.inventoryArrayPtr);
    if (arrayBase == 0) {
        CDMP_LOG_WARN("inventory array pointer null; cannot append beacon");
        return false;
    }

    const int32_t count = SafeRead<int32_t>(g_offsets.itemCountPtr);
    if (count < 0 || count > 2000) {
        CDMP_LOG_WARN("implausible item count; aborting beacon append");
        return false;
    }

    // APPEND: write at slot index == current count, never overwrite an existing
    // slot. Then bump the count to count+1.
    const uintptr_t slot =
        arrayBase + static_cast<uintptr_t>(count) * g_offsets.itemStride;
    if (!IsReadable(slot, g_offsets.itemStride)) {
        CDMP_LOG_WARN("append slot not writable; aborting");
        return false;
    }

    WriteInt(slot, g_offsets.offItemId, MULTIPLAYER_BEACON_ID);
    WriteInt(slot, g_offsets.offItemQuantity, 1);
    WriteInt(slot, g_offsets.offItemDurability, 9999);
    WriteInt(slot, g_offsets.offItemFlags, 0);
    WriteU32(slot, g_offsets.offItemNameHash, MULTIPLAYER_BEACON_NAME_HASH);

    WriteInt(g_offsets.itemCountPtr, 0, count + 1);

    beaconAppended_ = true;
    CDMP_LOG_INFO("Multiplayer Beacon appended at slot " + std::to_string(count));
    return true;
}

void InventoryHook::RegisterFallbackHotkey() {
    CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
}

void InventoryHook::Shutdown() {
    if (g_offsets.itemUseFn != 0) {
        MH_DisableHook(reinterpret_cast<void*>(g_offsets.itemUseFn));
    }
    if (g_hotkeyWnd) {
        UnregisterHotKey(g_hotkeyWnd, kHotkeyId);
        DestroyWindow(g_hotkeyWnd);
        g_hotkeyWnd = nullptr;
    }
}

} // namespace cdmp
