/**
 * @file        MilanHost/Online/ChallengeStorage.hpp
 * @brief       Utilities for "fixing" the challenge system.
 */

#pragma once

#include <LESDK/Headers.hpp>

namespace ChallengeStorage
{

/// Grows accomplishment arrays to accommodate challenges.
void EnsureArrays(USFXAccomplishmentManager* Manager);

/// Clears completion state of a group's repeatable children.
void OnGrinderProgressUpdated(USFXAccomplishmentManager* Manager, SFXName ProgressName, int Progress, bool bProgressUsed);

/// Clears child completion counts when a repeatable group is completed.
void OnGroupCompleted(USFXAccomplishmentManager* Manager, SFXName GroupName);

/// Clears stale child completion counts for groups that are already reset (counter is 0).
void RepairGroupCompletionCounts(USFXAccomplishmentManager* Manager);

}
