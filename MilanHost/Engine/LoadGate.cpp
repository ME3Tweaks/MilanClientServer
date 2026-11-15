#include <atomic>
#include <cwchar>

#include "Base/Logger.hpp"
#include "Engine/Hooks.hpp"
#include "Engine/LoadGate.hpp"
#include "Shell/CmdArgs.hpp"


namespace LoadGate
{
    // Attach normally finishes in well under a second; this only bounds a broken attach.
    static constexpr DWORD k_maxStallMs = 30'000;

    static bool gb_enabled = false;
    static DWORD g_attachThreadId = 0;
    static HANDLE g_openEvent = nullptr;    // Manual-reset, never closed: late waiters may still hold it.

    static std::atomic_bool gb_attachComplete{ false };
    static std::atomic_bool gb_timedOut{ false };
    static std::atomic_int g_heldCount{ 0 };
    static std::atomic<ULONGLONG> g_firstHoldTick{ 0 };

    // The first package name any thread asked for, captured by whichever thread gets there first.
    static std::atomic_flag g_firstPackageClaimed = ATOMIC_FLAG_INIT;
    static std::atomic_bool gb_firstPackageReady{ false };
    static wchar_t g_firstPackage[128]{};


    static void RecordFirstPackage(wchar_t const* const PackageName)
    {
        if (g_firstPackageClaimed.test_and_set(std::memory_order_relaxed))
            return;

        ::wcsncpy_s(g_firstPackage, PackageName != nullptr ? PackageName : L"(null)", _TRUNCATE);
        gb_firstPackageReady.store(true, std::memory_order_release);
    }

    /** Matches "Core", "Core.pcc" and any path ending in either. */
    static bool IsCorePackage(wchar_t const* const PackageName)
    {
        wchar_t const* Leaf = PackageName;
        for (wchar_t const* Cursor = PackageName; *Cursor != L'\0'; ++Cursor)
        {
            if (*Cursor == L'\\' || *Cursor == L'/')
                Leaf = Cursor + 1;
        }

        return ::_wcsnicmp(Leaf, L"Core", 4) == 0 && (Leaf[4] == L'\0' || Leaf[4] == L'.');
    }


    void Install(::LESDK::Initializer& Context)
    {
        g_attachThreadId = ::GetCurrentThreadId();
        gb_enabled = !CmdArgs::Contain(L"-noloadgate");

        if (gb_enabled)
        {
            g_openEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            gb_enabled = g_openEvent != nullptr;
        }

        // The hook is needed with or without gating: LoadPackage_hook also drives the linker
        // patcher and the console auto-complete install. A fixed RVA rather than a pattern scan,
        // because every millisecond spent here is time the game thread has to beat us to Core.
        INSTALL_HOOK_ONE_ADDR_NO_LOG(LoadPackage, Context.ResolveTyped<t_LoadPackage>(GLOBAL_LOADPACKAGE_RVA));

        if (LoadPackage_orig == nullptr)
            gb_enabled = false;
    }

    void Open()
    {
        gb_attachComplete.store(true, std::memory_order_release);

        if (LoadPackage_orig == nullptr)
        {
            MILAN_ERRORW(ELOG_Startup, L"load gate: LoadPackage hook failed to install; "
                L"linker patches and console auto-complete are unavailable");
            return;
        }

        if (!gb_enabled)
        {
            MILAN_WARNW(ELOG_Startup, L"load gate: disabled (-noloadgate or event creation failed); "
                L"hooks installed during attach may have missed early package loads");
            return;
        }

        ::SetEvent(g_openEvent);

        int const HeldCount = g_heldCount.load();
        ULONGLONG const FirstHold = g_firstHoldTick.load();
        bool const bSawPackage = gb_firstPackageReady.load(std::memory_order_acquire);

        if (!bSawPackage)
        {
            MILAN_DEBUGW(ELOG_Startup, L"load gate: opened before the game requested any package (no stall)");
        }
        else if (!IsCorePackage(g_firstPackage))
        {
            // The gate only sees loads that happen after it was installed, so if the first one it
            // saw is not Core, the game was already loading content before MilanHost attached.
            MILAN_WARNW(ELOG_Startup, L"load gate: first package seen was '%s', not Core - the game started "
                L"loading before MilanHost attached, so gated hooks may have missed earlier packages",
                g_firstPackage);
        }
        else
        {
            MILAN_INFOW(ELOG_Startup, L"load gate: held %d package load(s) at '%s' for %llu ms until attach completed",
                HeldCount, g_firstPackage, FirstHold != 0 ? ::GetTickCount64() - FirstHold : 0ull);
        }

        if (gb_timedOut.load())
        {
            MILAN_ERRORW(ELOG_Startup, L"load gate: a held thread gave up after %lu ms and resumed loading "
                L"while attach was still running", k_maxStallMs);
        }

        MILAN_UNUSED_2(HeldCount, FirstHold);
    }

    bool IsOpen() noexcept
    {
        return gb_attachComplete.load(std::memory_order_acquire);
    }

    void Wait(wchar_t const* const PackageName)
    {
        RecordFirstPackage(PackageName);

        if (!gb_enabled || gb_attachComplete.load(std::memory_order_acquire))
            return;

        // Attach never loads packages itself today, but holding it here would be a self-deadlock.
        if (::GetCurrentThreadId() == g_attachThreadId)
            return;

        g_heldCount.fetch_add(1);
        ULONGLONG Expected = 0;
        g_firstHoldTick.compare_exchange_strong(Expected, ::GetTickCount64());

        if (::WaitForSingleObject(g_openEvent, k_maxStallMs) != WAIT_OBJECT_0)
        {
            gb_timedOut.store(true);
            ::OutputDebugStringW(L"MilanHost: load gate timed out, resuming package load before attach finished.\n");
        }
    }
}
