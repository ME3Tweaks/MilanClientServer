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
 * Everything that reads this value (SFXGRIMP.MatchStartTime, SFXPRIMP.GatherMatchResults'
 * MatchEndTime) only ever uses the *difference* between two calls, so any monotonically
 * increasing per-second counter works fine here - it doesn't need to be wall-clock time.
 */
void SFXOnlineComponentBlazeHub::GetCurrentTime_Hook(USFXOnlineComponentBlazeHub* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //GetCurrentTime_Original(Context, Stack, Result);

    NATIVE_FINISH();

    *(INT*)Result = (INT)(::GetTickCount64() / 1000);
}
