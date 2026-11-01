#include "Components/Cconstants.hpp"
#include "Shell/CmdArgs.hpp"

#include <atomic>
#include <cstring>
#include <cwchar>


// ! CConstants implementation.
// ========================================
#define REL_PATH_RVA ::LESDK::Address::FromOffset(0x12db1c0)
#define REL_LICENSEE_RVA ::LESDK::Address::FromOffset(0x1755c1c)
#define REL_MOUNT_CMP_INSTRUCTION_START ::LESDK::Address::FromOffset(0xafe043) // +1 = MIN, +6 = MAX (CMP is 2D + [INT])
#define REL_MP_STRING_FILTER ::LESDK::Address::FromOffset(0x136141)

// UGameEngine::LoadMap - DLC mount gate on the boot path (GWorld == NULL). R13D = bMountDLC, set
// by 'CMOVZ R13D,EDI' (EDI == 1) only when the URL map is not a menu level (EntryMenu).
#define REL_LOADMAP_BOOT_MOUNT_GATE 0xb93570

// UGameEngine::Exec - 'set'/'setnopec' multiplayer gate (see Ghidra/notes/set-command-multiplayer-gate.md)
#define REL_SET_GATE_PENDINGLEVEL_JNZ ::LESDK::Address::FromOffset(0x54408c) // JNZ on this->GPendingLevel != NULL
#define REL_SET_GATE_NETMODE_JNZ ::LESDK::Address::FromOffset(0x5440a0) // JNZ on GetNetMode(GWorld) != NM_Standalone
#define REL_SET_GATE_DISALLOWTRAVEL_WRITE ::LESDK::Address::FromOffset(0x5440a6) // GDisallowNetworkTravel = TRUE

// UGameViewportClient::Exec - 'show'/'showset'/'showclear' multiplayer gate (see Ghidra/notes/show-command-multiplayer-gate.md)
#define REL_SHOW_GATE_NETMODE_JNZ ::LESDK::Address::FromOffset(0x6d49ef) // JNZ on GetNetMode(GWorld) != NM_Standalone
#define REL_SHOW_GATE_PENDINGLEVEL_JNZ ::LESDK::Address::FromOffset(0x6d4a19) // JNZ on UGameEngine::GPendingLevel != NULL
#define REL_SHOW_GATE_DISALLOWTRAVEL_WRITE ::LESDK::Address::FromOffset(0x6d4a1f) // GDisallowNetworkTravel = TRUE

namespace
{
    std::atomic_bool g_resolutionWatchdogStop{ false };
    HANDLE g_resolutionWatchdogHandle = nullptr;

    struct ResolutionDialogInfo
    {
        HWND OkButton = nullptr;
        int VisibleButtonCount = 0;
        bool HasMessageText = false;
    };

    BOOL CALLBACK InspectResolutionDialogChild(HWND child, LPARAM parameter)
    {
        auto* const info = reinterpret_cast<ResolutionDialogInfo*>(parameter);
        wchar_t className[32]{};
        if (GetClassNameW(child, className, _countof(className)) == 0)
            return TRUE;

        if (std::wcscmp(className, L"BUTTON") == 0 && IsWindowVisible(child))
        {
            ++info->VisibleButtonCount;
            if (GetDlgCtrlID(child) == IDOK && IsWindowEnabled(child))
                info->OkButton = child;
        }
        else if (std::wcscmp(className, L"STATIC") == 0)
        {
            wchar_t text[1024]{};
            if (GetWindowTextW(child, text, _countof(text)) > 0)
                info->HasMessageText = true;
        }
        return TRUE;
    }

    bool IsResolutionDialog(HWND window, DWORD currentProcessId)
    {
        if (!IsWindowVisible(window))
            return false;

        DWORD processId = 0;
        if (GetWindowThreadProcessId(window, &processId) == 0 || processId != currentProcessId)
            return false;

        wchar_t className[32]{};
        if (GetClassNameW(window, className, _countof(className)) == 0 || std::wcscmp(className, L"#32770") != 0)
            return false;

        wchar_t title[128]{};
        if (GetWindowTextW(window, title, _countof(title)) != 0)
            return false;

        const HWND owner = GetWindow(window, GW_OWNER);
        if (owner != nullptr)
        {
            DWORD ownerProcessId = 0;
            if (GetWindowThreadProcessId(owner, &ownerProcessId) == 0 || ownerProcessId != currentProcessId)
                return false;
        }

        ResolutionDialogInfo info;
        EnumChildWindows(window, InspectResolutionDialogChild, reinterpret_cast<LPARAM>(&info));
        return info.OkButton != nullptr && info.VisibleButtonCount == 1 && info.HasMessageText;
    }

    BOOL CALLBACK FindResolutionDialog(HWND window, LPARAM parameter)
    {
        auto* const result = reinterpret_cast<HWND*>(parameter);
        if (IsResolutionDialog(window, GetCurrentProcessId()))
        {
            *result = window;
            return FALSE;
        }
        return TRUE;
    }

    DWORD WINAPI ResolutionDialogWatchdogThread(LPVOID)
    {
        constexpr DWORD PollIntervalMs = 25;
        constexpr DWORD WatchdogDurationMs = 10000;
        for (DWORD elapsed = 0; elapsed < WatchdogDurationMs && !g_resolutionWatchdogStop.load(); elapsed += PollIntervalMs)
        {
            HWND dialog = nullptr;
            EnumWindows(FindResolutionDialog, reinterpret_cast<LPARAM>(&dialog));
            if (dialog != nullptr)
            {
                const HWND okButton = GetDlgItem(dialog, IDOK);
                if (okButton != nullptr && PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), reinterpret_cast<LPARAM>(okButton)))
                {
                    OutputDebugStringA("MilanHost: dismissed the startup resolution message box.\n");
                    return 0;
                }
            }
            Sleep(PollIntervalMs);
        }
        return 0;
    }

    void StartResolutionDialogWatchdog()
    {
        g_resolutionWatchdogStop.store(false);
        g_resolutionWatchdogHandle = CreateThread(nullptr, 0, ResolutionDialogWatchdogThread, nullptr, 0, nullptr);
        if (g_resolutionWatchdogHandle == nullptr)
            OutputDebugStringA("MilanHost: failed to start the startup resolution dialog watchdog.\n");
    }

    bool IsMassEffect3Executable()
    {
        wchar_t executablePath[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, executablePath, _countof(executablePath));
        if (length == 0 || length >= _countof(executablePath))
            return false;
        const wchar_t* const fileName = wcsrchr(executablePath, L'\\');
        return _wcsicmp(fileName != nullptr ? fileName + 1 : executablePath, L"MassEffect3.exe") == 0;
    }

    bool PatchExpectedBytes(::LESDK::Initializer& Init, size_t rva, const BYTE* expected, const BYTE* replacement, size_t size)
    {
        auto* const address = static_cast<BYTE*>(Init.Resolve(::LESDK::Address::FromOffset(rva)));
        if (address == nullptr)
            return false;

        MEMORY_BASIC_INFORMATION memoryInfo{};
        if (VirtualQuery(address, &memoryInfo, sizeof(memoryInfo)) == 0 || memoryInfo.State != MEM_COMMIT)
            return false;

        const uintptr_t addressValue = reinterpret_cast<uintptr_t>(address);
        const uintptr_t regionStart = reinterpret_cast<uintptr_t>(memoryInfo.BaseAddress);
        const uintptr_t regionEnd = regionStart + memoryInfo.RegionSize;
        if (addressValue < regionStart || addressValue >= regionEnd || size > regionEnd - addressValue)
            return false;

        constexpr DWORD ReadableProtection = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if ((memoryInfo.Protect & 0xff & ReadableProtection) == 0)
            return false;
        if (std::memcmp(address, replacement, size) == 0)
            return true;
        if (std::memcmp(address, expected, size) != 0)
            return false;

        DWORD oldProtection = 0;
        if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtection))
            return false;
        std::memcpy(address, replacement, size);
        const BOOL protectionRestored = VirtualProtect(address, size, oldProtection, &oldProtection);
        FlushInstructionCache(GetCurrentProcess(), address, size);
        return protectionRestored != FALSE;
    }
}

void CConstants::ApplyEarlyMinimumResolutionCompatibility(::LESDK::Initializer& Context)
{
#if defined(SDK_TARGET_LE3)
    if (!IsMassEffect3Executable())
    {
        OutputDebugStringA("MilanHost: minimum-resolution patch skipped; process is not MassEffect3.exe.\n");
        return;
    }

    constexpr BYTE ResolutionGuardExpected[] = { 0x7c, 0x0f };
    constexpr BYTE ResolutionGuardPatch[] = { 0xeb, 0x27 };
    constexpr BYTE ResolutionMessageCall[] = { 0xe8, 0x97, 0x13, 0x79, 0xff };
    constexpr BYTE ResolutionExitCall[] = { 0xe8, 0x8b, 0x32, 0x79, 0xff };
    constexpr BYTE NopCall[] = { 0x90, 0x90, 0x90, 0x90, 0x90 };

    const bool guardPatched = PatchExpectedBytes(Context, 0x9bb69c, ResolutionGuardExpected, ResolutionGuardPatch, sizeof(ResolutionGuardPatch));
    const bool messagePatched = PatchExpectedBytes(Context, 0x9bb6b4, ResolutionMessageCall, NopCall, sizeof(NopCall));
    const bool exitPatched = PatchExpectedBytes(Context, 0x9bb6c0, ResolutionExitCall, NopCall, sizeof(NopCall));
    if (!guardPatched)
        OutputDebugStringA("MilanHost: minimum-resolution branch patch at RVA 0x9bb69c failed validation or writing.\n");
    if (!messagePatched)
        OutputDebugStringA("MilanHost: minimum-resolution message-call patch at RVA 0x9bb6b4 failed validation or writing.\n");
    if (!exitPatched)
        OutputDebugStringA("MilanHost: minimum-resolution exit-call patch at RVA 0x9bb6c0 failed validation or writing.\n");
    if (guardPatched && messagePatched && exitPatched)
        OutputDebugStringA("MilanHost: applied the LE3 minimum-resolution compatibility patch.\n");

    if (guardPatched)
        StartResolutionDialogWatchdog();
#else
    MILAN_UNUSED(Context);
#endif
}

void CConstants::StopMinimumResolutionDialogWatchdog()
{
    g_resolutionWatchdogStop.store(true);
    if (g_resolutionWatchdogHandle != nullptr)
    {
        WaitForSingleObject(g_resolutionWatchdogHandle, 1000);
        CloseHandle(g_resolutionWatchdogHandle);
        g_resolutionWatchdogHandle = nullptr;
    }
}

/**
 * \brief Patches a segment of process memory.
 * \param address The address to start the overwrite of patch data to
 * \param patch The patch data
 * \param patchSize The size of patch
 * \return true if patched, false otherwise
 */
bool PatchMemory(void* address, const void* patch, const SIZE_T patchSize)
{
    //make the memory we're going to patch writeable
    DWORD  oldProtect;
    if (!VirtualProtect(address, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    //overwrite with our patch
    memcpy(address, patch, patchSize);

    //restore the memory's old protection level
    VirtualProtect(address, patchSize, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), address, patchSize);
    return true;
}

// Makes mount files able to load on a full range of values.
void CConstants::PatchMountLoading(::LESDK::Initializer& Init)
{
    // Resolve base address
    uint8_t* baseOffset = Init.ResolveTyped<uint8_t>(REL_MOUNT_CMP_INSTRUCTION_START);

    if (baseOffset != nullptr)
    {
        // 1. Target address at +1 byte: set value to 1
        uint8_t* targetOffset1 = baseOffset + 1;
        const int value1 = 1;

        // 2. Target address at +6 bytes: set value to uint32_t Max - 1 (0xFFFFFFFE / 4294967294)
        uint8_t* targetOffset2 = baseOffset + 6;
        const uint32_t value2 = 0xFFFFFFFFu - 1;

        // Apply patches
        bool res1 = PatchMemory(targetOffset1, &value1, sizeof(value1));
        bool res2 = PatchMemory(targetOffset2, &value2, sizeof(value2));

        if (!res1 || !res2)
        {
            MILAN_DEBUGW(ELOG_Startup, L"Failed to patch mount loading offsets");
        }
        else
        {
            MILAN_DEBUGW(ELOG_Startup, L"Successfully patched mount loading limits (+1: %i, +6: %u)", value1, value2);
        }
    }
}

void CConstants::PatchDocumentsDirectory(::LESDK::Initializer& Init)
{
    wchar_t* oPatchOffset = Init.ResolveTyped<wchar_t>(REL_PATH_RVA);
    void* PatchOffset = (char*)oPatchOffset + 0x56; // Start of 'ME3'
    const BYTE patchData[] = { 0x4C, 0x00, 0x4D, 0x00, 0x50, 0x00 }; // LMP
    if (PatchOffset != nullptr)
    {
        auto res = PatchMemory(PatchOffset, patchData, 6);
        if (!res) {
            MILAN_DEBUGW(ELOG_Startup, L"Failed to patch ME3 directory to LMP");
        }
        else {
            MILAN_DEBUGW(ELOG_Startup, L"Patched ME3 directory to LMP: %s", oPatchOffset);
        }
    }
}

/**
 * \brief Overwrites an instruction at a resolved RVA with NOPs (0x90).
 * \param Init The initializer used to resolve the module-relative address
 * \param address RVA of the first byte of the instruction to remove
 * \param size Length of the instruction in bytes
 * \param name Name used in the failure log message
 * \return true if patched, false otherwise
 */
static bool PatchNop(::LESDK::Initializer& Init, ::LESDK::Address const address, SIZE_T const size, wchar_t const* const name)
{
    MILAN_UNUSED(name);

    void* const target = Init.Resolve(address);
    if (target == nullptr)
    {
        MILAN_DEBUGW(ELOG_Startup, L"Failed to resolve %s", name);
        return false;
    }

    BYTE nops[10];
    MILAN_CHECKW(size <= sizeof(nops), L"NOP patch size exceeds scratch buffer");
    memset(nops, 0x90, sizeof(nops));

    bool const res = PatchMemory(target, nops, size);
    if (!res)
    {
        MILAN_DEBUGW(ELOG_Startup, L"Failed to NOP %s", name);
    }
    return res;
}

// Removes the NetMode/GPendingLevel checks (and the GDisallowNetworkTravel write that follows
// them on success) that stock UE3 uses to silently swallow 'set'/'setnopec' and
// 'show'/'showset'/'showclear' while in a networked game. Since the write is NOPed out too,
// GDisallowNetworkTravel is never set by these paths, so the two read sites
// (UGameEngine::Browse, UNetPendingLevel::UNetPendingLevel) don't need patching.
void CConstants::PatchMultiplayerCommandGates(::LESDK::Initializer& Init)
{
    bool ok = true;
    ok &= PatchNop(Init, REL_SET_GATE_PENDINGLEVEL_JNZ, 6, L"set-command GPendingLevel gate");
    ok &= PatchNop(Init, REL_SET_GATE_NETMODE_JNZ, 6, L"set-command NetMode gate");
    ok &= PatchNop(Init, REL_SET_GATE_DISALLOWTRAVEL_WRITE, 10, L"set-command GDisallowNetworkTravel write");
    ok &= PatchNop(Init, REL_SHOW_GATE_NETMODE_JNZ, 6, L"show-command NetMode gate");
    ok &= PatchNop(Init, REL_SHOW_GATE_PENDINGLEVEL_JNZ, 6, L"show-command GPendingLevel gate");
    ok &= PatchNop(Init, REL_SHOW_GATE_DISALLOWTRAVEL_WRITE, 10, L"show-command GDisallowNetworkTravel write");

    if (ok)
    {
        MILAN_DEBUGW(ELOG_Startup, L"Patched out 'set'/'show' multiplayer command gates");
    }
}

void CConstants::PatchMPDLCFolderNameFilter(::LESDK::Initializer& Init)
{
    wchar_t* PatchOffset = Init.ResolveTyped<wchar_t>(REL_MP_STRING_FILTER);
    const BYTE patchData[] = { 0x3C, 0x00, 0x3D, 0x00 }; // <> - this is invalid windows filename chars so this should never appear in the filename list.
    if (PatchOffset != nullptr)
    {
        auto res = PatchMemory(PatchOffset, patchData, 6);
        if (!res) {
            MILAN_DEBUGW(ELOG_Startup, L"Failed to patch out the MP DLC name check");
        }
        else {
            MILAN_DEBUGW(ELOG_Startup, L"Patched out MP DLC name check", PatchOffset);
        }
    }
}

// Stock LoadMap mounts DLC (GContentManager: finish discovery, then MountContentForMode) only when
// leaving a menu level, or at boot when the first map is not a menu level. Booting into EntryMenu
// therefore loads it from basegame before any DLC is mounted. Forcing bMountDLC on the boot path
// runs the game's own mount at its usual spot in LoadMap: after the pre-map work, right before the
// EntryMenu package is loaded. The normal mount when leaving EntryMenu still runs afterwards.
void CConstants::PatchMountDLCBeforeEntryMenu(::LESDK::Initializer& Init)
{
    constexpr BYTE Expected[] = { 0x44, 0x0f, 0x44, 0xef }; // CMOVZ R13D,EDI
    constexpr BYTE Patch[] = { 0x44, 0x8b, 0xef, 0x90 };    // MOV R13D,EDI; NOP
    if (PatchExpectedBytes(Init, REL_LOADMAP_BOOT_MOUNT_GATE, Expected, Patch, sizeof(Patch)))
    {
        MILAN_DEBUGW(ELOG_Startup, L"Patched boot LoadMap to mount DLC before EntryMenu");
    }
    else
    {
        MILAN_DEBUGW(ELOG_Startup, L"Failed to patch boot LoadMap DLC mount gate (bytes did not match)");
    }
}

bool CConstants::Initialize(::LESDK::Initializer& Context)
{
    PatchDocumentsDirectory(Context);
    PatchMountLoading(Context);
    PatchMPDLCFolderNameFilter(Context);
    PatchMountDLCBeforeEntryMenu(Context);
    PatchMultiplayerCommandGates(Context);
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CConstants::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);
}


