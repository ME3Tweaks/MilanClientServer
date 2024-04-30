/**
 * @file        MilanHost/Hooks.hpp
 * @brief       Declarations and patterns for hooked or found functions in the engine.
 */

#pragma once

#include <d3d11.h>
#include <imgui.h>

#include <LESDK/Headers.hpp>


 // TODO: Replace this with a post-hook pattern search.
#define GLOBAL_WNDPROC_RVA  ::LESDK::Address::FromOffset(0x99D2C0)

#define GLOBAL_UGAMEENGINE_EXEC_PHOOK \
    ::LESDK::Address::FromPostHook(/* 40 55 56 57 41 */ "54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC 40 0A 00 00")

#define GLOBAL_UOBJECT_SKIPFUNCTION_PHOOK \
    ::LESDK::Address::FromPostHook(/* 40 55 41 54 41 */ "55 41 56 41 57 48 83 EC 30 48 8D 6C 24 ?? 48 89 5D ?? 48 89 75 ?? 48 89 7D ?? 48 8B 05 ?? ?? ?? ?? 48 33 C5 48 89 45 00 48 8B DA")

#define GLOBAL_UWORLD_TICK_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 F3 0F */ "11 50 ?? 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A1")


// ! Miscellaneous functions / methods.
// ========================================

using tWndProc = LRESULT __stdcall(HWND, UINT, WPARAM, LPARAM);
extern tWndProc* WndProc_orig;

LRESULT __stdcall WndProc_hook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

using t_IDXGISwapChain_Present = HRESULT __stdcall(IDXGISwapChain*, UINT, UINT);
extern t_IDXGISwapChain_Present* IDXGISwapChain_Present_orig;

HRESULT __stdcall IDXGISwapChain_Present_hook(IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags);


// ! UGameEngine methods.
// ========================================

using t_UGameEngine_Exec = UBOOL(UGameEngine*, WCHAR const*, void*);
extern t_UGameEngine_Exec* UGameEngine_Exec_orig;

UBOOL UGameEngine_Exec_hook(UGameEngine* Context, WCHAR const* Command, void* Ar);


// ! UObject methods.
// ========================================

using t_UObject_CallFunction = tCallFunction;
extern t_UObject_CallFunction* UObject_CallFunction_orig;

void UObject_CallFunction_hook(UObject* Context, FFrame* Stack, void* Result, UFunction* Function);

using t_UObject_ProcessEvent = tProcessEvent;
extern t_UObject_ProcessEvent* UObject_ProcessEvent_orig;

void UObject_ProcessEvent_hook(UObject* Context, UFunction* Function, void* Parms, void* Result);

using t_UObject_ProcessInternal = tProcessInternal;
extern t_UObject_ProcessInternal* UObject_ProcessInternal_orig;

void UObject_ProcessInternal_hook(UObject* Context, FFrame* Stack, void* Result);

using t_UObject_SkipFunction = void(UObject* Context, void* Result, UFunction* Function);
extern t_UObject_SkipFunction* UObject_SkipFunction;


// ! UWorld methods.
// ========================================

using t_UWorld_Tick = void(UWorld* Context, int Unknown, float Delta);
extern t_UWorld_Tick* UWorld_Tick_orig;

void UWorld_Tick_hook(UWorld* Context, int Unknown, float Delta);
