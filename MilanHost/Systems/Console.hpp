/**
 * @file        MilanHost/Systems/Console.hpp
 * @header      System for registering console commands.
 */

#pragma once
#include <functional>

#define GLOBAL_UGAMEENGINE_EXEC_PHOOK \
    ::LESDK::Address::FromPostHook(/* 40 55 56 57 41 */ "54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC 40 0A 00 00")


class UGameEngine;
namespace LESDK { class Initializer; }

namespace Milan::Console
{
    using t_UGameEngine_Exec = UBOOL(UGameEngine*, WCHAR const*, void*);
    extern t_UGameEngine_Exec* UGameEngine_Exec_orig;


    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Subsystem hooks.
    // ========================================

    UBOOL UGameEngine_Exec_hook(UGameEngine* Context, WCHAR const* Command, void* Ar);


    // ! Public interface.
    // ========================================

    using tOwnConsoleCommand = bool(wchar_t const* Params);
    void RegisterCommand(wchar_t const* Namespace, wchar_t const* Command, std::function<tOwnConsoleCommand>&& Callback);
}
