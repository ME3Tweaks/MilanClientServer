#include <array>

#include "Base/IOUtils.hpp"
#include "Base/Logger.hpp"
#include "Components/CCommands.hpp"
#ifndef _FINAL_RELEASE
#include "Components/CDesync.hpp"
#endif
#include "Components/CGlobals.hpp"
#ifdef WITH_IMGUI
#include "Components/CImgui.hpp"
#endif
#include "Components/CMatchmaking.hpp"
#include "Components/COutput.hpp"
#include "Components/CProfilers.hpp"
#include "Components/CState.hpp"
#include "Components/CRenderdoc.hpp"
#include "Engine/ConsoleAutoComplete.hpp"
#include "Engine/ExtObject.hpp"
#include "Engine/Hooks.hpp"
#include "Engine/Intercept.hpp"
#include "Engine/LinkerPatcher.hpp"
#include "Engine/LoadGate.hpp"
#include "Misc/GuidCacheUtils.hpp"
#include "NetProf/NetProfStream.hpp"
#include "Online/SFXOnlineHijacks.hpp"
#include "Shell/CmdArgs.hpp"
#include "Shell/Entry.hpp"
#include "Version/SharedVersion.h"


bool gb_logCallFunction = false;
bool gb_logProcessDelegate = false;
bool gb_logProcessEvent = false;
bool gb_logProcessInternal = false;
bool gb_logSerializeObject = false;

#ifdef WITH_IMGUI
t_WndProc* WndProc_orig = nullptr;
LRESULT CALLBACK WndProc_hook(HWND const hWnd, UINT const uMsg, WPARAM const wParam, LPARAM const lParam)
{
    if (COMPONENT(CImgui).On_WndProc(hWnd, uMsg, wParam, lParam)) return TRUE;
    return (*WndProc_orig)(hWnd, uMsg, wParam, lParam);
}

t_IDXGISwapChain_Present* IDXGISwapChain_Present_orig = nullptr;
HRESULT IDXGISwapChain_Present_hook(IDXGISwapChain* const pChain, UINT const SyncInterval, UINT const Flags)
{
    if (COMPONENT(CImgui).On_SwapChainPresent(pChain, SyncInterval, Flags)) return S_OK;
    return IDXGISwapChain_Present_orig(pChain, SyncInterval, Flags);
}
#endif

/// <summary>
/// Applies the milan icon to the specified HWND
/// </summary>
/// <param name="windowHwnd"></param>
void ApplyIconToWindow(HWND windowHwnd) {
    HICON hIconBig = (HICON)LoadImageW(
        g_hModule, MAKEINTRESOURCEW(MILAN_WINDOW_ICON),
        IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);

    HICON hIconSmall = (HICON)LoadImageW(
        g_hModule, MAKEINTRESOURCEW(MILAN_WINDOW_ICON),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    if (hIconBig)   SendMessageW(windowHwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    if (hIconSmall) SendMessageW(windowHwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);

    // Some taskbar/Alt+Tab paths read the class icon instead of the
    // instance icon, so set both to be safe.
    if (hIconBig)   SetClassLongPtrW(windowHwnd, GCLP_HICON, (LONG_PTR)hIconBig);
    if (hIconSmall) SetClassLongPtrW(windowHwnd, GCLP_HICONSM, (LONG_PTR)hIconSmall);
}

t_CreateWindowExW* CreateWindowExW_orig = nullptr;
HWND WINAPI CreateWindowExW_hook(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    auto windowHwnd = (*CreateWindowExW_orig)(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (nullptr != lpWindowName) {
        // Look for title Mass Effect 3
        if (0 == wcscmp(lpWindowName, L"Mass Effect 3") || 0 == wcscmp(lpWindowName, L"SplashScreen"))
        {
            ApplyIconToWindow(windowHwnd);
        }
    }
    return windowHwnd;
}


t_BioRequestExit* BioRequestExit_orig = nullptr;
void BioRequestExit_hook(UBOOL const bForce, INT const ExitCode, INT const Unused)
{
#ifndef _FINAL_RELEASE
    if (gp_mainLogger != nullptr)
    {
        if (bForce)
        {
            MILAN_WARNW(ELOG_EngineHooks, L"BioRequestExit: bForce = TRUE, ExitCode = %d", ExitCode);
            ::Sleep(1000);
        }
        else
        {
            MILAN_DEBUGW(ELOG_EngineHooks, L"BioRequestExit: bForce = FALSE, ExitCode = %d", ExitCode);
        }
    }
#endif

    (*BioRequestExit_orig)(bForce, ExitCode, Unused);
}


// ! UCanvas methods.
// ========================================

t_UUIString_StringSize* UUIString_StringSize = nullptr;
t_UCanvas_WrappedPrint* UCanvas_WrappedPrint = nullptr;


// ! UFunction methods.
// ========================================

t_UFunction_Bind* UFunction_Bind_orig = nullptr;
void UFunction_Bind_hook(UFunction* const Context)
{
    (*UFunction_Bind_orig)(Context);
}


// ! UGameEngine methods.
// ========================================

t_UGameEngine_Exec* UGameEngine_Exec_orig = nullptr;
UBOOL UGameEngine_Exec_hook(UGameEngine* const Context, WCHAR const* const Command, void* const Output)
{
    if (COMPONENT(CCommands).On_UGameEngineExec(Context, Command, Output)) return TRUE;
    return (*UGameEngine_Exec_orig)(Context, Command, Output);
}

t_UGameEngine_LoadMap* UGameEngine_LoadMap_orig = nullptr;
UBOOL UGameEngine_LoadMap_hook(UGameEngine* const Context, FURL* const Url, UPendingLevel* const Level, FString* const Error)
{
    MILAN_TRACEW(ELOG_EngineHooks, L"UGameEngine::LoadMap: Url = %s...", *Url->Map);
    NETPROF(CaptureLevelStart(Url));

    auto const Result = (*UGameEngine_LoadMap_orig)(Context, Url, Level, Error);
    if (!Result) MILAN_ERRORW(ELOG_EngineHooks, L"UGameEngine::LoadMap failed: %s", *(*Error));

    // Refreshes exec-function/map completions for the new level (e.g. DLC-added commands).
    if (Result) ConsoleAutoComplete::RebuildForCurrentLevel();

    return Result;
}

t_UGameEngine_Tick* UGameEngine_Tick_orig = nullptr;
void UGameEngine_Tick_hook(UGameEngine* const Context, float const Delta)
{
    //MILAN_TRACEW(L"(UGameEngine::Tick): Delta=%f", Delta);
    NETPROF(CaptureFrameStart(Delta));
    (*UGameEngine_Tick_orig)(Context, Delta);
    COMPONENT(CMatchmaking).On_GameEngineTick(Delta);
#ifndef _FINAL_RELEASE
    COMPONENT(CDesync).On_GameEngineTick(Delta);
#endif
}


// ! Package loading and linker methods.
// ========================================

t_LoadPackage* LoadPackage_orig = nullptr;
UPackage* LoadPackage_hook(UObject* const Outer, WCHAR const* const Filename, INT const Flags)
{
    // Installed before anything else in attach; parks this thread until attach has finished.
    LoadGate::Wait(Filename);

    // Only reachable with -noloadgate or a timed-out gate: nothing (logger included) is set up yet.
    if (!LoadGate::IsOpen())
        return (*LoadPackage_orig)(Outer, Filename, Flags);

    LinkerPatcher::EnsureConfigLoaded();

    UPackage* const Result = (*LoadPackage_orig)(Outer, Filename, Flags);

    if (!ConsoleAutoComplete::TryInstall())
    {
        MILAN_WARNW(ELOG_EngineHooks, L"Engine.Console class not available after LoadPackage('%s'); retrying after next package load", Filename);
    }

    return Result;
}

t_FPackageFileCache_FindPackageFile* FPackageFileCache_FindPackageFile_orig = nullptr;
UBOOL FPackageFileCache_FindPackageFile_hook(void* const Context, WCHAR const* const InName, FGuid* const Guid,
    FString* const OutFileName, WCHAR const* const Language, UBOOL const Unknown)
{
    UBOOL const Result = (*FPackageFileCache_FindPackageFile_orig)(Context, InName, Guid, OutFileName, Language, Unknown);
    if (Result) return Result;

    // Patch packages live outside PCConsoleTOC.bin, so the original lookup can never find them.
    return LinkerPatcher::ResolvePatchPackageFile(InName, OutFileName) ? TRUE : FALSE;
}

t_ULinkerLoad_CreateExport* ULinkerLoad_CreateExport_orig = nullptr;
UObject* ULinkerLoad_CreateExport_hook(ULinkerLoad* const Context, INT const Index)
{
    return LinkerPatcher::CreateExport(Context, Index);
}

t_ULinkerLoad_FinalizeCreation* ULinkerLoad_FinalizeCreation_orig = nullptr;
INT ULinkerLoad_FinalizeCreation_hook(ULinkerLoad* const Context)
{
    return LinkerPatcher::FinalizeCreation(Context);
}

t_UObject_EndLoad* UObject_EndLoad_orig = nullptr;
void UObject_EndLoad_hook()
{
    LinkerPatcher::EndLoad();
}

t_UObject_StaticFindObject* UObject_StaticFindObject_orig = nullptr;
UObject* UObject_StaticFindObject_hook(UClass* const ObjectClass, UObject* const InObjectPackage, WCHAR const* const OrigInName, UBOOL const bExactClass)
{
    return LinkerPatcher::StaticFindObject(ObjectClass, InObjectPackage, OrigInName, bExactClass);
}

t_UObject_StaticLoadObject* UObject_StaticLoadObject_orig = nullptr;
UObject* UObject_StaticLoadObject_hook(UClass* const ObjectClass, UObject* const InOuter, WCHAR const* const InName, WCHAR const* const Filename,
    INT const LoadFlags, UPackageMap* const Sandbox, UBOOL const bAllowObjectReconciliation)
{
    return LinkerPatcher::StaticLoadObject(ObjectClass, InOuter, InName, Filename, LoadFlags, Sandbox, bAllowObjectReconciliation);
}


// ! UObject methods.
// ========================================

t_UObject_CallFunction* UObject_CallFunction_orig = nullptr;
void UObject_CallFunction_hook(UObject* const Context, FFrame* const Stack, void* const Result, UFunction* const Function)
{
#ifndef _FINAL_RELEASE
    // --- DEBUG CACHE BLOCK ---
    // thread_local keeps these private per execution thread. 
    // They persist across frames so you can inspect them anytime you break inside this thread.
    static thread_local FString DebugContextName{};
    static thread_local FString DebugFunctionName{};

    DebugContextName.Clear();
    DebugFunctionName.Clear();

    if (Context)
    {
        Context->AppendName(DebugContextName, SFXName::k_formatBasic);
    }
    else
    {
        DebugContextName = L"None";
    }

    if (Function)
    {
        Function->AppendName(DebugFunctionName, SFXName::k_formatBasic);
    }
    else
    {
        DebugFunctionName = L"None";
    }
    // -------------------------

    if (gb_logCallFunction)
    {
        MILAN_TRACEW(L"UObject::CallFunction: %s.%s", *DebugContextName, *DebugFunctionName);
    }
#endif

#ifndef _FINAL_RELEASE
    if (Function == nullptr)
    {
        MILAN_ERRORW(ELOG_EngineHooks, L"UObject::CallFunction: %s", *Context->StaticFullName());
        MILAN_ERRORW(ELOG_EngineHooks, L"  Function is null, stack dumped:");

        int i = 0;
        for (FFrame* Current = Stack; Current; Current = Current->PreviousFrame)
        {
            MILAN_WARNW(ELOG_EngineHooks, L"%3d Stack.Object = %s\n", i, Current->Object ? *Current->Object->StaticFullName() : L"???");
            MILAN_WARNW(ELOG_EngineHooks, L"    Stack.Node = %s\n", Current->Node ? *Current->Node->StaticFullName() : L"???");
            --i;
        }

        MILAN_BREAK_ATTACHED();
    }
#endif

    (*UObject_CallFunction_orig)(Context, Stack, Result, Function);
}

t_UObject_ProcessDelegate* UObject_ProcessDelegate_orig = nullptr;
void UObject_ProcessDelegate_hook(UObject* const Context, SFXName const DelegateName,
    FScriptDelegate* const Delegate, void* const Parms, void* const Result)
{
#ifndef _FINAL_RELEASE
    if (gb_logProcessDelegate)
    {
        static FString ContextName{}, StrDelegateName{}, DelegateContextName{}, StrDelegateFunctionName{};

        ContextName.Clear();
        StrDelegateName.Clear();
        DelegateContextName.Clear();
        StrDelegateFunctionName.Clear();

        Context->AppendName(ContextName, SFXName::k_formatBasic);
        DelegateName.AppendToString(StrDelegateName, SFXName::k_formatBasic);
        if (Delegate->Object) Delegate->Object->AppendName(DelegateContextName, SFXName::k_formatBasic);
        Delegate->FunctionName.AppendToString(StrDelegateFunctionName, SFXName::k_formatBasic);

        MILAN_TRACEW(L"UObject::ProcessDelegate:");
        MILAN_TRACEW(L"  Context: %s", *ContextName);
        MILAN_TRACEW(L"  DelegateName: %s", *StrDelegateName);
        MILAN_TRACEW(L"  Delegate.Object: %s", *DelegateContextName);
        MILAN_TRACEW(L"  Delegate.Function: %s", *StrDelegateFunctionName);
    }
#endif

    (*UObject_ProcessDelegate_orig)(Context, DelegateName, Delegate, Parms, Result);
}

t_UObject_ProcessEvent* UObject_ProcessEvent_orig = nullptr;
void UObject_ProcessEvent_hook(UObject* const Context, UFunction* const Function, void* const Parms, void* const Result)
{
#ifndef _FINAL_RELEASE
    if (gb_logProcessEvent)
    {
        static FString ObjectName{}, FunctionName{};

        ObjectName.Clear();
        FunctionName.Clear();

        Context->AppendName(ObjectName, SFXName::k_formatBasic);
        Function->AppendName(FunctionName, SFXName::k_formatBasic);

        MILAN_DEBUGW(L"UObject::ProcessEvent: %s.%s", *ObjectName, *FunctionName);
    }
#endif

    // We can't do stuff in SFXOnlineFoundation on MilanHost initialization because classes might not have been fully loaded yet.
    // And we can't place this logic in ProcessInternal so as to minimize the chance of replacing UFunction::Func right from under it.
    static bool s_sfxOnlineInitialized = false;
    if (!s_sfxOnlineInitialized && Function->GetOutermost()->Name == SFXName(L"SFXOnlineFoundation", 0))
    {
        RelativeTimer HijackTimer{};

        SFXOnlineHijacks::InstallComponentBlazeGame();
        SFXOnlineHijacks::InstallComponentBlazeLogin();
        SFXOnlineHijacks::InstallComponentBlazeHub();
        SFXOnlineHijacks::InstallComponentUnrealPlayer();
        s_sfxOnlineInitialized = true;

        MILAN_INFOW(ELOG_EngineHooks, L"Installed SFXOnlineFoundation hijacks in %3.2f ms", HijackTimer.GetMilliseconds());
    }

    //if (IsFunctionWithinDerivedClass(Function, L"SFXOnlineComponentImageManager"))
    //    MILAN_TRACEW(ELOG_DevelUtils, L"%S: %s", __func__, *Function->GetFullName());

#ifndef _FINAL_RELEASE
    // Desync snapshot requests ride on ClientMessage / ServerMutate and must not reach script.
    if (COMPONENT(CDesync).On_UObject_ProcessEvent(Context, Function, Parms)) return;
#endif

    COMPONENT(CProfilers).On_UObject_ProcessEvent(Context, Function, Parms, Result);
    COMPONENT(CRenderdoc).On_UObject_ProcessEvent(Context, Function, Parms, Result);

    (*UObject_ProcessEvent_orig)(Context, Function, Parms, Result);

    UClass* const ObjectClass = Context->Class;
    MILAN_UNUSED(ObjectClass);

#ifndef _FINAL_RELEASE

    static constexpr auto kbForceFailureNetMessagePrelogin = false;
    if (kbForceFailureNetMessagePrelogin && Context->IsA<AGameInfo>() && Function->StaticName().Contains(L"PreLogin"))
    {
        auto const CastParms = reinterpret_cast<AGameInfo_eventPreLogin_Parms*>(Parms);
        MILAN_DEBUGW(ELOG_EngineHooks, L"AGameInfo::PreLogin: Options = %s, Address = %s, Error = %s",
            *CastParms->Options, *CastParms->Address, *CastParms->ErrorMessage);

        if (CastParms->ErrorMessage.Empty())
            CastParms->ErrorMessage = L"debug forced error";
    }

#endif
}

t_UObject_ProcessInternal* UObject_ProcessInternal_orig = nullptr;
void UObject_ProcessInternal_hook(UObject* const Context, FFrame* const Stack, void* const Result)
{
    [[maybe_unused]] bool bLogAnyway = false;
    [[maybe_unused]] bool bCallOriginal = true;

#ifndef _FINAL_RELEASE
    if (gb_logProcessInternal || bLogAnyway)
    {
        static FString ObjectName{}, FunctionName{};

        ObjectName.Clear();
        FunctionName.Clear();

        Context->AppendName(ObjectName, SFXName::k_formatBasic);
        Stack->Node->AppendName(FunctionName, SFXName::k_formatBasic);

        MILAN_DEBUGW(L"UObject::ProcessInternal: %s.%s", *ObjectName, *FunctionName);
    }
#endif

    //if (IsFunctionWithinDerivedClass(Stack->Node, L"SFXOnlineComponentImageManager"))
    //    MILAN_TRACEW(ELOG_DevelUtils, L"%S: %s", __func__, *Stack->Node->GetFullName());

    if (!COMPONENT(CState).On_UObject_ProcessInternal_Pre(Context, Stack, Result))
    {
        bCallOriginal = false;
    }
    if (!COMPONENT(CMatchmaking).On_ScriptCall(Stack->Node))
        bCallOriginal = false;

    if (bCallOriginal)
    {
        (*UObject_ProcessInternal_orig)(Context, Stack, Result);
        COMPONENT(CState).On_UObject_ProcessInternal_Post(Context, Stack, Result);
    }

#ifndef _FINAL_RELEASE

    // Utilities for speeding up multiplayer launch.

    static bool s_skipMainMenuChecked = false;
    static bool s_skipMainMenu = false;

    if (!s_skipMainMenuChecked)
    {
        s_skipMainMenuChecked = true;
        s_skipMainMenu = CmdArgs::Contain(L"-skiptomp");
    }

    if (s_skipMainMenu)
    {
        static bool s_checkMainMenuSplash = true;
        static FunctionNamedIntercept s_checkMainMenuIntercept{};

        if (s_checkMainMenuSplash && s_checkMainMenuIntercept
            .Applies(Stack->Node, L"SFXGame", L"BioSeqAct_ShouldShowSplashSequence", L"Activated"))
        {
            s_checkMainMenuSplash = false;

            auto* const Action = Context->CastDirect<UBioSeqAct_ShouldShowSplashSequence>();
            Action->GetWorldInfo()->Game->ShowSplashScreen = FALSE;
            Action->OutputLinks(0).bHasImpulse = FALSE;
            Action->OutputLinks(1).bHasImpulse = TRUE;
        }

        static bool s_checkMainMenuLobbyFlow = true;
        static FunctionNamedIntercept s_checkMainMenuLobbyFlowIntercept{};

        if (s_checkMainMenuLobbyFlow && s_checkMainMenuLobbyFlowIntercept
            .Applies(Stack->Node, L"SFXGameContentLiveKismet", L"BioSeqAct_ShowMainMenu", L"Activated"))
        {
            s_checkMainMenuLobbyFlow = false;
            for (TypedObjectIterator<USFXGUI_MainMenu_RTT, false, false> Iter{}; Iter; ++Iter)
                (*Iter)->eventStartMultiplayerFlow();
        }
    }

    // Additional information in the title bar for debugging.

    static bool s_updateWindow = true;
    if (s_updateWindow)
    {
        for (TypedObjectIterator<UGameViewportClient, true, false> Iter{}; Iter; ++Iter)
        {
            auto const Viewport = (*Iter)->Viewport;
            if (Viewport == nullptr) break;

            auto const Window = reinterpret_cast<HWND*>(reinterpret_cast<unsigned char*>(Viewport) + 0xC4);
            auto const Name = reinterpret_cast<FString*>(reinterpret_cast<unsigned char*>(Viewport) + 0xD4);

            MILAN_TRACEW(ELOG_Startup, L"Updating title for %s, viewport at %p", *(*Iter)->StaticName(), Viewport);

            *Name = FString::Printf(L"Project Milan (INST = %d, PID = %d)", GetGameInstanceOrdinal(), ::GetCurrentProcessId());
            DefWindowProcW(*Window, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(Name->Chars()));

            s_updateWindow = false;
        }
    }
#endif
}

t_UObject_SkipFunction* UObject_SkipFunction = nullptr;


// ! USFXEngine methods.
// ========================================

t_USFXEngine_UpdateDynamicLoadMappingFindHash* USFXEngine_UpdateDynamicLoadMappingFindHash_orig = nullptr;
int USFXEngine_UpdateDynamicLoadMappingFindHash_hook(USFXEngine* const Context, int const ObjectNetID)
{
    return (*USFXEngine_UpdateDynamicLoadMappingFindHash_orig)(Context, ObjectNetID);
}

// UGameEngine::Browse loads this map for ?closed and ?failed (the "disconnect" command, a lost
// or failed connection), and the host-left prompt opens it. LE3 returns the lobby map only when
// signed in to Blaze, which LMP never is, so leaving a lobby or match fell back to EntryMenu.
// Away from the local map it now returns to the multiplayer menu. Exit Multiplayer opens
// EntryMenu directly and is unaffected.
t_USFXEngine_GetDisconnectFallbackMap* USFXEngine_GetDisconnectFallbackMap_orig = nullptr;
FString* USFXEngine_GetDisconnectFallbackMap_hook(USFXEngine* const Context, FString* const Result)
{
    (*USFXEngine_GetDisconnectFallbackMap_orig)(Context, Result);

    UWorld* const World = (GWorld != nullptr) ? *GWorld : nullptr;
    if (World == nullptr || World->Outer == nullptr || Result->StartsWith(*World->Outer->GetName(), true))
        return Result;

    FString LobbyMap = Context->GetDefaultLobbyMap();
    MILAN_DEBUGW(ELOG_EngineHooks, L"disconnect fallback map: %s instead of %s", *LobbyMap, **Result);
    *Result = std::move(LobbyMap);
    return Result;
}


// ! UWorld methods.
// ========================================

t_UWorld_SpawnActor* UWorld_SpawnActor_orig = nullptr;
AActor* UWorld_SpawnActor_hook(
    UWorld* const Context,
    UClass* const Class, SFXName const Name,
    FVector* const Location, FRotator* const Rotation,
    AActor* const Template,
    int const Unknown1, int const Unknown2,
    AActor* const Owner, AActor* const Instigator,
    int const Unknown4)
{
    MILAN_UNUSED(Context);
    MILAN_UNUSED_2(Class, Name);
    MILAN_UNUSED_2(Location, Rotation);
    MILAN_UNUSED(Template);
    MILAN_UNUSED_2(Unknown1, Unknown2);
    MILAN_UNUSED_2(Owner, Instigator);
    MILAN_UNUSED(Unknown4);

    static std::uint64_t s_errorCounter = 0u;

#ifndef _FINAL_RELEASE
    if (Class == nullptr || !Class->IsChildOf<AActor>())  [[unlikely]]
    {
        MILAN_WARNW(ELOG_EngineHooks, L"UWorld::SpawnActor [%llu] will fail: Class is null or not an actor", s_errorCounter);
    }

    static constexpr std::uint32_t k_classFlagAbstract = 0x00000001;
    static constexpr std::uint32_t k_classFlagDeprecated = 0x02000000;
    if (!!(Class->ClassFlags & k_classFlagAbstract) || !!(Class->ClassFlags & k_classFlagDeprecated))  [[unlikely]]
    {
        MILAN_WARNW(ELOG_EngineHooks, L"UWorld::SpawnActor [%llu] will fail: Class is abstract or deprecated", s_errorCounter);
    }

    AActor* const Default = Class->ClassDefaultObject ? Class->ClassDefaultObject->Cast<AActor>() : nullptr;
    if (ActorUtils::GlobalBegunPlay() && Default != nullptr && (Default->bStatic || Default->bNoDelete)) [[unlikely]]
    {
        MILAN_WARNW(ELOG_EngineHooks, L"UWorld::SpawnActor [%llu] will fail: CDO is bStatic or bNoDelete and GWorld has begun play", s_errorCounter);
    }

    if (Template != nullptr && Template->Class != Class)  [[unlikely]]
    {
        MILAN_WARNW(ELOG_EngineHooks, L"UWorld::SpawnActor [%llu] will fail: Template (%s) != Class (%s)", s_errorCounter, *Template->Class->GetName(), *Class->GetName());
    }

#endif // ! _FINAL_RELEASE

    //MILAN_TRACEW(ELOG_EngineHooks, L"UWorld::SpawnActor: class = %s, template = %s, name = %s",
    //    Class ? *Class->GetName() : L"n/a", Template ? *Template->GetName() : L"n/a", *Name.ToString());

    AActor* const Spawned = (*UWorld_SpawnActor_orig)(
        Context,
        Class, Name,
        Location, Rotation,
        Template,
        Unknown1, Unknown2,
        Owner, Instigator,
        Unknown4);

    if (Spawned == nullptr)  [[unlikely]]
    {
        // Avoid polluting log if we can attribute spawn failure to a known issue.
        bool bAvoidLogging = false;

        if (Class != nullptr && Class->GetName().Equals(L"SFXMutator_DifficultySpeed"))
        {
            AWorldInfo* const WorldInfo = ActorUtils::GlobalWorldInfo();
            if (WorldInfo && (WorldInfo->bIsMenuLevel || WorldInfo->bIsUIWorld || WorldInfo->bIsLobbyLevel))
                bAvoidLogging = true;
        }

        if (!bAvoidLogging)
        {
            MILAN_WARNW(ELOG_EngineHooks, L"UWorld::SpawnActor: [%llu] failed, class = %s, template = %s, name = %s",
                s_errorCounter, Class ? *Class->GetName() : L"n/a", Template ? *Template->GetName() : L"n/a", *Name.ToString());
        }
    }

    s_errorCounter++;
    return Spawned;
}

t_UWorld_Tick* UWorld_Tick_orig = nullptr;
void UWorld_Tick_hook(UWorld* const Context, int const Unknown, float const Delta)
{
    (*UWorld_Tick_orig)(Context, Unknown, Delta);
    COMPONENT(CState).On_UWorld_Tick(Context, Unknown, Delta);
}


// ! Game socket I/O (all builds: matchmaking shares the UE3 socket).
// ========================================

t_FSocketWin_SendTo* FSocketWin_SendTo_orig = nullptr;
UINT FSocketWin_SendTo_hook(FSocket* Context, BYTE* Data, INT Count, INT* Sent, SOCKADDR_IN* Destination)
{
    UINT const Ret = (*FSocketWin_SendTo_orig)(Context, Data, Count, Sent, Destination);
    if (Count > 0)
    {
        // This would be too verbose even for ELOG_NetHooksTrace.

        //auto const Remote = FString::Printf(L"%S:%hu", inet_ntoa(Destination->sin_addr), ntohs(Destination->sin_port));
        //MILAN_TRACEW(ELOG_NetHooksTrace, L"FSocketWin::SendTo: type=%d, desc=%s, count=%d, sent=%d, dest=%s", Context->Type, *Context->Description, Count, *Sent, *Remote);

#ifndef _FINAL_RELEASE
        NETPROF(CaptureSocketCall(Destination, true, *Sent * 8));
#else
        MILAN_UNUSED_2(Context, Destination);
#endif
    }
    return Ret;
}

t_FSocketWin_RecvFrom* FSocketWin_RecvFrom_orig = nullptr;
UINT FSocketWin_RecvFrom_hook(FSocket* Context, BYTE* Buffer, INT Max, INT* Read, SOCKADDR_IN* Source)
{
    // Matchmaking datagrams share the game socket. Consume them here and read the next
    // datagram, so UE3 only ever sees its own traffic. The last call's result (and its
    // Winsock error, e.g. WSAEWOULDBLOCK) is what UE3 observes, as without the hook.
    for (int Consumed = 0; ; ++Consumed)
    {
        if (Consumed == 256)
        {
            // Yield to the frame instead of draining an unbounded flood.
            *Read = -1;
            ::WSASetLastError(WSAEWOULDBLOCK);
            return FALSE;
        }

        UINT const Ret = (*FSocketWin_RecvFrom_orig)(Context, Buffer, Max, Read, Source);
        if (Ret && *Read > 0 && COMPONENT(CMatchmaking).On_ReceivedDatagram(Context, Buffer, *Read, Source))
            continue;

        if (*Read > 0)
        {
            // This would be too verbose even for ELOG_NetHooksTrace.

            //auto const Remote = FString::Printf(L"%S:%hu", inet_ntoa(Source->sin_addr), ntohs(Source->sin_port));
            //MILAN_TRACEW(ELOG_NetHooksTrace, L"FSocketWin::RecvFrom: type=%d, desc=%s, read=%d, source=%s", Context->Type, *Context->Description, *Read, *Remote);

#ifndef _FINAL_RELEASE
            NETPROF(CaptureSocketCall(Source, false, *Read * 8));
#endif
        }
        return Ret;
    }
}


// A joiner resolves each package the host announces from UWorld::Tick, once per tick until this
// returns nonzero. For a package it lacks, the engine starts an async load and returns 0; on the
// next tick it finds the package object that load created and compares GUIDs at once, before the
// load has read the GUID from the file. The zero GUID fails the comparison and the joiner drops
// with "Package '...' version mismatch". Large on-demand packages (Milan's GUI image packages)
// stay in that state for several ticks, so wait while the package is still loading.
t_Net_VerifyPackageInfo* Net_VerifyPackageInfo_orig = nullptr;
void* Net_VerifyPackageInfo_hook(void* const Context, FPackageInfo* const Info)
{
    constexpr unsigned long long k_rfAsyncLoading = 0x0000040000000000ull;

    if (Info != nullptr && UObject_StaticFindObject_orig != nullptr)
    {
        FString const Name = Info->PackageName.ToString();
        UObject* const Existing = (*UObject_StaticFindObject_orig)(UPackage::StaticClass(), nullptr, *Name, FALSE);
        if (Existing != nullptr && Existing->Outer == nullptr && (Existing->ObjectFlags & k_rfAsyncLoading) != 0)
        {
            static SFXName s_lastWaited{};
            if (!(s_lastWaited == Info->PackageName))
            {
                s_lastWaited = Info->PackageName;
                MILAN_DEBUGW(ELOG_NetHooks, L"package %s the host uses is still loading; checking it again next tick", *Name);
            }
            return nullptr;
        }
    }

    return (*Net_VerifyPackageInfo_orig)(Context, Info);
}


// ! <Methods relevant to network debug.>
// ========================================

#ifndef _FINAL_RELEASE

t_UActorChannel_ReplicateActor* UActorChannel_ReplicateActor_orig = nullptr;
void UActorChannel_ReplicateActor_hook(UActorChannel* const Context)
{
    MILAN_CHECKW(Context != nullptr, L"");

    if (false)
    {
        MILAN_DEBUGW(ELOG_NetHooksTrace, L"UActorChannel::ReplicateActor:");
        MILAN_DEBUGW(ELOG_NetHooksTrace, L"  Channel: %s", *Context->StaticFullName());
        MILAN_DEBUGW(ELOG_NetHooksTrace, L"    Props: OpenedLocally=%d, OpenPacketId=%d, NumInRec=%d, NumOutRec=%d, NegotiatedVer=%d",
            Context->OpenedLocally, Context->OpenPacketId, Context->NumInRec, Context->NumOutRec, Context->NegotiatedVer);
        MILAN_DEBUGW(ELOG_NetHooksTrace, L"  Actor: %s", Context->Actor ? *Context->Actor->StaticFullName() : L"n/a");
        if (Context->Actor != nullptr)
        {
            auto* const Actor = Context->Actor;
            MILAN_DEBUGW(ELOG_NetHooksTrace, L"    Props: bNetOwner=%d, bNetInitial=%d Top=%s",
                !!Actor->bNetOwner, !!Actor->bNetInitial, *Actor->StaticName());
        }
    }

    (*UActorChannel_ReplicateActor_orig)(Context);
}

t_UActorChannel_SetChannelActor* UActorChannel_SetChannelActor_orig = nullptr;
void UActorChannel_SetChannelActor_hook(UActorChannel* const Context, AActor* const Actor)
{
    MILAN_TRACEW(ELOG_NetHooksTrace, L"UActorChannel::SetChannelActor: %s <- %s", *Context->GetName(), *Actor->GetName());
    (*UActorChannel_SetChannelActor_orig)(Context, Actor);
}

t_UGuidCache_LookupPackageGuid* UGuidCache_LookupPackageGuid_orig = nullptr;
int UGuidCache_LookupPackageGuid_hook(UGuidCache* const Context, SFXName const Package, FGuid* const OutGuid)
{
    auto const bFound = (*UGuidCache_LookupPackageGuid_orig)(Context, Package, OutGuid);

    //if (!bFound)  [[unlikely]]
    //{
    //    MILAN_WARNW(ELOG_NetHooksTrace, L"UGuidCache::LookupPackageGuid failed for cache = '%s', package = '%s'",
    //        *Context->Filename, *Package.ToString());
    //}

#if defined(ENABLE_GUIDCACHE_OVERRIDE) && ENABLE_GUIDCACHE_OVERRIDE

    if (gp_guidCacheOverride != nullptr)
    {
        auto const Override = gp_guidCacheOverride->Get(Package);
        if (Override.has_value())
        {
            MILAN_WARNW(ELOG_NetHooks, L"UGuidCache::LookupPackageGuid using override for package = '%s', new guid = '%s'",
                *Package.ToString(), *(*Override).ToString());
            *OutGuid = *Override;
            return TRUE;
        }
    }

#endif

    return bFound;
}

t_UNetConnection_ClientSupportsLevel* UNetConnection_ClientSupportsLevel_orig = nullptr;
int UNetConnection_ClientSupportsLevel_hook(UNetConnection* const Context, UObject* const Object)
{
    int const Return = (*UNetConnection_ClientSupportsLevel_orig)(Context, Object);

    {
        if (Return != 0)
        {
            //MILAN_TRACEW(ELOG_NetHooksTrace, L"UNetConnection::ClientSupportsLevel: %s (%s) => TRUE",
            //    Context ? *Context->GetName() : L"???", Object ? *Object->GetFullName() : L"???");
        }
        else
        {
            MILAN_WARNW(ELOG_NetHooks, L"UNetConnection::ClientSupportsLevel: %s (%s) => FALSE",
                Context ? *Context->GetName() : L"???", Object ? *Object->GetFullName() : L"???");
        }
    }

    return Return;
}

t_UNetConnection_Close* UNetConnection_Close_orig = nullptr;
void UNetConnection_Close_hook(UNetConnection* const Context)
{
    if (Context->Driver != nullptr && Context->ConnectionState != USOCK_Closed)
    {
        FString RemoteAddress{};

        auto* const TcpipConnection = Context->Cast<UTcpipConnection>();
        if (TcpipConnection != nullptr)
        {
            auto const& Address = TcpipConnection->Address;
            RemoteAddress = FString::Printf(L"%S:%hu", inet_ntoa(Address.sin_addr), ntohs(Address.sin_port));
        }

        MILAN_TRACEW(ELOG_NetHooks, L"UNetConnection::Close, remote = %s", *RemoteAddress);
        NETPROF(CaptureSessionEvent(Context, L"CLOSE", L""));
    }

    (*UNetConnection_Close_orig)(Context);
}

t_UNetConnection_CreateChannel* UNetConnection_CreateChannel_orig = nullptr;
UChannel* UNetConnection_CreateChannel_hook(UNetConnection* const Context, EChannelType const Type, INT const bLocal, INT const Index)
{
    [[maybe_unused]]
    auto const DescType = [](EChannelType const T) -> FString
        {
            switch (T)
            {
            case CHTYPE_None: return L"None";
            case CHTYPE_Control: return L"Control";
            case CHTYPE_Actor: return L"Actor";
            case CHTYPE_File: return L"File";
            case CHTYPE_Voice: return L"Voice";
            }
            return FString::Printf(L"%d", (int)T);
        };

    MILAN_TRACEW(ELOG_NetHooksTrace, L"UNetConnection::CreateChannel: type = %s, local = %s, index = %d", *DescType(Type), bLocal ? L"true" : L"false", Index);
    UChannel* const Return = (*UNetConnection_CreateChannel_orig)(Context, Type, bLocal, Index);

    if (Return == nullptr)
    {
        MILAN_WARNW(ELOG_NetHooks, L"UNetConnection::CreateChannel:   failed, returning nullptr");
    }

    return Return;
}

t_UPackageMap_SupportsObject* UPackageMap_SupportsObject_orig = nullptr;
int UPackageMap_SupportsObject_hook(UPackageMap* const Context, UObject* const Object)
{
    int const Return = (*UPackageMap_SupportsObject_orig)(Context, Object);

    {
        //// This is left so that we can see PackageMap fields in an attached debugger.
        //auto* const PackageMap = reinterpret_cast<UPackageMap_Proxy*>(Context);
        //MILAN_UNUSED(PackageMap);

        if (Return != 0)
        {
            //MILAN_TRACEW(ELOG_NetHooksTrace, L"UPackageMap::SupportsObject: %s (%s) => TRUE",
            //    Context ? *Context->GetName() : L"???", Object ? *Object->GetFullName() : L"???");
        }
        else
        {
            auto const ObjectOutermost = [](UObject* const Object) -> UPackage*
                {
                    UObject* Outermost = Object;
                    for (; Outermost != nullptr && Outermost->Outer != nullptr; Outermost = Outermost->Outer);
                    return Outermost->Cast<UPackage>();
                };

            UPackage* const OutermostPackage = ObjectOutermost(Object);
            if (OutermostPackage == nullptr)
            {
                MILAN_WARNW(ELOG_NetHooks, L"UPackageMap::SupportsObject: %s => FALSE (outermost is NULL)",
                    Object ? *Object->GetFullName() : L"???");
                return Return;
            }

            int const* const ListIndex = Context->PackageMapList.Find(SFXName(OutermostPackage->Name));
            if (ListIndex == nullptr)
            {
                // Uncomment this variable to see available packages in Visual Studio debugger.
                //[[maybe_unused]] auto const DebugPackageMapList = CollectMapKeys(PackageMap->PackageMapList);

                MILAN_WARNW(ELOG_NetHooks, L"UPackageMap::SupportsObject: %s => FALSE (package=%s not found)",
                    Object ? *Object->GetFullName() : L"???", *OutermostPackage->Name.ToString());
                return Return;
            }

            FPackageInfo const& PackageInfo = Context->List((UINT)*ListIndex);
            if (Object->NetIndex >= PackageInfo.ObjectCount)
            {
                MILAN_WARNW(ELOG_NetHooks, L"UPackageMap::SupportsObject: %s => FALSE (NetIndex=%d >= PackageInfo.ObjectCount=%d)",
                    Object ? *Object->GetFullName() : L"???", Object->NetIndex, PackageInfo.ObjectCount);
                MILAN_WARNW(ELOG_NetHooks, L"  PackageInfo.PackageName = %s, PackageInfo.ObjectBase = %d",
                    *PackageInfo.PackageName.ToString(), PackageInfo.ObjectBase);
                return Return;
            }

            MILAN_WARNW(ELOG_NetHooks, L"UPackageMap::SupportsObject: %s => FALSE (NetIndex=%d)",
                Object ? *Object->GetFullName() : L"???", Object->NetIndex);
        }
    }

    return Return;
}

t_UPackageMap_SerializeObject* UPackageMap_SerializeObject_orig = nullptr;
int UPackageMap_SerializeObject_hook(UPackageMap* const Context, FArchive* const Archive, UClass* const Class, UObject** const Object)
{
    if (Archive->ArIsSaving && gb_logSerializeObject)
    {
        MILAN_DEBUGW(ELOG_NetHooks, L"UPackageMap::SerializeObject: class = %s, in = %s",
            Class ? *Class->GetName() : L"n/a", *Object ? *(*Object)->GetName() : L"n/a");

        // default or archetype objects
        if (*Object != nullptr && ((*Object)->ObjectFlags & 0x600) != 0)
        {
            //auto const PackageMap = reinterpret_cast<UPackageMap_Proxy*>(Context);
            auto const Outermost = (*Object)->GetOutermost();
            auto const Index = Context->PackageMapList.Find(SFXName(Outermost->Name));

            FPackageInfo const* Info = nullptr;
            if (Index != nullptr)
                Info = &Context->List(*Index);

            MILAN_DEBUGW(ELOG_NetHooks, L"  NetIndex = %d, Outermost = %s, Index = %s, ObjBase = %s, ObjCount = %s",
                (*Object)->NetIndex,
                Outermost ? *Outermost->GetName() : L"n/a",
                Index ? *FString::Printf(L"%d", *Index) : L"n/a",
                Info ? *FString::Printf(L"%d", Info->ObjectBase) : L"n/a",
                Info ? *FString::Printf(L"%d", Info->ObjectCount) : L"n/a");
        }
    }

    int const Return = (*UPackageMap_SerializeObject_orig)(Context, Archive, Class, Object);

    if (Archive->ArIsLoading)
    {
        if (gb_logSerializeObject || *Object == nullptr)
        {
            if (*Object != nullptr)
            {
                MILAN_DEBUGW(ELOG_NetHooks, L"UPackageMap::SerializeObject: class = %s, out = %s  ( => %d )",
                    Class ? *Class->GetName() : L"n/a", *Object ? *(*Object)->GetName() : L"n/a", Return);
            }
            else
            {
                // We want to log these failures.
                MILAN_WARNW(ELOG_NetHooks, L"UPackageMap::SerializeObject: class = %s, out = %s  ( => %d )",
                    Class ? *Class->GetName() : L"n/a", *Object ? *(*Object)->GetName() : L"n/a", Return);
            }

        }

    }

    //if (Archive->ArIsLoading)
    //{
    //    // Let's assume that this type only ever gets FInBunch as Archive.
    //    auto const InBunch = reinterpret_cast<FInBunch*>(Archive);
    //    MILAN_DEBUGW(ELOG_NetHooks, L"  Pos => %d, Bytes:", InBunch->BunchPos);
    //    PrintHex(consoleOutput, InBunch->Buffer.GetData(), 16, "");
    //}

    return Return;
}


namespace
{
    wchar_t const* MessageTypeString(unsigned char const Type) noexcept
    {
        switch (Type)
        {
        case 0u:    return L"Hello";
        case 1u:    return L"Welcome";
        case 2u:    return L"Upgrade";
        case 3u:    return L"Challenge";
        case 4u:    return L"Netspeed";
        case 5u:    return L"Login";
        case 6u:    return L"Failure";
        case 7u:    return L"Uses";
        case 8u:    return L"Have";
        case 9u:    return L"Join";
        case 11u:   return L"DlMngr";
        case 13u:   return L"Abort";
        case 14u:   return L"Unload";
        case 19u:   return L"Unk19_DLM";
        case 21u:   return L"Unk21_VIS";
        }
        return nullptr;
    }

    wchar_t const* MessageTypeDesc(unsigned char const Type) noexcept
    {
        auto const Str = MessageTypeString(Type);
        return Str != nullptr ? Str : L"???";
    }
}

t_UNetPendingLevel_NotifyControlMessage* UNetPendingLevel_NotifyControlMessage_orig = nullptr;
void UNetPendingLevel_NotifyControlMessage_hook(UNetPendingLevel* const Context, UNetConnection* const Connection, BYTE const Type, FInBunch* const InBunch)
{
    static std::array<unsigned char, 4> HandledTypes{ 1u, 3u, 7u, 11u };

    if (Type == 6u)  // if failure
        MILAN_ERRORW(ELOG_NetHooks, L"UNetPendingLevel::NotifyControlMessage: Type = %d (%s)", Type, MessageTypeDesc(Type));
    else if (std::find(HandledTypes.begin(), HandledTypes.end(), Type) == HandledTypes.end())  // if not handled
        MILAN_WARNW(ELOG_NetHooks, L"UNetPendingLevel::NotifyControlMessage: Type = %d (%s)", Type, MessageTypeDesc(Type));

    (*UNetPendingLevel_NotifyControlMessage_orig)(Context, Connection, Type, InBunch);
}

t_UWorld_NotifyControlMessage* UWorld_NotifyControlMessage_orig = nullptr;
void UWorld_NotifyControlMessage_hook(UWorld* const Context, UNetConnection* const Connection, BYTE const Type, FInBunch* const InBunch)
{
    // Server: 0, 4, 5, 8, 9, 13, 21;  Client: 7, 14, 19   (- receives)
    static std::array<unsigned char, 10> HandledTypes{ 0u, 4u, 5u, 7u, 8u, 9u, 13u, 14u, 19u, 21u };

    if (Type == 6u)  // if failure
        MILAN_ERRORW(ELOG_NetHooks, L"UWorld::NotifyControlMessage: Type = %d (%s)", Type, MessageTypeDesc(Type));
    else if (std::find(HandledTypes.begin(), HandledTypes.end(), Type) == HandledTypes.end())  // if not handled
        MILAN_WARNW(ELOG_NetHooks, L"UWorld::NotifyControlMessage: Type = %d (%s)", Type, MessageTypeDesc(Type));

    (*UWorld_NotifyControlMessage_orig)(Context, Connection, Type, InBunch);
}

t_UWorld_ServerTickClients* UWorld_ServerTickClients_orig = nullptr;
int UWorld_ServerTickClients_hook(UWorld* const Context, float const Delta)
{
    if (false)
    {
        auto* const WorldInfo = (*GEngine)->GetCurrentWorldInfo();

        MILAN_WARNW(ELOG_NetHooks, L"UWorld::ServerTickClients: delta = %f", Delta);
        MILAN_WARNW(ELOG_NetHooks, L"--------------------------------------");

        for (int LevelIndex = 0; LevelIndex < (int)Context->Levels.Count(); ++LevelIndex)
        {
            auto* const Level = Context->Levels(LevelIndex);
            if (Level == nullptr) continue;

            MILAN_DEBUGW(ELOG_NetHooks, L" Level(%d): %s", LevelIndex, *Level->StaticFullName());
            MILAN_DEBUGW(ELOG_NetHooks, L" Level(%d): actor count = %d, first net index = %d", LevelIndex, Level->Actors.Count(), Level->FirstNetRelevantActor);

            for (int ActorIndex = Level->FirstNetRelevantActor; ActorIndex < (int)Level->Actors.Count(); ++ActorIndex)
            {
                auto* const Actor = Level->Actors(ActorIndex);
                if (Actor == nullptr) continue;

                float const TimeSeconds = WorldInfo ? WorldInfo->TimeSeconds : Actor->NetUpdateTime;
                if ((Actor->RemoteRole != ROLE_None) && (Actor->bPendingNetUpdate || Actor->bForceNetUpdate || TimeSeconds > Actor->NetUpdateTime))
                {
                    // Plenty of actors here are still irrelevant...
                    MILAN_TRACEW(ELOG_NetHooks, L" Level(%d).Actor(%d): %s", LevelIndex, ActorIndex, *Actor->StaticFullName());
                }
            }
        }
    }

    return (*UWorld_ServerTickClients_orig)(Context, Delta);
}

t_SortReplicatedActors* SortReplicatedActors_orig = nullptr;
void SortReplicatedActors_hook(CReplicatedActorRecord** const Actors, int const Count)
{
    (*SortReplicatedActors_orig)(Actors, Count);

    if (false)
    {
        MILAN_WARNW(ELOG_NetHooks, L"SortReplicatedActors");
        MILAN_WARNW(ELOG_NetHooks, L"----------------------");

        for (int Index = 0; Index < Count; ++Index)
        {
            auto const* const Record = Actors[Index];
            MILAN_DEBUGW(ELOG_NetHooks, L" %d. actor = %s, channel = %s, key = %d",
                Index,
                Record->Actor ? *Record->Actor->GetName() : L"???",
                Record->Channel ? *Record->Channel->GetName() : L"???",
                Record->SortKey);
        }
    }
}

t_GlobalDynamicBuildVersion* GlobalDynamicBuildVersion_orig = nullptr;
UINT GlobalDynamicBuildVersion_hook()
{
    UINT const Version = (*GlobalDynamicBuildVersion_orig)();

    static constexpr bool kbForceUpgradeNetMessage = false;
    if (kbForceUpgradeNetMessage && GetGameInstanceOrdinal() != 0)
        return 123456u;

    return Version;
}

t_FSocketWin_Send* FSocketWin_Send_orig = nullptr;
UINT FSocketWin_Send_hook(FSocket* Context, BYTE* Data, INT Count, INT* Sent)
{
    UINT const Ret = (*FSocketWin_Send_orig)(Context, Data, Count, Sent);
    if (Count > 0)
    {
        // This remains uncommented because I haven't managed to trigger a call to this function yet...
        MILAN_TRACEW(ELOG_NetHooksTrace, L"FSocketWin::Send: type=%d, desc=%s, count=%d, sent=%d", Context->Type, *Context->Description, Count, *Sent);
    }
    return Ret;
}

t_FSocketWin_Recv* FSocketWin_Recv_orig = nullptr;
UINT FSocketWin_Recv_hook(FSocket* Context, BYTE* Buffer, INT Max, INT* Read)
{
    UINT const Ret = (*FSocketWin_Recv_orig)(Context, Buffer, Max, Read);
    if (*Read > 0)
    {
        // This remains uncommented because I haven't managed to trigger a call to this function yet...
        MILAN_WARNW(ELOG_NetHooksTrace, L"FSocketWin::Recv: type=%d, desc=%s, read=%d", Context->Type, *Context->Description, *Read);
    }
    return Ret;
}

#endif
