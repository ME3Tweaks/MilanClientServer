#include <Windows.h>

#include <atomic>
#include <fstream>

#include "Systems/Output.hpp"
#include "Misc.hpp"



int GetGameInstanceOrdinal()
{
    static std::atomic_bool s_initialized{ false };
    static int s_ordinal{ -1 };

    if (!s_initialized.exchange(true))
    {
        HANDLE  Mutex = nullptr;
        int     Ordinal = 0;

        while (true)
        {
            auto const Name = FString::Printf(L"MilanHost-%d", Ordinal);
            Mutex = ::CreateMutexW(nullptr, true, *Name);

            if (Mutex != nullptr && ::GetLastError() != ERROR_ALREADY_EXISTS)
            {
                s_ordinal = Ordinal;
                break;
            }

            Mutex = nullptr;
            Ordinal++;
        }

    }

    return s_ordinal;
}


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

void ReadFileFull(std::wstring_view const InPath, ::game::vector<byte>& OutBuffer)
{
    std::ifstream Stream{ InPath.data(), std::ios::binary | std::ios::ate };
    MILAN_CHECKW(Stream.is_open(), L"failed to open file at %s", InPath.data());

    std::streamsize const Length = Stream.tellg();

    OutBuffer.clear();
    OutBuffer.resize(static_cast<size_t>(Length));

    Stream.seekg(0, std::ios::beg);
    Stream.read(reinterpret_cast<char*>(OutBuffer.data()), Length);
}

void WriteFileFull(std::wstring_view const InPath, std::span<byte const> const InBuffer)
{
    std::ofstream Stream{ InPath.data(), std::ios::binary | std::ios::out | std::ios::trunc };
    MILAN_CHECKW(Stream.is_open(), L"failed to open or create file at %s", InPath.data());

    Stream.write(reinterpret_cast<char const*>(InBuffer.data()), static_cast<std::streamsize>(InBuffer.size()));
}
