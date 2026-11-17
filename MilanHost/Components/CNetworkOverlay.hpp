/**
 * @file        MilanHost/Components/CNetworkOverlay.hpp
 * @header      Component for drawing network stats overlay.
 */

#pragma once

#include "Base/Base.hpp"
#include "Engine/Hooks.hpp"


class CNetworkOverlay final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"netstats";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    void On_UObject_ProcessInternal_Post(UObject* Context, FFrame* Stack, void* Result);
};

static_assert(ComponentType<CNetworkOverlay>);
