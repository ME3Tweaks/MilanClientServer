#include <cstdio>

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

SPI_PLUGINSIDE_SUPPORT(L"CrashSandbox", L"d00telemental", L"0.1.0", SPI_GAME_LE3, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_SEQATTACH;


void HexDump(FILE* OutStream, BYTE* InBuffer, SIZE_T InSize)
{
    static constexpr SIZE_T HEXDUMP_LINE_LEN = 16u;

    SIZE_T Line = 0;
    for (auto i = 0u; i < InSize; i += HEXDUMP_LINE_LEN)
    {
        Line++;

        SIZE_T MaxLen = HEXDUMP_LINE_LEN;
        if (Line * HEXDUMP_LINE_LEN > InSize) {
            MaxLen = InSize - HEXDUMP_LINE_LEN * (Line - 1);
        }

        // Address
        fprintf_s(OutStream, "%08X  ", i);

        // Hex bytes
        for (auto j = 0u; j < MaxLen; j++)
        {
            auto const c = static_cast<BYTE>(InBuffer[i + j]);
            fprintf_s(OutStream, "%02hhX ", c);
        }

        // Padding
        for (int j = 0u; j < HEXDUMP_LINE_LEN - MaxLen + 1; j++)
        {
            fprintf_s(OutStream, "   ");
        }

        // Characters
        for (int j = 0u; j < MaxLen; j++)
        {
            auto const l = static_cast<BYTE>(InBuffer[i + j]);
            fprintf_s(OutStream, "%c", (l >= 0x20) ? l : '.');
        }

        // Newline
        fprintf_s(OutStream, "\n");
    }

    //// Final newline
    //fprintf_s(OutStream, "\n");
}


tNative* execStructMember_orig = nullptr;
tNative* execStructMember_target = nullptr;
void execStructMember_hook(UObject* Context, FFrame* Stack, void* Result)
{
    if (Context->Class->StaticName().StartsWith(L"SFXCustomAction_AIMantleUp"))
    {
        std::fwprintf(stdout, L"UObject::execStructMember:\n");
        std::fwprintf(stdout, L"  Context = %s\n", *Context->StaticFullName());

        HexDump(stdout, Stack->Code, 8 + 8 + 1 + 1 + 1);

        int i = 0;
        for (FFrame* Current = Stack; Current; Current = Current->PreviousFrame)
        {
            std::fwprintf(stdout, L"  %3d Stack.Object = %s\n", i, Current->Object ? *Current->Object->StaticFullName() : L"???");
            std::fwprintf(stdout, L"      Stack.Node = %s\n", Current->Node ? *Current->Node->StaticFullName() : L"???");
            --i;
        }
    }

    (*execStructMember_orig)(Context, Stack, Result);
}

tNative* execVectorToRotator_orig = nullptr;
tNative* execVectorToRotator_target = nullptr;
void execVectorToRotator_hook(UObject* Context, FFrame* Stack, void* Result)
{
    //std::fwprintf(stdout, L"UObject::execVectorToRotator:\n");
    //std::fwprintf(stdout, L"  Context = %s\n", *Context->StaticFullName());

    //int i = 0;
    //for (FFrame* Current = Stack; Current; Current = Current->PreviousFrame)
    //{
    //    std::fwprintf(stdout, L"  %03d Stack.Object = %s\n", i, Current->Object ? *Current->Object->StaticFullName() : L"???");
    //    std::fwprintf(stdout, L"      Stack.Node = %s\n", Current->Node ? *Current->Node->StaticFullName() : L"???");
    //    --i;
    //}

    if (Result == nullptr)
    {
        std::fwprintf(stdout, L"  !! CRASHY-CRASHY this one is\n");
        ::DebugBreak();
    }

    (*execVectorToRotator_orig)(Context, Stack, Result);
}


SPI_IMPLEMENT_ATTACH
{
    ::LESDK::Initializer Init{ InterfacePtr, "CrashSandbox" };
    ::LESDK::InitializeConsole();

    std::fwprintf(stdout, L"hello there!\n");

    GEngine = Init.ResolveTyped<UEngine*>(BUILTIN_GENGINE_RIP);
    GMalloc = Init.ResolveTyped<FMallocLike*>(BUILTIN_GMALLOC_RIP);
    GNatives = Init.ResolveTyped<tNative*>(BUILTIN_GNATIVES_RIP);
    GSys = Init.ResolveTyped<USystem*>(BUILTIN_GSYS_RIP);
    GWorld = Init.ResolveTyped<UWorld*>(BUILTIN_GWORLD_RIP);

    UObject::GObjObjects = Init.ResolveTyped<TArray<UObject*>>(BUILTIN_GOBOBJECTS_RIP);
    SFXName::GBioNamePools = Init.ResolveTyped<SFXNameEntry const*>(BUILTIN_SFXNAMEPOOLS_RIP);
    SFXName::GInitMethod = Init.ResolveTyped<SFXName::tInitMethod>(BUILTIN_SFXNAMEINIT_PHOOK);

    auto const execStructMember_pattern = "40 55 41 54 41 55 41 56 41 57 48 83 EC 40 48 8D 6C 24 ?? 48 89 5D ?? 48 89 75 ?? 48 89 7D ?? 48 8B 05 ?? ?? ?? ?? 48 33 C5 48 89 45 ?? 48 8B 42";
    execStructMember_target = Init.ResolveTyped<tNative>(::LESDK::Address::FromPattern(execStructMember_pattern));
    if (execStructMember_target != nullptr)
    {
        std::fwprintf(stdout, L"found execStructMember = %p\n", (void*)execStructMember_target);
        execStructMember_orig = (tNative*)Init.InstallHook("execStructMember", execStructMember_target, execStructMember_hook);
    }

    auto const execVectorToRotator_pattern = "40 53 48 83 EC 40 48 8B 42 ?? 4C 8D 15 ?? ?? ?? ?? 48 C7 44 24 ?? 00 00 00 00 49 8B D8 C7 44 24 ?? 00 00 00 00 4C 8D 44 24 ?? 0F B6 08";
    execVectorToRotator_target = Init.ResolveTyped<tNative>(::LESDK::Address::FromPattern(execVectorToRotator_pattern));
    if (execVectorToRotator_target != nullptr)
    {
        std::fwprintf(stdout, L"found execVectorToRotator = %p\n", (void*)execVectorToRotator_target);
        execVectorToRotator_orig = (tNative*)Init.InstallHook("execVectorToRotator", execVectorToRotator_target, execVectorToRotator_hook);
    }

    return true;
}

SPI_IMPLEMENT_DETACH
{
    (void)InterfacePtr;
    ::LESDK::TerminateConsole();
    return true;
}
