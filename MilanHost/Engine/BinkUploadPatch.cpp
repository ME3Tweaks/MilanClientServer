#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "Engine/BinkUploadPatch.hpp"

// Yeah, Codex did this, cause I could not figure this shit out back in 2023
// This fixes Bink videos crippling performance on eGPU systems
// Gated by -egpu flag
namespace BinkUploadPatch
{
    namespace
    {
        // LE3 FBinkTextureSet::InitDynamicRHI: Y, cR, cB, optional alpha.
        // mov dword ptr [rsp+30h], 00080050h. Clear only TexCreate_Dynamic (0x40)
        // to select the engine's cached write-lock + UpdateSubresource route.
        constexpr std::array<std::size_t, 4> Sites{ 0x48490c, 0x4849c6, 0x484a80, 0x484b3a };
        constexpr unsigned char Original[]{ 0xc7, 0x44, 0x24, 0x30, 0x50, 0x00, 0x08, 0x00 };
        constexpr unsigned char Patched[]{ 0xc7, 0x44, 0x24, 0x30, 0x10, 0x00, 0x08, 0x00 };
        constexpr std::size_t Span = Sites.back() - Sites.front() + sizeof(Original);

        void WriteFlags(unsigned char* Base, unsigned char Value) noexcept
        {
            for (auto const Rva : Sites)
                *reinterpret_cast<volatile unsigned char*>(Base + Rva + 4) = Value;
        }
    }

    Result Apply(void* const ModuleBase) noexcept
    {
        if (ModuleBase == nullptr)
            return { Status::Refused };

        auto* const Base = static_cast<unsigned char*>(ModuleBase);
        auto* const Begin = Base + Sites.front();
        MEMORY_BASIC_INFORMATION Memory{};
        if (::VirtualQuery(Begin, &Memory, sizeof(Memory)) == 0)
            return { Status::Refused, Sites.front(), ::GetLastError() };

        // The verified sites fit in one executable region. Never read uncommitted,
        // guarded, execute-only, foreign, or differently protected pages.
        auto const Offset = reinterpret_cast<std::uintptr_t>(Begin) - reinterpret_cast<std::uintptr_t>(Memory.BaseAddress);
        constexpr DWORD ReadableExecutable = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (Memory.AllocationBase != ModuleBase || Memory.State != MEM_COMMIT
            || (Memory.Protect & PAGE_GUARD) != 0 || (Memory.Protect & ReadableExecutable) == 0
            || Offset >= Memory.RegionSize || Span > Memory.RegionSize - Offset)
        {
            return { Status::Refused, Sites.front() };
        }

        std::size_t OriginalCount = 0;
        std::size_t PatchedCount = 0;
        for (auto const Rva : Sites)
        {
            if (std::memcmp(Base + Rva, Original, sizeof(Original)) == 0)
                ++OriginalCount;
            else if (std::memcmp(Base + Rva, Patched, sizeof(Patched)) == 0)
                ++PatchedCount;
            else
                return { Status::Refused, Rva };
        }

        if (PatchedCount == Sites.size())
            return { Status::AlreadyEnabled };
        // Refuse partial patches too: don't take ownership of another mod's work.
        if (OriginalCount != Sites.size())
            return { Status::Refused };

        DWORD Previous = 0;
        if (!::VirtualProtect(Begin, Span, PAGE_EXECUTE_READWRITE, &Previous))
            return { Status::WriteFailed, Sites.front(), ::GetLastError() };

        WriteFlags(Base, 0x10);
        BOOL const Flushed = ::FlushInstructionCache(::GetCurrentProcess(), Begin, Span);
        DWORD Error = Flushed ? ERROR_SUCCESS : ::GetLastError();
        DWORD Ignored = 0;
        BOOL const Restored = ::VirtualProtect(Begin, Span, Previous, &Ignored);
        if (!Restored && Error == ERROR_SUCCESS)
            Error = ::GetLastError();
        if (Flushed && Restored)
            return { Status::Enabled };

        // Restore all four original bytes on a cache/protection failure. No game
        // pointers target this ASI; successful patches remain for process lifetime.
        DWORD RollbackPrevious = 0;
        if (!::VirtualProtect(Begin, Span, PAGE_EXECUTE_READWRITE, &RollbackPrevious))
            return { Status::RollbackFailed, Sites.front(), ::GetLastError() };

        WriteFlags(Base, 0x50);
        BOOL const RollbackFlushed = ::FlushInstructionCache(::GetCurrentProcess(), Begin, Span);
        BOOL const RollbackRestored = ::VirtualProtect(Begin, Span, Previous, &Ignored);
        return { RollbackFlushed && RollbackRestored ? Status::RolledBack : Status::RollbackFailed, Sites.front(), Error };
    }
}
