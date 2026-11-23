/**
 * @file        MilanHost/Online/PlayerNamePolicy.hpp
 * @brief       Logic for cleaning names other players see.
 *
 * Must match SFXPRIMP.SanitizeName in ProjectMilan (ClassOverride/SFXPRIMP.uc).
 * The profile name is compared against the host's replicated PlayerName.
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace PlayerNames::Policy
{
    // SFXPRIMP.MAX_PLAYER_NAME_LENGTH
    inline constexpr std::size_t k_maxPlayerNameLength = 22;
    // SFXPRIMP.MAX_CHARACTER_NAME_LENGTH
    inline constexpr std::size_t k_maxCharacterNameLength = 15;
    // Only this many UTF-16 units are read, so a huge name costs little
    inline constexpr std::size_t k_maxScanLength = 256;

    // Characters that count as a single space between words
    constexpr bool IsSpace(wchar_t const C)
    {
        return C == 0x0020 || C == 0x00A0 || C == 0x1680 || (C >= 0x2000 && C <= 0x200A)
            || C == 0x202F || C == 0x205F || C == 0x3000;
    }

    // Drops control/invisible characters, combining marks, non-BMP (emoji), private use and symbol blocks
    constexpr bool IsAllowed(wchar_t const C)
    {
        // Control characters
        if (C < 0x0020 || (C >= 0x007F && C <= 0x009F))
        {
            return false;
        }
        // Invisible and direction-changing characters
        if (C == 0x00AD || C == 0x034F || C == 0x061C || C == 0x115F || C == 0x1160
            || C == 0x17B4 || C == 0x17B5 || (C >= 0x180B && C <= 0x180E)
            || (C >= 0x200B && C <= 0x200F) || (C >= 0x2028 && C <= 0x202E)
            || (C >= 0x2060 && C <= 0x206F) || C == 0x3164 || C == 0xFEFF || C == 0xFFA0)
        {
            return false;
        }
        // Combining marks and variation selectors
        if ((C >= 0x0300 && C <= 0x036F) || (C >= 0x1AB0 && C <= 0x1AFF) || (C >= 0x1DC0 && C <= 0x1DFF)
            || (C >= 0x20D0 && C <= 0x20FF) || (C >= 0xFE00 && C <= 0xFE0F) || (C >= 0xFE20 && C <= 0xFE2F))
        {
            return false;
        }
        // Surrogates, private use, specials
        if ((C >= 0xD800 && C <= 0xF8FF) || C >= 0xFFF0)
        {
            return false;
        }
        // Technical symbols, enclosed alphanumerics, box drawing, shapes, dingbats, Braille, arrows, CJK emoji
        if ((C >= 0x2300 && C <= 0x23FF) || (C >= 0x2460 && C <= 0x27BF) || (C >= 0x2800 && C <= 0x28FF)
            || (C >= 0x2B00 && C <= 0x2BFF) || C == 0x3030 || C == 0x303D || C == 0x3297 || C == 0x3299)
        {
            return false;
        }
        return true;
    }

    // Drops disallowed characters, collapses and trims spaces, and stops at MaxLength. Empty if nothing is left
    inline std::wstring Sanitize(std::wstring_view const Name, std::size_t const MaxLength)
    {
        std::wstring Out{};
        std::size_t const ScanLength = Name.size() < k_maxScanLength ? Name.size() : k_maxScanLength;
        bool bPendingSpace = false;
        for (std::size_t Index = 0; Index < ScanLength && Out.size() < MaxLength; ++Index)
        {
            wchar_t const C = Name[Index];
            if (IsSpace(C))
            {
                bPendingSpace = !Out.empty();
                continue;
            }
            if (!IsAllowed(C))
            {
                continue;
            }
            if (bPendingSpace)
            {
                // Only add the space if a character can follow it, otherwise trimmed
                if (Out.size() + 1 >= MaxLength)
                {
                    break;
                }
                Out.push_back(L' ');
                bPendingSpace = false;
            }
            Out.push_back(C);
        }
        return Out;
    }

    inline std::wstring SanitizePlayerName(std::wstring_view const Name)
    {
        return Sanitize(Name, k_maxPlayerNameLength);
    }
}
