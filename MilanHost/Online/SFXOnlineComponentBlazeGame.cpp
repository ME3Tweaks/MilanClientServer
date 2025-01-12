#include "Base/Base.hpp"
#include "Engine/Intercept.hpp"
#include "Online/SFXOnlineHijacks.hpp"


// ! Forward-declarations.
// ========================================

namespace SFXOnlineComponentBlazeGame
{
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, GetGameSettings);
}


// ! Public interface.
// ========================================

void SFXOnlineHijacks::InstallComponentBlazeGame()
{
    MILAN_DEBUGW(L"Installing hijacks for: %s...", L"SFXOnlineComponentBlazeGame");
    UClass* const SearchedClass = HijackUtils::FindClass(SFXName(L"SFXOnlineComponentBlazeGame", 0));

    using namespace SFXOnlineComponentBlazeGame;
    INSTALL_HIJACK(GetGameSettings, SearchedClass, L"GetGameSettings");
}


// ! Hijacked function bodies.
// ========================================

void SFXOnlineComponentBlazeGame::GetGameSettings_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //GetGameSettings_Original(Context, Stack, Result);

    NATIVE_GET_NAME(SessionName);
    NATIVE_FINISH();

    *(UObject**)Result = Context->GameSettings;
}
