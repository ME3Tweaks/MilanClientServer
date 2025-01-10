#include "Components/CCommands.hpp"
#include "Components/CProfilers.hpp"
#include "Engine/ExtObject.hpp"
#include "Engine/Intercept.hpp"
#include "NetProf/NetProfStream.hpp"


// ! CProfilers implementation.
// ========================================

bool CProfilers::Initialize(::LESDK::Initializer& Context)
{
    SetupProfilerRecords();
    RegisterCommands();

    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CProfilers::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);
}

void CProfilers::On_VectoredException(void* const Address, DWORD const Code, _EXCEPTION_POINTERS* const Exceptions)
{
    MILAN_UNUSED_3(Address, Code, Exceptions);
}

void CProfilers::On_UObject_ProcessEvent(UObject* const Context, UFunction* const Function, void* const Parms, void* const Result)
{
    MILAN_UNUSED_4(Context, Function, Parms, Result);

    static FunctionStructIntercept GameViewportClient_PostRender{};
    if (GameViewportClient_PostRender.Applies(Function, L"Function Engine.GameViewportClient.PostRender"))
    {
        auto* const TParms = reinterpret_cast<UGameViewportClient_eventPostRender_Parms*>(Parms);

        m_activeCanvas.UpdateCanvas(TParms->Canvas);
        m_activeCanvas.UpdateGlobals((*GEngine)->Cast<USFXEngine>());
        m_activeCanvas.Reset();

        UObject* const Target = nullptr;

        if (m_profilerMode != CPM_None)
        {
            MILAN_CHECKW(m_profilerMode >= 0 && m_profilerMode < CPM_Max, L"");
            auto const& Profiler = m_profilerRecords[m_profilerMode];

            m_activeCanvas.DrawHeader(Profiler.Subtitle, Profiler.Description, Target, MilanCanvasDrawer::k_warnColor);
            if (Profiler.Function) (*Profiler.Function)(m_activeCanvas, Target);
        }
    }
}

void CProfilers::SetupProfilerRecords()
{
    m_profilerRecords[CPM_None] = {
        .Subtitle = L"",
        .Description = L"",
        .Function = nullptr,
    };

    m_profilerRecords[CPM_PendingPackages] = {
        .Subtitle = L"Pending packages",
        .Description = L"Displays pending packages in the active server connection",
        .Function = ProfilePendingPackages,
    };

    m_profilerRecords[CPM_PlayerControllers] = {
        .Subtitle = L"Player controllers",
        .Description = L"Displays active APlayerController instances",
        .Function = ProfilePlayerControllers,
    };

    m_profilerRecords[CPM_Network] = {
        .Subtitle = L"Network",
        .Description = L"Displays active channels and related networking info",
        .Function = ProfileNetwork,
    };

    m_profilerRecords[CPM_WorldInfo] = {
        .Subtitle = L"WorldInfo",
        .Description = L"Displays information about active world",
        .Function = ProfileWorldInfo,
    };
}

void CProfilers::RegisterCommands()
{
    auto& Commands = COMPONENT(CCommands);

    Commands.RegisterCommand(L"dbg", L"nextprofile",
        [&](wchar_t const*)
        {
            auto NextMode = (ECanvasProfilerMode)((int)m_profilerMode + 1);
            if (NextMode >= CPM_Max) NextMode = CPM_None;
            m_profilerMode = NextMode;
            return true;
        });

    Commands.RegisterCommand(L"dbg", L"profile",
        [&](wchar_t const* const Params)
        {
            //MILAN_INFOW(L"dbg.profile: params = %s (%d)",
            //    Params ? Params : L"(null)",
            //    Params ? (int)::wcslen(Params) : -1);

            if (!Params || ::wcslen(Params) == 0)
            {
                MILAN_WARNW(L"empty dbg.profile parameter");
                return true;
            }

            if (!::_wcsicmp(Params, L"none"))
                m_profilerMode = CPM_None;
            else if (!::_wcsicmp(Params, L"pending") || !::_wcsicmp(Params, L"pend"))
                m_profilerMode = CPM_PendingPackages;
            else if (!::_wcsicmp(Params, L"playercontrollers") || !::_wcsicmp(Params, L"pcs"))
                m_profilerMode = CPM_PlayerControllers;
            else if (!::_wcsicmp(Params, L"network") || !::_wcsicmp(Params, L"net"))
                m_profilerMode = CPM_Network;
            else if (!::_wcsicmp(Params, L"worldinfo") || !::_wcsicmp(Params, L"wi"))
                m_profilerMode = CPM_WorldInfo;
            else
                MILAN_WARNW(L"unknown dbg.profile parameter: %s", Params);

            return true;
        });

    Commands.RegisterCommand(L"dump", L"pckgmap",
        [](wchar_t const*)
        {
            MILAN_DEBUGW(L"dumping package maps for active connections");
            for (TypedObjectIterator<UPackageMap, true, false> Iter; Iter; ++Iter)
            {
                MILAN_DEBUGW(L"  %s", *(*Iter)->StaticFullName());
                //auto const Packages = reinterpret_cast<UPackageMap_Proxy*>(*Iter);
                for (FPackageInfo const& Package : (*Iter)->List)
                {
                    MILAN_UNUSED(Package);
                    MILAN_DEBUGW(L"    %s, guid = %s, gen = %d / %d",
                        *Package.PackageName.ToString(), *Package.Guid.ToString(),
                        Package.LocalGeneration, Package.RemoteGeneration);
                }
            }

            return true;
        });
}


// ! Implementation of canvas profilers.
// ========================================

wchar_t const* NetModeString(unsigned char const Mode)
{
    if (Mode == NM_Standalone)          return L"Standalone";
    if (Mode == NM_Client)              return L"Client";
    if (Mode == NM_ListenServer)        return L"ListenServer";
    if (Mode == NM_DedicatedServer)     return L"DedicatedServer";

    return L"???";
}

wchar_t const* NetRoleString(unsigned char const Role)
{
    if (Role == ROLE_None)              return L"ROLE_None";
    if (Role == ROLE_SimulatedProxy)    return L"ROLE_SimulatedProxy";
    if (Role == ROLE_AutonomousProxy)   return L"ROLE_AutonomousProxy";
    if (Role == ROLE_Authority)         return L"ROLE_Authority";

    return L"???";
}

void CProfilers::ProfilePendingPackages(MilanCanvasDrawer& Drawer, UObject* Target)
{
    MILAN_UNUSED_2(Drawer, Target);

    auto const NetDriver = (*GWorld)->NetDriver;
    if (NetDriver == nullptr)
    {
        // This is quite weird...
        return;
    }

    for (FPackageInfo const& Pending : NetDriver->ServerConnection->PendingPackageInfos)
    {
        Drawer.DrawString(Pending.PackageName.ToString());
        Drawer.PushIndent();
        Drawer.DrawProperty(L"Guid", *Pending.Guid.ToString());
        Drawer.DrawProperty(L"Parent", Pending.Parent ? Pending.Parent->GetFullName() : L"n/a");
        Drawer.DrawProperty(L"ForcedBase", Pending.ForcedExportBasePackageName.ToString());
        Drawer.PopIndent();
    }
}

void CProfilers::ProfilePlayerControllers(MilanCanvasDrawer& Drawer, UObject* Target)
{
    MILAN_UNUSED_2(Drawer, Target);

    Drawer.SetBackground(FColor{ .B = 0, .G = 0, .R = 0, .A = 25 });

    for (TypedObjectIterator<APlayerController, true, false> Iter; Iter; ++Iter)
    {
        APlayerController* const Controller = *Iter;
        Drawer.DrawString(Controller->StaticFullName());
        Drawer.PushIndent();
        Drawer.DrawProperty(L"Pawn", Controller->Pawn ? Controller->Pawn->StaticFullName() : L"n/a");
        Drawer.DrawProperty(L"Player", Controller->Player ? Controller->Player->StaticFullName() : L"n/a");
        Drawer.DrawProperty(L"PRI", Controller->PlayerReplicationInfo ? Controller->PlayerReplicationInfo->StaticFullName() : L"n/a");
        if (Controller->PlayerReplicationInfo != nullptr)
        {
            Drawer.DrawProperty(L"PRI.Owner", Controller->PlayerReplicationInfo->Owner ? Controller->PlayerReplicationInfo->Owner->StaticFullName() : L"n/a");
            //Drawer.DrawProperty(L"PRI.UniqueId", FString::Printf(L"%u:%u", Controller->PlayerReplicationInfo->UniqueId.Uid.A, Controller->PlayerReplicationInfo->UniqueId.Uid.B));
            Drawer.DrawProperty(L"PRI.SavedAddress", Controller->PlayerReplicationInfo->SavedNetworkAddress);
        }
        Drawer.DrawProperty(L"Actor", FString::Printf(L"bDeleteMe=%d, bStatic=%d, bGameRelevant=%d",
            !!Controller->bDeleteMe, !!Controller->bStatic, !!Controller->bGameRelevant));
        Drawer.DrawProperty(L"Actor.Owner", Controller->Owner ? Controller->Owner->StaticFullName() : L"n/a");
        Drawer.DrawProperty(L"Actor.WI", Controller->WorldInfo ? Controller->WorldInfo->StaticFullName() : L"n/a");
        Drawer.DrawProperty(L"Net", FString::Printf(L"Role=%s, RemoteRole=%s",
            NetRoleString(Controller->Role), NetRoleString(Controller->RemoteRole)));
        Drawer.DrawProperty(L"Net", FString::Printf(L"NetTag=%d, NetUpdateTime=%f, NetUpdateFrequency=%f, NetPriority=%f, LastNetUpdateTime=%f",
            Controller->NetTag, Controller->NetUpdateTime, Controller->NetUpdateFrequency, Controller->NetPriority, Controller->LastNetUpdateTime));

        Drawer.PopIndent();
    }

    Drawer.DrawSeparator();

    auto const First = (*GWorld)->PersistentLevel->Actors(0);

    Drawer.DrawString(L"controller chain on persistent level:");
    Drawer.PushIndent();

    if (auto const WorldInfo = First->Cast<AWorldInfo>(); WorldInfo)
    {
        for (AController* Controller = WorldInfo->ControllerList;
            Controller != nullptr; Controller = Controller->NextController)
        {
            auto Pawn = Controller->Pawn ? Controller->Pawn->GetName() : L"n/a";
            auto Player = FString(L"n/a");

            if (auto const BioPlayerController = Controller->Cast<ABioPlayerController>(); BioPlayerController)
                Player = BioPlayerController->Player ? BioPlayerController->Player->GetName() : L"n/a";

            Drawer.DrawString(*FString::Printf(L"%s, Pawn = %s, Player = %s", *Controller->GetName(), *Pawn, *Player));
        }
    }
    else
    {
        Drawer.PushColor(255, 0, 0, 255);
        Drawer.DrawString(L"first actor in persistent level is not a world info");
        Drawer.PopColor();
    }

    Drawer.PopIndent();
}

void CProfilers::ProfileNetwork(MilanCanvasDrawer& Drawer, UObject* const Target)
{
    MILAN_UNUSED_2(Drawer, Target);

    auto* const Engine = (*GEngine)->Cast<USFXEngine>();
    auto* const WorldInfo = Engine->GetRealWorldInfo();

    if (WorldInfo == nullptr)
    {
        Drawer.PushColor(Drawer.k_warnColor);
        Drawer.DrawString(L"WorldInfo not available, aborting...");
        Drawer.PopColor();
        return;
    }

    Drawer.SetBackground(FColor{ .B = 0, .G = 0, .R = 0, .A = 25 });

    // WorldInfo

    Drawer.DrawString(FString::Printf(L"WorldInfo: %s", *WorldInfo->StaticName()));

    Drawer.PushIndent();
    Drawer.DrawProperty(L"URL", WorldInfo->GetLocalURL());
    //Drawer.DrawProperty(L"World", (*GWorld)->StaticFullName());
    Drawer.DrawProperty(L"GameInfo", WorldInfo->Game ? WorldInfo->Game->StaticName() : L"n/a");
    Drawer.DrawProperty(L"ReplicationInfo", WorldInfo->GRI ? WorldInfo->GRI->StaticName() : L"n/a");
    Drawer.DrawProperty(L"NetMode", FString::Printf(L"%d (%s)", WorldInfo->NetMode, NetModeString(WorldInfo->NetMode)));
    Drawer.PopIndent();

    //Drawer.DrawSeparator();

    // NetDriver

    auto* const NetDriver = (*GWorld)->NetDriver;

    //Drawer.DrawString(FString::Printf(L"World: %s", *World->StaticName()));
    Drawer.DrawString(FString::Printf(L"NetDriver: %s", NetDriver ? *NetDriver->StaticName() : L"n/a"));

    if (NetDriver && WorldInfo->NetMode == NM_ListenServer)
    {
        Drawer.PushIndent();

        for (UINT i = 0u; i < NetDriver->ChildConnections.Count(); ++i)
        {
            auto* const ChildConn = NetDriver->ChildConnections(i);

            Drawer.DrawString(FString::Printf(L"ChildConnections(%d): %s", i, *ChildConn->StaticName()));

            Drawer.PushIndent();
            Drawer.DrawProperty(L"Viewer", ChildConn->Viewer ? ChildConn->Viewer->StaticFullName() : L"n/a");
            //Drawer.DrawProperty(L"ClientWorldPackageName", ChildConn->ClientWorldPackageName.ToString());
            Drawer.DrawString(L"Actor channels");
            unsigned int ChannelIndex = 0;
            for (auto const& [Actor, Channel] : ChildConn->ActorChannels)
            {
                Drawer.PushIndent();
                MILAN_CHECKW(Channel != nullptr, L"");
                DrawActorChannel(Drawer, Channel, ChannelIndex++);
                Drawer.PopIndent();

                if (Actor != Channel->Actor)
                {
                    MILAN_WARNW(L"server has actor channel with actor != channel->actor");
                    MILAN_WARNW(L"  actor = %s", Actor ? *Actor->StaticFullName() : L"n/a");
                    MILAN_WARNW(L"  channel->actor = %s", Channel->Actor ? *Channel->Actor->StaticFullName() : L"n/a");
                }
            }
            Drawer.DrawString(L"Client visible levels");
            unsigned int VisibleIndex = 0;
            for (auto const VisibleLevelName : ChildConn->ClientVisibleLevelNames)
            {
                Drawer.PushIndent();
                Drawer.DrawString(*FString::Printf(L"%d. %s", VisibleIndex++, *VisibleLevelName.ToString()));
                Drawer.PopIndent();
            }
            Drawer.DrawString(L"Pending packages");
            unsigned int PendingIndex = 0;
            for (auto const& PendingPackage : ChildConn->PendingPackageInfos)
            {
                Drawer.PushIndent();
                Drawer.DrawProperty(FString::Printf(L"%d", PendingIndex++),
                    FString::Printf(L"Name=%s, Parent=%s, Guid=%s",
                    *PendingPackage.PackageName.ToString(),
                    PendingPackage.Parent ? *PendingPackage.Parent->StaticName() : L"n/a",
                    *PendingPackage.Guid.ToString()));
                Drawer.PopIndent();
            }
            Drawer.PopIndent();
        }

        Drawer.PopIndent();
    }
    else if (NetDriver && NetDriver->ServerConnection)
    {
        Drawer.PushIndent();

        Drawer.DrawProperty(L"Viewer", NetDriver->ServerConnection->Viewer ? NetDriver->ServerConnection->Viewer->StaticFullName() : L"n/a");

        Drawer.DrawString(L"ServerConnection:");
        Drawer.PushIndent();

        Drawer.DrawString(L"Actor channels");
        unsigned int ChannelIndex = 0;
        for (auto const& [Actor, Channel] : NetDriver->ServerConnection->ActorChannels)
        {
            Drawer.PushIndent();
            MILAN_CHECKW(Channel != nullptr, L"");
            DrawActorChannel(Drawer, Channel, ChannelIndex++);
            Drawer.PopIndent();

            if (Actor != Channel->Actor)
            {
                MILAN_WARNW(L"client has actor channel with actor != channel->actor");
                MILAN_WARNW(L"  actor = %s", Actor ? *Actor->StaticFullName() : L"n/a");
                MILAN_WARNW(L"  channel->actor = %s", Channel->Actor ? *Channel->Actor->StaticFullName() : L"n/a");
            }
        }

        Drawer.PopIndent();
        Drawer.PopIndent();
    }
}

void CProfilers::ProfileWorldInfo(MilanCanvasDrawer& Drawer, UObject* const Target)
{
    MILAN_UNUSED_2(Drawer, Target);
}

void CProfilers::DrawActorChannel(MilanCanvasDrawer& Drawer, UActorChannel* const Channel, unsigned int const Index)
{
    static FColor s_flagsActorColor{ .B = 210, .G = 210, .R = 200, .A = 255 };
    static FColor s_flagsBaseColor{ .B = 255, .G = 185, .R = 185, .A = 255 };
    static FColor s_flagsDerivedColor{ .B = 185, .G = 255, .R = 185, .A = 255 };

    //// I'd very much like to know what's supposed to be in here...
    //MILAN_CHECKW(Channel->Unknown_114.Count() == 0, L"");

    Drawer.DrawString(FString::Printf(L"Channel %d: %s", Index, Channel->Actor ? *Channel->Actor->StaticFullName() : L"n/a"), 0.8f);
    Drawer.PushIndent();

    // > Replicated properties.
    // ----------------------------------------

    //unsigned int PropertyIndex = 0;
    //for (auto const& [Offset, Property] : Channel->ReplicatedProperties)
    //{
    //    Drawer.DrawString(FString::Printf(L"%d. %s -> %d", PropertyIndex++, Property ? *Property->StaticName() : L"n/a", Offset));
    //}

    // > Relevancy timer.
    // ----------------------------------------

    //auto* const NetDriver = Channel->World->NetDriver;
    //Drawer.DrawProperty(L"relevancy timer", FString::Printf(L"Time=%f, RelevantTime=%f, RelevantTimeout=%f",
    //    NetDriver->Time, Channel->RelevantTime, NetDriver->RelevantTimeout));

    // > Actors and ownerships.
    // ----------------------------------------

    //Drawer.DrawProperty(L"Actor->Owner", (Channel->Actor && Channel->Actor->Owner) ? Channel->Actor->Owner->StaticFullName() : L"n/a");
    //Drawer.DrawProperty(L"Connection->Actor", (Channel->Connection && Channel->Connection->Actor) ? Channel->Connection->Actor->StaticFullName() : L"n/a");

    // > Channel actor flags.
    // ----------------------------------------

    //if (AActor* Actor = Channel->Actor)
    //{
    //    Drawer.PushColor(s_flagsActorColor);
    //    Drawer.DrawProperty(L"Channel actor", FString::Printf(L"Role=%s, RemoteRole=%s, bNetInitial=%d, bNetOwner=%d, bGameRelevant=%d, bNetTemporary=%d, bNetDirty=%d, SkipPropRepl=%d",
    //        NetRoleString(Actor->Role), NetRoleString(Actor->RemoteRole), !!Actor->bNetInitial, !!Actor->bNetOwner, !!Actor->bGameRelevant, !!Actor->bNetTemporary, !!Actor->bNetDirty, !!Actor->bSkipActorPropertyReplication));
    //    Drawer.PopColor();
    //}

    // > Channel flags.
    // ----------------------------------------

    //Drawer.PushColor(s_flagsBaseColor);
    //Drawer.DrawProperty(L"Flags (base)", FString::Printf(L"bOpenAcked=%d, bClosing=%d, bOpenTemporary=%d, bBroken=%d, bTornOff=%d",
    //    !!Channel->bOpenAcked, !!Channel->bClosing, !!Channel->bOpenTemporary, !!Channel->bBroken, !!Channel->bTornOff));
    //Drawer.PopColor();
    //Drawer.PushColor(s_flagsDerivedColor);
    //Drawer.DrawProperty(L"Flags (derived)", FString::Printf(L"SpawnAcked=%d, ActorDirty=%d, MustStayDirty=%d, StillInitial=%d, IsReplicating=%d",
    //    !!Channel->SpawnAcked, !!Channel->ActorDirty, !!Channel->bActorMustStayDirty, !!Channel->bActorStillInitial, !!Channel->bIsReplicatingActor));
    //Drawer.PopColor();

    Drawer.PopIndent();
}
