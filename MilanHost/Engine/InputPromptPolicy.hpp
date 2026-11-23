/**
 * @file        MilanHost/Engine/InputPromptPolicy.hpp
 * @brief       Logic for device-aware Shared_* prompt bindings
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#define GLOBAL_COLLECTBINDINGS_GETBOUNDKEY_RETURN_RVA   0xBBFE29
#define GLOBAL_COLLECTBINDINGS_CONTROLTOKEN_RETURN_RVA  0xC23F0E

namespace InputPromptFix::Policy
{
    enum class Device { Unbound, KeyboardMouse, Gamepad };

    template<typename Char>
    constexpr bool StartsWithAsciiNoCase(std::basic_string_view<Char> Text, std::string_view Prefix)
    {
        if (Text.size() < Prefix.size())
        {
            return false;
        }
        for (std::size_t Index = 0; Index < Prefix.size(); ++Index)
        {
            auto Left = Text[Index];
            auto Right = static_cast<Char>(Prefix[Index]);
            if (Left >= 'A' && Left <= 'Z')
            {
                Left += 'a' - 'A';
            }
            if (Right >= 'A' && Right <= 'Z')
            {
                Right += 'a' - 'A';
            }
            if (Left != Right)
            {
                return false;
            }
        }
        return true;
    }

    constexpr bool IsPromptLookup(std::uintptr_t CallerRva, bool Defaults)
    {
        return !Defaults && (CallerRva == GLOBAL_COLLECTBINDINGS_GETBOUNDKEY_RETURN_RVA ||
            CallerRva == GLOBAL_COLLECTBINDINGS_CONTROLTOKEN_RETURN_RVA);
    }

    constexpr bool ShouldAdjust(std::uintptr_t CallerRva, bool Defaults, std::wstring_view Alias)
    {
        return IsPromptLookup(CallerRva, Defaults) && Alias.size() > 7 && StartsWithAsciiNoCase(Alias, "Shared_");
    }

    template<typename Char>
    constexpr Device ClassifyKey(std::basic_string_view<Char> Key)
    {
        if (Key.empty() || (Key.size() == 4 && StartsWithAsciiNoCase(Key, "None")) ||
            (Key.size() == 7 && StartsWithAsciiNoCase(Key, "Unbound")))
        {
            return Device::Unbound;
        }

        // Physical controller keys use XboxTypeS_
        return StartsWithAsciiNoCase(Key, "XboxTypeS_") ? Device::Gamepad : Device::KeyboardMouse;
    }

    template<typename Classifier>
    std::size_t SelectBinding(std::size_t Count, bool UsingGamepad, Classifier Classify)
    {
        Device const Wanted = UsingGamepad ? Device::Gamepad : Device::KeyboardMouse;
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            if (Classify(Index) == Wanted)
            {
                return Index;
            }
        }
        return Count; // No binding for this device; never display the other device's key.
    }

    template<typename Binding, typename Label>
    bool PromoteBinding(std::span<Binding> Bindings, std::span<Label> Labels, std::size_t Index)
    {
        if (Bindings.size() != Labels.size() || Index >= Bindings.size())
        {
            return false;
        }
        // Move the complete binding (including modifiers) with its localized name.
        // Rotate instead of swapping to preserve the priority of every other key.
        auto const Offset = static_cast<std::ptrdiff_t>(Index);
        std::rotate(Bindings.begin(), Bindings.begin() + Offset, Bindings.begin() + Offset + 1);
        std::rotate(Labels.begin(), Labels.begin() + Offset, Labels.begin() + Offset + 1);
        return true;
    }
}
