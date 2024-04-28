#include <LESDK/Init.hpp>
#include "Systems/Panels.hpp"


namespace Milan::Panels
{
    bool gb_debugPanelEnabled = false;

    ID3D11Device* gp_d3d11Device = nullptr;
    ID3D11DeviceContext* gp_d3d11DeviceContext = nullptr;
    ID3D11RenderTargetView* gp_d3d11RenderTargetView = nullptr;

    tWndProc* WndProc_orig = nullptr;
    tSwapChainPresent* SwapChainPresent_orig = nullptr;


    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context)
    {
        // TODO: Use custom allocation functions.

        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

        tSwapChainPresent* SwapChainPresent_target = FindSwapChainPresent();
        SwapChainPresent_orig = (tSwapChainPresent*)Context.InstallHook("SwapChainPresent", SwapChainPresent_target, SwapChainPresent_hook);
        WndProc_orig = Context.ResolveTyped<tWndProc>(GLOBAL_WNDPROC_RVA);
    }

    void Terminate()
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    void OpenInterface(wchar_t const* const Reason)
    {
        std::fwprintf(stdout, L"opening interface, reason = %s\n", Reason ? Reason : L"???");
        gb_debugPanelEnabled = true;
    }

    void CloseInterface(wchar_t const* const Reason)
    {
        std::fwprintf(stdout, L"closing interface, reason = %s\n", Reason ? Reason : L"???");
        gb_debugPanelEnabled = false;
    }


    // ! Hooks to enable Imgui integration.
    // ========================================

    LRESULT CALLBACK WndProc_hook(HWND const hWnd, UINT const uMsg, WPARAM const wParam, LPARAM const lParam)
    {
        ImGuiIO& IO = ImGui::GetIO();
        POINT MousePosition{};

        GetCursorPos(&MousePosition);
        ScreenToClient(hWnd, &MousePosition);

        IO.MousePos.x = static_cast<float>(MousePosition.x);
        IO.MousePos.y = static_cast<float>(MousePosition.y);

        if (uMsg == WM_KEYUP && wParam == VK_DELETE && !gb_debugPanelEnabled)
            OpenInterface(L"keybind (vk_delete)");

        if (gb_debugPanelEnabled)
        {
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
            if (!MessageImguiPassthrough(uMsg, wParam, lParam))
                return true;
        }

        if (WndProc_orig == nullptr)
            return true;

        return (*WndProc_orig)(hWnd, uMsg, wParam, lParam);
    }

    HRESULT SwapChainPresent_hook(IDXGISwapChain* const pChain, UINT const SyncInterval, UINT const Flags)
    {
        static bool bFailedInternal = false;
        static bool bSwapChainInitialized = false;

        if (!bFailedInternal)
        {
            if (!bSwapChainInitialized)
            {
                if (FAILED(GetDeviceAndContext(pChain, &gp_d3d11Device, &gp_d3d11DeviceContext)))
                {
                    bFailedInternal = true;
                    std::fwprintf(stdout, L"GetDeviceAndContext failed\n");
                    return SwapChainPresent_orig(pChain, SyncInterval, Flags);
                }

                DXGI_SWAP_CHAIN_DESC SwapChainDescription{};
                pChain->GetDesc(&SwapChainDescription);

                HWND const CurrentWindow = SwapChainDescription.OutputWindow;
                SetWindowLongPtr(CurrentWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc_hook));


                ImGui_ImplWin32_Init(CurrentWindow);
                ImGui_ImplDX11_Init(gp_d3d11Device, gp_d3d11DeviceContext);

                ID3D11Texture2D* TextureBuffer = nullptr;
                pChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<LPVOID*>(&TextureBuffer));
                assert(TextureBuffer != nullptr && "failed to create temporary ID3D11Texture2D");
                gp_d3d11Device->CreateRenderTargetView(TextureBuffer, nullptr, &gp_d3d11RenderTargetView);
                TextureBuffer->Release();

                bSwapChainInitialized = true;
            }

            ImGui_ImplWin32_NewFrame();
            ImGui_ImplDX11_NewFrame();

            ImGui::NewFrame();

            if (gb_debugPanelEnabled)
            {
                bool bShouldRemain = true;
                ImGui::ShowDemoWindow(&bShouldRemain);

                if (!bShouldRemain)
                    CloseInterface(L"ui");
            }

            ImGui::EndFrame();
            ImGui::Render();

            gp_d3d11DeviceContext->OMSetRenderTargets(1, &gp_d3d11RenderTargetView, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        return SwapChainPresent_orig(pChain, SyncInterval, Flags);
    }


    // ! Utilities for DirectX / Imgui integration.
    // ========================================

    HRESULT GetDeviceAndContext(IDXGISwapChain* const pChain, ID3D11Device** ppDevice, ID3D11DeviceContext** ppContext)
    {
        HRESULT const Result = pChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(ppDevice));
        if (SUCCEEDED(Result)) (*ppDevice)->GetImmediateContext(ppContext);
        return Result;
    }

    tSwapChainPresent* FindSwapChainPresent()
    {
        DXGI_SWAP_CHAIN_DESC SCDesc{};
        std::memset(&SCDesc, 0, sizeof SCDesc);

        SCDesc.BufferCount = 2;
        SCDesc.BufferDesc.Width = 0;
        SCDesc.BufferDesc.Height = 0;
        SCDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        SCDesc.BufferDesc.RefreshRate.Numerator = 60u;
        SCDesc.BufferDesc.RefreshRate.Denominator = 0u;
        SCDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        SCDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        SCDesc.OutputWindow = GetForegroundWindow();
        SCDesc.SampleDesc.Count = 1;
        SCDesc.SampleDesc.Quality = 0;
        SCDesc.Windowed = TRUE;
        SCDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* pSwapChain = nullptr;

        D3D_FEATURE_LEVEL const LEVELS[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        HRESULT const Result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_DEBUG,
            LEVELS, 2, D3D11_SDK_VERSION, &SCDesc,
            &pSwapChain, nullptr, nullptr, nullptr);

        if (FAILED(Result))
        {
            std::fwprintf(stderr, L"D3D11CreateDeviceAndSwapChain failed: %d\n", Result);
            return nullptr;
        }

        void** pSwapChainVtable = *(void***)pSwapChain;
        pSwapChain->Release();

        return (tSwapChainPresent*)pSwapChainVtable[8];
    }

    bool MessageImguiPassthrough(UINT const uMsg, WPARAM const, LPARAM const)
    {
        // Cases directly from ImGui_ImplWin32_WndProcHandler.
        switch (uMsg)
        {
        case WM_MOUSEMOVE: case WM_NCMOUSEMOVE:
        case WM_MOUSELEAVE: case WM_NCMOUSELEAVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        case WM_KEYDOWN: case WM_KEYUP:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_SETFOCUS: case WM_KILLFOCUS:
        case WM_INPUTLANGCHANGE:
        case WM_CHAR:
        case WM_SETCURSOR:
        case WM_DEVICECHANGE:
            return false;
        };

        return true;
    }
}
