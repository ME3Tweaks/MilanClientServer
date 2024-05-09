/**
 * @file        MilanHost/Misc.hpp
 * @header      Miscellaneous utilities.
 */

#pragma once

#include "Containers/string.hpp"
#include "Containers/vector.hpp"


class RelativeTimer final
{
    long long Frequency{};
    long long CounterStart{};

public:

    RelativeTimer();
    float GetSeconds() const;
    float GetMilliseconds() const;
};


void ReadFileFull(std::wstring_view InPath, ::game::vector<byte>& OutBuffer);
void WriteFileFull(std::wstring_view InPath, std::span<byte const> InBuffer);
