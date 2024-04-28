/**
 * @file    Utilities/UtilsShared.hpp
 * @brief   Common header-only utilities for auxilliary .asi targets.
 */

#pragma once

#include <cstdio>
#include <LESDK/Init.hpp>


namespace Utils
{

    void InitializeSdkBuiltins(::LESDK::Initializer& Context)
    {
        GEngine = Context.ResolveTyped<UEngine*>(BUILTIN_GENGINE_RIP);
        GMalloc = Context.ResolveTyped<FMallocLike*>(BUILTIN_GMALLOC_RIP);
        GNatives = Context.ResolveTyped<tNative*>(BUILTIN_GNATIVES_RIP);
        GSys = Context.ResolveTyped<USystem*>(BUILTIN_GSYS_RIP);
        GWorld = Context.ResolveTyped<UWorld*>(BUILTIN_GWORLD_RIP);

        UObject::GObjObjects = Context.ResolveTyped<TArray<UObject*>>(BUILTIN_GOBOBJECTS_RIP);
        SFXName::GBioNamePools = Context.ResolveTyped<SFXNameEntry const*>(BUILTIN_SFXNAMEPOOLS_RIP);
        SFXName::GInitMethod = Context.ResolveTyped<SFXName::tInitMethod>(BUILTIN_SFXNAMEINIT_PHOOK);
    }


    void DumpFrameStack(FILE* const OutStream, FFrame* const InStack, wchar_t const* const InPrefix)
    {
        int i = 0;
        for (FFrame* Current = InStack; Current; Current = Current->PreviousFrame)
        {
            std::fwprintf(OutStream, L"%s%3d Stack.Object = %s\n", InPrefix, i, Current->Object ? *Current->Object->StaticFullName() : L"???");
            std::fwprintf(OutStream, L"%s    Stack.Node = %s\n", InPrefix, Current->Node ? *Current->Node->StaticFullName() : L"???");
            --i;
        }
    }


    template<SIZE_T HEXDUMP_LINE_LEN = 16u>
        requires (HEXDUMP_LINE_LEN >= 4 && HEXDUMP_LINE_LEN <= 32)
    void DumpBlobHex(FILE* const OutStream, BYTE* const InBuffer, SIZE_T const InSize, char const* const InPrefix)
    {
        SIZE_T Line = 0;
        for (auto i = 0u; i < InSize; i += HEXDUMP_LINE_LEN)
        {
            Line++;

            SIZE_T MaxLen = HEXDUMP_LINE_LEN;
            if (Line * HEXDUMP_LINE_LEN > InSize) {
                MaxLen = InSize - HEXDUMP_LINE_LEN * (Line - 1);
            }

            // Address
            fprintf_s(OutStream, "%s%08X  ", InPrefix, i);

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
    }

}
