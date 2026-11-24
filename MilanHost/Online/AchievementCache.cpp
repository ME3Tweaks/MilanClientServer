#include <algorithm>
#include <vector>

#include <LESDK/Headers.hpp>
#include "Base/Logger.hpp"
#include "Engine/Hooks.hpp"
#include "Online/AchievementCache.hpp"


// ! Hook patterns.
// ========================================

// USFXOnlineComponentOrigin vtable slot 120.
// Runs a blocking query of all acheivements to determine if a single one is unlocked
#define GLOBAL_USFXONLINECOMPONENTORIGIN_ISACHIEVEMENTGRANTED_PHOOK ::LESDK::Address::FromPostHook(/* 48 8B C4 89 50 */ "10 53 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 78 48 C7 40 98 FE FF FF FF 8B EA 45 33 ED")

// OriginSDK 10.6.2.9 calls made by IsAchievementGranted
#define GLOBAL_ORIGINGETDEFAULTUSER_RVA         ::LESDK::Address::FromOffset(0xE2E5E0)
#define GLOBAL_ORIGINGETDEFAULTPERSONA_RVA      ::LESDK::Address::FromOffset(0xE2E5A0)
#define GLOBAL_ORIGINQUERYACHIEVEMENTSSYNC_RVA  ::LESDK::Address::FromOffset(0xE2EAA0)
#define GLOBAL_ORIGINREADENUMERATION_RVA        ::LESDK::Address::FromOffset(0xE2EDE0)
#define GLOBAL_ORIGINDESTROYHANDLE_RVA          ::LESDK::Address::FromOffset(0xE2E530)


namespace AchievementCache
{
    namespace
    {
        // Struct SDK returns for an acheivement
        struct FOriginAchievement
        {
            int Id;
            // Don't know or care about this - probably date or something
            unsigned char Unknown_04[0x4C];
        };
        static_assert(sizeof(FOriginAchievement) == 0x50);

        struct FOriginAchievementSet
        {
            void const* Unknown_00;
            void const* Unknown_08;
            FOriginAchievement const* Achievements;
            std::size_t AchievementCount;
        };
        static_assert(sizeof(FOriginAchievementSet) == 0x20);

        using t_OriginGetDefaultUser = unsigned long long();
        t_OriginGetDefaultUser* OriginGetDefaultUser = nullptr;

        using t_OriginGetDefaultPersona = unsigned long long();
        t_OriginGetDefaultPersona* OriginGetDefaultPersona = nullptr;

        using t_OriginQueryAchievementsSync = int(unsigned long long User, unsigned long long Persona, void const* GameIds, std::size_t GameIdCount, int Mode, std::size_t* OutTotal, int Timeout, unsigned long long* OutHandle);
        t_OriginQueryAchievementsSync* OriginQueryAchievementsSync = nullptr;

        using t_OriginReadEnumeration = int(unsigned long long Handle, void* Buffer, std::size_t BufferSize, std::size_t Start, std::size_t Count, std::size_t* OutRead);
        t_OriginReadEnumeration* OriginReadEnumeration = nullptr;

        using t_OriginDestroyHandle = int(unsigned long long Handle);
        t_OriginDestroyHandle* OriginDestroyHandle = nullptr;

        using t_USFXOnlineComponentOrigin_IsAchievementGranted = UBOOL(USFXOnlineComponentOrigin*, int);
        t_USFXOnlineComponentOrigin_IsAchievementGranted* USFXOnlineComponentOrigin_IsAchievementGranted_orig = nullptr;

        // Since you can't each achievements in LMP we can do a single cache for the whole session
        bool g_cached = false;

        // List of granted achievements
        std::vector<int> g_grantedIds;

        bool QueryGrantedIds(int Timeout, std::vector<int>& OutIds, int& OutError)
        {
            unsigned long long Handle = 0;
            std::size_t Total = 0;
            OutError = OriginQueryAchievementsSync(OriginGetDefaultUser(), OriginGetDefaultPersona(), nullptr, 0, 0, &Total, Timeout, &Handle);
            if (OutError != 0)
            {
                return false;
            }

            std::vector<FOriginAchievementSet> Sets(Total);
            std::size_t Read = 0;
            if (Total != 0)
            {
                OutError = OriginReadEnumeration(Handle, Sets.data(), Total * sizeof(FOriginAchievementSet), 0, Total, &Read);
            }
            if (OutError == 0)
            {
                for (std::size_t SetIdx = 0; SetIdx < Read && SetIdx < Sets.size(); ++SetIdx)
                {
                    for (std::size_t AchIdx = 0; AchIdx < Sets[SetIdx].AchievementCount; ++AchIdx)
                    {
                        OutIds.push_back(Sets[SetIdx].Achievements[AchIdx].Id);
                    }
                }
            }
            OriginDestroyHandle(Handle);

            // No sets means Origin isn't ready, not zero achievements
            return OutError == 0 && Read != 0;
        }

        // Cache-based reimplementation of the original native function
        UBOOL USFXOnlineComponentOrigin_IsAchievementGranted_hook(USFXOnlineComponentOrigin* Origin, int AchievementId)
        {
            if (!g_cached)
            {
                // Fetch acheivements
                g_cached = true;
                unsigned long long const Start = ::GetTickCount64();
                std::vector<int> Ids;
                int Error = 0;
                if (!QueryGrantedIds(Origin->mQueryAchievementsTimeOut, Ids, Error))
                {
                    MILAN_WARNW(ELOG_SFXOnline, L"achievement cache: granted achievement query failed (error 0x%08X, %llu ms)", static_cast<unsigned>(Error), ::GetTickCount64() - Start);
                    return FALSE;
                }

                g_grantedIds = std::move(Ids);
                MILAN_INFOW(ELOG_SFXOnline, L"achievement cache: read %llu granted achievements in %llu ms", static_cast<unsigned long long>(g_grantedIds.size()), ::GetTickCount64() - Start);
            }
            return std::find(g_grantedIds.begin(), g_grantedIds.end(), AchievementId) != g_grantedIds.end() ? TRUE : FALSE;
        }
    }

    void Initialize(::LESDK::Initializer& Context)
    {
        void* const IsGrantedAddress = Context.Resolve(GLOBAL_USFXONLINECOMPONENTORIGIN_ISACHIEVEMENTGRANTED_PHOOK);
        // We have to find these values to feed into the acheivement query call
        FIND_VAR_ONE(OriginGetDefaultUser, GLOBAL_ORIGINGETDEFAULTUSER_RVA);
        FIND_VAR_ONE(OriginGetDefaultPersona, GLOBAL_ORIGINGETDEFAULTPERSONA_RVA);
        FIND_VAR_ONE(OriginQueryAchievementsSync, GLOBAL_ORIGINQUERYACHIEVEMENTSSYNC_RVA);
        FIND_VAR_ONE(OriginReadEnumeration, GLOBAL_ORIGINREADENUMERATION_RVA);
        FIND_VAR_ONE(OriginDestroyHandle, GLOBAL_ORIGINDESTROYHANDLE_RVA);
        if (IsGrantedAddress == nullptr || OriginGetDefaultUser == nullptr || OriginGetDefaultPersona == nullptr || OriginQueryAchievementsSync == nullptr || OriginReadEnumeration == nullptr || OriginDestroyHandle == nullptr)
        {
            MILAN_WARNW(ELOG_Startup, L"achievement cache: could not find at least one required Origin SDK method, acheivement packs won't be available this session");
            return;
        }

        INSTALL_HOOK_TWO_ADDR(USFXOnlineComponentOrigin, IsAchievementGranted, IsGrantedAddress);
        if (USFXOnlineComponentOrigin_IsAchievementGranted_orig == nullptr)
        {
            MILAN_ERRORW(ELOG_Startup, L"achievement cache: failed to create the IsAchievementGranted hook");
            return;
        }
        MILAN_INFOW(ELOG_Startup, L"achievement cache: installed granted achievement cache");
    }

    void Uninstall(::LESDK::Initializer& Context)
    {
        if (USFXOnlineComponentOrigin_IsAchievementGranted_orig != nullptr)
        {
            Context.UninstallHook("USFXOnlineComponentOrigin::IsAchievementGranted");
        }
        USFXOnlineComponentOrigin_IsAchievementGranted_orig = nullptr;
    }
}
