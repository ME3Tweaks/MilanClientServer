// TEMPORARY: Until LExSDKv2 is updated with a fix.
#define SDK_TARGET_LE3

// Include LESDK, with the SPI it ships.
#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

// TEMPORARY: Until LExSDKv2 pushes a fix.
#undef DWORD
#undef BYTE
#undef WORD
#undef FLOAT
#undef INT
#undef UINT

#undef INT8
#undef UINT8
#undef INT16
#undef UINT16
#undef INT32
#undef UINT32
#undef INT64
#undef UINT64

#undef CHAR
#undef WCHAR
#undef DOUBLE


// Include stuff for injecting ImGui rendering.
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>

// Include auto-generated ECSM bindings.
#include <milan_ecsm_ffi.h>

// Include host's own stuff.
#include "Entry.hpp"


SPI_PLUGINSIDE_SUPPORT(L"MilanHost", L"Project Milan Team", L"0.1.0", SPI_GAME_LE3, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_SEQATTACH;


void AttachPoint(ISharedProxyInterface* Interface);
void DetachPoint(ISharedProxyInterface* Interface);


SPI_IMPLEMENT_ATTACH
{
    ::LESDK::InitializeConsole();
    ::AttachPoint(InterfacePtr);
    return true;
}

SPI_IMPLEMENT_DETACH
{
    ::DetachPoint(InterfacePtr);
    ::LESDK::TerminateConsole();
    return true;
}


// Copied from <backends/imgui_impl_win32.h> according to the instructions found there.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


static HWND g_gameWindow;
static bool gb_swapChainPresentInitialized = false;
static bool gb_debugPanelEnabled = false;
static ID3D11Device* gp_d3d11Device = nullptr;
static ID3D11DeviceContext* gp_d3d11DeviceContext = nullptr;
static ID3D11RenderTargetView* gp_d3d11RenderTargetView = nullptr;


HRESULT GetDeviceAndContext(IDXGISwapChain* const pChain, ID3D11Device** ppDevice, ID3D11DeviceContext** ppContext)
{
    HRESULT const Result = pChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(ppDevice));
    if (SUCCEEDED(Result)) (*ppDevice)->GetImmediateContext(ppContext);
    return Result;
}


using tWndProc = LRESULT CALLBACK(HWND, UINT, WPARAM, LPARAM);
static tWndProc* WndProc_orig = nullptr;

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

LRESULT CALLBACK WndProc_hook(HWND const hWnd, UINT const uMsg, WPARAM const wParam, LPARAM const lParam)
{
    ImGuiIO& IO = ImGui::GetIO();
    POINT MousePosition{};

    GetCursorPos(&MousePosition);
    ScreenToClient(hWnd, &MousePosition);

    IO.MousePos.x = static_cast<float>(MousePosition.x);
    IO.MousePos.y = static_cast<float>(MousePosition.y);

    if (uMsg == WM_KEYUP && wParam == VK_DELETE && !gb_debugPanelEnabled)
        gb_debugPanelEnabled = true;

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

using tSwapChainPresent = HRESULT(IDXGISwapChain*, UINT, UINT);
static tSwapChainPresent* SwapChainPresent_orig = nullptr;

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

HRESULT SwapChainPresent_hook(IDXGISwapChain* const pChain, UINT const SyncInterval, UINT const Flags)
{
    static bool bFailedInternal = false;

    if (!bFailedInternal)
    {
        if (!gb_swapChainPresentInitialized)
        {
            if (FAILED(GetDeviceAndContext(pChain, &gp_d3d11Device, &gp_d3d11DeviceContext)))
            {
                bFailedInternal = true;
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

            gb_swapChainPresentInitialized = true;
        }

        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX11_NewFrame();

        ImGui::NewFrame();

        if (gb_debugPanelEnabled)
        {
            ImGui::ShowDemoWindow(&gb_debugPanelEnabled);
        }

        ImGui::EndFrame();
        ImGui::Render();

        gp_d3d11DeviceContext->OMSetRenderTargets(1, &gp_d3d11RenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return SwapChainPresent_orig(pChain, SyncInterval, Flags);
}


void AttachPoint(ISharedProxyInterface* const Interface)
{
    ::LESDK::Initializer Init{ Interface, "MilanHost" };

    // Initialize built-in globals.
    // ----------------------------------------

    GEngine = Init.ResolveTyped<UEngine*>(BUILTIN_GENGINE_RIP);
    GMalloc = Init.ResolveTyped<FMallocLike*>(BUILTIN_GMALLOC_RIP);
    GNatives = Init.ResolveTyped<tNative*>(BUILTIN_GNATIVES_RIP);
    GSys = Init.ResolveTyped<USystem*>(BUILTIN_GSYS_RIP);
    GWorld = Init.ResolveTyped<UWorld*>(BUILTIN_GWORLD_RIP);

    UObject::GObjObjects = Init.ResolveTyped<TArray<UObject*>>(BUILTIN_GOBOBJECTS_RIP);
    SFXName::GBioNamePools = Init.ResolveTyped<SFXNameEntry const*>(BUILTIN_SFXNAMEPOOLS_RIP);
    SFXName::GInitMethod = Init.ResolveTyped<SFXName::tInitMethod>(BUILTIN_SFXNAMEINIT_PHOOK);


    // Initialize DirectX 11 hooks and ImGui.
    // ----------------------------------------

    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

    tSwapChainPresent* SwapChainPresent_target = FindSwapChainPresent();
    SwapChainPresent_orig = (tSwapChainPresent*)Init.InstallHook("SwapChainPresent", SwapChainPresent_target, SwapChainPresent_hook);

    WndProc_orig = Init.ResolveTyped<tWndProc>(::LESDK::Address::FromOffset(0x99D2C0));


    // This is linked to ecsm ffi.
    ::say_hello("stranger");
}

void DetachPoint(ISharedProxyInterface* const Interface)
{
    (void)Interface;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
