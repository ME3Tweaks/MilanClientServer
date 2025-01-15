#include "Base/Base.hpp"
#include "Engine/Intercept.hpp"
#include "Online/SFXOnlineHijacks.hpp"


// ! Forward-declarations.
// ========================================

namespace SFXOnlineComponentBlazeGame
{
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, AllowMatchmaking);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, OnInitialize);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, OnRelease);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, IsPlayerInGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, CancelJoinOnlineGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, CanMigrate);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, HostMigrationMsgBoxCallback);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, IsLocalPlayer);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, UpdateGameProtocolVersion);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, ValidateDLCMasterList);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, IsInvalidHost);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, CreateOnlineGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, FindOnlineGames);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, JoinOnlineGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, StartOnlineGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, EndOnlineGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, RegisterPlayer);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, UnregisterPlayer);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, DestroyOnlineGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, UpdateOnlineGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, AcceptGameInvite);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, GetResolvedConnectString);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, OnHostAddressResolved);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, ForceCleanUp);
    // ...
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, QuickMatch);
    // ...
}


// ! Public interface.
// ========================================

void SFXOnlineHijacks::InstallComponentBlazeGame()
{
    MILAN_DEBUGW(L"Installing hijacks for: %s...", L"SFXOnlineComponentBlazeGame");
    UClass* const SearchedClass = HijackUtils::FindClass(SFXName(L"SFXOnlineComponentBlazeGame", 0));

    using namespace SFXOnlineComponentBlazeGame;

    INSTALL_HIJACK(SearchedClass, AllowMatchmaking);
    INSTALL_HIJACK(SearchedClass, OnInitialize);
    INSTALL_HIJACK(SearchedClass, OnRelease);
    INSTALL_HIJACK(SearchedClass, IsPlayerInGame);
    INSTALL_HIJACK(SearchedClass, CancelJoinOnlineGame);
    INSTALL_HIJACK(SearchedClass, CanMigrate);
    INSTALL_HIJACK(SearchedClass, HostMigrationMsgBoxCallback);
    INSTALL_HIJACK(SearchedClass, IsLocalPlayer);
    INSTALL_HIJACK(SearchedClass, UpdateGameProtocolVersion);
    INSTALL_HIJACK(SearchedClass, ValidateDLCMasterList);
    INSTALL_HIJACK(SearchedClass, IsInvalidHost);
    INSTALL_HIJACK(SearchedClass, CreateOnlineGame);
    INSTALL_HIJACK(SearchedClass, FindOnlineGames);
    INSTALL_HIJACK(SearchedClass, JoinOnlineGame);
    INSTALL_HIJACK(SearchedClass, StartOnlineGame);
    INSTALL_HIJACK(SearchedClass, EndOnlineGame);
    INSTALL_HIJACK(SearchedClass, RegisterPlayer);
    INSTALL_HIJACK(SearchedClass, UnregisterPlayer);
    INSTALL_HIJACK(SearchedClass, DestroyOnlineGame);
    INSTALL_HIJACK(SearchedClass, UpdateOnlineGame);
    INSTALL_HIJACK(SearchedClass, AcceptGameInvite);
    INSTALL_HIJACK(SearchedClass, GetResolvedConnectString);
    INSTALL_HIJACK(SearchedClass, OnHostAddressResolved);
    INSTALL_HIJACK(SearchedClass, ForceCleanUp);
    // ...
    INSTALL_HIJACK(SearchedClass, QuickMatch);
}


// ! Hijacked function bodies.
// ========================================

void SFXOnlineComponentBlazeGame::AllowMatchmaking_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //AllowMatchmaking_Original(Context, Stack, Result);

    NATIVE_GET_INT(bAllow);
    NATIVE_GET_INT(bUpdateServer);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::AllowMatchmaking - bAllow = %d, bUpdateServer = %d", bAllow, bUpdateServer);
}

void SFXOnlineComponentBlazeGame::OnInitialize_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::OnInitialize - oOnlineSubsystem = ???");
    OnInitialize_Original(Context, Stack, Result);
}

void SFXOnlineComponentBlazeGame::OnRelease_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::OnRelease - oOnlineSubsystem = ???");
    OnRelease_Original(Context, Stack, Result);
}

void SFXOnlineComponentBlazeGame::IsPlayerInGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //IsPlayerInGame_Original(Context, Stack, Result);

    NATIVE_GET_STRUCT(FUniqueNetId, PlayerID);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::IsPlayerInGame - PlayerID = %d-%d", PlayerID.Uid.A, PlayerID.Uid.B);

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::CancelJoinOnlineGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //CancelJoinOnlineGame_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::CancelJoinOnlineGame");

    // No delegates to call!
    // No values to return!!!
}

void SFXOnlineComponentBlazeGame::CanMigrate_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //CanMigrate_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::CanMigrate");

    *(UBOOL*)Result = FALSE;  // I doubt we'll ever support host migration...
}

void SFXOnlineComponentBlazeGame::HostMigrationMsgBoxCallback_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::HostMigrationMsgBoxCallback - bAPressed = ???, Context = ???");
    HostMigrationMsgBoxCallback_Original(Context, Stack, Result);
}

void SFXOnlineComponentBlazeGame::IsLocalPlayer_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //IsLocalPlayer_Original(Context, Stack, Result);

    NATIVE_GET_STRUCT(FUniqueNetId, PlayerID);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::IsLocalPlayer - PlayerID = %d-%d", PlayerID.Uid.A, PlayerID.Uid.B);

    *(UBOOL*)Result = TRUE;  // For now, all players are local...
}

void SFXOnlineComponentBlazeGame::UpdateGameProtocolVersion_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //UpdateGameProtocolVersion_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::UpdateGameProtocolVersion");
}

void SFXOnlineComponentBlazeGame::ValidateDLCMasterList_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    ValidateDLCMasterList_Original(Context, Stack, Result);
    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::ValidateDLCMasterList => %s", ((FString*)Result)->Chars());
}

void SFXOnlineComponentBlazeGame::IsInvalidHost_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    IsInvalidHost_Original(Context, Stack, Result);
    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::IsInvalidHost => %d", *(UBOOL*)Result);
}

void SFXOnlineComponentBlazeGame::CreateOnlineGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //CreateOnlineGame_Original(Context, Stack, Result);

    NATIVE_GET_BYTE(HostingPlayerNum);
    NATIVE_GET_NAME(SessionName);
    NATIVE_GET_OBJECT(UOnlineGameSettings, NewGameSettings);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::CreateOnlineGame - HostingPlayerNum = %hhd, SessionName = %s, NewGameSettings = ...",
        HostingPlayerNum, *SessionName.ToString());
    MILAN_CHECKW(NewGameSettings->IsA<USFXOnlineGameSettings>(), L"");

    USFXOnlineComponentBlazeGame_execOnCreateOnlineGameComplete_Parms Parms{};
    Parms.SessionName = SessionName;
    Parms.bWasSuccessful = TRUE;

    SFXName const NAME_OnCreateOnlineGameComplete(L"OnCreateOnlineGameComplete", 0);
    Context->ProcessDelegate(NAME_OnCreateOnlineGameComplete, &Context->__OnCreateOnlineGameComplete__Delegate, &Parms, nullptr);

    for (FScriptDelegate const& Delegate : Context->CreateOnlineGameCompleteDelegates)
        Context->ProcessDelegate(NAME_OnCreateOnlineGameComplete, &Delegate, &Parms, nullptr);

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::FindOnlineGames_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //FindOnlineGames_Original(Context, Stack, Result);
    MILAN_BREAK_ATTACHED();     // Have never seen this called.

    NATIVE_GET_BYTE(SearchingPlayerNum);
    NATIVE_GET_OBJECT(UOnlineGameSettings, SearchSettings);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::FindOnlineGames - SearchingPlayerNum = %hhd, SearchSettings = ...", SearchingPlayerNum);
    MILAN_CHECKW(SearchSettings->IsA<USFXOnlineGameSettings>(), L"");

    USFXOnlineComponentBlazeGame_execOnFindOnlineGamesComplete_Parms Parms{};
    Parms.bWasSuccessful = TRUE;

    SFXName const NAME_OnFindOnlineGamesComplete(L"OnFindOnlineGamesComplete", 0);
    Context->ProcessDelegate(NAME_OnFindOnlineGamesComplete, &Context->__OnFindOnlineGamesComplete__Delegate, &Parms, nullptr);

    for (FScriptDelegate const& Delegate : Context->FindOnlineGamesCompleteDelegates)
        Context->ProcessDelegate(NAME_OnFindOnlineGamesComplete, &Delegate, &Parms, nullptr);

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::JoinOnlineGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //JoinOnlineGame_Original(Context, Stack, Result);

    NATIVE_GET_BYTE(PlayerNum);
    NATIVE_GET_NAME(SessionName);
    NATIVE_GET_STRUCT_BYREF(FOnlineGameSearchResult, DesiredGame);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::JoinOnlineGame - PlayerNum = %hhd, SessionName = %s, DesiredGame = ...",
        PlayerNum, *SessionName.ToString());

    USFXOnlineComponentBlazeGame_execOnJoinOnlineGameComplete_Parms Parms{};
    Parms.SessionName = SessionName;
    Parms.bWasSuccessful = FALSE;   // Can't mock a success scenario currently...

    SFXName const NAME_OnJoinOnlineGameComplete(L"OnJoinOnlineGameComplete", 0);
    Context->ProcessDelegate(NAME_OnJoinOnlineGameComplete, &Context->__OnJoinOnlineGameComplete__Delegate, &Parms, nullptr);

    for (FScriptDelegate const& Delegate : Context->JoinOnlineGameCompleteDelegates)
        Context->ProcessDelegate(NAME_OnJoinOnlineGameComplete, &Delegate, &Parms, nullptr);

    *(UBOOL*)Result = FALSE;        // Can't mock a success scenario currently...
}

void SFXOnlineComponentBlazeGame::StartOnlineGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //StartOnlineGame_Original(Context, Stack, Result);

    NATIVE_GET_NAME(SessionName);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::StartOnlineGame - SessionName = %s", *SessionName.ToString());

    USFXOnlineComponentBlazeGame_execOnStartOnlineGameComplete_Parms Parms{};
    Parms.SessionName = SessionName;
    Parms.bWasSuccessful = TRUE;

    SFXName const NAME_OnStartOnlineGameComplete(L"OnStartOnlineGameComplete", 0);
    Context->ProcessDelegate(NAME_OnStartOnlineGameComplete, &Context->__OnStartOnlineGameComplete__Delegate, &Parms, nullptr);

    // Add-/Clear- StartOnlineGameCompleteDelegate are nops
    // and there is no internal array for delegates of this type.

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::EndOnlineGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //EndOnlineGame_Original(Context, Stack, Result);

    NATIVE_GET_NAME(SessionName);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::EndOnlineGame - SessionName = %s", *SessionName.ToString());

    USFXOnlineComponentBlazeGame_execOnEndOnlineGameComplete_Parms Parms{};
    Parms.SessionName = SessionName;
    Parms.bWasSuccessful = TRUE;

    SFXName const NAME_OnEndOnlineGameComplete(L"OnEndOnlineGameComplete", 0);
    Context->ProcessDelegate(NAME_OnEndOnlineGameComplete, &Context->__OnEndOnlineGameComplete__Delegate, &Parms, nullptr);

    for (FScriptDelegate const& Delegate : Context->EndOnlineGameCompleteDelegates)
        Context->ProcessDelegate(NAME_OnEndOnlineGameComplete, &Delegate, &Parms, nullptr);

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::RegisterPlayer_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //RegisterPlayer_Original(Context, Stack, Result);

    NATIVE_GET_NAME(SessionName);
    NATIVE_GET_STRUCT(FUniqueNetId, PlayerID);
    NATIVE_GET_INT(bWasInvited);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::RegisterPlayer - SessionName = %s, PlayerID = %d-%d, bWasInvited = %d",
        *SessionName.ToString(), PlayerID.Uid.A, PlayerID.Uid.B, bWasInvited);

    USFXOnlineComponentBlazeGame_execOnRegisterPlayerComplete_Parms Parms{};
    Parms.SessionName = SessionName;
    Parms.PlayerID = PlayerID;
    Parms.bWasSuccessful = TRUE;

    SFXName const NAME_OnRegisterPlayerComplete(L"OnRegisterPlayerComplete", 0);
    Context->ProcessDelegate(NAME_OnRegisterPlayerComplete, &Context->__OnRegisterPlayerComplete__Delegate, &Parms, nullptr);

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::UnregisterPlayer_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //UnregisterPlayer_Original(Context, Stack, Result);

    NATIVE_GET_NAME(SessionName);
    NATIVE_GET_STRUCT(FUniqueNetId, PlayerID);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::UnregisterPlayer - SessionName = %s, PlayerID = %d-%d",
        *SessionName.ToString(), PlayerID.Uid.A, PlayerID.Uid.B);

    USFXOnlineComponentBlazeGame_execOnUnregisterPlayerComplete_Parms Parms{};
    Parms.SessionName = SessionName;
    Parms.PlayerID = PlayerID;
    Parms.bWasSuccessful = TRUE;

    SFXName const NAME_OnUnregisterPlayerComplete(L"OnUnregisterPlayerComplete", 0);
    Context->ProcessDelegate(NAME_OnUnregisterPlayerComplete, &Context->__OnUnregisterPlayerComplete__Delegate, &Parms, nullptr);

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::DestroyOnlineGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //DestroyOnlineGame_Original(Context, Stack, Result);

    NATIVE_GET_NAME(SessionName);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::DestroyOnlineGame - SessionName = %s", *SessionName.ToString());

    USFXOnlineComponentBlazeGame_execOnDestroyOnlineGameComplete_Parms Parms{};
    Parms.SessionName = SessionName;
    Parms.bWasSuccessful = TRUE;

    SFXName const NAME_OnDestroyOnlineGameComplete(L"OnDestroyOnlineGameComplete", 0);
    Context->ProcessDelegate(NAME_OnDestroyOnlineGameComplete, &Context->__OnDestroyOnlineGameComplete__Delegate, &Parms, nullptr);

    // Add-/Clear- StartOnlineGameCompleteDelegate are nops
    // and there is no internal array for delegates of this type.

    for (FScriptDelegate const& Delegate : Context->DestroyOnlineGameCompleteDelegates)
        Context->ProcessDelegate(NAME_OnDestroyOnlineGameComplete, &Delegate, &Parms, nullptr);

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

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::UpdateOnlineGame - SessionName = %s, UpdatedGameSettings = ..., bShouldRefreshOnlineData = %d",
        *SessionName.ToString(), bShouldRefreshOnlineData);
    MILAN_CHECKW(UpdatedGameSettings->IsA<USFXOnlineGameSettings>(), L"");

    USFXOnlineComponentBlazeGame_execOnUpdateOnlineGameComplete_Parms Parms{};
    Parms.SessionName = SessionName;
    Parms.bWasSuccessful = TRUE;

    SFXName const NAME_OnUpdateOnlineGameComplete(L"OnUpdateOnlineGameComplete", 0);
    Context->ProcessDelegate(NAME_OnUpdateOnlineGameComplete, &Context->__OnUpdateOnlineGameComplete__Delegate, &Parms, nullptr);

    for (FScriptDelegate const& Delegate : Context->UpdateOnlineGameCompleteDelegates)
        Context->ProcessDelegate(NAME_OnUpdateOnlineGameComplete, &Delegate, &Parms, nullptr);

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::AcceptGameInvite_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //AcceptGameInvite_Original(Context, Stack, Result);

    NATIVE_GET_BYTE(LocalUserNum);
    NATIVE_GET_NAME(SessionName);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::AcceptGameInvite - SessionName = %s", *SessionName.ToString());

    USFXOnlineComponentBlazeGame_execOnGameInviteAccepted_Parms Parms{};
    Parms.InviteResult.PlatformData = nullptr;  // NB: Remember that InviteResult is a CPF_OutParm!
    Parms.InviteResult.GameSettings = nullptr;  //  How does it affect passing parms to multiple delegates? who tf knows!

    SFXName const NAME_OnGameInviteAccepted(L"OnGameInviteAccepted", 0);
    Context->ProcessDelegate(NAME_OnGameInviteAccepted, &Context->__OnGameInviteAccepted__Delegate, &Parms, nullptr);

    // Judging by AddGameInviteAcceptedDelegate / NotifyGameInviteAcceptedDelegates,
    // we're only interested in the delegate array registered for the specific player index.

    static constexpr std::size_t k_maxPlayerInvites = sizeof(Context->InviteCache) / sizeof(*Context->InviteCache);
    MILAN_VERIFYW((std::size_t)LocalUserNum < k_maxPlayerInvites, L"");

    for (FScriptDelegate const& Delegate : Context->InviteCache[LocalUserNum].InviteDelegates)
        Context->ProcessDelegate(NAME_OnGameInviteAccepted, &Delegate, &Parms, nullptr);

    *(UBOOL*)Result = FALSE;  // NOT SUPPORTED YET
}

void SFXOnlineComponentBlazeGame::GetResolvedConnectString_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //GetResolvedConnectString_Original(Context, Stack, Result);

    NATIVE_GET_NAME(SessionName);
    NATIVE_GET_STRING_BYREF(ConnectInfo);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::GetResolvedConnectString - SessionName = %s", *SessionName.ToString());

    ConnectInfo = L"MPLobby?listen";
    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::OnHostAddressResolved_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::OnHostAddressResolved");
    OnHostAddressResolved_Original(Context, Stack, Result);
}

void SFXOnlineComponentBlazeGame::ForceCleanUp_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::ForceCleanUp");
    ForceCleanUp_Original(Context, Stack, Result);
}

/**
 * @brief
 * I have zero idea what these values really mean,
 * so this is mostly an exercise in uneducated guesswork...
 *
 * @todo    Revisit the meaning of these values...
 * @see     SFXGameInfoMP_Lobby.OnQuickMatchComplete
 */
enum EQuickMatchResult : BYTE
{
    EQMR_StartGame = 2u,
    EQMR_RetryQuickMatch = 3u,
    EQMR_Failure = 4u,
};

void SFXOnlineComponentBlazeGame::QuickMatch_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //QuickMatch_Original(Context, Stack, Result);

    NATIVE_GET_OBJECT_OPTIONAL(UOnlineGameSettings, quickMatchSettings, nullptr);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::QuickMatch - quickMatchSettings = ...");
    MILAN_CHECKW(quickMatchSettings->IsA<USFXOnlineGameSettings>(), L"");

    USFXOnlineComponentBlazeGame_execOnQuickMatchComplete_Parms Parms{};
    Parms.Result = EQMR_StartGame;

    SFXName const NAME_OnQuickMatchComplete(L"OnQuickMatchComplete", 0);
    Context->ProcessDelegate(NAME_OnQuickMatchComplete, &Context->__OnQuickMatchComplete__Delegate, &Parms, nullptr);

    for (FScriptDelegate const& Delegate : Context->QuickMatchCompleteDelegates)
        Context->ProcessDelegate(NAME_OnQuickMatchComplete, &Delegate, &Parms, nullptr);

    // Quick match search is kinda difficult to mock...
    *(UBOOL*)Result = TRUE;
}
