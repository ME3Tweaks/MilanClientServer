/**
 * @file        MilanHost/Systems/Globals.hpp
 * @header      Systems for initializing global variables and hooks.
 */

#pragma once
#include <LESDK/Init.hpp>


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
