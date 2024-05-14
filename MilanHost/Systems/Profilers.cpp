#include <optional>

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include "Base.hpp"
#include "Canvas.hpp"
#include "Hooks.hpp"
#include "Intercept.hpp"
#include "Misc.hpp"
#include "Systems/Console.hpp"
#include "Systems/Output.hpp"
#include "Systems/Profilers.hpp"


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

namespace Milan::Profilers
{
    MilanCanvasDrawer g_canvas{};
    EMilanProfilerMode g_mode = MPM_None;

    CCanvasProfiler g_profilers[MPM_Max] =
    {
        /* MPM_None */
        CCanvasProfiler
        {
            .Subtitle = L"",
            .Description = L"",
            .Function = nullptr
        },

        /* MPM_PlayerControllers */
        CCanvasProfiler
        {
            .Subtitle = L"Player controllers",
            .Description = L"Displays active APlayerController instances",
            .Function = ProfilePlayerControllers,
        },

        /* MPM_Network */
        CCanvasProfiler
        {
            .Subtitle = L"Network",
            .Description = L"Displays active channels and related networking info",
            .Function = ProfileNetwork
        },

        /* MPM_WorldInfo */
        CCanvasProfiler
        {
            .Subtitle = L"WorldInfo",
            .Description = L"Displays information about active world",
            .Function = ProfileWorldInfo
        },
    };

    void Initialize(::LESDK::Initializer& Context)
    {
        MILAN_UNUSED(Context);

        Console::RegisterCommand(L"dbg", L"nextprofile",
            [](wchar_t const*)
            {
                g_mode = (EMilanProfilerMode)((int)g_mode + 1);
                if (g_mode >= MPM_Max) g_mode = MPM_None;
                return true;
            });

        Console::RegisterCommand(L"dbg", L"profile",
            [](wchar_t const* const Params)
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
                    g_mode = MPM_None;
                else if (!::_wcsicmp(Params, L"playercontrollers") || !::_wcsicmp(Params, L"pcs"))
                    g_mode = MPM_PlayerControllers;
                else if (!::_wcsicmp(Params, L"network") || !::_wcsicmp(Params, L"net"))
                    g_mode = MPM_Network;
                else if (!::_wcsicmp(Params, L"worldinfo") || !::_wcsicmp(Params, L"wi"))
                    g_mode = MPM_WorldInfo;
                else
                    MILAN_WARNW(L"unknown dbg.profile parameter: %s", Params);

                return true;
            });

        MILAN_INFOW(L"profilers system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"profilers system terminating");
        g_canvas.~MilanCanvasDrawer();
    }

    void On_UObject_ProcessEvent(UObject* const Context, UFunction* const Function, void* const Parms, void* const Result)
    {
        MILAN_UNUSED_4(Context, Function, Parms, Result);

        static FunctionInterceptLegacy GameViewportClient_PostRender{};
        if (GameViewportClient_PostRender.Applies(Function, L"Function Engine.GameViewportClient.PostRender"))
        {
            auto* const TParms = reinterpret_cast<UGameViewportClient_eventPostRender_Parms*>(Parms);

            g_canvas.UpdateCanvas(TParms->Canvas);
            g_canvas.UpdateGlobals((*GEngine)->Cast<USFXEngine>());
            g_canvas.Reset();

            UObject* const Target = nullptr;

            if (g_mode != MPM_None)
            {
                MILAN_CHECKW(g_mode >= 0 && g_mode < MPM_Max, L"");
                auto const& Profiler = g_profilers[g_mode];

                g_canvas.DrawHeader(Profiler.Subtitle, Profiler.Description, Target, MilanCanvasDrawer::k_warnColor);
                if (Profiler.Function) (*Profiler.Function)(g_canvas, Target);
            }
        }
    }

    void ProfilePlayerControllers(MilanCanvasDrawer& Drawer, UObject* Target)
    {
        MILAN_UNUSED_2(Drawer, Target);

        Drawer.SetBackground(FColor{ .B = 0, .G = 0, .R = 0, .A = 25 });

        for (TypedObjectIterator<APlayerController, true, false> Iter; Iter; ++Iter)
        {
            APlayerController* const Controller = *Iter;
            Drawer.DrawString(Controller->StaticFullName());
            Drawer.PushIndent();
            Drawer.DrawProperty(L"Pawn", Controller->Pawn ? Controller->Pawn->StaticFullName() : L"n/a");
            Drawer.DrawProperty(L"PRI", Controller->PlayerReplicationInfo ? Controller->PlayerReplicationInfo->StaticFullName() : L"n/a");
            if (Controller->PlayerReplicationInfo != nullptr)
            {
                Drawer.DrawProperty(L"PRI.Owner", Controller->PlayerReplicationInfo->Owner ? Controller->PlayerReplicationInfo->Owner->StaticFullName() : L"n/a");
                Drawer.DrawProperty(L"PRI.UniqueId", FString::Printf(L"%u:%u", Controller->PlayerReplicationInfo->UniqueId.Uid.A, Controller->PlayerReplicationInfo->UniqueId.Uid.B));
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

    }

    void ProfileNetwork(MilanCanvasDrawer& Drawer, UObject* const Target)
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
        Drawer.DrawProperty(L"World", (*GWorld)->StaticFullName());
        Drawer.DrawProperty(L"GameInfo", WorldInfo->Game ? WorldInfo->Game->StaticName() : L"n/a");
        Drawer.DrawProperty(L"ReplicationInfo", WorldInfo->GRI ? WorldInfo->GRI->StaticName() : L"n/a");
        Drawer.DrawProperty(L"NetMode", FString::Printf(L"%d (%s)", WorldInfo->NetMode, NetModeString(WorldInfo->NetMode)));
        Drawer.PopIndent();

        Drawer.DrawSeparator();

        // NetDriver

        auto* const World = reinterpret_cast<UWorld_Proxy*>(*GWorld);
        auto* const NetDriver = reinterpret_cast<UNetDriver_Proxy*>(World->NetDriver);

        Drawer.DrawString(FString::Printf(L"World: %s", *World->StaticName()));
        Drawer.DrawString(FString::Printf(L"NetDriver: %s", NetDriver ? *NetDriver->StaticName() : L"n/a"));

        if (NetDriver && WorldInfo->NetMode == NM_ListenServer)
        {
            Drawer.PushIndent();

            for (UINT i = 0u; i < NetDriver->ChildConnections.Count(); ++i)
            {
                //MILAN_CHECKW(ChildConnIter->IsA<UChildConnection>(), L"");

                auto* const ChildConnIter = NetDriver->ChildConnections(i);
                auto* const ChildConn = reinterpret_cast<UNetConnection_Proxy*>(ChildConnIter);

                Drawer.DrawString(FString::Printf(L"ChildConnections(%d): %s", i, *ChildConnIter->StaticName()));

                Drawer.PushIndent();
                Drawer.DrawProperty(L"Viewer", ChildConn->Viewer ? ChildConn->Viewer->StaticFullName() : L"n/a");
                Drawer.DrawProperty(L"ClientWorldPackageName", ChildConn->ClientWorldPackageName.ToString());
                Drawer.DrawString(L"Actor channels");
                unsigned int ChannelIndex = 0;
                for (auto const& [Actor, Channel] : ChildConn->ActorChannels)
                {
                    Drawer.PushIndent();
                    MILAN_CHECKW(Channel != nullptr, L"");
                    auto* const ChannelProxy = reinterpret_cast<UActorChannel_Proxy*>(Channel);
                    DrawActorChannel(Drawer, ChannelProxy, ChannelIndex++);
                    Drawer.PopIndent();

                    if (Actor != ChannelProxy->Actor)
                    {
                        MILAN_WARNW(L"server has actor channel with actor != channel->actor");
                        MILAN_WARNW(L"  actor = %s", Actor ? *Actor->StaticFullName() : L"n/a");
                        MILAN_WARNW(L"  channel->actor = %s", ChannelProxy->Actor ? *ChannelProxy->Actor->StaticFullName() : L"n/a");
                    }
                }
                Drawer.DrawString(L"Client visible levels");
                unsigned int VisibleIndex = 0;
                for (auto const VisibleLevelName : ChildConn->ClientVisibleLevelNames)
                {
                    Drawer.PushIndent();
                    Drawer.DrawProperty(FString::Printf(L"%d", VisibleIndex++), VisibleLevelName.ToString());
                    Drawer.PopIndent();
                }
                Drawer.DrawString(L"Pending packages");
                unsigned int PendingIndex = 0;
                for (auto const& PendingPackage : ChildConn->PendingPackageInfos)
                {
                    Drawer.PushIndent();
                    Drawer.DrawProperty(FString::Printf(L"%d", PendingIndex++),
                        FString::Printf(L"Name=%s, Parent=%s, Guid=%08X%08X%08X%08X",
                            *PendingPackage->PackageName.ToString(),
                            PendingPackage->Parent ? *PendingPackage->Parent->StaticName() : L"n/a",
                            PendingPackage->Guid.A, PendingPackage->Guid.B, PendingPackage->Guid.C, PendingPackage->Guid.D));
                    Drawer.PopIndent();
                }
                Drawer.PopIndent();
            }

            Drawer.PopIndent();
        }
        else if (NetDriver && NetDriver->ServerConnection)
        {
            Drawer.PushIndent();

            auto* const ServerConn = reinterpret_cast<UNetConnection_Proxy*>(NetDriver->ServerConnection);
            Drawer.DrawProperty(L"Viewer", ServerConn->Viewer ? ServerConn->Viewer->StaticFullName() : L"n/a");

            Drawer.DrawString(L"ServerConnection:");
            Drawer.PushIndent();

            Drawer.DrawString(L"Actor channels");
            unsigned int ChannelIndex = 0;
            for (auto const& [Actor, Channel] : ServerConn->ActorChannels)
            {
                Drawer.PushIndent();
                MILAN_CHECKW(Channel != nullptr, L"");
                auto* const ChannelProxy = reinterpret_cast<UActorChannel_Proxy*>(Channel);
                DrawActorChannel(Drawer, ChannelProxy, ChannelIndex++);
                Drawer.PopIndent();

                if (Actor != ChannelProxy->Actor)
                {
                    MILAN_WARNW(L"client has actor channel with actor != channel->actor");
                    MILAN_WARNW(L"  actor = %s", Actor ? *Actor->StaticFullName() : L"n/a");
                    MILAN_WARNW(L"  channel->actor = %s", ChannelProxy->Actor ? *ChannelProxy->Actor->StaticFullName() : L"n/a");
                }
            }

            Drawer.PopIndent();
            Drawer.PopIndent();
        }
    }

    void ProfileWorldInfo(MilanCanvasDrawer& Drawer, UObject* const Target)
    {
        MILAN_UNUSED_2(Drawer, Target);
    }

    void DrawActorChannel(MilanCanvasDrawer& Drawer, UActorChannel_Proxy* const Channel, unsigned int const Index)
    {
        static FColor s_flagsActorColor{ .B = 210, .G = 210, .R = 200, .A = 255 };
        static FColor s_flagsBaseColor{ .B = 255, .G = 185, .R = 185, .A = 255 };
        static FColor s_flagsDerivedColor{ .B = 185, .G = 255, .R = 185, .A = 255 };

        // I'd very much like to know what's supposed to be in here...
        MILAN_CHECKW(Channel->Unknown_114.Count() == 0, L"");

        Drawer.DrawString(FString::Printf(L"Channel %d: %s", Index, Channel->Actor ? *Channel->Actor->StaticFullName() : L"n/a"));
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

        //auto* const NetDriver = (UNetDriver_Proxy*)(((UWorld_Proxy*)Channel->World)->NetDriver);
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
}
