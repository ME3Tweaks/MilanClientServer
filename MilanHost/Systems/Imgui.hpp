/**
 * @file        MilanHost/Systems/Imgui.hpp
 * @header      System for Imgui integration.
 */

#pragma once

#include <tuple>
#include <LESDK/Init.hpp>
#include "Milan/Hooks.hpp"


namespace Milan::Imgui
{
    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Public interface.
    // ========================================

    void OpenInterface(wchar_t const* Reason);
    void CloseInterface(wchar_t const* Reason);

    bool OnWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    bool OnSwapChainPresent(IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags);

    std::pair<ID3D11Device*, HWND> GetRenderingGlobals();


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
    t_IDXGISwapChain_Present* FindSwapChainPresent();

    /**
     * @brief       Checks if a WndProc message should pass through to the game.
     * @return      If this function returns FALSE, a hook WndProc must prevent it from reaching the game's WndProc.
     */
    bool MessageImguiPassthrough(UINT uMsg, WPARAM, LPARAM);
}
