#include <LESDK/Init.hpp>
#include <LESDK/Headers.hpp>

#include "Base.hpp"
#include "Hooks.hpp"
#include "Systems/Globals.hpp"
#include "Systems/Output.hpp"
#include "Systems/Panels.hpp"


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

namespace Milan::Globals
{
    void Initialize(::LESDK::Initializer& Context)
    {
        GEngine = Context.ResolveTyped<UEngine*>(BUILTIN_GENGINE_RIP);
        GNatives = Context.ResolveTyped<tNative*>(BUILTIN_GNATIVES_RIP);
        GSys = Context.ResolveTyped<USystem*>(BUILTIN_GSYS_RIP);
        GWorld = Context.ResolveTyped<UWorld*>(BUILTIN_GWORLD_RIP);

        UObject::GObjObjects = Context.ResolveTyped<TArray<UObject*>>(BUILTIN_GOBOBJECTS_RIP);
        SFXName::GBioNamePools = Context.ResolveTyped<SFXNameEntry const*>(BUILTIN_SFXNAMEPOOLS_RIP);
        SFXName::GInitMethod = Context.ResolveTyped<SFXName::tInitMethod>(BUILTIN_SFXNAMEINIT_PHOOK);

        t_IDXGISwapChain_Present* IDXGISwapChain_Present_target = ::Milan::Panels::FindSwapChainPresent();
        IDXGISwapChain_Present_orig = (t_IDXGISwapChain_Present*)Context.InstallHook("IDXGISwapChain::Present", IDXGISwapChain_Present_target, IDXGISwapChain_Present_hook);
        MILAN_DEBUGW(L"IDXGISwapChain::Present hooked: %p -> %p", (void*)IDXGISwapChain_Present_target, (void*)IDXGISwapChain_Present_orig);

        WndProc_orig = Context.ResolveTyped<tWndProc>(GLOBAL_WNDPROC_RVA);
        // WndProc is not hooked but replaced using Win32 mechanism,
        // see ::Milan::Panels::OnSwapChainPresent for more.

        t_UGameEngine_Exec* UGameEngine_Exec_target = Context.ResolveTyped<t_UGameEngine_Exec>(GLOBAL_UGAMEENGINE_EXEC_PHOOK);
        UGameEngine_Exec_orig = (t_UGameEngine_Exec*)Context.InstallHook("UGameEngine::Exec", UGameEngine_Exec_target, UGameEngine_Exec_hook);
        MILAN_DEBUGW(L"UGameEngine::Exec hooked: %p -> %p", (void*)UGameEngine_Exec_target, (void*)UGameEngine_Exec_orig);

        MILAN_INFOW(L"globals system initialized");
    }

    void Terminate()
    {
        MILAN_INFOW(L"globals system terminating");
    }
}
