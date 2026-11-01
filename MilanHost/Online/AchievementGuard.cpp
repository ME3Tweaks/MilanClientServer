#include <atomic>
#include <cstdint>
#include <cstring>

#include <LESDK/Headers.hpp>
#include "Base/Logger.hpp"
#include "Online/AchievementGuard.hpp"

namespace AchievementGuard
{
    namespace
    {
        // OriginSDK OriginGrantAchievement, statically linked. Its only caller is
        // USFXOnlineComponentOrigin's GrantAchievement (vtable slot 119). LE ships no Steam API,
        // so every platform unlock goes through here and EA mirrors it to Steam.
        constexpr char k_grantHookName[] = "OriginGrantAchievement";
        constexpr std::uintptr_t k_grantRva = 0xE2E810;
        constexpr unsigned char k_grantPrologue[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
            0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0xF2 };

        // USFXOnlineComponentUnrealPlayer's profile file writer, queued on a worker thread by
        // WriteProfileSettings (vtable slot 109) and called directly by the flush in slot 92.
        // It is the only code that opens Local_Profile or TrilogyGamerProfile for writing; the
        // trilogy file sits in the Legendary Edition root, outside the per-game Save folder.
        constexpr char k_writeHookName[] = "UnrealPlayerWriteProfile";
        constexpr std::uintptr_t k_writeRva = 0x96B410;
        constexpr unsigned char k_writePrologue[] = {
            0x48, 0x8B, 0xC4, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
            0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00 };

        // EOnlinePlayerStorageAsyncState::OPAS_FinishedWrite. The writer always leaves the profile
        // in this state, and the slot 92 flush spins while a profile is still OPAS_Write.
        constexpr unsigned char k_finishedWrite = 4;

        // GrantAchievement only tests for 0 (success). Skipping the SDK also means the grant
        // callback never fires, the same as a synchronous SDK failure.
        constexpr int k_grantBlocked = -1;

        using t_GrantAchievement = int(std::uint64_t User, std::uint64_t Persona, int AchievementId,
            int Progress, char const* AchievementSet, unsigned Timeout, void* Callback, void* CallbackContext);
        using t_WriteProfile = void(UOnlinePlayerStorage* Profile, UObject* PlayerInterface,
            std::uintptr_t bTrilogyProfile);

        t_GrantAchievement* g_grantOriginal = nullptr;
        t_WriteProfile* g_writeOriginal = nullptr;
        std::atomic_uint g_blockedTrilogyWrites{ 0 };

        // Both hooks can run during shutdown, after COutput has destroyed the logger.
        int GrantAchievementHook(std::uint64_t User, std::uint64_t Persona, int AchievementId,
            int Progress, char const* AchievementSet, unsigned Timeout, void* Callback, void* CallbackContext)
        {
            MILAN_UNUSED_4(User, Persona, Progress, AchievementId);
            MILAN_UNUSED_3(AchievementSet, Timeout, Callback);
            MILAN_UNUSED(CallbackContext);

            if (gp_mainLogger != nullptr)
                MILAN_INFOW(ELOG_SFXOnline, L"achievement guard: blocked platform grant of achievement %d", AchievementId);
            return k_grantBlocked;
        }

        void WriteProfileHook(UOnlinePlayerStorage* Profile, UObject* PlayerInterface, std::uintptr_t bTrilogyProfile)
        {
            if (bTrilogyProfile == 0)
            {
                g_writeOriginal(Profile, PlayerInterface, bTrilogyProfile);
                return;
            }

            if (Profile != nullptr)
                Profile->AsyncState = k_finishedWrite;

            unsigned const Count = g_blockedTrilogyWrites.fetch_add(1, std::memory_order_relaxed) + 1;
            if (gp_mainLogger == nullptr)
                return;
            if (Count == 1)
                MILAN_INFOW(ELOG_SFXOnline, L"achievement guard: blocked TrilogyGamerProfile write (further blocks log at trace)");
            else
                MILAN_TRACEW(ELOG_SFXOnline, L"achievement guard: blocked TrilogyGamerProfile write #%u", Count);
        }

        bool MatchesPrologue(std::uintptr_t ModuleBase, std::uintptr_t Rva, unsigned char const* Bytes, std::size_t Size)
        {
            auto const* const Base = reinterpret_cast<unsigned char const*>(ModuleBase);
            auto const* const Dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(Base);
            auto const* const Nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(Base + Dos->e_lfanew);
            return Nt->OptionalHeader.SizeOfImage > Rva + Size && std::memcmp(Base + Rva, Bytes, Size) == 0;
        }

        // Unknown or already-detoured code is left alone, and the failure is an error rather than a
        // warning: without these hooks LMP can unlock real achievements and write the trilogy file.
        template <typename T>
        T* Install(::LESDK::Initializer& Context, std::uintptr_t ModuleBase, char const* Name, wchar_t const* What,
            std::uintptr_t Rva, unsigned char const* Prologue, std::size_t PrologueSize, T* Hook)
        {
            if (!MatchesPrologue(ModuleBase, Rva, Prologue, PrologueSize))
            {
                MILAN_ERRORW(ELOG_Startup, L"achievement guard: unsupported native code for %s; hook not installed", What);
                return nullptr;
            }

            auto* const Original = reinterpret_cast<T*>(Context.InstallHookDeferred(
                Name, reinterpret_cast<void*>(ModuleBase + Rva), reinterpret_cast<void*>(Hook)));
            if (Original == nullptr)
            {
                MILAN_ERRORW(ELOG_Startup, L"achievement guard: failed to create hook for %s", What);
                return nullptr;
            }
            Context.EnableHook(Name);
            return Original;
        }
    }

    void Initialize(::LESDK::Initializer& Context)
    {
        auto const ModuleBase = reinterpret_cast<std::uintptr_t>(Context.ModuleBase);
        if (ModuleBase == 0)
        {
            MILAN_ERRORW(ELOG_Startup, L"achievement guard: no module base; hooks not installed");
            return;
        }

        g_grantOriginal = Install(Context, ModuleBase, k_grantHookName, L"OriginGrantAchievement",
            k_grantRva, k_grantPrologue, sizeof k_grantPrologue, &GrantAchievementHook);
        g_writeOriginal = Install(Context, ModuleBase, k_writeHookName, L"the profile writer",
            k_writeRva, k_writePrologue, sizeof k_writePrologue, &WriteProfileHook);

        if (g_grantOriginal != nullptr && g_writeOriginal != nullptr)
            MILAN_INFOW(ELOG_Startup, L"achievement guard: platform grants and TrilogyGamerProfile writes are blocked");
    }
}
