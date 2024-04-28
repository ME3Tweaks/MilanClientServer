/**
 * @file        MilanHost/Misc.hpp
 * @header      Miscellaneous utilities.
 */

#pragma once


class RelativeTimer final
{
    long long Frequency{};
    long long CounterStart{};

public:

    RelativeTimer();
    float GetSeconds() const;
    float GetMilliseconds() const;
};
