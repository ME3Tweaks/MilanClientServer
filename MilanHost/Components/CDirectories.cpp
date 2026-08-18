#include "Components/CDirectories.hpp"
#include "Shell/CmdArgs.hpp"


// ! CDirectories implementation.
// ========================================
#define REL_PATH_RVA ::LESDK::Address::FromOffset(0x12db1c0)
#define REL_LICENSEE_RVA ::LESDK::Address::FromOffset(0x1755c1c)

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

void CDirectories::PatchLicenseeVersion(::LESDK::Initializer& Init)
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

void CDirectories::PatchDocumentsDirectory(::LESDK::Initializer& Init)
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

bool CDirectories::Initialize(::LESDK::Initializer& Context)
{
    PatchLicenseeVersion(Context);
    PatchDocumentsDirectory(Context);
    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CDirectories::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);
}


