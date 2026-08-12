#ifdef WITH_IMGUI
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

#include "Components/CImgui.hpp"


// Copied from <backends/imgui_impl_win32.h> according to the instructions found there.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


namespace
{

void* MyImGuiAlloc(size_t const sz, void*)
{
    assert(GMalloc != nullptr);
    return (*GMalloc)->Malloc(static_cast<DWORD>(sz), UN_DEFAULT_ALIGNMENT);
}

void MyImGuiFree(void* const ptr, void*)
{
    assert(GMalloc != nullptr);
    return (*GMalloc)->Free(ptr);
}

}


// ! CImgui implementation.
// ========================================

bool CImgui::Initialize(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);

    ImGui::SetAllocatorFunctions(MyImGuiAlloc, MyImGuiFree);
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

    mp_dxgiSwapChainPresent = FindSwapChainPresent();
    MILAN_CHECKW(mp_dxgiSwapChainPresent != nullptr, L"couldn't find IDXGISwapChain::Present");

    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CImgui::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);

    if (m_activeWindow)
    {
        // Undo system hook to avoid WndProc_hook calling On_WndProc after CImgui has terminated.
        SetWindowLongPtr(m_activeWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc_orig));

        // Same with IDXGISwapChain::Present.
        Context.UninstallHook("IDXGISwapChain::Present");

        // Both of these led to crashes when called after CImgui::Terminate().
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

bool CImgui::On_WndProc(HWND const hWnd, UINT const uMsg, WPARAM const wParam, LPARAM const lParam)
{
    m_activeWindow = hWnd;

    static bool sb_skipOwnCode = false;
    if (sb_skipOwnCode)
    {
        return false;
    }
    else if (uMsg == WM_DESTROY)
    {
        sb_skipOwnCode = true;
        ::SingleDetachImpl();
        return false;
    }

    ImGuiIO&    IO = ImGui::GetIO();
    POINT       MousePosition{};

    GetCursorPos(&MousePosition);
    ScreenToClient(hWnd, &MousePosition);

    IO.MousePos.x = static_cast<float>(MousePosition.x);
    IO.MousePos.y = static_cast<float>(MousePosition.y);

    bool bInterfaceIntercepting = false;
    for (InterfacePtr_t& Interface : m_activeInterfaces)
    {
        Interface->OnWndProc(hWnd, uMsg, wParam, lParam);
        if (Interface->IsRendering())
        {
            if (Interface->IsIntercepting())
                bInterfaceIntercepting = true;
        }
    }

    if (bInterfaceIntercepting)
    {
        ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
        if (!MessageImguiPassthrough(uMsg, wParam, lParam))
            return true;
    }

    return false;
}

bool CImgui::On_SwapChainPresent(IDXGISwapChain* const pChain, UINT const SyncInterval, UINT const Flags)
{
    MILAN_UNUSED_2(SyncInterval, Flags);

    static bool bFailedInternal = false;
    static bool bSwapChainInitialized = false;

    if (!bFailedInternal)
    {
        if (!bSwapChainInitialized)
        {
            if (FAILED(GetDeviceAndContext(pChain, &mp_d3d11Device, &mp_d3d11DeviceContext)))
            {
                bFailedInternal = true;
                MILAN_ERRORW(ELOG_Startup, L"GetDeviceAndContext failed");
                return false;
            }

            DXGI_SWAP_CHAIN_DESC SwapChainDescription{};
            pChain->GetDesc(&SwapChainDescription);

            HWND const CurrentWindow = SwapChainDescription.OutputWindow;
            SetWindowLongPtr(CurrentWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc_hook));


            ImGui_ImplWin32_Init(CurrentWindow);
            ImGui_ImplDX11_Init(mp_d3d11Device, mp_d3d11DeviceContext);

            ID3D11Texture2D* TextureBuffer = nullptr;
            pChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<LPVOID*>(&TextureBuffer));
            assert(TextureBuffer != nullptr && "failed to create temporary ID3D11Texture2D");
            mp_d3d11Device->CreateRenderTargetView(TextureBuffer, nullptr, &mp_d3d11RenderTargetView);
            TextureBuffer->Release();

            bSwapChainInitialized = true;
        }

        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX11_NewFrame();

        ImGui::NewFrame();

        for (InterfacePtr_t& Interface : m_activeInterfaces)
        {
            if (Interface->IsRendering())
                Interface->OnRender();
        }

        ImGui::EndFrame();
        ImGui::Render();

        mp_d3d11DeviceContext->OMSetRenderTargets(1, &mp_d3d11RenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return false;
}

t_IDXGISwapChain_Present* CImgui::GetSwapChainPresent() const
{
    MILAN_CHECKW(mp_dxgiSwapChainPresent != nullptr, L"");
    return mp_dxgiSwapChainPresent;
}

HRESULT CImgui::GetDeviceAndContext(IDXGISwapChain* const pChain, ID3D11Device** const ppDevice, ID3D11DeviceContext** const ppContext)
{
    HRESULT const Result = pChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(ppDevice));
    if (SUCCEEDED(Result)) (*ppDevice)->GetImmediateContext(ppContext);
    return Result;
}

void CImgui::RegisterInterface(InterfacePtr_t Interface)
{
    MILAN_DEBUGW(ELOG_Startup, L"registering imgui-based interface '%S'", Interface->GetName().GetName());
    m_activeInterfaces.emplace_back(std::move(Interface));
}

t_IDXGISwapChain_Present* CImgui::FindSwapChainPresent()
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

    D3D_FEATURE_LEVEL const Levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT const Result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_DEBUG,
        Levels, sizeof(Levels) / sizeof(*Levels), D3D11_SDK_VERSION, &SCDesc,
        &pSwapChain, nullptr, nullptr, nullptr);

    if (FAILED(Result))  [[unlikely]]
    {
        MILAN_ERRORW(L"D3D11CreateDeviceAndSwapChain failed: %d", Result);
        return nullptr;
    }

    void** pSwapChainVtable = *(void***)pSwapChain;
    pSwapChain->Release();

    return (t_IDXGISwapChain_Present*)pSwapChainVtable[8];
}

bool CImgui::MessageImguiPassthrough(UINT const uMsg, WPARAM, LPARAM)
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


// ! DemoInterface implementation.
// ========================================

void DemoInterface::OnWndProc(HWND const hWnd, UINT const uMsg, WPARAM const wParam, LPARAM const lParam)
{
    MILAN_UNUSED_4(hWnd, uMsg, wParam, lParam);
    if (uMsg == WM_KEYUP && wParam == VK_DELETE && !mb_active)
        mb_active = !mb_active;
}

void DemoInterface::OnRender()
{
    // There must've been a reason I had a local variable for this here,
    // so if anything starts to behave weird just add a transient local boolean.
    ImGui::ShowDemoWindow(&mb_active);
}
#endif
