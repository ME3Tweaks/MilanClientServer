/**
 * @file        MilanHost/Components/CConfig.hpp
 * @header      Component for loading and providing client-side configuration.
 */

#pragma once

#include "Milan/Base.hpp"
#include "Milan/ClientConfig.hpp"


class CConfig final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"config";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    /** Config filename (path), relative to LE3 executable. */
    static constexpr wchar_t const* k_configFilename = L"./MilanHost.toml";

    /** Tries to load the client-side configuration. */
    void LoadConfig();

private:

    ClientConfig m_config;
};

static_assert(ComponentType<CConfig>);
