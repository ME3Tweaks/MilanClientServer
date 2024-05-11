/**
 * @file        MilanHost/Systems/Profilers.hpp
 * @header      Subsystem that implements custom canvas-based profilers.
 */

#pragma once


namespace LESDK { class Initializer; }

namespace Milan::Profilers
{
    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Public interface.
    // ========================================

    void On_UObject_ProcessEvent(UObject* Context, UFunction* Function, void* Parms, void* Result);

    // ...
}
