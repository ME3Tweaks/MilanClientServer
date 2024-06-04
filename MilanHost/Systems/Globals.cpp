#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include "Base.hpp"
#include "Hooks.hpp"
#include "Systems/Globals.hpp"
#include "Systems/Output.hpp"
#include "Systems/Imgui.hpp"

#include "Intercept.hpp"
#include "NetProf.hpp"


namespace Milan::Allocator
{
    void Initialize(::LESDK::Initializer& Context)
    {
        // Cannot use logging here because that is dependent on GMalloc.

        GMalloc = Context.ResolveTyped<FMallocLike*>(BUILTIN_GMALLOC_RIP);
        assert(GMalloc != nullptr && *GMalloc != nullptr);

        void* const BogusAllocation = (*GMalloc)->Malloc(4, UN_DEFAULT_ALIGNMENT);
        void* const BogusReallocation = (*GMalloc)->Realloc(BogusAllocation, 128, UN_DEFAULT_ALIGNMENT);
        (*GMalloc)->Free(BogusReallocation);
    }

    void Terminate()
    {
         // No logging here either because output has already been terminated.
    }
}

namespace Milan::Globals
{
    void Initialize(::LESDK::Initializer& Context)
    {
        MILAN_INFOW(L"base address = %p", (void*)::LESDK::GetMainModuleBase());

        // Some of the addresses initialized here might be unused completely or in non-debug flows.
        // TODO: Before public release, go through all of globals here and comment everything unneeded.

        GEngine = Context.ResolveTyped<UEngine*>(BUILTIN_GENGINE_RIP);
        MILAN_DEBUGW(L"GEngine found: %p (%p)", (void*)GEngine, (void*)*GEngine);
        GNatives = Context.ResolveTyped<tNative*>(BUILTIN_GNATIVES_RIP);
        MILAN_DEBUGW(L"GNatives found: %p (%p)", (void*)GNatives, (void*)*GNatives);
        GSys = Context.ResolveTyped<USystem*>(BUILTIN_GSYS_RIP);
        MILAN_DEBUGW(L"GSys found: %p (%p)", (void*)GSys, (void*)*GSys);
        GWorld = Context.ResolveTyped<UWorld*>(BUILTIN_GWORLD_RIP);
        MILAN_DEBUGW(L"GWorld found: %p (%p)", (void*)GWorld, (void*)*GWorld);

        UObject::GObjObjects = Context.ResolveTyped<TArray<UObject*>>(BUILTIN_GOBOBJECTS_RIP);
        MILAN_DEBUGW(L"UObject::GObjObjects found: %p", (void*)UObject::GObjObjects);
        SFXName::GBioNamePools = Context.ResolveTyped<SFXNameEntry const*>(BUILTIN_SFXNAMEPOOLS_RIP);
        MILAN_DEBUGW(L"SFXName::GBioNamePools found: %p", (void*)SFXName::GBioNamePools);
        SFXName::GInitMethod = Context.ResolveTyped<SFXName::tInitMethod>(BUILTIN_SFXNAMEINIT_PHOOK);
        MILAN_DEBUGW(L"SFXName::Init found: %p", (void*)SFXName::GInitMethod);

        t_BioRequestExit* BioRequestExit_target = Context.ResolveTyped<t_BioRequestExit>(GLOBAL_BIOREQUESTEXIT_PHOOK);
        BioRequestExit_orig = (t_BioRequestExit*)Context.InstallHook("BioRequestExit", BioRequestExit_target, BioRequestExit_hook);
        MILAN_DEBUGW(L"BioRequestExit hooked: %p -> %p", (void*)BioRequestExit_target, (void*)BioRequestExit_orig);

        t_IDXGISwapChain_Present* IDXGISwapChain_Present_target = ::Milan::Imgui::FindSwapChainPresent();
        IDXGISwapChain_Present_orig = (t_IDXGISwapChain_Present*)Context.InstallHook("IDXGISwapChain::Present", IDXGISwapChain_Present_target, IDXGISwapChain_Present_hook);
        MILAN_DEBUGW(L"IDXGISwapChain::Present hooked: %p -> %p", (void*)IDXGISwapChain_Present_target, (void*)IDXGISwapChain_Present_orig);

        WndProc_orig = Context.ResolveTyped<tWndProc>(GLOBAL_WNDPROC_RVA);
        MILAN_DEBUGW(L"WndProc found: %p", (void*)WndProc_orig);

        // WndProc is not hooked but replaced using Win32 mechanism,
        // see ::Milan::Imgui::OnSwapChainPresent for more.

        UUIString_StringSize = Context.ResolveTyped<t_UUIString_StringSize>(GLOBAL_UUISTRING_STRINGSIZE_PHOOK);
        MILAN_DEBUGW(L"UUIString::StringSize found: %p", (void*)UUIString_StringSize);

        UCanvas_WrappedPrint = Context.ResolveTyped<t_UCanvas_WrappedPrint>(GLOBAL_UCANVAS_WRAPPEDPRINT_PHOOK);
        MILAN_DEBUGW(L"UCanvas::WrappedPrint found: %p", (void*)UCanvas_WrappedPrint);

        t_UFunction_Bind* UFunction_Bind_target = Context.ResolveTyped<t_UFunction_Bind>(GLOBAL_UFUNCTION_BIND_PHOOK);
        UFunction_Bind_orig = (t_UFunction_Bind*)Context.InstallHook("UFunction::Bind", UFunction_Bind_target, UFunction_Bind_hook);
        MILAN_DEBUGW(L"UFunction::Bind hooked: %p -> %p", (void*)UFunction_Bind_target, (void*)UFunction_Bind_orig);

        t_UGameEngine_Exec* UGameEngine_Exec_target = Context.ResolveTyped<t_UGameEngine_Exec>(GLOBAL_UGAMEENGINE_EXEC_PHOOK);
        UGameEngine_Exec_orig = (t_UGameEngine_Exec*)Context.InstallHook("UGameEngine::Exec", UGameEngine_Exec_target, UGameEngine_Exec_hook);
        MILAN_DEBUGW(L"UGameEngine::Exec hooked: %p -> %p", (void*)UGameEngine_Exec_target, (void*)UGameEngine_Exec_orig);

        t_UObject_CallFunction* UObject_CallFunction_target = Context.ResolveTyped<t_UObject_CallFunction>(BUILTIN_CALLFUNCTION_PHOOK);
        UObject_CallFunction_orig = (t_UObject_CallFunction*)Context.InstallHook("UObject::CallFunction", UObject_CallFunction_target, UObject_CallFunction_hook);
        MILAN_DEBUGW(L"UObject::CallFunction hooked: %p -> %p", (void*)UObject_CallFunction_target, (void*)UObject_CallFunction_orig);

        t_UObject_ProcessDelegate* UObject_ProcessDelegate_target = Context.ResolveTyped<t_UObject_ProcessDelegate>(GLOBAL_UOBJECT_PROCESSDELEGATE_PHOOK);
        UObject_ProcessDelegate_orig = (t_UObject_ProcessDelegate*)Context.InstallHook("UObject::ProcessDelegate", UObject_ProcessDelegate_target, UObject_ProcessDelegate_hook);
        MILAN_DEBUGW(L"UObject::ProcessDelegate hooked: %p -> %p", (void*)UObject_ProcessDelegate_target, (void*)UObject_ProcessDelegate_orig);

        t_UObject_ProcessEvent* UObject_ProcessEvent_target = Context.ResolveTyped<t_UObject_ProcessEvent>(BUILTIN_PROCESSEVENT_PHOOK);
        UObject_ProcessEvent_orig = (t_UObject_ProcessEvent*)Context.InstallHook("UObject::ProcessEvent", UObject_ProcessEvent_target, UObject_ProcessEvent_hook);
        MILAN_DEBUGW(L"UObject::ProcessEvent hooked: %p -> %p", (void*)UObject_ProcessEvent_target, (void*)UObject_ProcessEvent_orig);

        t_UObject_ProcessInternal* UObject_ProcessInternal_target = Context.ResolveTyped<t_UObject_ProcessInternal>(BUILTIN_PROCESSINTERNAL_PHOOK);
        UObject_ProcessInternal_orig = (t_UObject_ProcessInternal*)Context.InstallHook("UObject::ProcessInternal", UObject_ProcessInternal_target, UObject_ProcessInternal_hook);
        MILAN_DEBUGW(L"UObject::ProcessInternal hooked: %p -> %p", (void*)UObject_ProcessInternal_target, (void*)UObject_ProcessInternal_orig);

        UObject_SkipFunction = Context.ResolveTyped<t_UObject_SkipFunction>(GLOBAL_UOBJECT_SKIPFUNCTION_PHOOK);
        MILAN_DEBUGW(L"UObject::SkipFunction found: %p", (void*)UObject_SkipFunction);

        t_UWorld_Tick* UWorld_Tick_target = Context.ResolveTyped<t_UWorld_Tick>(GLOBAL_UWORLD_TICK_PHOOK);
        UWorld_Tick_orig = (t_UWorld_Tick*)Context.InstallHook("UWorld::Tick", UWorld_Tick_target, UWorld_Tick_hook);
        MILAN_DEBUGW(L"UWorld::Tick hooked: %p -> %p", (void*)UWorld_Tick_target, (void*)UWorld_Tick_orig);

        // Network debug hooks.
        // ----------------------------------------

#ifndef _FINAL_RELEASE

        t_UActorChannel_ReplicateActor* UActorChannel_ReplicateActor_target = Context.ResolveTyped<t_UActorChannel_ReplicateActor>(GLOBAL_UACTORCHANNEL_REPLICATEACTOR_PHOOK);
        UActorChannel_ReplicateActor_orig = (t_UActorChannel_ReplicateActor*)Context.InstallHook("UActorChannel::ReplicateActor", UActorChannel_ReplicateActor_target, UActorChannel_ReplicateActor_hook);
        MILAN_DEBUGW(L"UActorChannel::ReplicateActor hooked: %p -> %p", (void*)UActorChannel_ReplicateActor_target, (void*)UActorChannel_ReplicateActor_orig);

        t_UNetConnection_ClientSupportsLevel* UNetConnection_ClientSupportsLevel_target = Context.ResolveTyped<t_UNetConnection_ClientSupportsLevel>(GLOBAL_UNETCONNECTION_CLIENTSUPPORTSLEVEL_PATTERN);
        UNetConnection_ClientSupportsLevel_orig = (t_UNetConnection_ClientSupportsLevel*)Context.InstallHook("UNetConnection::ClientSupportsLevel", UNetConnection_ClientSupportsLevel_target, UNetConnection_ClientSupportsLevel_hook);
        MILAN_DEBUGW(L"UNetConnection::ClientSupportsLevel hooked: %p -> %p", (void*)UNetConnection_ClientSupportsLevel_target, (void*)UNetConnection_ClientSupportsLevel_orig);

        t_UPackageMap_SupportsObject* UPackageMap_SupportsObject_target = Context.ResolveTyped<t_UPackageMap_SupportsObject>(GLOBAL_UPACKAGEMAP_SUPPORTSOBJECT_PATTERN);
        UPackageMap_SupportsObject_orig = (t_UPackageMap_SupportsObject*)Context.InstallHook("UPackageMap::SupportsObject", UPackageMap_SupportsObject_target, UPackageMap_SupportsObject_hook);
        MILAN_DEBUGW(L"UPackageMap::SupportsObject hooked: %p -> %p", (void*)UPackageMap_SupportsObject_target, (void*)UPackageMap_SupportsObject_orig);

        t_UWorld_ServerTickClients* UWorld_ServerTickClients_target = Context.ResolveTyped<t_UWorld_ServerTickClients>(GLOBAL_UWORLD_SERVERTICKCLIENTS_PHOOK);
        UWorld_ServerTickClients_orig = (t_UWorld_ServerTickClients*)Context.InstallHook("UWorld::ServerTickClients", UWorld_ServerTickClients_target, UWorld_ServerTickClients_hook);
        MILAN_DEBUGW(L"UWorld::ServerTickClients hooked: %p -> %p", (void*)UWorld_ServerTickClients_target, (void*)UWorld_ServerTickClients_orig);

        t_SortReplicatedActors* SortReplicatedActors_target = Context.ResolveTyped<t_SortReplicatedActors>(GLOBAL_SORT_REPLICATED_ACTORS_OFFSET);
        SortReplicatedActors_orig = (t_SortReplicatedActors*)(Context.InstallHook("SortReplicatedActors", SortReplicatedActors_target, SortReplicatedActors_hook));
        MILAN_DEBUGW(L"SortReplicatedActors hooked: %p -> %p", (void*)SortReplicatedActors_target, (void*)SortReplicatedActors_orig);

#endif

        // ----------------------------------------
        // Network debug end.


        // Temporary network profiler stuff.
        // ----------------------------------------

        ::CaveIndexer LocalCaveIndexer{ 20 };
        unsigned char* const MainBase = reinterpret_cast<unsigned char*>(::LESDK::GetMainModuleBase());

        new ::MiddleIntercept{ L"UChannel::SendBunch-1", MainBase + 0x48E3D9, MlnUChannelSendBunch1, &LocalCaveIndexer };
        new ::MiddleIntercept{ L"UChannel::SendBunch-2", MainBase + 0x48E645, MlnUChannelSendBunch2, &LocalCaveIndexer };
        new ::MiddleIntercept{ L"UActorChannel::ReplicateActor-1", MainBase + 0x48C82C, MlnUActorChannelReplicateActor1, &LocalCaveIndexer };
        new ::MiddleIntercept{ L"UActorChannel::ReplicateActor-2", MainBase + 0x48D3B0, MlnUActorChannelReplicateActor2, &LocalCaveIndexer };
        new ::MiddleIntercept{ L"UActorChannel::ReplicateActor-3", MainBase + 0x48D43D, MlnUActorChannelReplicateActor3, &LocalCaveIndexer };
        new ::MiddleIntercept{ L"InternalReplicateFunction", MainBase + 0x709CFC, MlnInternalReplicateFunction1, &LocalCaveIndexer };
        new ::MiddleIntercept{ L"SeamlessTravelTick", MainBase + 0x8CA57B, MlnSeamlessTravelTick1, &LocalCaveIndexer };
        new ::MiddleIntercept{ L"UGameEngine::LoadMap", MainBase + 0xB930E0, MlnUGameEngineLoadMap1, &LocalCaveIndexer };
        new ::MiddleIntercept{ L"UGameEngine::Tick", MainBase + 0x55031A, MlnUGameEngineTick1, &LocalCaveIndexer };


        // ----------------------------------------
        // Network profiler end.


        MILAN_INFOW(L"globals system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"globals system terminating");
    }
}
