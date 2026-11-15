#include "Base/Base.hpp"
#include "Engine/Intercept.hpp"
#include "Online/SFXOnlineHijacks.hpp"


// ! Forward-declarations.
// ========================================

namespace SFXOnlineComponentBlazeHub
{
    DECLARE_HIJACK(USFXOnlineComponentBlazeHub, GetCurrentTime);
}


// ! Public interface.
// ========================================

void SFXOnlineHijacks::InstallComponentBlazeHub()
{
    MILAN_DEBUGW(L"Installing hijacks for: %s...", L"SFXOnlineComponentBlazeHub");
    UClass* const SearchedClass = HijackUtils::FindClass(SFXName(L"SFXOnlineComponentBlazeHub", 0));

    using namespace SFXOnlineComponentBlazeHub;

    INSTALL_HIJACK(SearchedClass, GetCurrentTime);
}


// ! Hijacked function bodies.
// ========================================

/**
 * @brief
 * The real native implementation (which we're replacing here) is a dead stub that
 * always returns 0 - it presumably used to call out to a live Blaze server for a
 * synced clock, but never implemented as LE3 used no such online functionality.
 *
 * This counter is local machine uptime, not a clock synchronized between players.
 * The listen host measures both SFXGRIMP.MatchStartTime and the end time in
 * SFXGameInfoMP.MatchOver.CheckGoToLobby, then sends the duration to each player
 * through SFXPRIMP.GatherMatchResults. Clients must never subtract the replicated
 * host start time from their own uptime. Measuring both ends on the listen host
 * works without a matchmaking service or wall-clock synchronization.
 */
void SFXOnlineComponentBlazeHub::GetCurrentTime_Hook(USFXOnlineComponentBlazeHub* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //GetCurrentTime_Original(Context, Stack, Result);

    NATIVE_FINISH();

    *(INT*)Result = (INT)(::GetTickCount64() / 1000);
}
