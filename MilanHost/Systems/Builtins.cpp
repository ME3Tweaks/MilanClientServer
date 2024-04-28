#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include "Systems/Builtins.hpp"


namespace Milan::Builtins
{
    void Initialize(::LESDK::Initializer& Context)
    {
        GEngine = Context.ResolveTyped<UEngine*>(BUILTIN_GENGINE_RIP);
        GMalloc = Context.ResolveTyped<FMallocLike*>(BUILTIN_GMALLOC_RIP);
        GNatives = Context.ResolveTyped<tNative*>(BUILTIN_GNATIVES_RIP);
        GSys = Context.ResolveTyped<USystem*>(BUILTIN_GSYS_RIP);
        GWorld = Context.ResolveTyped<UWorld*>(BUILTIN_GWORLD_RIP);

        UObject::GObjObjects = Context.ResolveTyped<TArray<UObject*>>(BUILTIN_GOBOBJECTS_RIP);
        SFXName::GBioNamePools = Context.ResolveTyped<SFXNameEntry const*>(BUILTIN_SFXNAMEPOOLS_RIP);
        SFXName::GInitMethod = Context.ResolveTyped<SFXName::tInitMethod>(BUILTIN_SFXNAMEINIT_PHOOK);


        // There is one thing we absolutely MUST have, and it is GMalloc.
        // If we failed to initialize GMalloc, just bail out early because we can't recover gracefully.

        assert(GMalloc != nullptr && *GMalloc != nullptr);

        void* const BogusAllocation = (*GMalloc)->Malloc(4, UN_DEFAULT_ALIGNMENT);
        void* const BogusReallocation = (*GMalloc)->Realloc(BogusAllocation, 128, UN_DEFAULT_ALIGNMENT);
        (*GMalloc)->Free(BogusReallocation);
    }

    void Terminate() {}
}
