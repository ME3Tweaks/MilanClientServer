#include <array>
#include <atomic>
#include <fstream>

#include "Base/IOUtils.hpp"
#include "Base/Logger.hpp"


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

void WriteCallstackLog(CodePlaceW&& Place, ESeverity const Severity, int const Skip)
{
    MILAN_VERIFYW(Skip >= 0, L"");

    std::array<void*, 128> StackTrace{ nullptr };
    auto const Count = ::CaptureStackBackTrace(1 + Skip, (DWORD)StackTrace.size(), StackTrace.data(), nullptr);
    auto const BaseAddress = (unsigned char*)::LESDK::GetMainModuleBase();

    if (ILogger* const Logger = gp_mainLogger; gp_mainLogger != nullptr)
    {
        MILAN_CHECKW(Count <= StackTrace.size(), L"");
        for (unsigned short i = 0; i < Count; ++i)
        {
            auto const RelativeVirtualAddress = (unsigned char*)StackTrace[i] - BaseAddress;
            Logger->WriteFormatUnicode(Place, Severity, L"%02d. %p", i + 1, (void*)RelativeVirtualAddress);
        }
    }
}
