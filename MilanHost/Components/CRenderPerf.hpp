/**
 * @file        MilanHost/Components/CRenderPerf.hpp
 * @header      Render-thread performance improvements
 */

#pragma once

#include <cstdint>

#include "Base/Base.hpp"


/**
 * Caches ID3D11Device::CreateInputLayout results so they don't need to be recreated
 * for each shader every single frame. 
 */
class CRenderPerf final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"renderperf";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

private:

    void RegisterCommands();

    bool            mb_enabled{ false };
    bool            mb_createDeviceHooked{ false };

    // Hit rates
    std::uint64_t   m_lastHits{ 0 };
    std::uint64_t   m_lastMisses{ 0 };
    LONGLONG        m_lastTicks{ 0 };

};

static_assert(ComponentType<CRenderPerf>);
