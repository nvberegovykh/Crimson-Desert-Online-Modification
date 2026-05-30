// SPDX-License-Identifier: MIT
//
// Hooks IDXGISwapChain::Present (vtable index 8) to drive the ImGui overlay.

#pragma once

namespace cdmp {

// Creates a temporary device + swapchain to read the Present vtable, then
// installs a MinHook detour. Returns true on success.
bool InstallRenderHook();
void RemoveRenderHook();

} // namespace cdmp
