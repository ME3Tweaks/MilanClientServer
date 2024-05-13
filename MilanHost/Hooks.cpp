#include "Systems/Console.hpp"
#include "Systems/Imgui.hpp"
#include "Systems/Output.hpp"
#include "Systems/Profilers.hpp"
#include "Systems/State.hpp"

#include "Hooks.hpp"
#include "Intercept.hpp"
#include "Logger.hpp"


static bool gb_logCallFunction = false;
static bool gb_logProcessDelegate = false;
static bool gb_logProcessEvent = false;
static bool gb_logProcessInternal = false;


tWndProc* WndProc_orig = nullptr;
LRESULT CALLBACK WndProc_hook(HWND const hWnd, UINT const uMsg, WPARAM const wParam, LPARAM const lParam)
{
    if (::Milan::Imgui::OnWndProc(hWnd, uMsg, wParam, lParam))
        return true;

    //if (WndProc_orig == nullptr)
    //    return true;

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

t_BioRequestExit* BioRequestExit = nullptr;


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

//#ifndef _FINAL_RELEASE
//    auto const& FullName = Stack->Node->StaticFullName();
//    if (FullName.Equals(L"Function SFXGameMPContent.SFXPRIMP.SetPower"))
//    {
//        struct USFXPRIMP_SetPower_Parms
//        {
//            INT         PowerIndex;             // 0x0000 (0x0004)
//            SFXName     PowerClassPath;         // 0x0004 (0x0008)
//            INT         EvolvedChoices[6];      // 0x000C (0x0018)
//            INT         CurrentRank;            // 0x0024 (0x0004)
//            UBOOL       ReturnValue;            // 0x0028
//        };
//
//        auto const TParms = (USFXPRIMP_SetPower_Parms*)Stack->Locals;
//
//        MILAN_DEBUGW(L"intercepted SFXPRIMP.SetPower:");
//        MILAN_DEBUGW(L"  power index = %d, power class path = %s", TParms->PowerIndex, *TParms->PowerClassPath.ToString());
//    }
//#endif

    bool bCallOriginal = true;

    if (!::Milan::State::On_UObject_ProcessInternal_Pre(Context, Stack, Result))
        bCallOriginal = false;

    if (bCallOriginal)
    {
        (*UObject_ProcessInternal_orig)(Context, Stack, Result);
        ::Milan::State::On_UObject_ProcessInternal_Post(Context, Stack, Result);
    }
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

t_UActorChannel_ReplicateActor* UActorChannel_ReplicateActor_orig = nullptr;

void UActorChannel_ReplicateActor_hook(UActorChannel* const Context)
{
    MILAN_CHECKW(Context != nullptr, L"");
    auto* const Channel = reinterpret_cast<UActorChannel_Proxy*>(Context);

    //MILAN_TRACEW(L"UActorChannel::ReplicateActor:");
    //MILAN_TRACEW(L"  Channel: %s", *Channel->StaticFullName());
    //MILAN_TRACEW(L"    Props: OpenedLocally=%d, OpenPacketId=%d, NumInRec=%d, NumOutRec=%d, NegotiatedVer=%d",
    //    Channel->OpenedLocally, Channel->OpenPacketId, Channel->NumInRec, Channel->NumOutRec, Channel->NegotiatedVer);
    //MILAN_TRACEW(L"  Actor: %s", Channel->Actor ? *Channel->Actor->StaticFullName() : L"n/a");
    //if (Channel->Actor)
    //{
    //    auto* const Actor = Channel->Actor;
    //    MILAN_TRACEW(L"    Props: bNetOwner=%d, bNetInitial=%d Top=%s",
    //        !!Actor->bNetOwner, !!Actor->bNetInitial, *Actor->StaticName());
    //}

    //MILAN_TRACEW(L"UActorChannel::ReplicateActor: %s", Channel->Actor ? *Channel->Actor->StaticFullName() : L"n/a");

    MILAN_UNUSED(Channel);
    (*UActorChannel_ReplicateActor_orig)(Context);
}
