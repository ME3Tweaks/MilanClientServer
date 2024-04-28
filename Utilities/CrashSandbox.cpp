#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

#include "UtilsShared.hpp"


SPI_PLUGINSIDE_SUPPORT(L"CrashSandbox", L"d00telemental", L"0.1.0", SPI_GAME_LE3, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_SEQATTACH;


tNative* execStructMember_orig = nullptr;
tNative* execStructMember_target = nullptr;
void execStructMember_hook(UObject* Context, FFrame* Stack, void* Result)
{
    if (Context->Class->StaticName().StartsWith(L"SFXCustomAction_AIMantleUp"))
    {
        std::fwprintf(stdout, L"UObject::execStructMember:\n");
        std::fwprintf(stdout, L"  Context = %s\n", *Context->StaticFullName());
        Utils::DumpBlobHex(stdout, Stack->Code, 8 + 8 + 1 + 1 + 1, "  ");
        Utils::DumpFrameStack(stdout, Stack, L"  ");
    }

    (*execStructMember_orig)(Context, Stack, Result);
}

tNative* execVectorToRotator_orig = nullptr;
tNative* execVectorToRotator_target = nullptr;
void execVectorToRotator_hook(UObject* Context, FFrame* Stack, void* Result)
{
    //std::fwprintf(stdout, L"UObject::execVectorToRotator:\n");
    //std::fwprintf(stdout, L"  Context = %s\n", *Context->StaticFullName());

    //Utils::DumpFrameStack(stdout, Stack, L"  ");

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
    ::Utils::InitializeSdkBuiltins(Init);

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
