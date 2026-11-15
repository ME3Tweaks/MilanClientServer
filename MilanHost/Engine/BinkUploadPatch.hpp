#pragma once

#include <cstddef>

namespace BinkUploadPatch
{
    enum class Status
    {
        Disabled,
        Enabled,
        AlreadyEnabled,
        Refused,
        WriteFailed,
        RolledBack,
        RollbackFailed
    };

    struct Result
    {
        Status State = Status::Disabled;
        std::size_t SiteRva = 0;
        unsigned long Error = 0;
    };

    // No engine allocations or logging; call during immediate attach, before movies start.
    // ModuleBase must be the main LE3/LMP executable. All sites are checked before writing.
    Result Apply(void* ModuleBase) noexcept;
}
