/**
 * @file        MilanHost/Hooks.hpp
 * @brief       Declarations and patterns for hooked functions in the engine.
 */

#pragma once

#include <d3d11.h>
#include <imgui.h>
#include <LESDK/Headers.hpp>


 // TODO: Replace this with a post-hook pattern search.
#define GLOBAL_WNDPROC_RVA  ::LESDK::Address::FromOffset(0x99D2C0)

#define GLOBAL_UGAMEENGINE_EXEC_PHOOK \
    ::LESDK::Address::FromPostHook(/* 40 55 56 57 41 */ "54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC 40 0A 00 00")


using tWndProc = LRESULT __stdcall(HWND, UINT, WPARAM, LPARAM);
extern tWndProc* WndProc_orig;

LRESULT __stdcall WndProc_hook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

using t_IDXGISwapChain_Present = HRESULT __stdcall(IDXGISwapChain*, UINT, UINT);
extern t_IDXGISwapChain_Present* IDXGISwapChain_Present_orig;

HRESULT __stdcall IDXGISwapChain_Present_hook(IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags);

using t_UGameEngine_Exec = UBOOL(UGameEngine*, WCHAR const*, void*);
extern t_UGameEngine_Exec* UGameEngine_Exec_orig;

UBOOL UGameEngine_Exec_hook(UGameEngine* Context, WCHAR const* Command, void* Ar);
