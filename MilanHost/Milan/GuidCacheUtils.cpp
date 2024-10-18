#include <cstdio>
#include "Milan/GuidCacheUtils.hpp"
#include "Milan/Logger.hpp"


// ! GuidCacheOverride implementation.
// ========================================

GuidCacheOverride::GuidCacheOverride(char const* const InPath)
{
    MILAN_CHECKW(InPath != nullptr, L"");
    MILAN_CHECKW(std::strlen(InPath) != 0, L"");
    Source = InPath;
}

std::optional<FGuid> GuidCacheOverride::Get(SFXName const Package)
{
    std::unique_lock<std::mutex> _{ Mutex };
    auto const Iter = Lookup.find(Package);
    if (Iter != Lookup.end())
        return std::make_optional(Iter->second);
    return std::nullopt;
}

void GuidCacheOverride::UpdateLookup()
{
    std::unique_lock<std::mutex> Lock{ Mutex };
    MILAN_UNUSED(Lock);

    RelativeTimer const Timer{};
    MILAN_DEBUGW(L"GuidCacheOverride: updating from file %S", Source.c_str());

    auto const File = std::fopen(Source.c_str(), "r");
    if (File == nullptr && errno == ENOENT)
    {
        MILAN_TRACEW(L"GuidCacheOverride: file %S doesn't exist", Source.c_str());
        return;
    }
    else if (File == nullptr)
    {
        MILAN_WARNW(L"GuidCacheOverride: failed to open file %S: %S", Source.c_str(), std::strerror(errno));
        return;
    }

    struct DeferClose
    {
        FILE* const File{ nullptr };
        ~DeferClose() { std::fclose(File); }
    };
    DeferClose _{ File };

    decltype(Lookup) Temp{};
    char Name[256]{ '\0' };
    FGuid Guid{};

    while (!std::feof(File))
    {
        std::memset(Name, 0, sizeof Name);
        std::memset(&Guid, 0, sizeof Guid);

        int const Count = std::fscanf(File, "%s = %08X%08X%08X%08X\n", Name, &Guid.A, &Guid.B, &Guid.C, &Guid.D);

        MILAN_UNUSED(Count);
        MILAN_CHECKW(Count == 5, L"");

        SFXName const Package(Name, 0);
        MILAN_TRACEW(L"GuidCacheOverride: adding %s => %s override", *Package.ToString(), *FormatGuid(Guid));

        Temp.insert({ Package, Guid });
    }

    auto const EntryCount = Temp.size();
    Lookup = std::move(Temp);

    MILAN_DEBUGW(L"GuidCacheOverride: total %llu package(s) overriden in %f ms.", EntryCount, Timer.GetMilliseconds());
    MILAN_UNUSED(EntryCount);
}


