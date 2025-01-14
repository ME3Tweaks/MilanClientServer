#include "Base/Base.hpp"
#include "Engine/Intercept.hpp"
#include "Online/SFXOnlineHijacks.hpp"


// ! Forward-declarations.
// ========================================

namespace SFXOnlineComponentBlazeGame
{
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, GetGameSettings);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, CreateOnlineGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, FindOnlineGames);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, StartOnlineGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, EndOnlineGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, UpdateOnlineGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, GetResolvedConnectString);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, QuickMatch);
}


// ! Public interface.
// ========================================

void SFXOnlineHijacks::InstallComponentBlazeGame()
{
    MILAN_DEBUGW(L"Installing hijacks for: %s...", L"SFXOnlineComponentBlazeGame");
    UClass* const SearchedClass = HijackUtils::FindClass(SFXName(L"SFXOnlineComponentBlazeGame", 0));

    using namespace SFXOnlineComponentBlazeGame;
    INSTALL_HIJACK(SearchedClass, GetGameSettings);
    INSTALL_HIJACK(SearchedClass, CreateOnlineGame);
    INSTALL_HIJACK(SearchedClass, StartOnlineGame);
    INSTALL_HIJACK(SearchedClass, EndOnlineGame);
    INSTALL_HIJACK(SearchedClass, UpdateOnlineGame);
    INSTALL_HIJACK(SearchedClass, GetResolvedConnectString);
    INSTALL_HIJACK(SearchedClass, QuickMatch);
    // ...

    // None of these are getting called... why?
    INSTALL_HIJACK(SearchedClass, FindOnlineGames);
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

void SFXOnlineComponentBlazeGame::CreateOnlineGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //CreateOnlineGame_Original(Context, Stack, Result);

    NATIVE_GET_BYTE(HostingPlayerNum);
    NATIVE_GET_NAME(SessionName);
    NATIVE_GET_OBJECT(UOnlineGameSettings, NewGameSettings);
    NATIVE_FINISH();

    SFXName const NAME_OnCreateOnlineGameComplete{ L"OnCreateOnlineGameComplete", 0 };
    for (FScriptDelegate const& Delegate : Context->CreateOnlineGameCompleteDelegates)
    {
        USFXOnlineComponentBlazeGame_execOnCreateOnlineGameComplete_Parms Parms{};
        Parms.SessionName = SessionName;
        Parms.bWasSuccessful = TRUE;

        Context->ProcessDelegate(NAME_OnCreateOnlineGameComplete, &Delegate, &Parms, nullptr);
    }

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::FindOnlineGames_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    MILAN_BREAK_ATTACHED();

    FindOnlineGames_Original(Context, Stack, Result);
}

void SFXOnlineComponentBlazeGame::StartOnlineGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //StartOnlineGame_Original(Context, Stack, Result);

    NATIVE_GET_NAME(SessionName);
    NATIVE_FINISH();

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::EndOnlineGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //EndOnlineGame_Original(Context, Stack, Result);

    NATIVE_GET_NAME(SessionName);
    NATIVE_FINISH();

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::UpdateOnlineGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //UpdateOnlineGame_Original(Context, Stack, Result);

    NATIVE_GET_NAME(SessionName);
    NATIVE_GET_OBJECT(UOnlineGameSettings, UpdatedGameSettings);
    NATIVE_GET_INT_OPTIONAL(bShouldRefreshOnlineData, FALSE);
    NATIVE_FINISH();

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::GetResolvedConnectString_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //EndOnlineGame_Original(Context, Stack, Result);

    NATIVE_GET_NAME(SessionName);
    NATIVE_GET_STRING_BYREF(ConnectInfo);
    NATIVE_FINISH();

    ConnectInfo = L"MPLobby?listen";
    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::QuickMatch_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //QuickMatch_Original(Context, Stack, Result);

    NATIVE_GET_OBJECT_OPTIONAL(UOnlineGameSettings, quickMatchSettings, nullptr);
    NATIVE_FINISH();

    // Quick match search is kinda difficult to mock...
    *(UBOOL*)Result = FALSE;
}
