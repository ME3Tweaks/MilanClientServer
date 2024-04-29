#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include "Base.hpp"
#include "Systems/Builtins.hpp"
#include "Systems/Output.hpp"


namespace Milan::Allocator
{
    void Initialize(::LESDK::Initializer& Context)
    {
        // Cannot use logging here because that is dependend on GMalloc.

        GMalloc = Context.ResolveTyped<FMallocLike*>(BUILTIN_GMALLOC_RIP);
        assert(GMalloc != nullptr && *GMalloc != nullptr);

        void* const BogusAllocation = (*GMalloc)->Malloc(4, UN_DEFAULT_ALIGNMENT);
        void* const BogusReallocation = (*GMalloc)->Realloc(BogusAllocation, 128, UN_DEFAULT_ALIGNMENT);
        (*GMalloc)->Free(BogusReallocation);
    }

    void Terminate() {}
}

namespace Milan::Builtins
{
    void Initialize(::LESDK::Initializer& Context)
    {
        // Uncomment stuff as you need it.

        GEngine = Context.ResolveTyped<UEngine*>(BUILTIN_GENGINE_RIP);
        GNatives = Context.ResolveTyped<tNative*>(BUILTIN_GNATIVES_RIP);
        //GSys = Context.ResolveTyped<USystem*>(BUILTIN_GSYS_RIP);
        //GWorld = Context.ResolveTyped<UWorld*>(BUILTIN_GWORLD_RIP);

        UObject::GObjObjects = Context.ResolveTyped<TArray<UObject*>>(BUILTIN_GOBOBJECTS_RIP);
        SFXName::GBioNamePools = Context.ResolveTyped<SFXNameEntry const*>(BUILTIN_SFXNAMEPOOLS_RIP);
        SFXName::GInitMethod = Context.ResolveTyped<SFXName::tInitMethod>(BUILTIN_SFXNAMEINIT_PHOOK);

        MILAN_INFOA("builtins system initialized");
    }

    void Terminate()
    {
        MILAN_INFOA("builtins system terminating");
    }
}
