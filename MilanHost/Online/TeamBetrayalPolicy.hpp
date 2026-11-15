#pragma once

#include <string_view>

namespace TeamBetrayal::Policy
{
    // SFXGRIMP.PrivacySetting
    inline constexpr int k_privacyPublic = 0;
    inline constexpr int k_privacyPrivate = 1;
    inline constexpr int k_privacyFriends = 2;

    // Only affect public lobbies
    constexpr bool ShouldPunish(int const PrivacySetting)
    {
        return PrivacySetting == k_privacyPublic;
    }

    // Searches for playername in the ticker text. Not foolproof.
    constexpr bool EntryMentions(std::wstring_view const Entry, std::wstring_view const PlayerName)
    {
        return !PlayerName.empty() && Entry.find(PlayerName) != std::wstring_view::npos;
    }
}
