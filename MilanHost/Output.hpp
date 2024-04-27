/**
 * @file        MilanHost/Output.hpp
 * @header      Subsystem for logging and other debug output.
 */

#pragma once


namespace LESDK { class Initializer; }

namespace Milan::Output
{
    void Initialize(::LESDK::Initializer& Context);
    void Terminate();
}
