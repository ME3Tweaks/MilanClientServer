/**
 * @file        MilanHost/Engine/Hooks.hpp
 * @brief       Declarations and patterns for hooked or found functions in the engine.
 */

#pragma once

#include <d3d11.h>
#include <imgui.h>

#include <LESDK/Headers.hpp>


// ! Enable / disable definitions.
// ========================================

#define ENABLE_GUIDCACHE_OVERRIDE 0

// ! Hooking macros.
// ========================================
//
// Every hook goes through SPIv4's deferred path: the detour is created disabled, its trampoline is
// published into X_orig, and only then is it enabled. A plain InstallHook enables the detour before
// returning, so a game thread entering the target in between would run X_hook with X_orig still
// null and call straight through address 0. The game keeps running on its own threads while
// MilanHost attaches, so that window is real (see Engine/LoadGate.hpp).

#define LOG_SDK_GLOBAL(pCheck, pReal)                                                       \
    MILAN_CHECKW(pCheck != nullptr, L"failed to find " #pCheck);                            \
    MILAN_TRACEW(ELOG_StartupGlobals, L"" #pCheck " found: 0x%p -> 0x%p", (void*)pCheck, (void*)pReal)

#define _INSTALL_HOOK_ADDR_NO_LOG(Address, String, Function)                                       \
    auto* const MILAN_VARCAT(Function, _target) = (MILAN_VARCAT(t_, Function)*)Address;     \
    MILAN_VARCAT(Function, _orig) = (MILAN_VARCAT(t_, Function)*)(Context.InstallHookDeferred(String, MILAN_VARCAT(Function, _target), MILAN_VARCAT(Function, _hook))); \
    if (MILAN_VARCAT(Function, _orig) != nullptr) Context.EnableHook(String)

#define _INSTALL_HOOK_ADDR(Address, String, Function)                                       \
    _INSTALL_HOOK_ADDR_NO_LOG(Address, String, Function);                                   \
    MILAN_TRACEW(ELOG_StartupGlobals, L"" String L" hooked: 0x%p -> 0x%p", (void*)MILAN_VARCAT(Function, _target), (void*)MILAN_VARCAT(Function, _orig))

#define _INSTALL_HOOK_INIT(Pattern, String, Function)                                       \
    _INSTALL_HOOK_ADDR(Context.ResolveTyped<MILAN_VARCAT(t_, Function)>(Pattern), String, Function)


#define INSTALL_HOOK_ONE(Function, Pattern)  _INSTALL_HOOK_INIT(Pattern, #Function, Function)
#define INSTALL_HOOK_TWO(Class, Function, Pattern)  _INSTALL_HOOK_INIT(Pattern, #Class "::" #Function, MILAN_VARCAT(MILAN_VARCAT(Class, _), Function))

#define INSTALL_HOOK_ONE_ADDR(Function, Address)  _INSTALL_HOOK_ADDR(Address, #Function, Function)
#define INSTALL_HOOK_ONE_ADDR_NO_LOG(Function, Address)  _INSTALL_HOOK_ADDR_NO_LOG(Address, #Function, Function)
#define INSTALL_HOOK_TWO_ADDR(Class, Function, Address)  _INSTALL_HOOK_ADDR(Address, #Class "::" #Function, MILAN_VARCAT(MILAN_VARCAT(Class, _), Function))


#define _FIND_VAR_ADDR(Address, String, Variable)                                           \
    Variable = (MILAN_VARCAT(t_, Variable)*)Address;                                        \
    MILAN_TRACEW(ELOG_StartupGlobals, L"" String L" found: 0x%p", (void*)Variable)

#define _FIND_VAR_INIT(Pattern, String, Variable)                                           \
    _FIND_VAR_ADDR(Context.ResolveTyped<MILAN_VARCAT(t_, Variable)>(Pattern), String, Variable)


#define FIND_VAR_ONE(Variable, Pattern)  _FIND_VAR_INIT(Pattern, #Variable, Variable)
#define FIND_VAR_TWO(Class, Variable, Pattern)  _FIND_VAR_INIT(Pattern, #Class "::" #Variable, MILAN_VARCAT(MILAN_VARCAT(Class, _), Variable))


// ! Globals and hook patterns.
// ========================================

#define GLOBAL_GPROPERTY_POSTRIP \
    ::LESDK::Address::FromInstrRelative("48 8B 15 ?? ?? ?? ?? FF 90 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 8D 7C 24 ?? 48 85 C0 C7 44 24")

#define GLOBAL_GPROPADDR_POSTRIP \
    ::LESDK::Address::FromInstrRelative("48 8B 35 ?? ?? ?? ?? 44 89 75 ?? 48 8B 43 28 0F B6 10")

#define GLOBAL_GPROPOBJECT_POSTRIP \
    ::LESDK::Address::FromInstrRelative("48 89 0D ?? ?? ?? ?? 45 33 F6 4C 89 35 ?? ?? ?? ?? 48 8B 42 28")

#define GLOBAL_GRUNTIMEUCFLAGS_OFFSET \
    ::LESDK::Address::FromOffset(0x17CD620)
    // TODO: Use post-logical lookup once sdk implements that.
    // ::LESDK::Address::FromInstrLogical("83 0D ?? ?? ?? ?? 02 48 8B 45 28 0F B6 08 48 FF C0")

#define GLOBAL_BIOREQUESTEXIT_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 89 5C 24 ?? */ "48 89 6C 24 ?? 48 89 74 24 ?? 57 48 81 EC 50 03 00 00")

// Installed by LoadGate before anything else in attach, so it is an RVA rather than a pattern scan.
// Matches the old pattern "48 8B C4 44 89 40 18 48 89 48 08 53 56 57 41 56 41 57 48 83 EC 50 ...".
#define GLOBAL_LOADPACKAGE_RVA  ::LESDK::Address::FromOffset(0x12B250)

// Linker patching. RVAs shared with the LExASIs LinkerPatcher, confirmed in Ghidra.
#define GLOBAL_FPACKAGEFILECACHE_FINDPACKAGEFILE_RVA  ::LESDK::Address::FromOffset(0xABF790)
#define GLOBAL_ULINKERLOAD_CREATEEXPORT_RVA           ::LESDK::Address::FromOffset(0xFBDD0)
#define GLOBAL_ULINKERLOAD_FINALIZECREATION_RVA       ::LESDK::Address::FromOffset(0xFF520)
#define GLOBAL_UOBJECT_STATICFINDOBJECT_RVA           ::LESDK::Address::FromOffset(0x134510)
#define GLOBAL_UOBJECT_STATICLOADOBJECT_RVA           ::LESDK::Address::FromOffset(0x134DD0)
#define GLOBAL_UOBJECT_ENDLOAD_RVA                   ::LESDK::Address::FromOffset(0x123BF0)
#define GLOBAL_GOBJBEGINLOADCOUNT_RVA                 ::LESDK::Address::FromOffset(0x1807D80)
#define GLOBAL_FCONFIGCACHEINI_CREATECONFIGFILE_RVA   ::LESDK::Address::FromOffset(0x91700)
#define GLOBAL_FCONFIGCACHEINI_GETARRAY_RVA           ::LESDK::Address::FromOffset(0x94F60)
#define GLOBAL_GCONFIG_RVA                            ::LESDK::Address::FromOffset(0x17CD5B8)

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

#define GLOBAL_USFXENGINE_UPDATEDYNAMICLOADMAPPINGFINDHASH_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 89 5C 24 08 */ "48 89 6C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 41 56 48 83 EC 20 33 FF 44 8B F2")

// USFXEngine's override of the UGameEngine virtual (vtable slot 123).
#define GLOBAL_USFXENGINE_GETDISCONNECTFALLBACKMAP_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 89 54 24 10 */ "57 48 83 EC 40 48 C7 44 24 20 FE FF FF FF 48 89 5C 24 50 48 89 6C 24 60 48 89 74 24 68 48 8B DA 48 8B E9 E8 ?? ?? ?? ?? 48 8B F8 48 85 C0 0F 84")

#define GLOBAL_UWORLD_SPAWNACTOR_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 55 41 */ "54 41 55 41 56 41 57 48 8D 68 ?? 48 81 EC A0 00 00 00 48 C7 45 ?? FE FF FF FF 48 89 58 ?? 48 89 70 ?? 48 89 78 ?? 49 8B F9")

#define GLOBAL_UWORLD_TICK_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 F3 0F */ "11 50 ?? 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A1")


#define GLOBAL_UACTORCHANNEL_REPLICATEACTOR_PHOOK \
    ::LESDK::Address::FromPostHook(/* 40 55 53 56 57 */ "41 54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC 08 02 00 00 48 C7 85 ?? ?? ?? ?? FE FF FF FF")

#define GLOBAL_UACTORCHANNEL_SETCHANNELACTOR_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 57 41 */ "56 41 57 48 81 EC 40 01 00 00 48 C7 44 24 50 FE FF FF FF 48 89 58 10 48 89 68 18 48 89 70 20 48 8B FA")

#define GLOBAL_UGUIDCACHE_LOOKUPPACKAGEGUID_PHOOK \
    ::LESDK::Address::FromPostHook(/* 40 53 48 83 EC */ "20 48 83 C1 60 49 8B D8 E8 ?? ?? ?? ?? 48 8B C8 48 85 C0 74 06")

#define GLOBAL_UNETCONNECTION_CLIENTSUPPORTSLEVEL_PATTERN \
    ::LESDK::Address::FromPattern("48 89 5C 24 ?? 57 48 83 EC 20 48 8B DA 48 8B F9 48 85 D2 74 ?? 48 8B 15 ?? ?? ?? ?? 0F 1F 40 00")

#define GLOBAL_UNETCONNECTION_CLOSE_PATTERN \
    ::LESDK::Address::FromPattern("40 53 48 83 EC 20 48 83 B9 ?? ?? ?? ?? 00 48 8B D9 74 ?? 48 8B 89 ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01")

#define GLOBAL_UNETCONNECTION_CREATECHANNEL_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 89 5C 24 10 */ "48 89 6C 24 ?? 56 57 41 56 48 83 EC 40 48 8B 01 41 8B D9 41 8B E8 48 63 FA 48 8B F1")

#define GLOBAL_UPACKAGEMAP_SUPPORTSOBJECT_PATTERN \
    ::LESDK::Address::FromPattern("48 83 EC 28 48 8B 01 FF 90 ?? ?? ?? ?? 33 C9 83 F8 FF")

#define GLOBAL_UPACKAGEMAP_SERIALIZEOBJECT_PATTERN \
    ::LESDK::Address::FromPattern("48 89 5C 24 08 48 89 74 24 ?? 48 89 7C 24 ?? 55 41 56 41 57 48 8B EC 48 83 EC 30 45 33 FF 4D 8B F1 49 8B F8 48 8B DA")

#define GLOBAL_UNETPENDINGLEVEL_NOTIFYCONTROLMESSAGE_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 48 89 */ "50 ?? 48 89 48 ?? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC F0 01 00 00 48 C7 45 00 FE FF FF FF")

#define GLOBAL_UWORLD_NOTIFYCONTROLMESSAGE_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 48 89 */ "48 ?? 55 41 54 41 55 41 56 41 57 48 8D A8 D8 FE FF FF 48 81 EC 00 02 00 00 48 C7 85 F0 00 00 00 FE FF FF FF")

#define GLOBAL_UWORLD_SERVERTICKCLIENTS_PHOOK \
    ::LESDK::Address::FromPostHook(/* 48 8B C4 48 89 */ "48 ?? 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 00 02 00 00")

#define GLOBAL_SORT_REPLICATED_ACTORS_OFFSET \
    ::LESDK::Address::FromOffset(0x58B0A0)

#define GLOBAL_GLOBALDYNAMICBUILDVERSION_PATTERN \
    ::LESDK::Address::FromPattern("48 83 EC 28 48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 88 ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01 48 83 C4 28 48 FF A0 ?? ?? ?? ?? 33 C0")

#define GLOBAL_FSOCKETWIN_SENDTO_OFFSET     ::LESDK::Address::FromOffset(0x909F30)
#define GLOBAL_FSOCKETWIN_SEND_OFFSET       ::LESDK::Address::FromOffset(0x9091D0)
#define GLOBAL_FSOCKETWIN_RECVFROM_OFFSET   ::LESDK::Address::FromOffset(0x908520)
#define GLOBAL_FSOCKETWIN_RECV_OFFSET       ::LESDK::Address::FromOffset(0x9084F0)


// ! Logging toggles.
// ========================================

extern bool gb_logCallFunction;
extern bool gb_logProcessDelegate;
extern bool gb_logProcessEvent;
extern bool gb_logProcessInternal;
extern bool gb_logSerializeObject;


// ! Miscellaneous functions / methods.
// ========================================
void ApplyIconToWindow(HWND window);
using t_CreateWindowExW = HWND(WINAPI)(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
extern t_CreateWindowExW* CreateWindowExW_orig;
HWND WINAPI CreateWindowExW_hook(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);

using t_WndProc = LRESULT __stdcall(HWND, UINT, WPARAM, LPARAM);
extern t_WndProc* WndProc_orig;

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


// ! Package loading and linker methods.
// ========================================

using t_LoadPackage = UPackage*(UObject*, WCHAR const*, INT);
extern t_LoadPackage* LoadPackage_orig;

UPackage* LoadPackage_hook(UObject* Outer, WCHAR const* Filename, INT Flags);

using t_FPackageFileCache_FindPackageFile = UBOOL(void* Context, WCHAR const* InName, FGuid* Guid, FString* OutFileName, WCHAR const* Language, UBOOL Unknown);
extern t_FPackageFileCache_FindPackageFile* FPackageFileCache_FindPackageFile_orig;

UBOOL FPackageFileCache_FindPackageFile_hook(void* Context, WCHAR const* InName, FGuid* Guid, FString* OutFileName, WCHAR const* Language, UBOOL Unknown);

using t_ULinkerLoad_CreateExport = UObject*(ULinkerLoad* Context, INT Index);
extern t_ULinkerLoad_CreateExport* ULinkerLoad_CreateExport_orig;

UObject* ULinkerLoad_CreateExport_hook(ULinkerLoad* Context, INT Index);

// Last stage of ULinkerLoad::Tick; returns the ELinkerStatus (0 = timed out, 1 = loaded).
using t_ULinkerLoad_FinalizeCreation = INT(ULinkerLoad* Context);
extern t_ULinkerLoad_FinalizeCreation* ULinkerLoad_FinalizeCreation_orig;

INT ULinkerLoad_FinalizeCreation_hook(ULinkerLoad* Context);


// ! UObject methods.
// ========================================

// UObject::EndLoad (static, no arguments). Only the outermost EndLoad does the work: nested calls just
// decrement GObjBeginLoadCount and return.
using t_UObject_EndLoad = void();
extern t_UObject_EndLoad* UObject_EndLoad_orig;

void UObject_EndLoad_hook();

// UObject::StaticFindObject (static). InObjectPackage == ANY_PACKAGE ((UObject*)-1) or null means
// OrigInName is a full path. Pure in-memory lookup; a null class is a wildcard. (Ghidra shows a fifth
// argument; the body never reads it.)
using t_UObject_StaticFindObject = UObject*(UClass* ObjectClass, UObject* InObjectPackage, WCHAR const* OrigInName, UBOOL bExactClass);
extern t_UObject_StaticFindObject* UObject_StaticFindObject_orig;

UObject* UObject_StaticFindObject_hook(UClass* ObjectClass, UObject* InObjectPackage, WCHAR const* OrigInName, UBOOL bExactClass);

// UObject::StaticLoadObject (static). Finds the object in memory first, else loads it from its package.
// ObjectClass must not be null: ULinkerLoad::Create dereferences it.
using t_UObject_StaticLoadObject = UObject*(UClass* ObjectClass, UObject* InOuter, WCHAR const* InName, WCHAR const* Filename,
    INT LoadFlags, UPackageMap* Sandbox, UBOOL bAllowObjectReconciliation);
extern t_UObject_StaticLoadObject* UObject_StaticLoadObject_orig;

UObject* UObject_StaticLoadObject_hook(UClass* ObjectClass, UObject* InOuter, WCHAR const* InName, WCHAR const* Filename,
    INT LoadFlags, UPackageMap* Sandbox, UBOOL bAllowObjectReconciliation);

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


// ! USFXEngine methods.
// ========================================

using t_USFXEngine_UpdateDynamicLoadMappingFindHash = int(USFXEngine* Context, int ObjectNetID);
extern t_USFXEngine_UpdateDynamicLoadMappingFindHash* USFXEngine_UpdateDynamicLoadMappingFindHash_orig;

int USFXEngine_UpdateDynamicLoadMappingFindHash_hook(USFXEngine* Context, int ObjectNetID);

using t_USFXEngine_GetDisconnectFallbackMap = FString*(USFXEngine* Context, FString* Result);
extern t_USFXEngine_GetDisconnectFallbackMap* USFXEngine_GetDisconnectFallbackMap_orig;

FString* USFXEngine_GetDisconnectFallbackMap_hook(USFXEngine* Context, FString* Result);


// ! UWorld methods.
// ========================================

using t_UWorld_SpawnActor = AActor* (
    UWorld* Context,
    UClass* Class, SFXName Name,
    FVector* Location, FRotator* Rotation,
    AActor* Template,
    int Unknown1, int Unknown2,
    AActor* Owner, AActor* Instigator,
    int Unknown4);
extern t_UWorld_SpawnActor* UWorld_SpawnActor_orig;

AActor* UWorld_SpawnActor_hook(
    UWorld* Context,
    UClass* Class, SFXName Name,
    FVector* Location, FRotator* Rotation,
    AActor* Template,
    int Unknown1, int Unknown2,
    AActor* Owner, AActor* Instigator,
    int Unknown4);


using t_UWorld_Tick = void(UWorld* Context, int Unknown, float Delta);
extern t_UWorld_Tick* UWorld_Tick_orig;

void UWorld_Tick_hook(UWorld* Context, int Unknown, float Delta);


// ! Game socket I/O (all builds: matchmaking shares the UE3 socket).
// ========================================

using t_FSocketWin_SendTo = UINT(FSocket*, BYTE*, INT, INT*, SOCKADDR_IN*);
extern t_FSocketWin_SendTo* FSocketWin_SendTo_orig;

UINT FSocketWin_SendTo_hook(FSocket* Context, BYTE* Data, INT Count, INT* Sent, SOCKADDR_IN* Destination);

using t_FSocketWin_RecvFrom = UINT(FSocket*, BYTE*, INT, INT*, SOCKADDR_IN*);
extern t_FSocketWin_RecvFrom* FSocketWin_RecvFrom_orig;

UINT FSocketWin_RecvFrom_hook(FSocket* Context, BYTE* Buffer, INT Max, INT* Read, SOCKADDR_IN* Source);

// LE3 11.3.2, RVA 0x8CB640: a joiner checking a package the host uses against its own GUID.
#define GLOBAL_NET_VERIFYPACKAGEINFO_RVA  ::LESDK::Address::FromOffset(0x8CB640)
using t_Net_VerifyPackageInfo = void*(void* Context, FPackageInfo* Info);
extern t_Net_VerifyPackageInfo* Net_VerifyPackageInfo_orig;

void* Net_VerifyPackageInfo_hook(void* Context, FPackageInfo* Info);


// ! <Methods relevant to network debug.>
// ========================================

#ifndef _FINAL_RELEASE

using t_UActorChannel_ReplicateActor = void(UActorChannel* Context);
extern t_UActorChannel_ReplicateActor* UActorChannel_ReplicateActor_orig;

void UActorChannel_ReplicateActor_hook(UActorChannel* Context);

using t_UActorChannel_SetChannelActor = void(UActorChannel* Context, AActor* Actor);
extern t_UActorChannel_SetChannelActor* UActorChannel_SetChannelActor_orig;

void UActorChannel_SetChannelActor_hook(UActorChannel* Context, AActor* Actor);

using t_UGuidCache_LookupPackageGuid = int(UGuidCache* Context, SFXName Package, FGuid* OutGuid);
extern t_UGuidCache_LookupPackageGuid* UGuidCache_LookupPackageGuid_orig;

int UGuidCache_LookupPackageGuid_hook(UGuidCache* Context, SFXName Package, FGuid* OutGuid);

using t_UNetConnection_ClientSupportsLevel = int(UNetConnection* Context, UObject* Object);
extern t_UNetConnection_ClientSupportsLevel* UNetConnection_ClientSupportsLevel_orig;

int UNetConnection_ClientSupportsLevel_hook(UNetConnection* Context, UObject* Object);

using t_UNetConnection_Close = void(UNetConnection* Context);
extern t_UNetConnection_Close* UNetConnection_Close_orig;

void UNetConnection_Close_hook(UNetConnection* Context);

using t_UNetConnection_CreateChannel = UChannel* (UNetConnection* Context, EChannelType Type, INT bLocal, INT Index);
extern t_UNetConnection_CreateChannel* UNetConnection_CreateChannel_orig;

UChannel* UNetConnection_CreateChannel_hook(UNetConnection* Context, EChannelType Type, INT bLocal, INT Index);

using t_UPackageMap_SupportsObject = int(UPackageMap* Context, UObject* Object);
extern t_UPackageMap_SupportsObject* UPackageMap_SupportsObject_orig;

int UPackageMap_SupportsObject_hook(UPackageMap* Context, UObject* Object);

using t_UPackageMap_SerializeObject = int(UPackageMap* Context, FArchive* Archive, UClass* Class, UObject** Object);
extern t_UPackageMap_SerializeObject* UPackageMap_SerializeObject_orig;

int UPackageMap_SerializeObject_hook(UPackageMap* Context, FArchive* Archive, UClass* Class, UObject** Object);

using t_UNetPendingLevel_NotifyControlMessage = void(UNetPendingLevel* Context, UNetConnection* Connection, BYTE Type, FInBunch* InBunch);
extern t_UNetPendingLevel_NotifyControlMessage* UNetPendingLevel_NotifyControlMessage_orig;

void UNetPendingLevel_NotifyControlMessage_hook(UNetPendingLevel* Context, UNetConnection* Connection, BYTE Type, FInBunch* InBunch);

using t_UWorld_NotifyControlMessage = void(UWorld* Context, UNetConnection* Connection, BYTE Type, FInBunch* InBunch);
extern t_UWorld_NotifyControlMessage* UWorld_NotifyControlMessage_orig;

void UWorld_NotifyControlMessage_hook(UWorld* Context, UNetConnection* Connection, BYTE Type, FInBunch* InBunch);

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

using t_GlobalDynamicBuildVersion = UINT();
extern t_GlobalDynamicBuildVersion* GlobalDynamicBuildVersion_orig;

UINT GlobalDynamicBuildVersion_hook();

using t_FSocketWin_Send = UINT(FSocket*, BYTE*, INT, INT*);
extern t_FSocketWin_Send* FSocketWin_Send_orig;

UINT FSocketWin_Send_hook(FSocket* Context, BYTE* Data, INT Count, INT* Sent);

using t_FSocketWin_Recv = UINT(FSocket*, BYTE*, INT, INT*);
extern t_FSocketWin_Recv* FSocketWin_Recv_orig;

UINT FSocketWin_Recv_hook(FSocket* Context, BYTE* Buffer, INT Max, INT* Read);

#endif // _FINAL_RELEASE
