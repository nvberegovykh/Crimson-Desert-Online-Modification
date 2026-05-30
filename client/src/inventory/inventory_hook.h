// SPDX-License-Identifier: MIT
//
// Appends a synthetic "Multiplayer Beacon" item to the player inventory and
// hooks the item-use function so using the beacon opens the multiplayer modal.
// If the scanner could not resolve the inventory or use function, a global F9
// hotkey is registered as a fallback.

#pragma once

namespace cdmp {

class InventoryHook {
public:
    static InventoryHook& Get();

    // Resolve pointers, append the beacon item, and install the use hook.
    // Falls back to an F9 hotkey when scanning fails.
    void Init();
    void Shutdown();

    // Append the beacon at index == current item count (never overwrites).
    bool AppendBeaconItem();

    // Whether the F9 fallback path is active.
    bool UsingFallback() const { return fallback_; }

private:
    InventoryHook() = default;
    bool InstallUseHook();
    void RegisterFallbackHotkey();

    bool fallback_ = false;
    bool beaconAppended_ = false;
};

} // namespace cdmp
