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
    void DeferredInitialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Public interface.
    // ========================================

    void On_VectoredException(void* Address, DWORD Code, _EXCEPTION_POINTERS* Exceptions);

    bool HasInitialized() noexcept;

    ILogger& GetMainLogger();
    void FlushEverything();

    bool ToggleLogInternal() noexcept;
    bool ToggleWarnInternal() noexcept;
}
