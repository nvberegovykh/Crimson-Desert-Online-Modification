// SPDX-License-Identifier: MIT

#include "render_hook.h"
#include "../ui/modal.h"
#include "../session.h"
#include "../util/log.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM,
                                                             LPARAM);

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace cdmp {

namespace {

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
PresentFn g_origPresent = nullptr;

bool g_imguiInit = false;
HWND g_hwnd = nullptr;
WNDPROC g_origWndProc = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

void CreateRenderTarget(IDXGISwapChain* swap) {
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) && backBuffer) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
        backBuffer->Release();
    }
}

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // F9 toggles the modal regardless of game focus.
    if (msg == WM_KEYDOWN && wParam == VK_F9) {
        Modal::Get().Toggle();
        return 0;
    }
    if (Modal::Get().IsOpen()) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
        // Swallow input while the modal is open so the game does not react.
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN ||
            msg == WM_RBUTTONUP || msg == WM_MOUSEMOVE || msg == WM_KEYDOWN ||
            msg == WM_KEYUP || msg == WM_CHAR || msg == WM_MOUSEWHEEL) {
            return 0;
        }
    }
    return CallWindowProc(g_origWndProc, hwnd, msg, wParam, lParam);
}

void InitImGui(IDXGISwapChain* swap) {
    if (g_imguiInit) return;

    if (FAILED(swap->GetDevice(IID_PPV_ARGS(&g_device)))) {
        CDMP_LOG_ERROR("Present hook: GetDevice failed");
        return;
    }
    g_device->GetImmediateContext(&g_context);

    DXGI_SWAP_CHAIN_DESC desc{};
    swap->GetDesc(&desc);
    g_hwnd = desc.OutputWindow;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    CreateRenderTarget(swap);

    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(g_hwnd, GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(HookedWndProc)));

    Modal::Get().ApplyTheme();
    g_imguiInit = true;
    CDMP_LOG_INFO("ImGui initialized via Present hook");
}

HRESULT __stdcall HookedPresent(IDXGISwapChain* swap, UINT sync, UINT flags) {
    InitImGui(swap);

    if (g_imguiInit) {
        // Pump session sync each frame.
        Session::Get().Tick();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        Modal::Get().Render();

        ImGui::Render();
        if (g_rtv) {
            g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        }
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return g_origPresent(swap, sync, flags);
}

// Build a dummy swapchain to read the real Present pointer from the vtable.
bool AcquirePresentPointer(void**& vtableOut) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CDMP_Dummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0,
                                100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swap = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_0};

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fl, 1, D3D11_SDK_VERSION,
        &scd, &swap, &dev, nullptr, &ctx);

    bool ok = false;
    if (SUCCEEDED(hr) && swap) {
        vtableOut = *reinterpret_cast<void***>(swap);
        ok = true;
    }
    if (swap) swap->Release();
    if (dev) dev->Release();
    if (ctx) ctx->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

} // namespace

bool InstallRenderHook() {
    void** vtable = nullptr;
    if (!AcquirePresentPointer(vtable) || !vtable) {
        CDMP_LOG_ERROR("Failed to acquire IDXGISwapChain vtable");
        return false;
    }
    // Present is vtable index 8.
    void* presentTarget = vtable[8];
    if (MH_CreateHook(presentTarget, reinterpret_cast<void*>(&HookedPresent),
                      reinterpret_cast<void**>(&g_origPresent)) != MH_OK) {
        CDMP_LOG_ERROR("MH_CreateHook(Present) failed");
        return false;
    }
    if (MH_EnableHook(presentTarget) != MH_OK) {
        CDMP_LOG_ERROR("MH_EnableHook(Present) failed");
        return false;
    }
    CDMP_LOG_INFO("Present hook installed (vtable[8])");
    return true;
}

void RemoveRenderHook() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
    if (g_imguiInit) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imguiInit = false;
    }
}

} // namespace cdmp
