/**
 * @file        MilanHost/Systems/Builtins.hpp
 * @header      Implementation of a subsystem wrapping SDK built-ins initialization.
 */

#pragma once


namespace LESDK { class Initializer; }

namespace Milan::Builtins
{
    void Initialize(::LESDK::Initializer& Context);
    void Terminate();
}
