// SPDX-License-Identifier: MIT
//
// Proxy for the system dxgi.dll. We export the same symbols Windows expects and
// forward every call to the genuine dxgi.dll loaded from System32. This lets us
// be dropped into the game folder and loaded ahead of the real one (the classic
// "DLL proxy" technique used by ReShade et al.), which is compatible with
// Denuvo because we never touch the protected executable.

#pragma once

namespace cdmp {

// Loads the genuine System32\dxgi.dll and resolves the forwarded exports.
// Returns true on success. Safe to call multiple times.
bool InitDxgiProxy();

// Frees the real dxgi handle on shutdown.
void ShutdownDxgiProxy();

} // namespace cdmp
