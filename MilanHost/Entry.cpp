// TEMPORARY: Until LExSDKv2 is updated with a fix.
#define SDK_TARGET_LE3

// Include LESDK, with the SPI it ships.
#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

// Include auto-generated ECSM bindings.
#include <milan_ecsm_ffi.h>

// Include host's own stuff.
#include "Entry.hpp"


SPI_PLUGINSIDE_SUPPORT(L"MilanHost", L"Project Milan Team", L"0.1.0", SPI_GAME_LE3, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_SEQATTACH;


void AttachPoint(ISharedProxyInterface* Interface);
void DetachPoint(ISharedProxyInterface* Interface);


SPI_IMPLEMENT_ATTACH
{
    ::LESDK::InitializeConsole();
    ::AttachPoint(InterfacePtr);
    return true;
}

SPI_IMPLEMENT_DETACH
{
    ::DetachPoint(InterfacePtr);
    ::LESDK::TerminateConsole();
    return true;
}


void AttachPoint(ISharedProxyInterface* const Interface)
{
    ::LESDK::Initializer Init{ Interface, "MilanHost" };

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

    // This is linked to ecsm ffi.
    ::say_hello("stranger");
}

void DetachPoint(ISharedProxyInterface* const Interface)
{
    (void)Interface;
}
