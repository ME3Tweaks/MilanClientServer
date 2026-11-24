/**
 * @file        MilanHost/Online/AchievementCache.hpp
 * @brief       Answers Origin's IsAchievementGranted from one query of the granted achievement set
 *              per session, instead of a blocking EA app round trip for every ID
 */

#pragma once

#include <LESDK/Init.hpp>

namespace AchievementCache
{
    void Initialize(::LESDK::Initializer& Context);
    void Uninstall(::LESDK::Initializer& Context);
}
