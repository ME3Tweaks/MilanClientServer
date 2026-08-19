#include "Components/Cconstants.hpp"
#include "Shell/CmdArgs.hpp"


// ! CConstants implementation.
// ========================================
#define REL_PATH_RVA ::LESDK::Address::FromOffset(0x12db1c0)
#define REL_LICENSEE_RVA ::LESDK::Address::FromOffset(0x1755c1c)
#define REL_MOUNT_CMP_INSTRUCTION_START ::LESDK::Address::FromOffset(0xafe043) // +1 = MIN, +6 = MAX (CMP is 2D + [INT])
#define REL_MP_STRING_FILTER ::LESDK::Address::FromOffset(0x136141)

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
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CConstants::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);
}


