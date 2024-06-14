/**
 * @file        MilanHost/Systems/Console.hpp
 * @header      System for registering console commands.
 */

#pragma once
#include <functional>
#include <LESDK/Init.hpp>


class UGameEngine;

namespace Milan::Console
{
    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Public interface.
    // ========================================

    using tOwnConsoleCommand = bool(wchar_t const* Params);
    void RegisterCommand(wchar_t const* Namespace, wchar_t const* Command, std::function<tOwnConsoleCommand>&& Callback);

    bool OnUGameEngineExec(UGameEngine* Context, WCHAR const* Command, void* Output);
}
