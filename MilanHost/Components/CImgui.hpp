#ifdef WITH_IMGUI

/**
 * @file        MilanHost/Components/CImgui.hpp
 * @header      Component for ImGui instance integration.
 */

#pragma once

#include <tuple>

#include "Base/Base.hpp"
#include "Containers/vector.hpp"
#include "Engine/Hooks.hpp"


class CImgui final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"imgui";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    /** Executes some code when the engine's WndProc hook is triggered. */
    bool On_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    /** Executes some code when SwapChainPresent hook is triggered. */
    bool On_SwapChainPresent(IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags);

    /** Checks if any registered interface is rendering and intercepting window input. */
    bool IsCapturingInput() const;

    /** Retrieves a cached @c IDXGISwapChain::Present pointer. */
    t_IDXGISwapChain_Present* GetSwapChainPresent() const;

    /** Base interface for all user interactions using ImGui. */
    class IInterface : public NonCopyable
    {
    public:
        virtual ~IInterface() = default;

        /** Unique name which can be used to locate this instance. */
        virtual SFXName GetName() const = 0;
        /** Checks if this interface should be rendering. */
        virtual bool IsRendering() const = 0;
        /** Checks if this interface should be receiving window events. */
        virtual bool IsIntercepting() const = 0;

        /** Callback invoked in the WndProc hook. */
        virtual void OnWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) = 0;
        /** Callback invoked in the SwapChainPresent hook. */
        virtual void OnRender() = 0;
    };

    /** Type for passing ownership of an interface to this component. */
    using InterfacePtr_t = std::unique_ptr<IInterface>;

    /** Adds an ImGui-based user interaction to this component. */
    void RegisterInterface(InterfacePtr_t Interface);

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


    ID3D11Device*                   mp_d3d11Device{ nullptr };
    ID3D11DeviceContext*            mp_d3d11DeviceContext{ nullptr };
    ID3D11RenderTargetView*         mp_d3d11RenderTargetView{ nullptr };
    t_IDXGISwapChain_Present*       mp_dxgiSwapChainPresent{ nullptr };
    game::vector<InterfacePtr_t>    m_activeInterfaces{};
    HWND                            m_imguiWindow{};
};

static_assert(ComponentType<CImgui>);


// ! CImgui::IInterface implementations.
// ========================================

/** Draws ImGui's demo window, activated on DELETE release. */
class DemoInterface final : public CImgui::IInterface
{
public:

    virtual ~DemoInterface() override = default;

    virtual SFXName GetName() const override;
    virtual bool IsRendering() const override;
    virtual bool IsIntercepting() const override;

    virtual void OnWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    virtual void OnRender() override;

private:

    bool mb_active{ false };
};
#endif
