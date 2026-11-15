#pragma once

#include <LESDK/Init.hpp>

class CCommands;

#ifndef _FINAL_RELEASE
namespace DLCConfigReload
{
    /** Sets up DLC config reload */
    void Initialize(::LESDK::Initializer& Context);

    /** Registers DLC config commands */
    void RegisterCommands(CCommands& Commands);

    /** Reloads a DLC's config */
    bool Reload(wchar_t const* DLCName);
}
#endif // !_FINAL_RELEASE
