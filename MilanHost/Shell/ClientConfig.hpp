/**
 * @file        MilanHost/Shell/ClientConfig.hpp
 * @header      Client-side configuration.
 */

#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "Base/Base.hpp"
#include "Containers/string.hpp"
#include "Containers/vector.hpp"


/** The matchmaker used when no configuration, command line or pipe names another one. */
inline constexpr char const* k_defaultMatchmakerUrl = "https://vps.me3tweaks.com";

/**
 * @brief
 * Fully-parsed local configuration.
 */
struct ClientConfig final
{
    game::string                    MatchmakerUrl{ k_defaultMatchmakerUrl };    // What the settings below select; empty disables matchmaking.
    game::string                    ModVersion{}; // Optional [milan.game] version override; empty reads installed _metacmm.txt.
    game::vector<game::string>      LoggingSuppressed{};

    // [milan.matchmaker] settings, which the in-game multiplayer settings screen edits.
    bool                            MatchmakerEnabled{ true };
    bool                            MatchmakerCustom{ false };
    game::string                    MatchmakerCustomUrl{};                      // Never the default server.
    game::string                    MatchmakerPassword{};                       // Sent to the custom server only.

    // [milan.notices]: keyed by matchmaker URL mapped to the Id of the server's message of the day last shown
    game::vector<std::pair<game::string, game::string>> SeenNotices{};

    bool TryRead(std::wstring_view InPath, game::string& OutError) noexcept;

    /** Gets the server's last seen notice */
    game::string SeenNotice(std::string_view Server) const;
    /** Records a notice as seen */
    void RememberNotice(std::string_view Server, std::string_view Id);
    /** Attempts to commit the notice as seen to disk */
    bool TryWriteSeenNotice(std::wstring_view InSourcePath, std::wstring_view InTargetPath, std::string_view Server, game::string& OutError) const noexcept;

    /** The matchmaker the [milan.matchmaker] settings select, or empty when matchmaking is off. */
    game::string SelectedMatchmakerUrl() const;
    /** The access key for SelectedMatchmakerUrl, or empty for the default server. */
    game::string SelectedAccessKey() const;

    /**
     * Writes the file at InSourcePath to InTargetPath with the [milan.matchmaker] settings
     * replaced, keeping comments and other settings. Creates the section or file if missing.
     */
    bool TryWriteMatchmaker(std::wstring_view InSourcePath, std::wstring_view InTargetPath, game::string& OutError) const noexcept;
};

extern ClientConfig* gp_clientConfig;
extern game::string* gp_clientConfigError;

/** Where MilanHost.toml is saved to - not same as read since it transfers  */
std::wstring GetConfigPath();
/** Where MilanHost.toml is read from - handles migration of side-by-side to exe version */
std::wstring GetLoadConfigPath();

void InitGlobalConfig(wchar_t const* Filename);
void CleanupGlobalConfig();
