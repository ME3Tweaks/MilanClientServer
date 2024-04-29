/**
 * @file        MilanHost/Systems/Builtins.hpp
 * @header      Subsystems wrapping SDK built-ins initialization.
 */

#pragma once


namespace LESDK { class Initializer; }

// GMalloc is initialized in a dedicated subsystem because that needs to happen before anything else.

namespace Milan::Allocator
{
    void Initialize(::LESDK::Initializer& Context);
    void Terminate();
}

namespace Milan::Builtins
{
    void Initialize(::LESDK::Initializer& Context);
    void Terminate();
}
