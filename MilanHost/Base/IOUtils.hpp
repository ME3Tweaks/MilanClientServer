/**
 * @file        MilanHost/Base/IOUtils.hpp
 * @brief       Input / output utilities.
 */

#pragma once

#include "Containers/string.hpp"
#include "Containers/vector.hpp"


/** Reads contents of an entire file into a buffer. Returns true if the file existed, false if it didn't or was not opened. */
bool ReadFileFull(std::wstring_view InPath, ::game::vector<byte>& OutBuffer);
/** Writes contents of an entire buffer into a file. */
void WriteFileFull(std::wstring_view InPath, std::span<byte const> InBuffer);
/** Replaces a file only after the entire buffer has been written and closed successfully. */
bool TryWriteFileFullAtomic(std::wstring_view InPath, std::span<byte const> InBuffer);

/** Logs current callstack with the given severity and _as if_ at the given place. */
__declspec(noinline) void WriteCallstackLog(CodePlaceW&& Place, ESeverity Severity, int Skip = 0);


/**
 * @brief   Prints a multi-line block of bytes using hexadecimal formatting.
 * @tparam  HEXDUMP_LINE_LEN - Number of bytes per line.
 * @param   OutStream - Standard C file stream to receive the formatted block.
 * @param   InBuffer - Beginning of the buffer to format.
 * @param   InSize - Number of bytes to format, starting at @c InBuffer pointer.
 * @param   InPrefix - Thing to print out before each line.
 */
template<SIZE_T HEXDUMP_LINE_LEN = 16u>
    requires (HEXDUMP_LINE_LEN >= 4 && HEXDUMP_LINE_LEN <= 32)
void PrintHex(FILE* const OutStream, BYTE* const InBuffer, SIZE_T const InSize, char const* const InPrefix)
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
