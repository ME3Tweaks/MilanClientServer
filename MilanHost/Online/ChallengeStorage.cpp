#include "Online/ChallengeStorage.hpp"
#include "Base/Logger.hpp"

namespace
{

struct ExpectedSizes
{
    int DataCount = -1;
    int ProgressDataCount = -1;
    int RewardCount = -1;

    int Accomplishments = 0;   // max(FAccomplishment.Index + 1)
    int Progress = 0;          // max(FAccomplishmentProgress.Index + 1)
    int Rewards = 0;           // max(FAchievementReward.RewardIDGranted + 1)
};

int ComputeAccomplishmentCount(TArray<FAccomplishment> const& Data)
{
    int Result = 0;

    for (int i = 0; i < static_cast<int>(Data.Count()); ++i)
    {
        int const Index = Data(i).Index;
        if (Index >= Result) Result = Index + 1;
    }

    return Result;
}

int ComputeProgressCount(TArray<FAccomplishmentProgress> const& Data)
{
    int Result = 0;

    for (int i = 0; i < static_cast<int>(Data.Count()); ++i)
    {
        int const Index = Data(i).Index;
        if (Index >= Result) Result = Index + 1;
    }

    return Result;
}

int ComputeRewardCount(TArray<FAchievementReward> const& Data)
{
    int Result = 0;

    for (int i = 0; i < static_cast<int>(Data.Count()); ++i)
    {
        int const RewardId = Data(i).RewardIDGranted;
        if (RewardId >= Result) Result = RewardId + 1;
    }

    return Result;
}

void ResizeUpwards(TArray<int>& Values, int Needed, wchar_t const* Name)
{
    if (Needed > 0)
    {
        if (int const Current = static_cast<int>(Values.Count()); Current < Needed)
        {
            Values.Resize(Needed);
            MILAN_INFOW(ELOG_MultiProfile, L"challenge storage '%s': grown %d -> %d", Name, Current, Needed);
        }
    }
}

ExpectedSizes& GetCachedSizes()
{
    static ExpectedSizes Sizes{};
    return Sizes;
}

}

void ChallengeStorage::EnsureArrays(USFXAccomplishmentManager* const Manager)
{
    if (Manager != nullptr) [[likely]]
    {
        ExpectedSizes& Sizes = GetCachedSizes();

        if (Sizes.DataCount != static_cast<int>(Manager->AccomplishmentData.Count()) ||
            Sizes.ProgressDataCount != static_cast<int>(Manager->AccomplishmentProgressData.Count()) ||
            Sizes.RewardCount != static_cast<int>(Manager->Rewards.Count()))
        {
            Sizes.DataCount = static_cast<int>(Manager->AccomplishmentData.Count());
            Sizes.ProgressDataCount = static_cast<int>(Manager->AccomplishmentProgressData.Count());
            Sizes.RewardCount = static_cast<int>(Manager->Rewards.Count());

            Sizes.Accomplishments = ComputeAccomplishmentCount(Manager->AccomplishmentData);
            Sizes.Progress = ComputeProgressCount(Manager->AccomplishmentProgressData);
            Sizes.Rewards = ComputeRewardCount(Manager->Rewards);
        }

        if (Sizes.Accomplishments > 0)
        {
            ResizeUpwards(Manager->AccomplishmentIsComplete, Sizes.Accomplishments, L"AccomplishmentIsComplete");
            ResizeUpwards(Manager->AccomplishmentCompletionTimes, Sizes.Accomplishments, L"AccomplishmentCompletionTimes");
        }

        if (USFXAccomplishmentStorage* const Storage = Manager->AccomplishmentStorage; Storage != nullptr)
        {
            ResizeUpwards(Storage->ProgressValues, Sizes.Progress, L"ProgressValues");
            ResizeUpwards(Storage->RewardLevelValues, Sizes.Rewards, L"RewardLevelValues");
        }
    }
}

namespace
{

void ResetGroupChildren(USFXAccomplishmentManager* const Manager, SFXName const GroupName)
{
    int const CompleteCount = static_cast<int>(Manager->AccomplishmentIsComplete.Count());
    int const CompleteTimeCount = static_cast<int>(Manager->AccomplishmentCompletionTimes.Count());

    int Cleared = 0;

    for (FAccomplishment const& Accomplishment : Manager->AccomplishmentData)
    {
        if (Accomplishment.ChallengeGroupName == GroupName &&
            Accomplishment.Name != GroupName &&
            Accomplishment.bRepeatableChallenge &&
            Accomplishment.Index >= 0)
        {
            if (Accomplishment.Index < CompleteCount && Manager->AccomplishmentIsComplete(Accomplishment.Index) != 0)
            {
                Manager->AccomplishmentIsComplete(Accomplishment.Index) = 0;
                ++Cleared;
            }

            if (Accomplishment.Index < CompleteTimeCount)
            {
                Manager->AccomplishmentCompletionTimes(Accomplishment.Index) = 0;
            }
        }
    }

    if (Cleared > 0)
    {
        MILAN_INFOW(ELOG_MultiProfile,
            L"challenge group '%s': cleared completion for %d reset child challenge(s)",
            *GroupName.ToString(), Cleared);
    }
}

}

void ChallengeStorage::OnGroupCompleted(USFXAccomplishmentManager* const Manager, SFXName const GroupName)
{
    if (Manager != nullptr)
    {
        for (FAccomplishment const& Group : Manager->AccomplishmentData)
        {
            if (Group.Name == GroupName && Group.bRepeatableChallenge && Group.Name == Group.ChallengeGroupName)
            {
                ResetGroupChildren(Manager, GroupName);
                return;
            }
        }
    }
}

void ChallengeStorage::RepairGroupCompletionCounts(USFXAccomplishmentManager* const Manager)
{
    if (Manager != nullptr)
    {
        USFXAccomplishmentStorage* const Storage = Manager->AccomplishmentStorage;
        if (Storage == nullptr) [[unlikely]] return;

        int const CompleteCount = static_cast<int>(Manager->AccomplishmentIsComplete.Count());

        for (FAccomplishment const& Group : Manager->AccomplishmentData)
        {
            if (!Group.bRepeatableChallenge || Group.Name != Group.ChallengeGroupName)
                continue;

            if (Group.Index < 0 || Group.Index >= CompleteCount || Manager->AccomplishmentIsComplete(Group.Index) == 0)
                continue;

            int ProgressSlot = -1;

            for (FGrinderAccomplishment const& Grinder : Manager->GrinderAccomplishmentData)
            {
                if (Grinder.AccomplishmentName == Group.Name)
                {
                    for (FAccomplishmentProgress const& Progress : Manager->AccomplishmentProgressData)
                    {
                        if (Progress.Name == Grinder.AccomplishmentProgressName)
                        {
                            ProgressSlot = Progress.Index;
                            break;
                        }
                    }

                    if (ProgressSlot >= 0)
                        break;
                }
            }

            if (ProgressSlot >= 0 && ProgressSlot < static_cast<int>(Storage->ProgressValues.Count()))
            {
                // The group has completed at least once and its shared counter is 0: that is the
                // post-reset state, where every child count is supposed to be 0. Any leftover counts
                // are from the pre-fix saves (they were never cleared when the group reset).
                if (Storage->ProgressValues(ProgressSlot) == 0)
                {
                    ResetGroupChildren(Manager, Group.Name);
                }
            }
        }
    }
}

void ChallengeStorage::OnGrinderProgressUpdated
(
    USFXAccomplishmentManager* const    Manager,
    SFXName const                       ProgressName,
    int const                           Progress,
    bool const                          bProgressUsed
)
{
    if (Manager != nullptr && bProgressUsed)
    {
        if (USFXAccomplishmentStorage* const Storage = Manager->AccomplishmentStorage; Storage != nullptr)
        {
            int ProgressSlot = -1;

            for (FAccomplishmentProgress const& AccomplishmentProgress : Manager->AccomplishmentProgressData)
            {
                if (AccomplishmentProgress.Name == ProgressName)
                {
                    ProgressSlot = AccomplishmentProgress.Index;
                    break;
                }
            }

            if (ProgressSlot < 0 ||
                ProgressSlot >= static_cast<int>(Storage->ProgressValues.Count()) ||
                Storage->ProgressValues(ProgressSlot) != 0)
            {
                return;
            }

            for (FGrinderAccomplishment const& Grinder : Manager->GrinderAccomplishmentData)
            {
                if (ProgressName == Grinder.AccomplishmentProgressName && (Progress <= 0 || Progress >= Grinder.Goal))
                {
                    for (FAccomplishment const& Group : Manager->AccomplishmentData)
                    {
                        if (Group.Name == Grinder.AccomplishmentName &&
                            Group.Name == Group.ChallengeGroupName &&
                            Group.bRepeatableChallenge &&
                            Group.Index >= 0 &&
                            Group.Index < static_cast<int>(Manager->AccomplishmentIsComplete.Count()) &&
                            Manager->AccomplishmentIsComplete(Group.Index) != 0)
                        {
                            ResetGroupChildren(Manager, Group.Name);
                            break;
                        }
                    }
                }
            }
        }
    }
}
