/**
 * @file        MilanHost/Systems/Globals.hpp
 * @header      Systems for initializing global variables and hooks.
 */

#pragma once
#include <LESDK/Init.hpp>


class GuidCacheOverride;
extern GuidCacheOverride* gp_guidCacheOverride;


namespace Milan::Allocator
{
    void Initialize(::LESDK::Initializer& Context);
    void Terminate();
}

namespace Milan::Globals
{
    // ! Global subsystem functions.
    // ========================================

    void Initialize(::LESDK::Initializer& Context);
    void Terminate();


    // ! Public interface.
    // ========================================

    wchar_t const* GetCmdString();
    wchar_t const* GetCmdArgs();

    constexpr bool IsFinalBuild() noexcept
    {
#ifdef _FINAL_RELEASE
        return true;
#else
        return false;
#endif
    }
}
