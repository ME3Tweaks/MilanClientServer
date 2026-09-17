#include "Components/CGlobals.hpp"
#include "Components/CImgui.hpp"
#include "Engine/ConsoleAutoComplete.hpp"
#include "Engine/Hooks.hpp"
#include "Engine/Intercept.hpp"
#include "Misc/GuidCacheUtils.hpp"
#include "NetProf/NetProfDecls.hpp"
#include "Online/SFXOnlineHijacks.hpp"

// ! CGlobals implementation.
// ========================================

bool CGlobals::Initialize(::LESDK::Initializer& Context)
{
    MILAN_INFOW(ELOG_Startup, L"base address = 0x%p", (void*)::LESDK::GetMainModuleBase());

    // Some of the addresses initialized here might be unused completely or in non-debug flows.
    // TODO: Before public release, go through all of globals here and comment everything unneeded.

    InitSDK(Context);
    InitGlobals(Context);
    InitNetworkHooks(Context);
    InitNetworkInjections();
    InitGuidCacheOverride();

    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CGlobals::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);
    CleanupGuidCacheOverride();
}

void CGlobals::PostInitialize(::LESDK::Initializer& Context)
{
    // Depends on CImgui initialization.
    InitWindowProc(Context);

    // Engine is a base package, always loaded by this point (unlike e.g. SFXOnlineFoundation).
    ConsoleAutoComplete::Install();
}

void CGlobals::UninstallSomeHooks(::LESDK::Initializer& Context)
{
    Context.UninstallHook("UWorld::Tick");
    Context.UninstallHook("UObject::ProcessInternal");
    Context.UninstallHook("UObject::ProcessEvent");
    Context.UninstallHook("UObject::ProcessDelegate");
    Context.UninstallHook("UObject::CallFunction");
    Context.UninstallHook("UGameEngine::Tick");
    Context.UninstallHook("UGameEngine::Exec");
}

void CGlobals::UpdateGuidCacheOverride()
{
#ifndef _FINAL_RELEASE
#if defined(ENABLE_GUIDCACHE_OVERRIDE) && ENABLE_GUIDCACHE_OVERRIDE
    if (mp_guidCacheOverride != nullptr)
        mp_guidCacheOverride->UpdateLookup();
#endif
#endif
}

void CGlobals::InitSDK(::LESDK::Initializer& Context)
{
    GEngine = Context.ResolveTyped<UEngine*>(BUILTIN_GENGINE_RIP);
    LOG_SDK_GLOBAL(GEngine, *GEngine);

    GNatives = Context.ResolveTyped<tNative*>(BUILTIN_GNATIVES_RIP);
    LOG_SDK_GLOBAL(GNatives, *GNatives);

    GSys = Context.ResolveTyped<USystem*>(BUILTIN_GSYS_RIP);
    LOG_SDK_GLOBAL(GSys, *GSys);

    GWorld = Context.ResolveTyped<UWorld*>(BUILTIN_GWORLD_RIP);
    LOG_SDK_GLOBAL(GWorld, *GWorld);

    UObject::GObjObjects = Context.ResolveTyped<TArray<UObject*>>(BUILTIN_GOBOBJECTS_RIP);
    LOG_SDK_GLOBAL(UObject::GObjObjects, UObject::GObjObjects);
}

void CGlobals::InitGlobals(::LESDK::Initializer& Context)
{
    pGProperty = Context.ResolveTyped<UProperty*>(GLOBAL_GPROPERTY_POSTRIP);
    LOG_SDK_GLOBAL(pGProperty, *pGProperty);

    pGPropAddr = Context.ResolveTyped<unsigned char*>(GLOBAL_GPROPADDR_POSTRIP);
    LOG_SDK_GLOBAL(pGPropAddr, *pGPropAddr);

    pGPropObject = Context.ResolveTyped<UObject*>(GLOBAL_GPROPOBJECT_POSTRIP);
    LOG_SDK_GLOBAL(pGPropObject, *pGPropObject);

    pGRuntimeUCFlags = Context.ResolveTyped<unsigned int>(GLOBAL_GRUNTIMEUCFLAGS_OFFSET);
    LOG_SDK_GLOBAL(pGRuntimeUCFlags, pGRuntimeUCFlags);


    // From this point on, use the macro system to simplify search and installation.
    INSTALL_HOOK_ONE(BioRequestExit, GLOBAL_BIOREQUESTEXIT_PHOOK);

    FIND_VAR_TWO(UUIString, StringSize, GLOBAL_UUISTRING_STRINGSIZE_PHOOK);
    FIND_VAR_TWO(UCanvas, WrappedPrint, GLOBAL_UCANVAS_WRAPPEDPRINT_PHOOK);

    INSTALL_HOOK_TWO(UFunction, Bind, GLOBAL_UFUNCTION_BIND_PHOOK);

    INSTALL_HOOK_TWO(UGameEngine, Exec, GLOBAL_UGAMEENGINE_EXEC_PHOOK);
    INSTALL_HOOK_TWO(UGameEngine, LoadMap, GLOBAL_UGAMEENGINE_LOADMAP_PHOOK);
    INSTALL_HOOK_TWO(UGameEngine, Tick, GLOBAL_UGAMEENGINE_TICK_PHOOK);

    INSTALL_HOOK_TWO(UObject, CallFunction, BUILTIN_CALLFUNCTION_PHOOK);
    INSTALL_HOOK_TWO(UObject, ProcessDelegate, GLOBAL_UOBJECT_PROCESSDELEGATE_PHOOK);
    INSTALL_HOOK_TWO(UObject, ProcessEvent, BUILTIN_PROCESSEVENT_PHOOK);
    INSTALL_HOOK_TWO(UObject, ProcessInternal, BUILTIN_PROCESSINTERNAL_PHOOK);
    FIND_VAR_TWO(UObject, SkipFunction, GLOBAL_UOBJECT_SKIPFUNCTION_PHOOK);

    //INSTALL_HOOK_TWO(USFXEngine, UpdateDynamicLoadMappingFindHash, GLOBAL_USFXENGINE_UPDATEDYNAMICLOADMAPPINGFINDHASH_PHOOK);
    INSTALL_HOOK_TWO(UWorld, SpawnActor, GLOBAL_UWORLD_SPAWNACTOR_PHOOK);
    INSTALL_HOOK_TWO(UWorld, Tick, GLOBAL_UWORLD_TICK_PHOOK);
}

void CGlobals::InitWindowProc(::LESDK::Initializer& Context)
{
#ifdef WITH_IMGUI
    // WndProc is replaced via a Win32 mechanism, see CImgui::On_SwapChainPresent.
    WndProc_orig = Context.ResolveTyped<t_WndProc>(GLOBAL_WNDPROC_RVA);
    LOG_SDK_GLOBAL(WndProc_orig, WndProc_orig);
    INSTALL_HOOK_TWO_ADDR(IDXGISwapChain, Present, COMPONENT(CImgui).GetSwapChainPresent());
#else
    MILAN_UNUSED(Context);
#endif
}

void CGlobals::InitNetworkHooks(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);

#ifndef _FINAL_RELEASE

    INSTALL_HOOK_TWO(UActorChannel, ReplicateActor, GLOBAL_UACTORCHANNEL_REPLICATEACTOR_PHOOK);
    INSTALL_HOOK_TWO(UActorChannel, SetChannelActor, GLOBAL_UACTORCHANNEL_SETCHANNELACTOR_PHOOK);
    INSTALL_HOOK_TWO(UGuidCache, LookupPackageGuid, GLOBAL_UGUIDCACHE_LOOKUPPACKAGEGUID_PHOOK);
    INSTALL_HOOK_TWO(UNetConnection, ClientSupportsLevel, GLOBAL_UNETCONNECTION_CLIENTSUPPORTSLEVEL_PATTERN);
    INSTALL_HOOK_TWO(UNetConnection, Close, GLOBAL_UNETCONNECTION_CLOSE_PATTERN);
    INSTALL_HOOK_TWO(UNetConnection, CreateChannel, GLOBAL_UNETCONNECTION_CREATECHANNEL_PHOOK);
    INSTALL_HOOK_TWO(UPackageMap, SupportsObject, GLOBAL_UPACKAGEMAP_SUPPORTSOBJECT_PATTERN);
    INSTALL_HOOK_TWO(UPackageMap, SerializeObject, GLOBAL_UPACKAGEMAP_SERIALIZEOBJECT_PATTERN);
    INSTALL_HOOK_TWO(UNetPendingLevel, NotifyControlMessage, GLOBAL_UNETPENDINGLEVEL_NOTIFYCONTROLMESSAGE_PHOOK);
    INSTALL_HOOK_TWO(UWorld, NotifyControlMessage, GLOBAL_UWORLD_NOTIFYCONTROLMESSAGE_PHOOK);
    INSTALL_HOOK_TWO(UWorld, ServerTickClients, GLOBAL_UWORLD_SERVERTICKCLIENTS_PHOOK);
    INSTALL_HOOK_ONE(SortReplicatedActors, GLOBAL_SORT_REPLICATED_ACTORS_OFFSET);
    INSTALL_HOOK_ONE(GlobalDynamicBuildVersion, GLOBAL_GLOBALDYNAMICBUILDVERSION_PATTERN);
    INSTALL_HOOK_TWO(FSocketWin, SendTo, GLOBAL_FSOCKETWIN_SENDTO_OFFSET);
    INSTALL_HOOK_TWO(FSocketWin, Send, GLOBAL_FSOCKETWIN_SEND_OFFSET);
    INSTALL_HOOK_TWO(FSocketWin, RecvFrom, GLOBAL_FSOCKETWIN_RECVFROM_OFFSET);
    INSTALL_HOOK_TWO(FSocketWin, Recv, GLOBAL_FSOCKETWIN_RECV_OFFSET);

#endif // !_FINAL_RELEASE
}

void CGlobals::InitNetworkInjections()
{
#ifndef _FINAL_RELEASE

    static constexpr std::ptrdiff_t k_needCaveSize = 20;
    CaveIndexer Indexer{ k_needCaveSize };


    BinaryInjection::Install(L"UNetConnection::ReceivedPacket-1", 0x48B663, MlnUNetConnectionReceivedPacket1, Indexer);

    BinaryInjection::Install(L"UChannel::SendBunch-1", 0x48E3D9, MlnUChannelSendBunch1, Indexer);
    BinaryInjection::Install(L"UChannel::SendBunch-2", 0x48E645, MlnUChannelSendBunch2, Indexer);
    BinaryInjection::Install(L"UActorChannel::ReplicateActor-1", 0x48C82C, MlnUActorChannelReplicateActor1, Indexer);
    BinaryInjection::Install(L"UActorChannel::ReplicateActor-2", 0x48D3B0, MlnUActorChannelReplicateActor2, Indexer);
    BinaryInjection::Install(L"UActorChannel::ReplicateActor-3", 0x48D43D, MlnUActorChannelReplicateActor3, Indexer);
    BinaryInjection::Install(L"InternalReplicateFunction", 0x709CFC, MlnInternalReplicateFunction1, Indexer);
    BinaryInjection::Install(L"SeamlessTravelTick", 0x8CA57B, MlnSeamlessTravelTick1, Indexer);
    BinaryInjection::Install(L"UTcpNetDriverTickDispatch", 0x90CEDA, MlnUTcpNetDriverTickDispatch, Indexer);

    BinaryInjection::Install(L"UWorldNotifyControlMessageHello", 0x8C48FD, MlnUWorldNotifyControlMessageHello, Indexer);
    BinaryInjection::Install(L"UWorldNotifyControlMessageNetspeed", 0x8C4B96, MlnUWorldNotifyControlMessageNetspeed, Indexer);
    BinaryInjection::Install(L"UWorldNotifyControlMessageLogin", 0x8C4FA2, MlnUWorldNotifyControlMessageLogin, Indexer);
    BinaryInjection::Install(L"UWorldNotifyControlMessageFailure", 0x8C4559, MlnUWorldNotifyControlMessageFailure, Indexer);
    BinaryInjection::Install(L"UWorldNotifyControlMessageUses", 0x8C43D7, MlnUWorldNotifyControlMessageUses, Indexer);
    BinaryInjection::Install(L"UWorldNotifyControlMessageHave", 0x8C4C39, MlnUWorldNotifyControlMessageHave, Indexer);
    BinaryInjection::Install(L"UWorldNotifyControlMessageJoin", 0x8C518B, MlnUWorldNotifyControlMessageJoin, Indexer);
    BinaryInjection::Install(L"UWorldNotifyControlMessageAbort", 0x8C4DAE, MlnUWorldNotifyControlMessageAbort, Indexer);
    BinaryInjection::Install(L"UWorldNotifyControlMessageUnload", 0x8C4309, MlnUWorldNotifyControlMessageUnload, Indexer);
    BinaryInjection::Install(L"UWorldNotifyControlMessageUnknown19", 0x8C42B3, MlnUWorldNotifyControlMessageUnknown19, Indexer);
    BinaryInjection::Install(L"UWorldNotifyControlMessageUnknown21", 0x8C57B2, MlnUWorldNotifyControlMessageUnknown21, Indexer);

    BinaryInjection::Install(L"UNetPendingLevelNotifyControlMessageWelcome", 0x660814, MlnUNetPendingLevelNotifyControlMessageWelcome, Indexer);
    BinaryInjection::Install(L"UNetPendingLevelNotifyControlMessageUpgrade", 0x65F99C, MlnUNetPendingLevelNotifyControlMessageUpgrade, Indexer);
    BinaryInjection::Install(L"UNetPendingLevelNotifyControlMessageChallenge", 0x6600F7, MlnUNetPendingLevelNotifyControlMessageChallenge, Indexer);
    BinaryInjection::Install(L"UNetPendingLevelNotifyControlMessageFailure", 0x65FF4C, MlnUNetPendingLevelNotifyControlMessageFailure, Indexer);
    BinaryInjection::Install(L"UNetPendingLevelNotifyControlMessageUses", 0x65FB11, MlnUNetPendingLevelNotifyControlMessageUses, Indexer);
    BinaryInjection::Install(L"UNetPendingLevelNotifyControlMessageDlMngr", 0x66075A, MlnUNetPendingLevelNotifyControlMessageDlMngr, Indexer);
    BinaryInjection::Install(L"UNetPendingLevelNotifyControlMessageUnload", 0x65FF1B, MlnUNetPendingLevelNotifyControlMessageUnload, Indexer);

#endif // !_FINAL_RELEASE
}

void CGlobals::InitGuidCacheOverride()
{
#ifndef _FINAL_RELEASE
#if defined(ENABLE_GUIDCACHE_OVERRIDE) && ENABLE_GUIDCACHE_OVERRIDE
    MILAN_WARNW(ELOG_Startup, L"this session is using guid cache override");
    mp_guidCacheOverride = new (std::nothrow) GuidCacheOverride("./guidcache-override.txt");
    MILAN_CHECKW(L"failed to allocate guid cache override");
    mp_guidCacheOverride->UpdateLookup();
#endif
#endif // !_FINAL_RELEASE
}

void CGlobals::CleanupGuidCacheOverride()
{
#ifndef _FINAL_RELEASE
    if (mp_guidCacheOverride)
    {
        MILAN_DEBUGW(ELOG_Teardown, L"deleting guidcache override");
        delete std::exchange(mp_guidCacheOverride, nullptr);
    }
#endif // !_FINAL_RELEASE
}
