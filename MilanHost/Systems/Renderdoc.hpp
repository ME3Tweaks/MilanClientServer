/**
 * @file        MilanHost/Systems/Renderdoc.hpp
 * @header      System for integrating RenderDoc in-app API.
 */

#pragma once

#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

namespace Milan::Renderdoc
{
    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Public interface.
    // ========================================

    void On_VectoredException(void* Address, DWORD Code, _EXCEPTION_POINTERS* Exceptions);
    void On_UObject_ProcessEvent(UObject* Context, UFunction* Function, void* Parms, void* Result);
}
