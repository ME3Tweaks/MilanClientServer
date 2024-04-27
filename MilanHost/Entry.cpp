// TEMPORARY: Until LExSDKv2 is updated with a fix.
#define SDK_TARGET_LE3

// Include LESDK, with the SPI it ships.
#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

// TEMPORARY: Until LExSDKv2 is updated with a fix.
#undef DWORD
#undef BYTE
#undef WORD
#undef FLOAT
#undef INT
#undef UINT

#undef INT8
#undef UINT8
#undef INT16
#undef UINT16
#undef INT32
#undef UINT32
#undef INT64
#undef UINT64

#undef CHAR
#undef WCHAR
#undef DOUBLE


// Include auto-generated ECSM bindings.
#include <milan_ecsm_ffi.h>

// Include our own stuff.
#include "Entry.hpp"
#include "Profiler.hpp"


SPI_PLUGINSIDE_SUPPORT(L"MilanHost", L"Project Milan Team", L"0.1.0", SPI_GAME_LE3, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_SEQATTACH;

SPI_IMPLEMENT_ATTACH
{
    ::LESDK::InitializeConsole();
    ::LESDK::Initializer Init{ InterfacePtr, "MilanHost" };

    // Initialize built-in globals.
    // ----------------------------------------

    GEngine = Init.ResolveTyped<UEngine*>(BUILTIN_GENGINE_RIP);
    GMalloc = Init.ResolveTyped<FMallocLike*>(BUILTIN_GMALLOC_RIP);
    GNatives = Init.ResolveTyped<tNative*>(BUILTIN_GNATIVES_RIP);
    GSys = Init.ResolveTyped<USystem*>(BUILTIN_GSYS_RIP);
    GWorld = Init.ResolveTyped<UWorld*>(BUILTIN_GWORLD_RIP);

    UObject::GObjObjects = Init.ResolveTyped<TArray<UObject*>>(BUILTIN_GOBOBJECTS_RIP);
    SFXName::GBioNamePools = Init.ResolveTyped<SFXNameEntry const*>(BUILTIN_SFXNAMEPOOLS_RIP);
    SFXName::GInitMethod = Init.ResolveTyped<SFXName::tInitMethod>(BUILTIN_SFXNAMEINIT_PHOOK);


    // Initialize subsystems.
    // ----------------------------------------

    ::Milan::Profiler::Initialize(Init);


    // This is linked to ecsm ffi.
    ::say_hello("stranger");

    return true;
}

SPI_IMPLEMENT_DETACH
{
    (void)InterfacePtr;

    ::Milan::Profiler::Terminate();
    ::LESDK::TerminateConsole();

    return true;
}
