/**
 * @file        MilanHost/Components/CNetworkProfiler.hpp
 * @header      Component for controlling replication network profiler.
 */

#pragma once

#include "Base/Base.hpp"
#include "Engine/Hooks.hpp"
#include "Engine/Canvas.hpp"


class CNetworkProfiler final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"netprof";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    /** Executes some code when a vectored exception is handled by MilanHost. */
    void On_VectoredException(void* Address, DWORD Code, _EXCEPTION_POINTERS* Exceptions);
};

static_assert(ComponentType<CNetworkProfiler>);
