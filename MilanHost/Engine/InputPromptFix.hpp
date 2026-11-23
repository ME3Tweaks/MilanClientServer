/**
 * @file        MilanHost/Engine/InputPromptFix.hpp
 * @brief       Device-aware Shared_* prompts, and PC binding menu/profile hooks
 *
 * PC edits preserve controller mappings
 * PC_Use/PC_Sprint/PC_Cover aliases can share a single key
 */

#pragma once

#include <LESDK/Init.hpp>

namespace InputPromptFix
{
    void Initialize(::LESDK::Initializer& Context);
    void Uninstall(::LESDK::Initializer& Context);
}
