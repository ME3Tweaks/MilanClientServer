/**
 * @file        MilanHost/Engine/GuiMovieGuard.hpp
 * @brief       Stops SFX GUI movies reading through a missing or destroyed local player controller
 *              when they check for gamepad input
 */

#pragma once

#include <LESDK/Init.hpp>

namespace GuiMovieGuard
{
    void Initialize(::LESDK::Initializer& Context);
    void Uninstall(::LESDK::Initializer& Context);
}
