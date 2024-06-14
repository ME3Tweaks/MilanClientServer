/**
 * @file        MilanHost/Systems/Profilers.hpp
 * @header      System for initializing canvas and network profilers.
 */

#pragma once
#include <LESDK/Init.hpp>
#include "Milan/Canvas.hpp"


namespace Milan::Profilers
{
    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Public interface.
    // ========================================

    void On_UObject_ProcessEvent(UObject* Context, UFunction* Function, void* Parms, void* Result);
}
