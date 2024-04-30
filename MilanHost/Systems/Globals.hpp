/**
 * @file        MilanHost/Systems/Globals.hpp
 * @header      Subsystems for initializing global variables and hooks.
 */

#pragma once


namespace LESDK { class Initializer; }


namespace Milan::Allocator
{
    void Initialize(::LESDK::Initializer& Context);
    void Terminate();
}

namespace Milan::Globals
{
    void Initialize(::LESDK::Initializer& Context);
    void Terminate();
}
