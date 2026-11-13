/**
 * @file        MilanHost/Online/ChallengeStorage.hpp
 * @brief       Utilities for "fixing" the challenge system.
 */

#pragma once

#include <LESDK/Headers.hpp>
#include "Base/Base.hpp"

namespace ChallengeStorage
{

/// Grows accomplishment arrays to accommodate challenges.
void EnsureArrays(USFXAccomplishmentManager* Manager);

}
