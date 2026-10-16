#pragma once

#include <LESDK/Init.hpp>

namespace InputPromptFix
{
    // Installs a device-aware selection hook for Shared_* UI binding lookups.
    // Gameplay bindings and non-UI callers of the native collector are untouched.
    void Initialize(::LESDK::Initializer& Context);
    void Uninstall(::LESDK::Initializer& Context);
}
