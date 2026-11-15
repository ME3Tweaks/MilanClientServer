#pragma once

#include <LESDK/Init.hpp>

namespace AchievementGuard
{
    // Keeps LMP from changing anything that belongs to the base games: platform (EA, mirrored to
    // Steam) achievement grants are dropped, and the TrilogyGamerProfile shared by LE1-3 is never
    // written. Reading achievements through Origin and reading the trilogy profile still work.
    // There is deliberately no Uninstall: a profile save during shutdown must still be blocked.
    void Initialize(::LESDK::Initializer& Context);
}
