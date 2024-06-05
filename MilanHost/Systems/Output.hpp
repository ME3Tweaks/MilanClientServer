/**
 * @file        MilanHost/Systems/Output.hpp
 * @header      System for logging and other debug output.
 */

#pragma once
#include <LESDK/Init.hpp>

class ILogger;

namespace Milan::Output
{
    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Public interface.
    // ========================================

    bool HasInitialized() noexcept;

    ILogger& GetMainLogger();
    void FlushEverything();
}
