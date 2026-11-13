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
