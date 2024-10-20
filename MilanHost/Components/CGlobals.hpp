/**
 * @file        MilanHost/Components/CGlobals.hpp
 * @header      Component initializing global hooks and (some) variables.
 */

#pragma once
#include "Milan/Base.hpp"


// Will not be compiled in FR builds.
class GuidCacheOverride;


class CGlobals final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"globals";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    /** Additional initialization code that needs to run after all other components' initialization. */
    void PostInitialize(::LESDK::Initializer& Context);

    /** Uninstalls several hooks which may otherwise cause use-after-free during component termination. */
    void UninstallSomeHooks(::LESDK::Initializer& Context);

    void UpdateGuidCacheOverride();

private:

    void InitSDK(::LESDK::Initializer& Context);
    void InitGlobals(::LESDK::Initializer& Context);
    void InitWindowProc(::LESDK::Initializer& Context);
    void InitNetworkHooks(::LESDK::Initializer& Context);
    void InitNetworkInjections();

    void InitGuidCacheOverride();
    void CleanupGuidCacheOverride();

    GuidCacheOverride* mp_guidCacheOverride{ nullptr };
};

static_assert(ComponentType<CGlobals>);
