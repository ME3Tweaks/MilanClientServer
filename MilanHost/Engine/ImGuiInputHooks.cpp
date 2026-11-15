#ifdef WITH_IMGUI
#include <cstring>

#include <xinput.h>

#include "Base/Base.hpp"
#include "Components/CImgui.hpp"
#include "Engine/ImGuiInputHooks.hpp"


namespace ImGuiInputHooks
{
namespace
{

    // ! FWindowsViewport cursor getters (WinDrv).
    // ----------------------------------------

    // They sample the OS cursor with GetCursorPos + ScreenToClient on every call, so the game's
    // UI can follow the cursor without ever receiving a WM_MOUSEMOVE.
    constexpr std::size_t   k_getMousePositionRva = 0x997D40;   // void GetMousePosition(POINT*)
    constexpr std::size_t   k_getMouseXRva        = 0x997D90;   // INT  GetMouseX()
    constexpr std::size_t   k_getMouseYRva        = 0x997DC0;   // INT  GetMouseY()

    using t_GetMousePosition = void(void*, POINT*);
    using t_GetMouseAxis     = int(void*);

    t_GetMousePosition*     g_getMousePositionOriginal{};
    t_GetMouseAxis*         g_getMouseXOriginal{};
    t_GetMouseAxis*         g_getMouseYOriginal{};

    int  g_mouseX{};
    int  g_mouseY{};
    bool g_mouseXValid{};
    bool g_mouseYValid{};


    // ! DirectInput gamepad polling (WinDrv).
    // ----------------------------------------

    // WinDrv keeps its DirectInput devices in a TArray of 0x1F4-byte pad records; a record starts
    // with its IDirectInputDevice8 pointer.
    constexpr std::size_t   k_padArrayRva   = 0x1CB9380;
    constexpr std::size_t   k_padCountRva   = 0x1CB9388;
    constexpr std::size_t   k_padRecordSize = 0x1F4;

    // IDirectInputDevice8::GetDeviceState, vtable byte offset 0x48.
    constexpr std::size_t   k_getDeviceStateVtableIndex = 9;

    // The engine requests DIJOYSTATE2 (0x110 bytes): 6 axes, 2 sliders, then 4 POV hat switches
    // at offset 0x20. WinDrv normalizes axes as (raw - 32768) / 32768, so a neutral axis is
    // 0x8000, not zero; a zeroed POV would read as "up", so it needs 0xFFFFFFFF.
    constexpr std::size_t   k_joystickStateMinSize  = 0x30;
    constexpr std::size_t   k_joystickAxisCount     = 8;      // lX..lRz + 2 sliders
    constexpr unsigned long k_joystickAxisCenter    = 0x8000;
    constexpr std::size_t   k_joystickPovOffset     = 0x20;
    constexpr std::size_t   k_joystickPovCount      = 4;
    constexpr unsigned long k_povCentered           = 0xFFFFFFFF;

    using t_GetDeviceState = long(__stdcall)(void*, unsigned long, void*);

    t_GetDeviceState*       g_getDeviceStateOriginal{};
    bool                    g_gamepadHookAttempted{};
    unsigned                g_gamepadHookCooldown{};

    ::LESDK::Initializer*   g_context{};
    unsigned char*          g_moduleBase{};

    // Cleared before unhooking so a detour still running on another thread does not touch the
    // component after it has been terminated.
    bool g_active{ true };


    // ! XInput polling.
    // ----------------------------------------

    // The game reads the controller through XInput (the DirectInput enumeration does not produce a
    // device for XInput pads). ImGui's own platform backend polls the same export while the frame
    // is being built, so those calls are let through via ScopedImGuiFrame and everything else is
    // neutralized while an interface is capturing input.
    using t_XInputGetState = unsigned long(__stdcall)(unsigned long, XINPUT_STATE*);

    t_XInputGetState* g_xinputGetStateOriginal{};
    thread_local bool g_insideImGuiFrame{};


    // ! Helpers.
    // ========================================

    bool IsCapturingInput()
    {
        return g_active && ::gp_componentContainer != nullptr && COMPONENT(CImgui).IsCapturingInput();
    }

    template<typename T>
    bool InstallDeferredHook(::LESDK::Initializer& Context, char const* const Name, void* const Target, T*& Original, T* const Detour)
    {
        // Publish the original before enabling: the game may be executing the target right now.
        Original = reinterpret_cast<T*>(Context.InstallHookDeferred(Name, Target, reinterpret_cast<void*>(Detour)));
        if (Original == nullptr) [[unlikely]] return false;

        Context.EnableHook(Name);
        return true;
    }

    int FilterMouseX(int const Actual)
    {
        if (g_mouseXValid && IsCapturingInput())
        {
            return g_mouseX;
        }

        g_mouseX = Actual;
        g_mouseXValid = true;

        return Actual;
    }

    int FilterMouseY(int const Actual)
    {
        if (g_mouseYValid && IsCapturingInput())
        {
            return g_mouseY;
        }

        g_mouseY = Actual;
        g_mouseYValid = true;

        return Actual;
    }


    // ! Detours.
    // ========================================

    void GetMousePosition_Hook(void* const Context, POINT* const OutPosition)
    {
        (*g_getMousePositionOriginal)(Context, OutPosition);

        if (OutPosition != nullptr)
        {
            if (IsCapturingInput())
            {
                if (g_mouseXValid)
                {
                    OutPosition->x = g_mouseX;
                }
                else
                {
                    g_mouseX = OutPosition->x;
                    g_mouseXValid = true;
                }

                if (g_mouseYValid)
                {
                    OutPosition->y = g_mouseY;
                }
                else
                {
                    g_mouseY = OutPosition->y;
                    g_mouseYValid = true;
                }

                return;
            }

            g_mouseX = OutPosition->x;
            g_mouseY = OutPosition->y;
            g_mouseXValid = true;
            g_mouseYValid = true;
        }
    }

    int GetMouseX_Hook(void* const Context)
    {
        return FilterMouseX((*g_getMouseXOriginal)(Context));
    }

    int GetMouseY_Hook(void* const Context)
    {
        return FilterMouseY((*g_getMouseYOriginal)(Context));
    }

    long __stdcall GetDeviceState_Hook(void* const Self, unsigned long const StateSize, void* const State)
    {
        long const Result = (*g_getDeviceStateOriginal)(Self, StateSize, State);

        if (Result >= 0 && State != nullptr && StateSize > 0 && IsCapturingInput())
        {
            // Mouse/keyboard states are neutral when zeroed (relative deltas and no keys).
            std::memset(State, 0, StateSize);

            if (StateSize >= k_joystickStateMinSize)
            {
                // Joystick axes are absolute, keep them centered.
                auto* const Axes = static_cast<unsigned long*>(State);
                for (std::size_t i = 0; i < k_joystickAxisCount; ++i)
                {
                    Axes[i] = k_joystickAxisCenter;
                }

                auto* const Pov = reinterpret_cast<unsigned long*>(static_cast<unsigned char*>(State) + k_joystickPovOffset);
                for (std::size_t i = 0; i < k_joystickPovCount; ++i)
                {
                    Pov[i] = k_povCentered;
                }
            }
        }

        return Result;
    }

    unsigned long __stdcall XInputGetState_Hook(unsigned long const UserIndex, XINPUT_STATE* const State)
    {
        unsigned long const Result = (*g_xinputGetStateOriginal)(UserIndex, State);

        if (!g_insideImGuiFrame && Result == ERROR_SUCCESS && State != nullptr && IsCapturingInput())
        {
            std::memset(&State->Gamepad, 0, sizeof State->Gamepad);
        }

        return Result;
    }

}


// ! Public interface.
// ========================================

ScopedImGuiFrame::ScopedImGuiFrame()
{
    g_insideImGuiFrame = true;
}

ScopedImGuiFrame::~ScopedImGuiFrame()
{
    g_insideImGuiFrame = false;
}

void Initialize(::LESDK::Initializer& Context)
{
    g_context = &Context;
    g_moduleBase = static_cast<unsigned char*>(Context.ModuleBase);

    bool const bCursorInstalled =
        InstallDeferredHook(Context, "FViewport::GetMousePosition",
            g_moduleBase + k_getMousePositionRva, g_getMousePositionOriginal, &GetMousePosition_Hook) &&
        InstallDeferredHook(Context, "FViewport::GetMouseX",
            g_moduleBase + k_getMouseXRva, g_getMouseXOriginal, &GetMouseX_Hook) &&
        InstallDeferredHook(Context, "FViewport::GetMouseY",
            g_moduleBase + k_getMouseYRva, g_getMouseYOriginal, &GetMouseY_Hook);

    if (bCursorInstalled)
    {
        MILAN_DEBUGW(ELOG_Startup, L"cursor sampling hooks installed");
    }
    else
    {
        MILAN_ERRORW(ELOG_Startup, L"failed to install cursor sampling hooks");
    }

    HMODULE XInput = ::GetModuleHandleW(L"xinput1_4.dll");
    if (XInput == nullptr)
    {
        XInput = ::LoadLibraryW(L"xinput1_4.dll");
    }

    if (XInput != nullptr)
    {
        auto* const GetState = reinterpret_cast<t_XInputGetState*>(::GetProcAddress(XInput, "XInputGetState"));
        if (GetState != nullptr &&
            InstallDeferredHook(Context, "XInputGetState", reinterpret_cast<void*>(GetState),
                g_xinputGetStateOriginal, &XInputGetState_Hook))
        {
            MILAN_DEBUGW(ELOG_Startup, L"XInput gamepad hook installed");
        }
        else
        {
            MILAN_ERRORW(ELOG_Startup, L"failed to install XInput gamepad hook");
        }
    }
}

void TryInstallGamepadHook()
{
    if (g_getDeviceStateOriginal != nullptr || g_gamepadHookAttempted || !g_active ||
        g_context == nullptr || g_moduleBase == nullptr)
    {
        return;
    }

    // Devices are enumerated during engine startup (and on hot-plug),
    // so nothing is lost by only looking a few times per second.
    if (g_gamepadHookCooldown++ % 32 != 0)
        return;

    auto* const PadArray = *reinterpret_cast<void***>(g_moduleBase + k_padArrayRva);
    auto  const PadCount = *reinterpret_cast<int*>(g_moduleBase + k_padCountRva);

    if (PadArray == nullptr || PadCount <= 0 || PadCount > 16)
    {
        return;
    }

    HMODULE const DirectInput = ::GetModuleHandleW(L"dinput8.dll");

    if (DirectInput == nullptr)
    {
        g_gamepadHookAttempted = true;
        return;
    }

    for (int i = 0; i < PadCount; ++i)
    {
        void* const Device = *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(PadArray) + static_cast<std::size_t>(i) * k_padRecordSize);

        if (Device == nullptr) [[unlikely]]
        {
            continue;
        }

        void** const Vtable = *reinterpret_cast<void***>(Device);

        if (Vtable == nullptr) [[unlikely]]
        {
            continue;
        }

        auto* const GetDeviceState = reinterpret_cast<t_GetDeviceState*>(Vtable[k_getDeviceStateVtableIndex]);

        HMODULE Owner = nullptr;
        if (GetDeviceState == nullptr ||
            ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(GetDeviceState), &Owner) == FALSE ||
            Owner != DirectInput)
        {
            MILAN_WARNW(ELOG_Startup, L"gamepad input: unexpected DirectInput device vtable, gamepad hook not installed");
            g_gamepadHookAttempted = true;
            return;
        }

        bool const bInstalled = InstallDeferredHook(*g_context, "DirectInput::GetDeviceState",
            reinterpret_cast<void*>(GetDeviceState), g_getDeviceStateOriginal, &GetDeviceState_Hook);

        MILAN_INFOW(ELOG_Startup, L"gamepad input: IDirectInputDevice8::GetDeviceState at 0x%p %s",
            static_cast<void*>(GetDeviceState), bInstalled ? L"hooked" : L"hook failed");

        g_gamepadHookAttempted = true;
        return;
    }
}

void Uninstall(::LESDK::Initializer& Context)
{
    g_active = false;

    if (g_getMousePositionOriginal != nullptr)  Context.UninstallHook("FViewport::GetMousePosition");
    if (g_getMouseXOriginal != nullptr)         Context.UninstallHook("FViewport::GetMouseX");
    if (g_getMouseYOriginal != nullptr)         Context.UninstallHook("FViewport::GetMouseY");
    if (g_getDeviceStateOriginal != nullptr)    Context.UninstallHook("DirectInput::GetDeviceState");
    if (g_xinputGetStateOriginal != nullptr)    Context.UninstallHook("XInputGetState");
}

}
#endif
