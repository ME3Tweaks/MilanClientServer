#include <array>

#include "Milan/Hooks.hpp"
#include "Milan/Intercept.hpp"
#include "Milan/Logger.hpp"
#include "Systems/Console.hpp"
#include "Systems/Globals.hpp"
#include "Systems/Imgui.hpp"
#include "Systems/Output.hpp"
#include "Systems/Profilers.hpp"
#include "Systems/State.hpp"


bool gb_logCallFunction = false;
bool gb_logProcessDelegate = false;
bool gb_logProcessEvent = false;
bool gb_logProcessInternal = false;


t_WndProc* WndProc_orig = nullptr;
LRESULT CALLBACK WndProc_hook(HWND const hWnd, UINT const uMsg, WPARAM const wParam, LPARAM const lParam)
{
    if (::Milan::Imgui::OnWndProc(hWnd, uMsg, wParam, lParam))
        return TRUE;

    //if (WndProc_orig == nullptr)
    //    return TRUE;

    return (*WndProc_orig)(hWnd, uMsg, wParam, lParam);
}

t_IDXGISwapChain_Present* IDXGISwapChain_Present_orig = nullptr;
HRESULT IDXGISwapChain_Present_hook(IDXGISwapChain* const pChain, UINT const SyncInterval, UINT const Flags)
{
    // This should never return true.
    if (::Milan::Imgui::OnSwapChainPresent(pChain, SyncInterval, Flags))
        return S_OK;

    return IDXGISwapChain_Present_orig(pChain, SyncInterval, Flags);
}

t_BioRequestExit* BioRequestExit_orig = nullptr;
void BioRequestExit_hook(UBOOL const bForce, INT const ExitCode, INT const Unused)
{
#ifndef _FINAL_RELEASE
    if (::Milan::Output::HasInitialized())
    {
        if (bForce)
        {
            MILAN_WARNW(L"BioRequestExit: bForce = TRUE, ExitCode = %d", ExitCode);
            ::Sleep(1000);
        }
        else
        {
            MILAN_DEBUGW(L"BioRequestExit: bForce = FALSE, ExitCode = %d", ExitCode);
        }
    }
    else
    {
        thread_local std::array<wchar_t, 128> OutputBuffer{ '\0' };
        ::swprintf(OutputBuffer.data(), OutputBuffer.size(),
            L"BioRequestExit: bForce = %s, ExitCode = %d",
            bForce ? L"TRUE" : L"FALSE", ExitCode);
        ::OutputDebugStringW(OutputBuffer.data());
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

    if (Context->Func == nullptr)
    {
        MILAN_WARNW(L"UFunction::Bind failed to bind %s", *Context->StaticFullName());
    }
}


// ! UGameEngine methods.
// ========================================

t_UGameEngine_Exec* UGameEngine_Exec_orig = nullptr;
UBOOL UGameEngine_Exec_hook(UGameEngine* const Context, WCHAR const* const Command, void* const Output)
{
    //MILAN_TRACEW(L"UGameEngine::Exec: %s", Command);

    if (::Milan::Console::OnUGameEngineExec(Context, Command, Output))
        return TRUE;

    return (*UGameEngine_Exec_orig)(Context, Command, Output);
}

t_UGameEngine_LoadMap* UGameEngine_LoadMap_orig = nullptr;
UBOOL UGameEngine_LoadMap_hook(UGameEngine* const Context, FURL* const Url, UPendingLevel* const Level, FString* const Error)
{
    MILAN_TRACEW(L"NETPROF (UGameEngine::LoadMap): Url = %s...", *Url->Map);
    auto const Result = (*UGameEngine_LoadMap_orig)(Context, Url, Level, Error);
    if (!Result) MILAN_ERRORW(L"UGameEngine::LoadMap failed: %s", *(*Error));
    return Result;
}

t_UGameEngine_Tick* UGameEngine_Tick_orig = nullptr;
void UGameEngine_Tick_hook(UGameEngine* const Context, float const Delta)
{
    //MILAN_TRACEW(L"NETPROF (UGameEngine::Tick): Delta=%f", Delta);
    (*UGameEngine_Tick_orig)(Context, Delta);
}


// ! UObject methods.
// ========================================

t_UObject_CallFunction* UObject_CallFunction_orig = nullptr;
void UObject_CallFunction_hook(UObject* const Context, FFrame* const Stack, void* const Result, UFunction* const Function)
{
#ifndef _FINAL_RELEASE
    if (gb_logCallFunction)
    {
        static FString ObjectName{}, FunctionName{};

        ObjectName.Clear();
        FunctionName.Clear();

        Context->AppendName(ObjectName, SFXName::k_formatBasic);
        Function->AppendName(FunctionName, SFXName::k_formatBasic);

        MILAN_TRACEW(L"UObject::CallFunction: %s.%s", *ObjectName, *FunctionName);
    }
#endif

#ifndef _FINAL_RELEASE
    if (Function == nullptr)
    {
        MILAN_ERRORW(L"UObject::CallFunction: %s", *Context->StaticFullName());
        MILAN_ERRORW(L"  Function is null, stack dumped:");

        int i = 0;
        for (FFrame* Current = Stack; Current; Current = Current->PreviousFrame)
        {
            MILAN_WARNW(L"%3d Stack.Object = %s\n", i, Current->Object ? *Current->Object->StaticFullName() : L"???");
            MILAN_WARNW(L"    Stack.Node = %s\n", Current->Node ? *Current->Node->StaticFullName() : L"???");
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

        MILAN_TRACEW(L"UObject::ProcessEvent: %s.%s", *ObjectName, *FunctionName);
    }
#endif

    ::Milan::Profilers::On_UObject_ProcessEvent(Context, Function, Parms, Result);
    (*UObject_ProcessEvent_orig)(Context, Function, Parms, Result);
}

t_UObject_ProcessInternal* UObject_ProcessInternal_orig = nullptr;
void UObject_ProcessInternal_hook(UObject* const Context, FFrame* const Stack, void* const Result)
{
    bool bCallOriginal = true;

#ifndef _FINAL_RELEASE
    if (gb_logProcessInternal)
    {
        static FString ObjectName{}, FunctionName{};

        ObjectName.Clear();
        FunctionName.Clear();

        Context->AppendName(ObjectName, SFXName::k_formatBasic);
        Stack->Node->AppendName(FunctionName, SFXName::k_formatBasic);

        MILAN_TRACEW(L"UObject::ProcessInternal: %s.%s", *ObjectName, *FunctionName);
    }
#endif

    if (!::Milan::State::On_UObject_ProcessInternal_Pre(Context, Stack, Result))
        bCallOriginal = false;

    if (bCallOriginal)
    {
        (*UObject_ProcessInternal_orig)(Context, Stack, Result);
        ::Milan::State::On_UObject_ProcessInternal_Post(Context, Stack, Result);
    }


#ifndef _FINAL_RELEASE
    static bool s_skipMainMenuChecked = false;
    static bool s_skipMainMenu = false;

    if (!s_skipMainMenuChecked)
    {
        s_skipMainMenuChecked = true;
        s_skipMainMenu = ::wcsstr(::Milan::Globals::GetCmdString(), L"-skiptomp") != nullptr;
    }

    if (s_skipMainMenu)
    {
        static bool s_checkMainMenuSplash = true;
        if (s_checkMainMenuSplash && Stack->Node->StaticFullName()
            .Equals(L"Function SFXGame.BioSeqAct_ShouldShowSplashSequence.Activated"))
        {
            s_checkMainMenuSplash = false;

            MILAN_TRACEW(L"BioSeqAct_ShouldShowSplashSequence activated, overriding");
            auto* const Action = Context->CastDirect<UBioSeqAct_ShouldShowSplashSequence>();

            Action->GetWorldInfo()->Game->ShowSplashScreen = FALSE;
            Action->OutputLinks(0).bHasImpulse = FALSE;
            Action->OutputLinks(1).bHasImpulse = TRUE;
        }

        static bool s_checkMainMenuLobbyFlow = true;
        if (s_checkMainMenuLobbyFlow && Stack->Node->StaticFullName()
            .Equals(L"Function SFXGameContentLiveKismet.BioSeqAct_ShowMainMenu.Activated"))
        {
            s_checkMainMenuLobbyFlow = false;
            MILAN_TRACEW(L"BioSeqAct_ShowMainMenu activated");

            for (TypedObjectIterator<USFXGUI_MainMenu_RTT, false, false> Iter{}; Iter; ++Iter)
            {
                //MILAN_TRACEW(L"  %s: starting mp flow", *(*Iter)->StaticName());
                (*Iter)->eventStartMultiplayerFlow();
            }
        }
    }

    static bool s_updateWindow = true;
    if (s_updateWindow)
    {
        for (TypedObjectIterator<UGameViewportClient, true, false> Iter{}; Iter; ++Iter)
        {
            auto const Viewport = (*Iter)->Viewport;
            if (Viewport == nullptr)
                break;

            auto const Window = *reinterpret_cast<HWND*>(reinterpret_cast<unsigned char*>(Viewport) + 0xC4);
            auto const Name = reinterpret_cast<FString*>(reinterpret_cast<unsigned char*>(Viewport) + 0xD4);

            MILAN_TRACEW(L"Updating title for %s, FViewport* = %p, HWND = %p, Name = %s",
                *(*Iter)->StaticName(), Viewport, (void*)Window, Name->Chars());

            *Name = FString::Printf(L"Mass Effect 3 (INST = %d) PID = %d", GetGameInstanceOrdinal(), ::GetCurrentProcessId());
            DefWindowProcW(Window, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(Name->Chars()));

            s_updateWindow = false;
        }
    }
#endif
}

t_UObject_SkipFunction* UObject_SkipFunction = nullptr;


// ! UWorld methods.
// ========================================

t_UWorld_Tick* UWorld_Tick_orig = nullptr;
void UWorld_Tick_hook(UWorld* const Context, int const Unknown, float const Delta)
{
    (*UWorld_Tick_orig)(Context, Unknown, Delta);
    ::Milan::State::On_UWorld_Tick(Context, Unknown, Delta);
}


// ! <Methods relevant to network debug.>
// ========================================

#ifndef _FINAL_RELEASE

bool gb_logReplicateActor = false;

t_UActorChannel_ReplicateActor* UActorChannel_ReplicateActor_orig = nullptr;
void UActorChannel_ReplicateActor_hook(UActorChannel* const Context)
{
    MILAN_CHECKW(Context != nullptr, L"");

    auto* const Channel = reinterpret_cast<UActorChannel_Proxy*>(Context);
    MILAN_UNUSED(Channel);

    if (false)
    {
        MILAN_DEBUGW(L"UActorChannel::ReplicateActor:");
        MILAN_DEBUGW(L"  Channel: %s", *Channel->StaticFullName());
        MILAN_DEBUGW(L"    Props: OpenedLocally=%d, OpenPacketId=%d, NumInRec=%d, NumOutRec=%d, NegotiatedVer=%d",
            Channel->OpenedLocally, Channel->OpenPacketId, Channel->NumInRec, Channel->NumOutRec, Channel->NegotiatedVer);
        MILAN_DEBUGW(L"  Actor: %s", Channel->Actor ? *Channel->Actor->StaticFullName() : L"n/a");
        if (Channel->Actor != nullptr)
        {
            auto* const Actor = Channel->Actor;
            MILAN_DEBUGW(L"    Props: bNetOwner=%d, bNetInitial=%d Top=%s",
                !!Actor->bNetOwner, !!Actor->bNetInitial, *Actor->StaticName());
        }
    }
    else if (gb_logReplicateActor)
    {
        MILAN_DEBUGW(L"UActorChannel::ReplicateActor: %s",
            Channel->Actor ? *Channel->Actor->StaticFullName() : L"n/a");
    }

    (*UActorChannel_ReplicateActor_orig)(Context);
}

t_UNetConnection_ClientSupportsLevel* UNetConnection_ClientSupportsLevel_orig = nullptr;
int UNetConnection_ClientSupportsLevel_hook(UNetConnection* const Context, UObject* const Object)
{
    int const Return = (*UNetConnection_ClientSupportsLevel_orig)(Context, Object);

    {
        // This is left so that we can see PackageMap fields in an attached debugger.
        auto* const Connection = reinterpret_cast<UNetConnection_Proxy*>(Context);
        MILAN_UNUSED(Connection);

        if (Return != 0)
        {
            //MILAN_TRACEW(L"UNetConnection::ClientSupportsLevel: %s (%s) => TRUE",
            //    Context ? *Context->GetName() : L"???", Object ? *Object->GetFullName() : L"???");
        }
        else
        {
            MILAN_WARNW(L"UNetConnection::ClientSupportsLevel: %s (%s) => FALSE",
                Context ? *Context->GetName() : L"???", Object ? *Object->GetFullName() : L"???");
        }
    }

    return Return;
}

t_UNetConnection_Close* UNetConnection_Close_orig = nullptr;
void UNetConnection_Close_hook(UNetConnection* const Context)
{
    auto* const Connection = reinterpret_cast<UNetConnection_Proxy*>(Context);
    if (Connection->Driver != nullptr && Connection->ConnectionState != USOCK_Closed)
    {
        FString RemoteAddress{};

        auto* const TcpipConnection = reinterpret_cast<UTcpipConnection_Proxy*>(Context->Cast<UTcpipConnection>());
        if (TcpipConnection != nullptr)
        {
            auto const& Address = TcpipConnection->Address;
            RemoteAddress = FString::Printf(L"%S:%hu", inet_ntoa(Address.sin_addr), ntohs(Address.sin_port));
        }

        MILAN_TRACEW(L"UNetConnection::Close, remote = %s", *RemoteAddress);
    }

    (*UNetConnection_Close_orig)(Context);
}

t_UPackageMap_SupportsObject* UPackageMap_SupportsObject_orig = nullptr;
int UPackageMap_SupportsObject_hook(UPackageMap* const Context, UObject* const Object)
{
    int const Return = (*UPackageMap_SupportsObject_orig)(Context, Object);

    {
        // This is left so that we can see PackageMap fields in an attached debugger.
        auto* const PackageMap = reinterpret_cast<UPackageMap_Proxy*>(Context);
        MILAN_UNUSED(PackageMap);

        if (Return != 0)
        {
            //MILAN_TRACEW(L"UPackageMap::SupportsObject: %s (%s) => TRUE",
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
                MILAN_WARNW(L"UPackageMap::SupportsObject: %s => FALSE (outermost is NULL)",
                    Object ? *Object->GetFullName() : L"???");
                return Return;
            }

            int const* const ListIndex = PackageMap->PackageMapList.Find(SFXName(OutermostPackage->Name));
            if (ListIndex == nullptr)
            {
                MILAN_WARNW(L"UPackageMap::SupportsObject: %s => FALSE (package=%s not found)",
                    Object ? *Object->GetFullName() : L"???", *OutermostPackage->Name.ToString());
                return Return;
            }

            FPackageInfo const& PackageInfo = PackageMap->List((UINT)*ListIndex);
            if (Object->NetIndex >= PackageInfo.ObjectCount)
            {
                MILAN_WARNW(L"UPackageMap::SupportsObject: %s => FALSE (NetIndex=%d >= PackageInfo.ObjectCount=%d)",
                    Object ? *Object->GetFullName() : L"???", Object->NetIndex, PackageInfo.ObjectCount);
                MILAN_WARNW(L"  PackageInfo.PackageName = %s, PackageInfo.ObjectBase = %d",
                    *PackageInfo.PackageName.ToString(), PackageInfo.ObjectBase);
                return Return;
            }

            MILAN_WARNW(L"UPackageMap::SupportsObject: %s => FALSE (NetIndex=%d)",
                Object ? *Object->GetFullName() : L"???", Object->NetIndex);
        }
    }

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
    // 1, 3, 7, 11
    //MILAN_WARNW(L"UNetPendingLevel::NotifyControlMessage: Type = %d (%s)", Type, MessageTypeDesc(Type));
    (*UNetPendingLevel_NotifyControlMessage_orig)(Context, Connection, Type, InBunch);
}

t_UWorld_NotifyControlMessage* UWorld_NotifyControlMessage_orig = nullptr;
void UWorld_NotifyControlMessage_hook(UWorld* const Context, UNetConnection* const Connection, BYTE const Type, FInBunch* const InBunch)
{
    // Server: 0, 4, 5, 8, 9, 21;  Client: 7, 14, 19
    static std::array<unsigned char, 9> HandledTypes{ 0u, 4u, 5u, 7u, 8u, 9u, 14u, 19u, 21u };
    if (std::find(HandledTypes.begin(), HandledTypes.end(), Type) == HandledTypes.end())
        MILAN_WARNW(L"UWorld::NotifyControlMessage: Type = %d (%s) not intercepted", Type, MessageTypeDesc(Type));

    (*UWorld_NotifyControlMessage_orig)(Context, Connection, Type, InBunch);
}

t_UWorld_ServerTickClients* UWorld_ServerTickClients_orig = nullptr;
int UWorld_ServerTickClients_hook(UWorld* const Context, float const Delta)
{
    if (false)
    {
        auto* const World = reinterpret_cast<UWorld_Proxy*>(Context);
        auto* const WorldInfo = (*GEngine)->GetCurrentWorldInfo();

        MILAN_WARNW(L"UWorld::ServerTickClients: delta = %f", Delta);
        MILAN_WARNW(L"--------------------------------------");

        for (int LevelIndex = 0; LevelIndex < (int)World->Levels.Count(); ++LevelIndex)
        {
            auto* const Level = reinterpret_cast<ULevel_Proxy*>(World->Levels(LevelIndex));
            if (Level == nullptr) continue;

            MILAN_DEBUGW(L" Level(%d): %s", LevelIndex, *Level->StaticFullName());
            MILAN_DEBUGW(L" Level(%d): actor count = %d, first net index = %d", LevelIndex, Level->Actors.Count(), Level->FirstNetRelevantActor);

            for (int ActorIndex = Level->FirstNetRelevantActor; ActorIndex < (int)Level->Actors.Count(); ++ActorIndex)
            {
                auto* const Actor = Level->Actors(ActorIndex);
                if (Actor == nullptr) continue;

                float const TimeSeconds = WorldInfo ? WorldInfo->TimeSeconds : Actor->NetUpdateTime;
                if ((Actor->RemoteRole != ROLE_None) && (Actor->bPendingNetUpdate || Actor->bForceNetUpdate || TimeSeconds > Actor->NetUpdateTime))
                {
                    // Plenty of actors here are still irrelevant...
                    MILAN_TRACEW(L" Level(%d).Actor(%d): %s", LevelIndex, ActorIndex, *Actor->StaticFullName());
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
        MILAN_WARNW(L"SortReplicatedActors");
        MILAN_WARNW(L"----------------------");

        for (int Index = 0; Index < Count; ++Index)
        {
            auto const* const Record = Actors[Index];
            MILAN_DEBUGW(L" %d. actor = %s, channel = %s, key = %d",
                Index,
                Record->Actor ? *Record->Actor->GetName() : L"???",
                Record->Channel ? *Record->Channel->GetName() : L"???",
                Record->SortKey);
        }
    }
}

t_FSocketWin_SendTo* FSocketWin_SendTo_orig = nullptr;
UINT FSocketWin_SendTo_hook(FSocket* Context, BYTE* Data, INT Count, INT* Sent, SOCKADDR_IN* Destination)
{
    UINT const Ret = (*FSocketWin_SendTo_orig)(Context, Data, Count, Sent, Destination);
    if (Count > 0)
    {
        //auto const Remote = FString::Printf(L"%S:%hu", inet_ntoa(Destination->sin_addr), ntohs(Destination->sin_port));
        //MILAN_TRACEW(L"FSocketWin::SendTo: type=%d, desc=%s, count=%d, sent=%d, dest=%s", Context->Type, *Context->Description, Count, *Sent, *Remote);
    }
    return Ret;
}

t_FSocketWin_Send* FSocketWin_Send_orig = nullptr;
UINT FSocketWin_Send_hook(FSocket* Context, BYTE* Data, INT Count, INT* Sent)
{
    UINT const Ret = (*FSocketWin_Send_orig)(Context, Data, Count, Sent);
    if (Count > 0)
    {
        // This remains uncommented because I haven't managed to trigger a call to this function yet...
        MILAN_WARNW(L"FSocketWin::Send: type=%d, desc=%s, count=%d, sent=%d", Context->Type, *Context->Description, Count, *Sent);
    }
    return Ret;
}

t_FSocketWin_RecvFrom* FSocketWin_RecvFrom_orig = nullptr;
UINT FSocketWin_RecvFrom_hook(FSocket* Context, BYTE* Buffer, INT Max, INT* Read, SOCKADDR_IN* Source)
{
    UINT const Ret = (*FSocketWin_RecvFrom_orig)(Context, Buffer, Max, Read, Source);
    if (*Read > 0)
    {
        //auto const Remote = FString::Printf(L"%S:%hu", inet_ntoa(Source->sin_addr), ntohs(Source->sin_port));
        //MILAN_TRACEW(L"FSocketWin::RecvFrom: type=%d, desc=%s, read=%d, source=%s", Context->Type, *Context->Description, *Read, *Remote);
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
        MILAN_WARNW(L"FSocketWin::Recv: type=%d, desc=%s, read=%d", Context->Type, *Context->Description, *Read);
    }
    return Ret;
}

#endif
