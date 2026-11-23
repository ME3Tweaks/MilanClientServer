/**
 * @file        MilanHost/Engine/InputActionGroup.hpp
 * @brief       Runs the split omnikey rows (PC_Use, PC_Sprint, PC_Cover) as one chain when they share a key
 */

#pragma once

#include <LESDK/Init.hpp>

namespace InputActionGroup
{
    void Initialize(::LESDK::Initializer& Context);
    void Uninstall(::LESDK::Initializer& Context);
}
