#include "Systems/Console.hpp"
#include "Systems/Output.hpp"
#include "Systems/Panels.hpp"
#include "Systems/State.hpp"

#include "Hooks.hpp"
#include "Logger.hpp"


static bool gb_logCallFunction = false;
static bool gb_logProcessDelegate = false;
static bool gb_logProcessEvent = false;
static bool gb_logProcessInternal = false;


tWndProc* WndProc_orig = nullptr;
LRESULT CALLBACK WndProc_hook(HWND const hWnd, UINT const uMsg, WPARAM const wParam, LPARAM const lParam)
{
    if (::Milan::Panels::OnWndProc(hWnd, uMsg, wParam, lParam))
        return true;

    //if (WndProc_orig == nullptr)
    //    return true;

    return (*WndProc_orig)(hWnd, uMsg, wParam, lParam);
}

t_IDXGISwapChain_Present* IDXGISwapChain_Present_orig = nullptr;
HRESULT IDXGISwapChain_Present_hook(IDXGISwapChain* const pChain, UINT const SyncInterval, UINT const Flags)
{
    // This should never return true.
    if (::Milan::Panels::OnSwapChainPresent(pChain, SyncInterval, Flags))
        return S_OK;

    return IDXGISwapChain_Present_orig(pChain, SyncInterval, Flags);
}

t_BioRequestExit* BioRequestExit = nullptr;

t_UFunction_Bind* UFunction_Bind_orig = nullptr;
void UFunction_Bind_hook(UFunction* const Context)
{
    (*UFunction_Bind_orig)(Context);

    if (Context->Func == nullptr)
    {
        MILAN_WARNW(L"UFunction::Bind failed to bind %s", *Context->StaticFullName());
    }
}

t_UGameEngine_Exec* UGameEngine_Exec_orig = nullptr;
UBOOL UGameEngine_Exec_hook(UGameEngine* const Context, WCHAR const* const Command, void* const Output)
{
    //MILAN_TRACEW(L"UGameEngine::Exec: %s", Command);

    if (::Milan::Console::OnUGameEngineExec(Context, Command, Output))
        return TRUE;

    return (*UGameEngine_Exec_orig)(Context, Command, Output);
}

t_UObject_CallFunction* UObject_CallFunction_orig = nullptr;
void UObject_CallFunction_hook(UObject* const Context, FFrame* const Stack, void* const Result, UFunction* const Function)
{
#ifndef _FINAL_RELEASE
    if (gb_logCallFunction)
    {
        static FStringRAII ObjectName{}, FunctionName{};

        ObjectName.Clear();
        FunctionName.Clear();

        Context->AppendName(ObjectName, SFXName::k_formatBasic);
        Function->AppendName(FunctionName, SFXName::k_formatBasic);

        MILAN_TRACEW(L"UObject::CallFunction: %s.%s", *ObjectName, *FunctionName);
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
        static FStringRAII ContextName{}, StrDelegateName{}, DelegateContextName{}, StrDelegateFunctionName{};

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
        static FStringRAII ObjectName{}, FunctionName{};

        ObjectName.Clear();
        FunctionName.Clear();

        Context->AppendName(ObjectName, SFXName::k_formatBasic);
        Function->AppendName(FunctionName, SFXName::k_formatBasic);

        MILAN_TRACEW(L"UObject::ProcessEvent: %s.%s", *ObjectName, *FunctionName);
    }
#endif

    (*UObject_ProcessEvent_orig)(Context, Function, Parms, Result);
}

t_UObject_ProcessInternal* UObject_ProcessInternal_orig = nullptr;
void UObject_ProcessInternal_hook(UObject* const Context, FFrame* const Stack, void* const Result)
{
#ifndef _FINAL_RELEASE
    if (gb_logProcessInternal)
    {
        static FStringRAII ObjectName{}, FunctionName{};

        ObjectName.Clear();
        FunctionName.Clear();

        Context->AppendName(ObjectName, SFXName::k_formatBasic);
        Stack->Node->AppendName(FunctionName, SFXName::k_formatBasic);

        MILAN_TRACEW(L"UObject::ProcessInternal: %s.%s", *ObjectName, *FunctionName);
    }
#endif

    bool bCallOriginal = true;

    if (!::Milan::State::On_UObject_ProcessInternal(Context, Stack, Result))
        bCallOriginal = false;

    if (bCallOriginal)
    {
        (*UObject_ProcessInternal_orig)(Context, Stack, Result);
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
