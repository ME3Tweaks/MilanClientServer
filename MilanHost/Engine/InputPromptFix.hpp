#pragma once

#include <LESDK/Init.hpp>

namespace InputPromptFix
{
    // Installs Shared_* prompt selection and PC binding menu/profile hooks.
    // PC edits preserve controller mappings and migrate renamed profile aliases.
    void Initialize(::LESDK::Initializer& Context);
    void Uninstall(::LESDK::Initializer& Context);
}
