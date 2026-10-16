#include "Base/Base.hpp"
#include "Components/CMatchmaking.hpp"
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
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, IsReadyForConnections);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, IsHostMigrationInProgress);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, CompleteHostMigration);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, CancelHostMigration);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, FailHostMigration);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, ShowHostMigrationMsgBox);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, HideHostMigrationMsgBox);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, GetPlayerCount);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, KickPlayer);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, OnTick);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, OnMPGameStatusChange);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, IsPlaying);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, LeaveGame);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, QuickMatch);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, CanStartMPMatch);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, SetCallRestrictedFunctionMode);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, PerformCallRestrictedFunction);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, SetPlatformPresence);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, ShowPopup);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, OnConnectionError);
    DECLARE_HIJACK(USFXOnlineComponentBlazeGame, CancelFindOnlineGames);
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
    INSTALL_HIJACK(SearchedClass, IsReadyForConnections);
    INSTALL_HIJACK(SearchedClass, IsHostMigrationInProgress);
    INSTALL_HIJACK(SearchedClass, CompleteHostMigration);
    INSTALL_HIJACK(SearchedClass, CancelHostMigration);
    INSTALL_HIJACK(SearchedClass, FailHostMigration);
    INSTALL_HIJACK(SearchedClass, ShowHostMigrationMsgBox);
    INSTALL_HIJACK(SearchedClass, HideHostMigrationMsgBox);
    INSTALL_HIJACK(SearchedClass, GetPlayerCount);
    INSTALL_HIJACK(SearchedClass, KickPlayer);
    INSTALL_HIJACK(SearchedClass, OnTick);
    INSTALL_HIJACK(SearchedClass, OnMPGameStatusChange);
    INSTALL_HIJACK(SearchedClass, IsPlaying);
    INSTALL_HIJACK(SearchedClass, LeaveGame);
    INSTALL_HIJACK(SearchedClass, QuickMatch);
    INSTALL_HIJACK(SearchedClass, CanStartMPMatch);
    INSTALL_HIJACK(SearchedClass, SetCallRestrictedFunctionMode);
    INSTALL_HIJACK(SearchedClass, PerformCallRestrictedFunction);
    INSTALL_HIJACK(SearchedClass, SetPlatformPresence);
    INSTALL_HIJACK(SearchedClass, ShowPopup);
    INSTALL_HIJACK(SearchedClass, OnConnectionError);
    INSTALL_HIJACK(SearchedClass, CancelFindOnlineGames);
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

    // Ghidra decomp:

    Context->bAllowMatchmaking = bAllow != 0;
    if (Context->GameSettings != nullptr && bUpdateServer)
    {
        // Virtual call at 0x338 with (this, SFXName(L"Game", 0), TRUE).
        //      Resolves to nop.
    }

    // End of ghidra decomp.

    COMPONENT(CMatchmaking).On_AllowMatchmaking(bAllow != 0);
}

void SFXOnlineComponentBlazeGame::OnInitialize_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::OnInitialize - oOnlineSubsystem = ???");
    MILAN_BREAK_ATTACHED();  // never seen this be called
    OnInitialize_Original(Context, Stack, Result);
}

void SFXOnlineComponentBlazeGame::OnRelease_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::OnRelease - oOnlineSubsystem = ???");
    MILAN_BREAK_ATTACHED();  // never seen this be called
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
    //HostMigrationMsgBoxCallback_Original(Context, Stack, Result);

    NATIVE_GET_INT(bAPressed);
    NATIVE_GET_INT(nContext);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::HostMigrationMsgBoxCallback - bAPressed = %d, Context = %d", bAPressed, nContext);
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
    //ValidateDLCMasterList_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::ValidateDLCMasterList");

    // Ghidra decomp:
    //      FString MasterList{};
    //      Virtual call at 0x2F0 with (this, &MasterList) -> &MasterList.
    //          Resolves to: MasterList->Clear();
    //      If &MasterList != Result    // notice it's a pointer comparison
    //          Result->Assign(MasterList)
    // End of ghidra decomp.

    *(FString*)Result = FString{};
}

void SFXOnlineComponentBlazeGame::IsInvalidHost_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //IsInvalidHost_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::IsInvalidHost");

    *(UBOOL*)Result = FALSE;
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

    // Ghidra decomp shows that the delegate array is copied before being executed.
    // Also, the default delegate property (__OnDestroyOnlineGameComplete__Delegate) doesn't seem to be called.

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

    // The matchmaker lobby follows these settings; this never fails the update.
    COMPONENT(CMatchmaking).On_HostSettingsChanged(static_cast<USFXOnlineGameSettings*>(UpdatedGameSettings));

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

    std::wstring const Target = COMPONENT(CMatchmaking).QuickMatchConnectString();
    ConnectInfo = Target.empty() ? L"MPLobby?listen" : Target.c_str();
    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::OnHostAddressResolved_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //OnHostAddressResolved_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::OnHostAddressResolved");

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::ForceCleanUp_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //ForceCleanUp_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::ForceCleanUp");

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::IsReadyForConnections_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //IsReadyForConnections_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::IsReadyForConnections");

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::IsHostMigrationInProgress_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //IsHostMigrationInProgress_Original(Context, Stack, Result);
    //MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::IsHostMigrationInProgress => %d", *(UBOOL*)Result);

    NATIVE_FINISH();

    *(UBOOL*)Result = Context->m_HostMigrationInProgress;
}

void SFXOnlineComponentBlazeGame::CompleteHostMigration_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //CompleteHostMigration_Original(Context, Stack, Result);

    NATIVE_GET_INT(bLocalPlayerIsHost);
    NATIVE_GET_INT(bIsInLobby);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::CompleteHostMigration - bLocalPlayerIsHost = %d, bIsInLobby = %d",
        bLocalPlayerIsHost, bIsInLobby);
}

void SFXOnlineComponentBlazeGame::CancelHostMigration_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //CancelHostMigration_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::CancelHostMigration");
}

void SFXOnlineComponentBlazeGame::FailHostMigration_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //FailHostMigration_Original(Context, Stack, Result);

    NATIVE_GET_STRING(Reason);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::FailHostMigration - Reason = %s", *Reason);
}

void SFXOnlineComponentBlazeGame::ShowHostMigrationMsgBox_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //ShowHostMigrationMsgBox_Original(Context, Stack, Result);

    NATIVE_GET_INT(bIsInLobby);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::ShowHostMigrationMsgBox - bIsInLobby = %d", bIsInLobby);
}

void SFXOnlineComponentBlazeGame::HideHostMigrationMsgBox_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //HideHostMigrationMsgBox_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::HideHostMigrationMsgBox");
}

void SFXOnlineComponentBlazeGame::GetPlayerCount_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //GetPlayerCount_Original(Context, Stack, Result);
    //MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::GetPlayerCount => %d", *(INT*)Result);

    NATIVE_FINISH();

    *(INT*)Result = 0;  // TODO: This should probably return the number of currently registered players?..
}

void SFXOnlineComponentBlazeGame::KickPlayer_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //KickPlayer_Original(Context, Stack, Result);

    NATIVE_GET_STRUCT(FUniqueNetId, PlayerID);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::KickPlayer - PlayerId = %d-%d", PlayerID.Uid.A, PlayerID.Uid.B);

    *(UBOOL*)Result = COMPONENT(CMatchmaking).On_KickPlayer(Context, PlayerID) ? TRUE : FALSE;
}

void SFXOnlineComponentBlazeGame::OnTick_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //OnTick_Original(Context, Stack, Result);

    NATIVE_GET_OBJECT(USFXOnlineEvent, Event);
    NATIVE_FINISH();

    //MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::OnTick - Event = ...");
}

void SFXOnlineComponentBlazeGame::OnMPGameStatusChange_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //OnMPGameStatusChange_Original(Context, Stack, Result);

    NATIVE_GET_OBJECT(USFXOnlineEvent, Event);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::OnMPGameStatusChange - Event = ...");
}

void SFXOnlineComponentBlazeGame::IsPlaying_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //IsPlaying_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::IsPlaying");

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::LeaveGame_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //LeaveGame_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::LeaveGame");
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
    // Milan additions. The lobby script (SFXGameInfoMP_Lobby.OnQuickMatchComplete) shows a reason for each error
    EQMR_MatchmakingOff = 5u,
    EQMR_ServerUnavailable = 6u,
    EQMR_AccessKeyRejected = 7u,
};

/// <summary>
/// Converts outcome to enum value for unrealscript
/// </summary>
/// <param name="Outcome"></param>
/// <returns></returns>
static EQuickMatchResult ToQuickMatchResult(CMatchmaking::EQuickMatchOutcome const Outcome)
{
    switch (Outcome)
    {
    case CMatchmaking::EQuickMatchOutcome::Matched:             return EQMR_StartGame;
    case CMatchmaking::EQuickMatchOutcome::MatchmakingOff:      return EQMR_MatchmakingOff;
    case CMatchmaking::EQuickMatchOutcome::ServerUnavailable:   return EQMR_ServerUnavailable;
    case CMatchmaking::EQuickMatchOutcome::AccessKeyRejected:   return EQMR_AccessKeyRejected;
    default:                                                    return EQMR_Failure;
    }
}

void SFXOnlineComponentBlazeGame::QuickMatch_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    NATIVE_GET_OBJECT_OPTIONAL(UOnlineGameSettings, quickMatchSettings, nullptr);
    NATIVE_FINISH();

    USFXOnlineGameSettings* const Settings = quickMatchSettings != nullptr && quickMatchSettings->IsA<USFXOnlineGameSettings>()
        ? static_cast<USFXOnlineGameSettings*>(quickMatchSettings) : nullptr;
    if (Settings == nullptr)
    {
        MILAN_WARNW(ELOG_Matchmaking, L"BlazeGame::QuickMatch called without SFXOnlineGameSettings");
        *(UBOOL*)Result = FALSE;
        return;
    }

    int const MapId = Settings->mME3MapName.Length() > 0
        ? Settings->eventGetMasterMapID(Settings->mME3MapName) : -1;
    int const EnemyModeId = Settings->EnemyType;
    int const DifficultyId = Settings->Difficulty;
    *(UBOOL*)Result = COMPONENT(CMatchmaking).StartQuickMatch(MapId, EnemyModeId, DifficultyId,
        [Context](CMatchmaking::EQuickMatchOutcome const Outcome)
        {
            USFXOnlineComponentBlazeGame_execOnQuickMatchComplete_Parms Parms{};
            Parms.Result = ToQuickMatchResult(Outcome);
            SFXName const Name(L"OnQuickMatchComplete", 0);
            // A script callback may clear its delegate. Snapshot the array before dispatch.
            std::vector<FScriptDelegate> Delegates{};
            for (FScriptDelegate const& Delegate : Context->QuickMatchCompleteDelegates)
                Delegates.push_back(Delegate);
            Context->ProcessDelegate(Name, &Context->__OnQuickMatchComplete__Delegate, &Parms, nullptr);
            for (FScriptDelegate const& Delegate : Delegates)
                Context->ProcessDelegate(Name, &Delegate, &Parms, nullptr);
        });
}

void SFXOnlineComponentBlazeGame::CanStartMPMatch_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //CanStartMPMatch_Original(Context, Stack, Result);

    NATIVE_GET_OBJECT(UOnlineGameSettings, NewGameSettings);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::CanStartMPMatch - NewGameSettings = ...");
    MILAN_CHECKW(NewGameSettings->IsA<USFXOnlineGameSettings>(), L"");

    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::SetCallRestrictedFunctionMode_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //SetCallRestrictedFunctionMode_Original(Context, Stack, Result);

    NATIVE_GET_INT(bEnable);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::SetCallRestrictedFunctionMode - bEnable = %d", bEnable);

    // Ghidra decomp:  does nothing.
}

void SFXOnlineComponentBlazeGame::PerformCallRestrictedFunction_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //PerformCallRestrictedFunction_Original(Context, Stack, Result);

    NATIVE_FINISH();

    //MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::PerformCallRestrictedFunction");

    // Ghidra decomp:  does nothing.
    *(UBOOL*)Result = TRUE;
}

void SFXOnlineComponentBlazeGame::SetPlatformPresence_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //SetPlatformPresence_Original(Context, Stack, Result);

    NATIVE_GET_INT(presencePrivate);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::SetPlatformPresence - presencePrivate = %d", presencePrivate);

    // Ghidra decomp:  does nothing.
}

void SFXOnlineComponentBlazeGame::ShowPopup_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_BREAK_ATTACHED();

    // We'll preserve the code pointer to parse parameters,
    // but leave engine's original implementation to handle the logic.
    BYTE* const OrigInstruction = Stack->Code;

    NATIVE_GET_INT(bodyStr);
    NATIVE_GET_INT(bShowOkBtn);
    NATIVE_GET_INT_OPTIONAL(bModal, FALSE);
    NATIVE_GET_INT_OPTIONAL(bPauseGameplay, FALSE);
    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::ShowPopup - bodyStr = $%d, bShowOkBtn = %d, bModal = %d, bPauseGameplay = %d",
        bodyStr, bShowOkBtn, bModal, bPauseGameplay);

    Stack->Code = OrigInstruction;
    ShowPopup_Original(Context, Stack, Result);
}

void SFXOnlineComponentBlazeGame::OnConnectionError_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //OnConnectionError_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::OnConnectionError");

    // LE3's native returns FALSE (vtable slot 118, a return-0 stub). TRUE means "silent
    // QuickMatch retry" to the script, which then skips its host-left handling.
    *(UBOOL*)Result = COMPONENT(CMatchmaking).On_ConnectionError() ? TRUE : FALSE;
}

void SFXOnlineComponentBlazeGame::CancelFindOnlineGames_Hook(USFXOnlineComponentBlazeGame* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);
    //CancelFindOnlineGames_Original(Context, Stack, Result);

    NATIVE_FINISH();

    MILAN_DEBUGW(ELOG_SFXOnline, L"BlazeGame::CancelFindOnlineGames");
    COMPONENT(CMatchmaking).CancelQuickMatch();

    USFXOnlineComponentBlazeGame_execOnCancelFindOnlineGamesComplete_Parms Parms{};
    Parms.bWasSuccessful = TRUE;

    SFXName const NAME_OnCancelFindOnlineGamesComplete(L"OnCancelFindOnlineGamesComplete", 0);
    Context->ProcessDelegate(NAME_OnCancelFindOnlineGamesComplete, &Context->__OnCancelFindOnlineGamesComplete__Delegate, &Parms, nullptr);

    // Ghidra decomp:  does nothing.
    *(UBOOL*)Result = TRUE;
}
