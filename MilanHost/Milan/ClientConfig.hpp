/**
 * @file        MilanHost/Milan/ClientConfig.hpp
 * @header      Client-side configuration.
 */

#pragma once

#include "Containers/string.hpp"
#include "Milan/Base.hpp"


/**
 * @brief
 * Fully-parsed local configuration.
 */
struct ClientConfig final
{
    std::string     ConnectHost{ "127.0.0.1" };
    int             ConnectPort{ 11111 };
    bool            LoggingVerbose{ false };

    bool TryRead(std::wstring_view InPath, FString& OutError);
};
