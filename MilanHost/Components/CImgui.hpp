/**
 * @file        MilanHost/Components/CImgui.hpp
 * @header      Component for ImGui instance integration.
 */

#pragma once

#include <tuple>
#include "Milan/Base.hpp"
#include "Milan/Hooks.hpp"


class CImgui final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"imgui";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    /** Executes some code when WndProc hook is triggered. */
    bool On_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    /** Executes some code when SwapChainPresent hook is triggered. */
    bool On_SwapChainPresent(IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags);

    /** Retrieves a cached @c IDXGISwapChain::Present pointer. */
    t_IDXGISwapChain_Present* GetSwapChainPresent() const;

private:

    /**
     * @brief       Finds @c IDXGISwapChain::Present by constructing a fake swap chain and looking in its vtable.
     * @return      Function pointer to the IDXGISwapChain::Present, good enough to be hooked.
     */
    static t_IDXGISwapChain_Present* FindSwapChainPresent();

    /**
     * @brief       Extracts associated DirectX device and device context from a swapchain.
     * @param[in]   pChain  Input swap chain, usually coming from a hooked @c IDXGISwapChain::Present.
     * @param[out]  ppDevice  Output device, must not be null.
     * @param[out]  ppContext  Output device context, must not be null.
     * @return      Return code of the @c IDXGISwapChain::GetDevice this function invokes.
     */
    static HRESULT GetDeviceAndContext(IDXGISwapChain* pChain, ID3D11Device** ppDevice, ID3D11DeviceContext** ppContext);

    /**
     * @brief       Checks if a WndProc message should pass through to the game.
     * @return      If this function returns FALSE, a hook WndProc must prevent it from reaching the game's WndProc.
     */
    static bool MessageImguiPassthrough(UINT uMsg, WPARAM, LPARAM);


    ID3D11Device*               mp_d3d11Device{ nullptr };
    ID3D11DeviceContext*        mp_d3d11DeviceContext{ nullptr };
    ID3D11RenderTargetView*     mp_d3d11RenderTargetView{ nullptr };
    t_IDXGISwapChain_Present*   mp_dxgiSwapChainPresent{ nullptr };
    bool                        mb_debugPanelEnabled{ false };
    HWND                        m_activeWindow{};
};
