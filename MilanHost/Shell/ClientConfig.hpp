/**
 * @file        MilanHost/Shell/ClientConfig.hpp
 * @header      Client-side configuration.
 */

#pragma once

#include "Base/Base.hpp"
#include "Containers/string.hpp"
#include "Containers/vector.hpp"


/**
 * @brief
 * Fully-parsed local configuration.
 */
struct ClientConfig final
{
    game::string                    ConnectHost{ "127.0.0.1" };
    int                             ConnectPort{ 11111 };
    game::string                    MatchmakerUrl{ "http://127.0.0.1:5080" };
    game::vector<game::string>      LoggingSuppressed{};

    bool TryRead(std::wstring_view InPath, game::string& OutError) noexcept;
};

extern ClientConfig* gp_clientConfig;
extern game::string* gp_clientConfigError;

static constexpr wchar_t const* k_configFilename = L"./MilanHost.toml";

void InitGlobalConfig(wchar_t const* Filename);
void CleanupGlobalConfig();
