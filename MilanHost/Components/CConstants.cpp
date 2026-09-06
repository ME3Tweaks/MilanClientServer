#include "Components/Cconstants.hpp"
#include "Shell/CmdArgs.hpp"


// ! CConstants implementation.
// ========================================
#define REL_PATH_RVA ::LESDK::Address::FromOffset(0x12db1c0)
#define REL_LICENSEE_RVA ::LESDK::Address::FromOffset(0x1755c1c)
#define REL_MOUNT_CMP_INSTRUCTION_START ::LESDK::Address::FromOffset(0xafe043) // +1 = MIN, +6 = MAX (CMP is 2D + [INT])
#define REL_MP_STRING_FILTER ::LESDK::Address::FromOffset(0x136141)

// UGameEngine::Exec - 'set'/'setnopec' multiplayer gate (see Ghidra/notes/set-command-multiplayer-gate.md)
#define REL_SET_GATE_PENDINGLEVEL_JNZ ::LESDK::Address::FromOffset(0x54408c) // JNZ on this->GPendingLevel != NULL
#define REL_SET_GATE_NETMODE_JNZ ::LESDK::Address::FromOffset(0x5440a0) // JNZ on GetNetMode(GWorld) != NM_Standalone
#define REL_SET_GATE_DISALLOWTRAVEL_WRITE ::LESDK::Address::FromOffset(0x5440a6) // GDisallowNetworkTravel = TRUE

// UGameViewportClient::Exec - 'show'/'showset'/'showclear' multiplayer gate (see Ghidra/notes/show-command-multiplayer-gate.md)
#define REL_SHOW_GATE_NETMODE_JNZ ::LESDK::Address::FromOffset(0x6d49ef) // JNZ on GetNetMode(GWorld) != NM_Standalone
#define REL_SHOW_GATE_PENDINGLEVEL_JNZ ::LESDK::Address::FromOffset(0x6d4a19) // JNZ on UGameEngine::GPendingLevel != NULL
#define REL_SHOW_GATE_DISALLOWTRAVEL_WRITE ::LESDK::Address::FromOffset(0x6d4a1f) // GDisallowNetworkTravel = TRUE

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

// Licnesee 205->206
void CConstants::PatchLicenseeVersion(::LESDK::Initializer& Init)
{
    // GMinPackageVersion
    int* PatchOffset = Init.ResolveTyped<int>(REL_LICENSEE_RVA);
    const int patchData = 206; // LMP
    if (PatchOffset != nullptr)
    {
        auto res = PatchMemory(PatchOffset, &patchData, sizeof(patchData));
        if (!res) {
            MILAN_DEBUGW(ELOG_Startup, L"Failed to patch licensee version to LMP");
        }
        else {
            MILAN_DEBUGW(ELOG_Startup, L"Patched licensee version to LMP %i: %s", *PatchOffset, PatchOffset);
        }
    }
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

bool CConstants::Initialize(::LESDK::Initializer& Context)
{
    PatchDocumentsDirectory(Context);
    PatchLicenseeVersion(Context);
    PatchMountLoading(Context);
    PatchMPDLCFolderNameFilter(Context);
    PatchMultiplayerCommandGates(Context);
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CConstants::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);
}


