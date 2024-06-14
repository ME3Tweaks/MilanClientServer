#include <LESDK/Headers.hpp>

#include "Milan/Base.hpp"
#include "Milan/Hooks.hpp"
#include "Milan/Intercept.hpp"
#include "Milan/NetProfDecls.hpp"
#include "Systems/Globals.hpp"
#include "Systems/Output.hpp"
#include "Systems/Imgui.hpp"


// ! Allocator subsystem.
// ========================================

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


// ! Hook / search macros for globals subsystem.
// ========================================

#define _INSTALL_HOOK_ADDR(Address, String, Function) \
    auto* const MILAN_VARCAT(Function, _target) = (MILAN_VARCAT(t_, Function)*)Address; \
    MILAN_VARCAT(Function, _orig) = (MILAN_VARCAT(t_, Function)*)(Context.InstallHook(String, MILAN_VARCAT(Function, _target), MILAN_VARCAT(Function, _hook))); \
    MILAN_DEBUGW(L"" String L" hooked: %p -> %p", (void*)MILAN_VARCAT(Function, _target), (void*)MILAN_VARCAT(Function, _orig))

#define _INSTALL_HOOK_INIT(Pattern, String, Function) \
    _INSTALL_HOOK_ADDR(Context.ResolveTyped<MILAN_VARCAT(t_, Function)>(Pattern), String, Function)


#define INSTALL_HOOK_ONE(Function, Pattern)  _INSTALL_HOOK_INIT(Pattern, #Function, Function)
#define INSTALL_HOOK_TWO(Class, Function, Pattern)  _INSTALL_HOOK_INIT(Pattern, #Class "::" #Function, MILAN_VARCAT(MILAN_VARCAT(Class, _), Function))

#define INSTALL_HOOK_ONE_ADDR(Function, Address)  _INSTALL_HOOK_ADDR(Address, #Function, Function)
#define INSTALL_HOOK_TWO_ADDR(Class, Function, Address)  _INSTALL_HOOK_ADDR(Address, #Class "::" #Function, MILAN_VARCAT(MILAN_VARCAT(Class, _), Function))


#define _FIND_VAR_ADDR(Address, String, Variable) \
    Variable = (MILAN_VARCAT(t_, Variable)*)Address; \
    MILAN_DEBUGW(L"" String L" found: %p", (void*)Variable)

#define _FIND_VAR_INIT(Pattern, String, Variable) \
    _FIND_VAR_ADDR(Context.ResolveTyped<MILAN_VARCAT(t_, Variable)>(Pattern), String, Variable)


#define FIND_VAR_ONE(Variable, Pattern)  _FIND_VAR_INIT(Pattern, #Variable, Variable)
#define FIND_VAR_TWO(Class, Variable, Pattern)  _FIND_VAR_INIT(Pattern, #Class "::" #Variable, MILAN_VARCAT(MILAN_VARCAT(Class, _), Variable))


// ! Globals subsystem.
// ========================================

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

        // WndProc is not hooked but replaced using Win32 mechanism, see ::Milan::Imgui::OnSwapChainPresent for more.

        WndProc_orig = Context.ResolveTyped<t_WndProc>(GLOBAL_WNDPROC_RVA);
        MILAN_DEBUGW(L"WndProc found: %p", (void*)WndProc_orig);

        // From this point on, use the macro system to simplify search and installation.

        INSTALL_HOOK_ONE(BioRequestExit, GLOBAL_BIOREQUESTEXIT_PHOOK);
        INSTALL_HOOK_TWO_ADDR(IDXGISwapChain, Present, ::Milan::Imgui::FindSwapChainPresent());

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

        INSTALL_HOOK_TWO(UWorld, Tick, GLOBAL_UWORLD_TICK_PHOOK);


        // Network debug hooks.
        // ----------------------------------------

#if !defined(_FINAL_RELEASE)

        // Full-function hooks:

        INSTALL_HOOK_TWO(UActorChannel, ReplicateActor, GLOBAL_UACTORCHANNEL_REPLICATEACTOR_PHOOK);
        INSTALL_HOOK_TWO(UNetConnection, ClientSupportsLevel, GLOBAL_UNETCONNECTION_CLIENTSUPPORTSLEVEL_PATTERN);
        INSTALL_HOOK_TWO(UNetConnection, Close, GLOBAL_UNETCONNECTION_CLOSE_PATTERN);
        INSTALL_HOOK_TWO(UPackageMap, SupportsObject, GLOBAL_UPACKAGEMAP_SUPPORTSOBJECT_PATTERN);
        INSTALL_HOOK_TWO(UNetPendingLevel, NotifyControlMessage, GLOBAL_UNETPENDINGLEVEL_NOTIFYCONTROLMESSAGE_PHOOK);
        INSTALL_HOOK_TWO(UWorld, NotifyControlMessage, GLOBAL_UWORLD_NOTIFYCONTROLMESSAGE_PHOOK);
        INSTALL_HOOK_TWO(UWorld, ServerTickClients, GLOBAL_UWORLD_SERVERTICKCLIENTS_PHOOK);
        INSTALL_HOOK_ONE(SortReplicatedActors, GLOBAL_SORT_REPLICATED_ACTORS_OFFSET);
        INSTALL_HOOK_TWO(FSocketWin, SendTo, GLOBAL_FSOCKETWIN_SENDTO_OFFSET);
        INSTALL_HOOK_TWO(FSocketWin, Send, GLOBAL_FSOCKETWIN_SEND_OFFSET);
        INSTALL_HOOK_TWO(FSocketWin, RecvFrom, GLOBAL_FSOCKETWIN_RECVFROM_OFFSET);
        INSTALL_HOOK_TWO(FSocketWin, Recv, GLOBAL_FSOCKETWIN_RECV_OFFSET);


        // Mid-function injections:

        [[maybe_unused]]
        ::CaveIndexer LocalCaveIndexer{ /* MinSize: */ 20 };

        ::MiddleIntercept::Install(L"UChannel::SendBunch-1", 0x48E3D9, MlnUChannelSendBunch1, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UChannel::SendBunch-2", 0x48E645, MlnUChannelSendBunch2, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UActorChannel::ReplicateActor-1", 0x48C82C, MlnUActorChannelReplicateActor1, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UActorChannel::ReplicateActor-2", 0x48D3B0, MlnUActorChannelReplicateActor2, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UActorChannel::ReplicateActor-3", 0x48D43D, MlnUActorChannelReplicateActor3, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"InternalReplicateFunction", 0x709CFC, MlnInternalReplicateFunction1, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"SeamlessTravelTick", 0x8CA57B, MlnSeamlessTravelTick1, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UTcpNetDriverTickDispatch", 0x90CEDA, MlnUTcpNetDriverTickDispatch, LocalCaveIndexer);

        ::MiddleIntercept::Install(L"UWorldNotifyControlMessageHello", 0x8C48FD, MlnUWorldNotifyControlMessageHello, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UWorldNotifyControlMessageNetspeed", 0x8C4B96, MlnUWorldNotifyControlMessageNetspeed, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UWorldNotifyControlMessageLogin", 0x8C4FA2, MlnUWorldNotifyControlMessageLogin, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UWorldNotifyControlMessageUses", 0x8C43D7, MlnUWorldNotifyControlMessageUses, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UWorldNotifyControlMessageHave", 0x8C4C39, MlnUWorldNotifyControlMessageHave, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UWorldNotifyControlMessageJoin", 0x8C518B, MlnUWorldNotifyControlMessageJoin, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UWorldNotifyControlMessageAbort", 0x8C4DAE, MlnUWorldNotifyControlMessageAbort, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UWorldNotifyControlMessageUnload", 0x8C4309, MlnUWorldNotifyControlMessageUnload, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UWorldNotifyControlMessageUnknown19", 0x8C42B3, MlnUWorldNotifyControlMessageUnknown19, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UWorldNotifyControlMessageUnknown21", 0x8C57B2, MlnUWorldNotifyControlMessageUnknown21, LocalCaveIndexer);

        ::MiddleIntercept::Install(L"UNetPendingLevelNotifyControlMessageWelcome", 0x660814, MlnUNetPendingLevelNotifyControlMessageWelcome, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UNetPendingLevelNotifyControlMessageChallenge", 0x6600F7, MlnUNetPendingLevelNotifyControlMessageChallenge, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UNetPendingLevelNotifyControlMessageUses", 0x65FB11, MlnUNetPendingLevelNotifyControlMessageUses, LocalCaveIndexer);
        ::MiddleIntercept::Install(L"UNetPendingLevelNotifyControlMessageDlMngr", 0x66075A, MlnUNetPendingLevelNotifyControlMessageDlMngr, LocalCaveIndexer);

#endif

        // ----------------------------------------
        // Network debug end.


        MILAN_INFOW(L"globals system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"globals system terminating");
    }


    // ! Public interface.
    // ========================================

    wchar_t const* GetCmdString()
    {
        static wchar_t const* String = nullptr;
        if (String == nullptr) String = ::GetCommandLineW();
        return String;
    }

    wchar_t const* GetCmdArgs()
    {
        static wchar_t const* String = nullptr;
        if (String == nullptr)
        {
            // Algorithm originates from Wine's implementation.
            // https://stackoverflow.com/a/36876057

            wchar_t* Args = ::GetCommandLineW();
            if (*Args == L'"')
            {
                ++Args;
                while (*Args)
                {
                    if (*Args++ == L'"')
                        break;
                }
            }
            else
            {
                while (*Args && *Args != L' ' && *Args != L'\t')
                    ++Args;
            }

            String = Args;
        }
        return String;
    }

}
