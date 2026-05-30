// SPDX-License-Identifier: MIT
//
// Implements the exported dxgi entry points by forwarding to the genuine
// System32\dxgi.dll. The exported function names are declared via a .def file
// (see dxgi.def) so the linker produces ordinals matching the real dxgi.

#include "dxgi_proxy.h"
#include "../util/log.h"

#include <windows.h>
#include <mutex>

namespace cdmp {

namespace {
HMODULE g_realDxgi = nullptr;
std::once_flag g_initFlag;

// Function pointer table for the exports we forward.
using PFN_CreateDXGIFactory = HRESULT(WINAPI*)(REFIID, void**);
using PFN_CreateDXGIFactory1 = HRESULT(WINAPI*)(REFIID, void**);
using PFN_CreateDXGIFactory2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
using PFN_DXGIGetDebugInterface1 = HRESULT(WINAPI*)(UINT, REFIID, void**);
using PFN_DXGID3D10CreateDevice = HRESULT(WINAPI*)(HMODULE, void*, UINT, void*, void**);
using PFN_DXGIDeclareAdapterRemovalSupport = HRESULT(WINAPI*)();

PFN_CreateDXGIFactory pCreateDXGIFactory = nullptr;
PFN_CreateDXGIFactory1 pCreateDXGIFactory1 = nullptr;
PFN_CreateDXGIFactory2 pCreateDXGIFactory2 = nullptr;
PFN_DXGIGetDebugInterface1 pDXGIGetDebugInterface1 = nullptr;
PFN_DXGID3D10CreateDevice pDXGID3D10CreateDevice = nullptr;
PFN_DXGIDeclareAdapterRemovalSupport pDXGIDeclareAdapterRemovalSupport = nullptr;

template <typename T>
void Resolve(T& fn, const char* name) {
    fn = reinterpret_cast<T>(GetProcAddress(g_realDxgi, name));
}

void DoInit() {
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring path = std::wstring(sys) + L"\\dxgi.dll";
    g_realDxgi = LoadLibraryW(path.c_str());
    if (!g_realDxgi) {
        CDMP_LOG_ERROR("Failed to load genuine dxgi.dll from System32");
        return;
    }
    Resolve(pCreateDXGIFactory, "CreateDXGIFactory");
    Resolve(pCreateDXGIFactory1, "CreateDXGIFactory1");
    Resolve(pCreateDXGIFactory2, "CreateDXGIFactory2");
    Resolve(pDXGIGetDebugInterface1, "DXGIGetDebugInterface1");
    Resolve(pDXGID3D10CreateDevice, "DXGID3D10CreateDevice");
    Resolve(pDXGIDeclareAdapterRemovalSupport, "DXGIDeclareAdapterRemovalSupport");
    CDMP_LOG_INFO("Genuine dxgi.dll loaded and forwarders resolved");
}
} // namespace

bool InitDxgiProxy() {
    std::call_once(g_initFlag, DoInit);
    return g_realDxgi != nullptr;
}

void ShutdownDxgiProxy() {
    if (g_realDxgi) {
        FreeLibrary(g_realDxgi);
        g_realDxgi = nullptr;
    }
}

} // namespace cdmp

// ---------------------------------------------------------------------------
// Exported forwarders. These names are exported via dxgi.def.
// ---------------------------------------------------------------------------

extern "C" {

HRESULT WINAPI Proxy_CreateDXGIFactory(REFIID riid, void** ppFactory) {
    if (!cdmp::InitDxgiProxy() || !cdmp::pCreateDXGIFactory) return E_FAIL;
    return cdmp::pCreateDXGIFactory(riid, ppFactory);
}

HRESULT WINAPI Proxy_CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    if (!cdmp::InitDxgiProxy() || !cdmp::pCreateDXGIFactory1) return E_FAIL;
    return cdmp::pCreateDXGIFactory1(riid, ppFactory);
}

HRESULT WINAPI Proxy_CreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory) {
    if (!cdmp::InitDxgiProxy() || !cdmp::pCreateDXGIFactory2) return E_FAIL;
    return cdmp::pCreateDXGIFactory2(flags, riid, ppFactory);
}

HRESULT WINAPI Proxy_DXGIGetDebugInterface1(UINT flags, REFIID riid, void** pDebug) {
    if (!cdmp::InitDxgiProxy() || !cdmp::pDXGIGetDebugInterface1) return E_FAIL;
    return cdmp::pDXGIGetDebugInterface1(flags, riid, pDebug);
}

HRESULT WINAPI Proxy_DXGID3D10CreateDevice(HMODULE mod, void* a, UINT b, void* c, void** d) {
    if (!cdmp::InitDxgiProxy() || !cdmp::pDXGID3D10CreateDevice) return E_FAIL;
    return cdmp::pDXGID3D10CreateDevice(mod, a, b, c, d);
}

HRESULT WINAPI Proxy_DXGIDeclareAdapterRemovalSupport() {
    if (!cdmp::InitDxgiProxy() || !cdmp::pDXGIDeclareAdapterRemovalSupport)
        return E_FAIL;
    return cdmp::pDXGIDeclareAdapterRemovalSupport();
}

} // extern "C"
