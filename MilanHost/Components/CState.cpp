#include "Base/IOUtils.hpp"
#include "Base/Streams.hpp"
#include "Components/CCommands.hpp"
#include "Components/CState.hpp"
#include "Engine/Intercept.hpp"
#include "Online/MPProfile.hpp"


namespace
{
    struct SFXPRIMP_SetComputerName_Parms final
    {
        FString ComputerName;
    };

    bool IsSFXPRIMPSetComputerName(UStruct* const Node)
    {
        static FunctionNamedIntercept Intercept{};
        return Intercept.Applies(Node, L"SFXGameMPContent", L"SFXPRIMP", L"SetComputerName");
    }
}


// ! CState implementation.
// ========================================

bool CState::Initialize(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CState::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);

    FunctionStructIntercept::CleanupRegistered();
}

void CState::On_VectoredException(void* const Address, DWORD const Code, _EXCEPTION_POINTERS* const Exceptions)
{
    MILAN_UNUSED_3(Address, Code, Exceptions);
}

bool CState::On_UObject_ProcessInternal_Pre(UObject* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);

    // Replace the offline online-subsystem placeholder with the MP_Profile player name,
    // or the Windows account name when the profile has none.
    static FString CachedWindowsUserName{};
    static bool bWindowsUserNameLoaded = false;
    static bool bLoggedWindowsUserName = false;
    static bool bLoggedProfilePlayerName = false;
    if (IsSFXPRIMPSetComputerName(Stack->Node))
    {
        auto* const Params = reinterpret_cast<SFXPRIMP_SetComputerName_Parms*>(Stack->Locals);
        if (Params != nullptr && (Params->ComputerName == L"Player" || Params->ComputerName == L"Local Profile"))
        {
            FString const& ProfilePlayerName = MPProfile::GetPlayerName();
            if (!ProfilePlayerName.Empty())
            {
                Params->ComputerName = ProfilePlayerName;
                if (!bLoggedProfilePlayerName)
                {
                    bLoggedProfilePlayerName = true;
                    MILAN_TRACEW(ELOG_MultiProfile, L"using MP_Profile name for lobby player name: %s",
                                 *Params->ComputerName);
                }
            }
            else if (!bWindowsUserNameLoaded)
            {
                bWindowsUserNameLoaded = true;
                wchar_t WindowsUserName[256]{};
                DWORD WindowsUserNameLength = static_cast<DWORD>(std::size(WindowsUserName));
                if (::GetUserNameW(WindowsUserName, &WindowsUserNameLength))
                    CachedWindowsUserName = FString::Printf(L"%s", WindowsUserName);
            }

            if (ProfilePlayerName.Empty() && !CachedWindowsUserName.Empty())
            {
                Params->ComputerName = CachedWindowsUserName;
                if (!bLoggedWindowsUserName)
                {
                    bLoggedWindowsUserName = true;
                    MILAN_TRACEW(ELOG_MultiProfile, L"using Windows account name for lobby player name: %s",
                                 *Params->ComputerName);
                }
            }
        }
    }

    // LMP's challenge reward uses profile setting 92, but the offline profile may not
    // have a cached row for it. Add the row before the game's setter updates the cache.
    static FunctionStructIntercept SFXAccomplishmentStorage_SetCurrentReward{};
    if (SFXAccomplishmentStorage_SetCurrentReward.Applies(
            Stack->Node, L"Function SFXGame.SFXAccomplishmentStorage.SetCurrentReward"))
    {
        auto* const AccomplishmentStorage = Context->Cast<USFXAccomplishmentStorage>();
        if (AccomplishmentStorage != nullptr && AccomplishmentStorage->CachedOnlinePlayerStorage != nullptr)
            AccomplishmentStorage->CachedOnlinePlayerStorage->AddSettingInt(CSF_CurrentReward);
        else
            MILAN_WARNW(ELOG_MultiProfile, L"cannot add current reward profile setting before storage is initialized");
    }

    return true;
}

void CState::On_UObject_ProcessInternal_Post(UObject* const Context, FFrame* const Stack, void* const Result)
{
    MILAN_UNUSED_3(Context, Stack, Result);

    // The lobby reads DisplayName, while in-match UI reads PlayerReplicationInfo.PlayerName.
    if (IsSFXPRIMPSetComputerName(Stack->Node))
    {
        auto* const Params = reinterpret_cast<SFXPRIMP_SetComputerName_Parms*>(Stack->Locals);
        auto* const PRI = Context != nullptr ? Context->Cast<ASFXPRI_NativeBase>() : nullptr;
        if (Params != nullptr && PRI != nullptr && !Params->ComputerName.Empty())
        {
            PRI->eventSetPlayerName(Params->ComputerName);
            static bool bLoggedPlayerName = false;
            if (!bLoggedPlayerName)
            {
                bLoggedPlayerName = true;
                MILAN_TRACEW(ELOG_MultiProfile, L"set in-game PlayerName to %s", *PRI->PlayerName);
            }
        }
    }

    // Save after kit strings are refreshed or accomplishment storage writes a new reward.
    static FunctionStructIntercept SFXSaveManagerMP_SaveRecordsImpl{};
    static FunctionStructIntercept SFXAccomplishmentStorage_Write{};
    bool const bSaveRecords = SFXSaveManagerMP_SaveRecordsImpl.Applies(
        Stack->Node, L"Function SFXGame.SFXSaveManagerMP.SaveRecordsImpl");
    bool const bWriteAccomplishments = SFXAccomplishmentStorage_Write.Applies(
        Stack->Node, L"Function SFXGame.SFXAccomplishmentStorage.Write");
    if (bSaveRecords || bWriteAccomplishments)
    {
        MILAN_TRACEW(ELOG_MultiProfile, L"profile save after %s", bSaveRecords
            ? L"SFXSaveManagerMP.SaveRecordsImpl" : L"SFXAccomplishmentStorage.Write");

        USFXEngine* Engine = (*GEngine)->Cast<USFXEngine>();
        if (Engine == nullptr || Engine->MPSaveManager == nullptr ||
            Engine->MPSaveManager->OnlineSave == nullptr ||
            Engine->AccomplishmentManager == nullptr ||
            Engine->AccomplishmentManager->AccomplishmentStorage == nullptr)
        {
            MILAN_WARNW(ELOG_MultiProfile, L"profile save skipped while storage is uninitialized");
            return;
        }

        MPProfile Profile{ Engine };
        ::RelativeTimer ProfileTimer{};

        MemoryOutputStream ProfileStream{ 204800 };  // reserve 200kb
        Profile.Serialize(ProfileStream);

        MILAN_TRACEW(ELOG_MultiProfile, L"profile serialized in %.3f ms.", ProfileTimer.GetMilliseconds());
        ::WriteFileFull(static_cast<std::wstring_view>(Profile.GetProfilePath()), ProfileStream.View());
    }

    // This needs to be done after ProcessInternal is called so that OnlineSave and friends will have been created.
    static FunctionStructIntercept SFXSaveManagerMP_Initialize{};
    if (SFXSaveManagerMP_Initialize.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.Initialize"))
    {
        MILAN_TRACEW(ELOG_MultiProfile, L"intercepted SFXGame.SFXSaveManagerMP.Initialize:");

        MPProfile Profile{ (*GEngine)->Cast<USFXEngine>() };
        // Fresh profiles need all local kit slots before their first save.
        Profile.PrepareCharacterStorage();

        auto profilePath = Profile.GetProfilePath();

        ::game::vector<BYTE> ProfileBuffer{};

        if (!::ReadFileFull(static_cast<std::wstring_view>(profilePath), ProfileBuffer))
        {
            MILAN_WARNW(ELOG_MultiProfile, L"online profile file '%s' is empty or missing, skipping deserialization", profilePath.Chars());
            return;
        }

        ::RelativeTimer ProfileTimer{};

        MemoryInputStream ProfileStream{ std::move(ProfileBuffer) };
        Profile.Deserialize(ProfileStream);
        MILAN_TRACEW(ELOG_MultiProfile, L"profile deserialized in %.3f ms.", ProfileTimer.GetMilliseconds());
    }
}

void CState::On_UWorld_Tick(UWorld* const Context, int const Unknown, float const Delta)
{
    MILAN_UNUSED_3(Context, Unknown, Delta);
}
