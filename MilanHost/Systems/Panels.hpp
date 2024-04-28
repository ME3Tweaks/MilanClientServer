/**
 * @file        MilanHost/Systems/Panels.hpp
 * @header      Declarations for Imgui integration.
 */

#pragma once

#include <d3d11.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>


namespace LESDK { class Initializer; }

// Copied from <backends/imgui_impl_win32.h> according to the instructions found there.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


#define GLOBAL_WNDPROC_RVA  ::LESDK::Address::FromOffset(0x99D2C0)


namespace Milan::Panels
{
    extern bool gb_debugPanelEnabled;

    extern ID3D11Device* gp_d3d11Device;
    extern ID3D11DeviceContext* gp_d3d11DeviceContext;
    extern ID3D11RenderTargetView* gp_d3d11RenderTargetView;

    using tWndProc = LRESULT __stdcall(HWND, UINT, WPARAM, LPARAM);
    extern tWndProc* WndProc_orig;

    using tSwapChainPresent = HRESULT __stdcall(IDXGISwapChain*, UINT, UINT);
    extern tSwapChainPresent* SwapChainPresent_orig;


    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();

    void OpenInterface(wchar_t const* Reason);
    void CloseInterface(wchar_t const* Reason);


    // ! Hooks to enable Imgui integration.
    // ========================================

    LRESULT __stdcall WndProc_hook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    HRESULT __stdcall SwapChainPresent_hook(IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags);


    // ! Utilities for DirectX / Imgui integration.
    // ========================================

    /**
     * @brief       Extracts associated DirectX device and device context from a swapchain.
     * @param[in]   pChain  Input swap chain, usually coming from a hooked @c IDXGISwapChain::Present.
     * @param[out]  ppDevice  Output device, must not be null.
     * @param[out]  ppContext  Output device context, must not be null.
     * @return      Return code of the @c IDXGISwapChain::GetDevice this function invokes.
     */
    HRESULT GetDeviceAndContext(IDXGISwapChain* pChain, ID3D11Device** ppDevice, ID3D11DeviceContext** ppContext);

    /**
     * @brief       Finds @c IDXGISwapChain::Present by constructing a fake swap chain and looking in its vtable.
     * @return      Function pointer to the IDXGISwapChain::Present, good enough to be hooked.
     */
    tSwapChainPresent* FindSwapChainPresent();

    /**
     * @brief       Checks if a WndProc message pass through to the game.
     * @return      If this function returns FALSE, a hook WndProc must prevent it from reaching the game's WndProc.
     */
    bool MessageImguiPassthrough(UINT uMsg, WPARAM, LPARAM);
}
