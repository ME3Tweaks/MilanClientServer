#include <Windows.h>
#include "Misc.hpp"


// ! RelativeTimer implementation.
// ========================================

RelativeTimer::RelativeTimer()
{
    LARGE_INTEGER liFreq{};
    LARGE_INTEGER liStart{};

    QueryPerformanceFrequency(&liFreq);
    QueryPerformanceCounter(&liStart);

    Frequency = liFreq.QuadPart;
    CounterStart = liStart.QuadPart;
}

float RelativeTimer::GetSeconds() const
{
    LARGE_INTEGER liNow{};
    QueryPerformanceCounter(&liNow);
    return static_cast<float>(liNow.QuadPart - CounterStart) / Frequency;
}

float RelativeTimer::GetMilliseconds() const
{
    return GetSeconds() * 1000;
}
