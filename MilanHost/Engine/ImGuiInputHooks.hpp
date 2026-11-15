#ifdef WITH_IMGUI

/**
 * @file        MilanHost/Engine/ImGuiInputHooks.hpp
 * @header      Hooks for engine input paths that bypass the WndProc message queue.
 */

#pragma once

#include <LESDK/Init.hpp>


/**
 * @brief
 *   The engine does not take all of its input from the window procedure.
 *   The controller is read through XInput (and, for pads DirectInput enumerates,
 *   through IDirectInputDevice8::GetDeviceState), and the mouse cursor is sampled
 *   with GetCursorPos from the viewport's GetMouseX / GetMouseY / GetMousePosition getters.
 *
 *   These detours hand the game neutral state while an ImGui interface is rendering and
 *   intercepting (see CImgui::IsCapturingInput), so the Scaleform UI behind the overlay cannot
 *   react to input that is supposed to belong to the overlay.
 */
namespace ImGuiInputHooks
{
void Initialize(::LESDK::Initializer& Context);
void Uninstall(::LESDK::Initializer& Context);

void TryInstallGamepadHook();

/**
 * @brief
 *   Marks the current thread as being inside an ImGui frame.
 *
 * @details
 *   ImGui's platform backend polls XInput while the frame is built and must see the real pad;
 *   any poll outside this scope is game code and is neutralized while an interface is capturing input.
 */
struct ScopedImGuiFrame final
{
    ScopedImGuiFrame();
    ~ScopedImGuiFrame();
};
}

#endif
