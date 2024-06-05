/**
 * @file        MilanHost/Milan/Hooks.hpp
 * @brief       Declarations and patterns for hooked or found functions in the engine.
 */

#pragma once

#include <d3d11.h>
#include <imgui.h>

#include <LESDK/Headers.hpp>


#define GLOBAL_BIOREQUESTEXIT_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 89 5C 24 ?? */ "48 89 6C 24 ?? 48 89 74 24 ?? 57 48 81 EC 50 03 00 00")

 // TODO: Replace this with a post-hook pattern search.
#define GLOBAL_WNDPROC_RVA  ::LESDK::Address::FromOffset(0x99D2C0)

#define GLOBAL_UUISTRING_STRINGSIZE_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 53 56 */ "57 41 56 41 57 48 81 EC D0 00 00 00 33 F6 48 8B DA 48 8B 51 28")

#define GLOBAL_UCANVAS_WRAPPEDPRINT_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 55 56 */ "57 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 90 02 00 00 48 C7 45 ?? FE FF FF FF")

#define GLOBAL_UFUNCTION_BIND_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 55 41 */ "56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 70 08 00 00 48 C7 44 24 50 ?? ?? ?? ??")

#define GLOBAL_UGAMEENGINE_EXEC_PHOOK \
    ::LESDK::Address::FromPostHook(/* 40 55 56 57 41 */ "54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC 40 0A 00 00")

#define GLOBAL_UGAMEENGINE_LOADMAP_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 4C 89 */ "48 ?? 4C 89 40 ?? 48 89 50 ?? 48 89 48 ?? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 F8 FE FF FF 48 81 EC C8 01 00 00 48 C7 45 28 FE FF FF FF")

#define GLOBAL_UGAMEENGINE_TICK_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 55 56 */ "57 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 20 04 00 00")

#define GLOBAL_UOBJECT_PROCESSDELEGATE_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 89 5C 24 ?? */ "48 89 54 24 ?? 55 57 41 54 41 56 41 57 48 83 EC 20 49 63 40 08")

#define GLOBAL_UOBJECT_SKIPFUNCTION_PHOOK \
    ::LESDK::Address::FromPostHook(/* 40 55 41 54 41 */ "55 41 56 41 57 48 83 EC 30 48 8D 6C 24 ?? 48 89 5D ?? 48 89 75 ?? 48 89 7D ?? 48 8B 05 ?? ?? ?? ?? 48 33 C5 48 89 45 00 48 8B DA")

#define GLOBAL_UWORLD_TICK_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 F3 0F */ "11 50 ?? 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A1")


#define GLOBAL_UACTORCHANNEL_REPLICATEACTOR_PHOOK \
    ::LESDK::Address::FromPostHook(/* 40 55 53 56 57 */ "41 54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC 08 02 00 00 48 C7 85 ?? ?? ?? ?? FE FF FF FF")

#define GLOBAL_UNETCONNECTION_CLIENTSUPPORTSLEVEL_PATTERN \
    ::LESDK::Address::FromPattern("48 89 5C 24 ?? 57 48 83 EC 20 48 8B DA 48 8B F9 48 85 D2 74 ?? 48 8B 15 ?? ?? ?? ?? 0F 1F 40 00")

#define GLOBAL_UPACKAGEMAP_SUPPORTSOBJECT_PATTERN \
    ::LESDK::Address::FromPattern("48 83 EC 28 48 8B 01 FF 90 ?? ?? ?? ?? 33 C9 83 F8 FF")

#define GLOBAL_UWORLD_SERVERTICKCLIENTS_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 48 89 */ "48 ?? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 00 02 00 00")

#define GLOBAL_SORT_REPLICATED_ACTORS_OFFSET \
    ::LESDK::Address::FromOffset(0x58B0A0)


// ! Miscellaneous functions / methods.
// ========================================

using tWndProc = LRESULT __stdcall(HWND, UINT, WPARAM, LPARAM);
extern tWndProc* WndProc_orig;

LRESULT __stdcall WndProc_hook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

using t_IDXGISwapChain_Present = HRESULT __stdcall(IDXGISwapChain*, UINT, UINT);
extern t_IDXGISwapChain_Present* IDXGISwapChain_Present_orig;

HRESULT __stdcall IDXGISwapChain_Present_hook(IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags);

using t_BioRequestExit = void(UBOOL bForce, INT ExitCode, INT Unused);
extern t_BioRequestExit* BioRequestExit_orig;

void BioRequestExit_hook(UBOOL bForce, INT ExitCode, INT Unused);

// ! UCanvas methods.
// ========================================

#pragma pack(push, 4)
struct CStringSizeParams
{
    unsigned char       Unknown_00[0x20];   // 0x00
    FVector2D           Scaling;            // 0x20
    UFont*              DrawFont;           // 0x28
    unsigned char       Unknown_30[0x18];   // 0x30
    FVector2D           OutSize;            // 0x48
    unsigned char       Unknown_50[0x20];   // 0x50  -  safety padding
};
#pragma pack(pop)

static_assert(offsetof(CStringSizeParams, Scaling) == 0x20);
static_assert(offsetof(CStringSizeParams, DrawFont) == 0x28);
static_assert(offsetof(CStringSizeParams, OutSize) == 0x48);

using t_UUIString_StringSize = void(CStringSizeParams* Params, WCHAR const* Text, WCHAR const* Unknown1, UBOOL Unknown2);
extern t_UUIString_StringSize* UUIString_StringSize;

using t_UCanvas_WrappedPrint = int(UCanvas* Context, UBOOL Draw, INT* OutX, INT* OutY, UFont* Font, float ScaleX, float ScaleY, UBOOL Center, WCHAR const* Text, FFontRenderInfo const* RenderInfo);
extern t_UCanvas_WrappedPrint* UCanvas_WrappedPrint;


// ! UFunction methods.
// ========================================

using t_UFunction_Bind = void(UFunction* Context);
extern t_UFunction_Bind* UFunction_Bind_orig;

void UFunction_Bind_hook(UFunction* Context);


// ! UGameEngine methods.
// ========================================

using t_UGameEngine_Exec = UBOOL(UGameEngine*, WCHAR const*, void*);
extern t_UGameEngine_Exec* UGameEngine_Exec_orig;

UBOOL UGameEngine_Exec_hook(UGameEngine* Context, WCHAR const* Command, void* Ar);

using t_UGameEngine_LoadMap = UBOOL(UGameEngine*, FURL*, UPendingLevel*, FString*);
extern t_UGameEngine_LoadMap* UGameEngine_LoadMap_orig;

UBOOL UGameEngine_LoadMap_hook(UGameEngine* Context, FURL* Url, UPendingLevel* Level, FString* Error);

using t_UGameEngine_Tick = void(UGameEngine*, float);
extern t_UGameEngine_Tick* UGameEngine_Tick_orig;

void UGameEngine_Tick_hook(UGameEngine* Context, float Delta);


// ! UObject methods.
// ========================================

extern bool gb_logCallFunction;
extern bool gb_logProcessDelegate;
extern bool gb_logProcessEvent;
extern bool gb_logProcessInternal;

using t_UObject_CallFunction = tCallFunction;
extern t_UObject_CallFunction* UObject_CallFunction_orig;

void UObject_CallFunction_hook(UObject* Context, FFrame* Stack, void* Result, UFunction* Function);

using t_UObject_ProcessDelegate = void(UObject* Context, SFXName DelegateName, FScriptDelegate* Delegate, void* Parms, void* Result);
extern t_UObject_ProcessDelegate* UObject_ProcessDelegate_orig;

void UObject_ProcessDelegate_hook(UObject* Context, SFXName DelegateName, FScriptDelegate* Delegate, void* Parms, void* Result);

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


// ! <Methods relevant to network debug.>
// ========================================

#ifndef _FINAL_RELEASE

extern bool gb_logReplicateActor;

using t_UActorChannel_ReplicateActor = void(UActorChannel* Context);
extern t_UActorChannel_ReplicateActor* UActorChannel_ReplicateActor_orig;

void UActorChannel_ReplicateActor_hook(UActorChannel* Context);

using t_UNetConnection_ClientSupportsLevel = int(UNetConnection* Context, UObject* Object);
extern t_UNetConnection_ClientSupportsLevel* UNetConnection_ClientSupportsLevel_orig;

int UNetConnection_ClientSupportsLevel_hook(UNetConnection* Context, UObject* Object);

using t_UPackageMap_SupportsObject = int(UPackageMap* Context, UObject* Object);
extern t_UPackageMap_SupportsObject* UPackageMap_SupportsObject_orig;

int UPackageMap_SupportsObject_hook(UPackageMap* Context, UObject* Object);

using t_UWorld_ServerTickClients = int(UWorld* Context, float Delta);
extern t_UWorld_ServerTickClients* UWorld_ServerTickClients_orig;

int UWorld_ServerTickClients_hook(UWorld* Context, float Delta);

#pragma pack(push, 4)
struct CReplicatedActorRecord
{
    int                 SortKey;
    AActor*             Actor;
    UActorChannel*      Channel;
};
static_assert(offsetof(CReplicatedActorRecord, Channel) == 12);
static_assert(sizeof(CReplicatedActorRecord) == 20);
#pragma pack(pop)

using t_SortReplicatedActors = void(CReplicatedActorRecord** Actors, int Count);
extern t_SortReplicatedActors* SortReplicatedActors_orig;

void SortReplicatedActors_hook(CReplicatedActorRecord** Actors, int Count);

#endif // _FINAL_RELEASE
