/**
 * @file        MilanHost/Engine/InputGroupPolicy.hpp
 * @brief       Logic for the split omnikey group (PC_Use, PC_Sprint, PC_Cover).
 */

#pragma once

#include "PcBindingPolicy.hpp"
#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace InputPromptFix::Policy
{
    // List of commands our group supports. The items listed here can all be bound to the same key and run in-order
    inline constexpr std::array<std::wstring_view, 3> k_actionGroup{ L"PC_Use", L"PC_Sprint", L"PC_Cover" };
    // Action that is split
    inline constexpr std::wstring_view k_sharedAction = L"Shared_Action";
    // Cover input key alias
    inline constexpr std::wstring_view k_actionCoverRow = L"PC_Cover";

    // EInputEvent
    inline constexpr unsigned char k_inputPressed = 0;
    inline constexpr unsigned char k_inputReleased = 1;

    constexpr bool EqualsAsciiNoCase(std::wstring_view Left, std::wstring_view Right)
    {
        if (Left.size() != Right.size())
        {
            return false;
        }
        for (std::size_t Index = 0; Index < Left.size(); ++Index)
        {
            auto A = Left[Index];
            auto B = Right[Index];
            if (A >= L'A' && A <= L'Z')
            {
                A += L'a' - L'A';
            }
            if (B >= L'A' && B <= L'Z')
            {
                B += L'a' - L'A';
            }
            if (A != B)
            {
                return false;
            }
        }
        return true;
    }

    // Returns the action group index that the alias matches. Returns the size if it wasn't found, in
    // line with other C++ conventions
    constexpr std::size_t ActionGroupIndex(std::wstring_view Alias)
    {
        for (std::size_t Index = 0; Index < k_actionGroup.size(); ++Index)
        {
            if (EqualsAsciiNoCase(Alias, k_actionGroup[Index]))
            {
                return Index;
            }
        }
        return k_actionGroup.size();
    }

    constexpr bool IsActionGroupAlias(std::wstring_view Alias)
    {
        return ActionGroupIndex(Alias) < k_actionGroup.size();
    }

    // Splits the | separator from a list of command strings and returns the entire list
    inline std::vector<std::wstring_view> SplitCommands(std::wstring_view Commands)
    {
        std::vector<std::wstring_view> Entries;
        while (!Commands.empty())
        {
            auto const End = std::min(Commands.find(L'|'), Commands.size());
            auto Entry = Commands.substr(0, End);
            Commands.remove_prefix(std::min(End + 1, Commands.size()));
            while (!Entry.empty() && (Entry.front() == L' ' || Entry.front() == L'\t'))
            {
                Entry.remove_prefix(1);
            }
            while (!Entry.empty() && (Entry.back() == L' ' || Entry.back() == L'\t'))
            {
                Entry.remove_suffix(1);
            }
            if (!Entry.empty())
            {
                Entries.push_back(Entry);
            }
        }
        return Entries;
    }

    // Orders the action group entries so they can be run in-order. Returns the size of the slots list after filtering stuff out
    inline std::size_t OrderActionGroup(std::vector<std::wstring_view>& Entries)
    {
        std::vector<std::size_t> Slots;
        std::vector<std::wstring_view> Members;
        for (std::size_t Index = 0; Index < Entries.size(); ++Index)
        {
            if (IsActionGroupAlias(Entries[Index]))
            {
                Slots.push_back(Index);
                Members.push_back(Entries[Index]);
            }
        }
        std::stable_sort(Members.begin(), Members.end(), [](auto Left, auto Right)
            {
                return ActionGroupIndex(Left) < ActionGroupIndex(Right);
            });
        for (std::size_t Index = 0; Index < Slots.size(); ++Index)
        {
            Entries[Slots[Index]] = Members[Index];
        }
        return Slots.size();
    }

    // Once a group entry consumes a press, future groups should not run it as it has already been consumed
    constexpr bool ShouldRunCommand(bool GroupEntry, bool Consumed, unsigned char Event)
    {
        return !(GroupEntry && Consumed && Event == k_inputPressed);
    }

    constexpr std::wstring_view PromptFallbackAlias(std::wstring_view Alias, bool UsingGamepad)
    {
        if (UsingGamepad && IsActionGroupAlias(Alias))
        {
            // Gamepad always uses Shared_Action
            return k_sharedAction;
        }
        if (!UsingGamepad && EqualsAsciiNoCase(Alias, k_sharedAction))
        {
            // Shared_Action maps to Cover in omni-split
            // May need to investigate more when v36 migrates to next version that this doesn't break stuff
            return k_actionCoverRow;
        }

        // Nothing
        return {};
    }
}
