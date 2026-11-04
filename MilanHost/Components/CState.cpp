#include "Base/IOUtils.hpp"
#include "Base/Streams.hpp"
#include "Components/CCommands.hpp"
#include "Components/CMatchmaking.hpp"
#include "Components/CState.hpp"
#include "Engine/Intercept.hpp"
#include "Online/MatchmakingNet.hpp"
#include "Online/MPProfile.hpp"
#include "Shell/ClientConfig.hpp"


namespace
{
    bool SaveLocalProfile()
    {
        ::RelativeTimer ProfileTimer{};

        auto* const Engine = (*GEngine)->Cast<USFXEngine>();
        if (Engine == nullptr ||
            Engine->MPSaveManager == nullptr ||
            Engine->MPSaveManager->OnlineSave == nullptr ||
            Engine->AccomplishmentManager == nullptr ||
            Engine->AccomplishmentManager->AccomplishmentStorage == nullptr)
        {
            MILAN_WARNW(ELOG_MultiProfile, L"local profile save failed: storage is uninitialized");
            return false;
        }

        MPProfile Profile{ Engine };
        MemoryOutputStream ProfileStream{ 204800 };
        Profile.Serialize(ProfileStream);

        ::RelativeTimer DiskTimer{};
        bool const bSaved = ::TryWriteFileFullAtomic(Profile.GetProfilePath(), ProfileStream.View());

        if (bSaved)
        {
            MILAN_INFOW(ELOG_MultiProfile, L"local profile saved in %.3f ms (disk %.3f ms) (to %s)",
                ProfileTimer.GetMilliseconds(), DiskTimer.GetMilliseconds(), *Profile.GetProfilePath());
        }
        else
        {
            MILAN_WARNW(ELOG_MultiProfile, L"local profile save failed (to %s)",
                *Profile.GetProfilePath());
        }

        return bSaved;
    }

    struct SFXPRIMP_SetComputerName_Parms final
    {
        FString ComputerName;
    };

    bool IsSFXPRIMPSetComputerName(UStruct* const Node)
    {
        static FunctionNamedIntercept Intercept{};
        return Intercept.Applies(Node, L"SFXGameMPContent", L"SFXPRIMP", L"SetComputerName");
    }

    // Suggested player name when the profile has none.
    FString const& WindowsUserName()
    {
        static FString Name{};
        static bool bLoaded = false;
        if (!bLoaded)
        {
            bLoaded = true;
            wchar_t Buffer[256]{};
            DWORD Length = static_cast<DWORD>(std::size(Buffer));
            if (::GetUserNameW(Buffer, &Length))
                Name = FString::Printf(L"%s", Buffer);
        }
        return Name;
    }

    std::string ToUtf8(std::wstring_view const Text)
    {
        if (Text.empty())
            return {};
        int const Length = ::WideCharToMultiByte(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0, nullptr, nullptr);
        std::string Out(static_cast<std::size_t>(Length), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), Out.data(), Length, nullptr, nullptr);
        return Out;
    }

    FString FromUtf8(game::string const& Text)
    {
        FString Out{};
        // Empty custom-server/password values are normal, including with no TOML.
        // The SDK's UTF-8 converter asserts when asked to convert zero bytes.
        if (!Text.empty())
            Out.AppendUtf8(Text.c_str());
        return Out;
    }

    std::wstring_view TrimSpaces(std::wstring_view const Text)
    {
        auto const First = Text.find_first_not_of(L" \t");
        if (First == std::wstring_view::npos)
            return {};
        return Text.substr(First, Text.find_last_not_of(L" \t") - First + 1);
    }

    // MilanNetConfigBridge.ApplySettings results; keep in step with its constants.
    enum ENetConfigApplyResult : int
    {
        NETCONFIG_APPLY_OK                  = 0,
        NETCONFIG_APPLY_INVALID_NAME        = 1,
        NETCONFIG_APPLY_INVALID_SERVER      = 2,
        NETCONFIG_APPLY_SAVE_FAILED         = 3,
        NETCONFIG_APPLY_INVALID_PASSWORD    = 4,
    };

    struct MilanNetConfigBridge_ApplySettings_Parms final
    {
        FString PlayerName;
        int Matchmaking;
        int UseCustomServer;
        FString CustomServer;
        FString ServerPassword;
    };

    // Saves the profile name to MP_Profile and the matchmaking settings to MilanHost.toml, then
    // switches the running game to the selected matchmaker.
    int ApplyNetConfig(MilanNetConfigBridge_ApplySettings_Parms const& Params)
    {
        if (gp_clientConfig == nullptr)
            return NETCONFIG_APPLY_SAVE_FAILED;

        // The screen is read-only outside the multiplayer menu; this covers any other caller.
        if (COMPONENT(CMatchmaking).IsInSession())
        {
            MILAN_WARNW(ELOG_MultiProfile, L"multiplayer settings not saved: other players are connected");
            return NETCONFIG_APPLY_SAVE_FAILED;
        }

        std::wstring_view const NameView = TrimSpaces(Params.PlayerName);
        if (NameView.empty())
            return NETCONFIG_APPLY_INVALID_NAME;
        FString Name{};
        Name.Append(NameView.data(), NameView.data() + NameView.size());

        ClientConfig Next = *gp_clientConfig;
        std::string const Url = ToUtf8(TrimSpaces(Params.CustomServer));
        std::string const Password = ToUtf8(Params.ServerPassword);
        Next.MatchmakerEnabled = Params.Matchmaking != 0;
        // The default server is never stored as a custom one, so the screen never shows it.
        bool const bDefaultUrl = Url == k_defaultMatchmakerUrl;
        Next.MatchmakerCustom = Params.UseCustomServer != 0 && !bDefaultUrl;
        Next.MatchmakerCustomUrl = bDefaultUrl ? "" : Url.c_str();
        Next.MatchmakerPassword = Password.c_str();
        if (Next.MatchmakerEnabled && Next.MatchmakerCustom)
        {
            if (!MatchmakingNet::IsValidBaseUrl(Url))
                return NETCONFIG_APPLY_INVALID_SERVER;
            if (!MatchmakingNet::IsValidAccessKey(Password))
                return NETCONFIG_APPLY_INVALID_PASSWORD;
        }
        Next.MatchmakerUrl = Next.SelectedMatchmakerUrl();

        bool const bMatchmakingChanged = Next.MatchmakerEnabled != gp_clientConfig->MatchmakerEnabled
            || Next.MatchmakerCustom != gp_clientConfig->MatchmakerCustom
            || Next.MatchmakerCustomUrl != gp_clientConfig->MatchmakerCustomUrl
            || Next.MatchmakerPassword != gp_clientConfig->MatchmakerPassword;
        if (bMatchmakingChanged)
        {
            game::string Error{};
            if (!Next.TryWriteMatchmaker(k_configFilename, Error))
            {
                MILAN_WARNW(ELOG_Matchmaking, L"could not save matchmaking settings to MilanHost.toml: %S", Error.c_str());
                return NETCONFIG_APPLY_SAVE_FAILED;
            }

            bool const bServerChanged = Next.MatchmakerUrl != gp_clientConfig->MatchmakerUrl
                || Next.SelectedAccessKey() != gp_clientConfig->SelectedAccessKey();
            *gp_clientConfig = Next;
            if (bServerChanged)
                COMPONENT(CMatchmaking).ApplySettings(Next.MatchmakerUrl.c_str(), Next.SelectedAccessKey().c_str());
        }

        if (!MPProfile::GetPlayerName().Equals(Name))
        {
            FString const Previous = MPProfile::GetPlayerName();
            MPProfile::SetPlayerName(Name);
            if (!SaveLocalProfile())
            {
                MPProfile::SetPlayerName(Previous);
                return NETCONFIG_APPLY_SAVE_FAILED;
            }
            MILAN_INFOW(ELOG_MultiProfile, L"profile name changed to %s", *Name);
        }
        return NETCONFIG_APPLY_OK;
    }

    // SFXGameContent_Milan.MilanNetConfigBridge: the multiplayer settings screen's reads and saves.
    // Returns false for any other function.
    bool HandleNetConfigBridge(FFrame* const Stack, void* const Result)
    {
        // Cheap class check first; this runs for every script call.
        static SFXName const k_class{ L"MilanNetConfigBridge", 0 };
        static SFXName const k_package{ L"SFXGameContent_Milan", 0 };
        UStruct* const Node = Stack->Node;
        if (Node->Outer == nullptr || !(Node->Outer->Name == k_class))
            return false;
        UPackage* const Package = Node->GetOutermost();
        if (Package == nullptr || !(Package->Name == k_package))
            return false;

        static SFXName const k_isAvailable{ L"IsAvailable", 0 };
        static SFXName const k_canApplySettings{ L"CanApplySettings", 0 };
        static SFXName const k_getPlayerName{ L"GetPlayerName", 0 };
        static SFXName const k_getDefaultPlayerName{ L"GetDefaultPlayerName", 0 };
        static SFXName const k_isMatchmakingEnabled{ L"IsMatchmakingEnabled", 0 };
        static SFXName const k_isCustomServer{ L"IsCustomServer", 0 };
        static SFXName const k_getCustomServer{ L"GetCustomServer", 0 };
        static SFXName const k_getServerPassword{ L"GetServerPassword", 0 };
        static SFXName const k_applySettings{ L"ApplySettings", 0 };

        auto const ReturnBool = [Result](bool const bValue) { if (Result != nullptr) *static_cast<UBOOL*>(Result) = bValue ? TRUE : FALSE; };
        auto const ReturnString = [Result](FString const& Value) { if (Result != nullptr) *static_cast<FString*>(Result) = Value; };

        SFXName const& Function = Node->Name;
        ClientConfig const* const Config = gp_clientConfig;
        if (Function == k_isAvailable)
            ReturnBool(Config != nullptr);
        else if (Function == k_canApplySettings)
            ReturnBool(Config != nullptr && !COMPONENT(CMatchmaking).IsInSession());
        else if (Function == k_getPlayerName)
            ReturnString(MPProfile::GetPlayerName());
        else if (Function == k_getDefaultPlayerName)
            ReturnString(WindowsUserName());
        else if (Function == k_isMatchmakingEnabled)
            ReturnBool(Config == nullptr || Config->MatchmakerEnabled);
        else if (Function == k_isCustomServer)
            ReturnBool(Config != nullptr && Config->MatchmakerCustom);
        else if (Function == k_getCustomServer)
            ReturnString(Config != nullptr ? FromUtf8(Config->MatchmakerCustomUrl) : FString{});
        else if (Function == k_getServerPassword)
            ReturnString(Config != nullptr ? FromUtf8(Config->MatchmakerPassword) : FString{});
        else if (Function == k_applySettings)
        {
            int const Applied = ApplyNetConfig(*reinterpret_cast<MilanNetConfigBridge_ApplySettings_Parms const*>(Stack->Locals));
            if (Result != nullptr)
                *static_cast<int*>(Result) = Applied;
        }
        else
            return false;
        return true;
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

    // BaseDelta's wrapper dispatches the normal completion delegate only after this
    // bridge returns. An unhooked bridge returns false rather than claiming a save.
    // BaseDelta compiles it into SFXGame.pcc under its own SFXGameContent_MilanBase package.
    static FunctionNamedIntercept MilanProfileSaveBridge_WriteProfile{};
    if (MilanProfileSaveBridge_WriteProfile.Applies(Stack->Node,
        L"SFXGameContent_MilanBase", L"MilanProfileSaveBridge", L"WriteProfile"))
    {
        if (Result != nullptr)
        {
            *static_cast<UBOOL*>(Result) = SaveLocalProfile() ? TRUE : FALSE;
        }

        return false;
    }

    if (HandleNetConfigBridge(Stack, Result))
        return false;

    // Replace the offline online-subsystem placeholder with the MP_Profile player name,
    // or the Windows account name when the profile has none.
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
            else if (!WindowsUserName().Empty())
            {
                Params->ComputerName = WindowsUserName();
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

    // Older packages lack the local completion bridge. Retain their disk-save hooks;
    // patched packages have already committed before dispatching their callbacks.
    static FunctionStructIntercept SFXSaveManagerMP_SaveRecordsImpl{};
    static FunctionStructIntercept SFXAccomplishmentStorage_Write{};
    bool const bSaveRecords = SFXSaveManagerMP_SaveRecordsImpl.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.SaveRecordsImpl");
    bool const bWriteAccomplishments = SFXAccomplishmentStorage_Write.Applies(Stack->Node, L"Function SFXGame.SFXAccomplishmentStorage.Write");
    // MilanProfileSaveBridge is in SFXGame, so it won't unload
    // So once we find class pointer we can just cache it
    static UClass* s_profileSaveBridge = nullptr;
    if ((bSaveRecords || bWriteAccomplishments) && s_profileSaveBridge == nullptr) {
        s_profileSaveBridge = HijackUtils::FindClass(SFXName(L"MilanProfileSaveBridge", 0));
    }
    if ((bSaveRecords || bWriteAccomplishments) && s_profileSaveBridge == nullptr)
    {
        MILAN_TRACEW(ELOG_MultiProfile, L"profile save after %s", bSaveRecords
            ? L"SFXSaveManagerMP.SaveRecordsImpl" : L"SFXAccomplishmentStorage.Write");

        SaveLocalProfile();
    }

    // This needs to be done after ProcessInternal is called so that OnlineSave and friends will have been created.
    static FunctionStructIntercept SFXSaveManagerMP_Initialize{};
    if (SFXSaveManagerMP_Initialize.Applies(Stack->Node, L"Function SFXGame.SFXSaveManagerMP.Initialize"))
    {
        MILAN_TRACEW(ELOG_MultiProfile, L"intercepted SFXGame.SFXSaveManagerMP.Initialize:");

        MPProfile Profile{ (*GEngine)->Cast<USFXEngine>() };
        Profile.PrepareCharacterStorage();

        FString const ProfilePath = MPProfile::GetLoadProfilePathStatic();
        ::game::vector<BYTE> ProfileBuffer{};

        if (!::ReadFileFull(ProfilePath, ProfileBuffer))
        {
            MILAN_WARNW(ELOG_MultiProfile, L"profile '%s' is empty or missing, skipping deserialization", *ProfilePath);
            return;
        }

        ::RelativeTimer ProfileTimer{};

        MemoryInputStream ProfileStream{ std::move(ProfileBuffer) };
        Profile.Deserialize(ProfileStream);

        MILAN_INFOW(ELOG_MultiProfile, L"profile loaded from %s in %.3f ms", *ProfilePath, ProfileTimer.GetMilliseconds());
    }
}

void CState::On_UWorld_Tick(UWorld* const Context, int const Unknown, float const Delta)
{
    MILAN_UNUSED_3(Context, Unknown, Delta);
}
