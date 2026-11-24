/**
 * @file        MilanHost/Components/CMatchmaking.hpp
 * @header      MilanServer client: lobby registration, invites, endpoint discovery,
 *              and peer probes sent through the UE3 game socket.
 *
 * Hosting never depends on the matchmaker. A hosted game registers itself in the
 * background once its listen server is up; if the matchmaker is unreachable the
 * game carries on and registration is retried.
 *
 * The matchmaker URL comes from config ([milan.matchmaker], which the in-game Multiplayer
 * Settings screen edits through ApplySettings), the command line
 * (-mmserver=<url>), the mm.server console command, or the control pipe
 * An empty URL disables matchmaking
 *  A join code from -mmjoin=<code> or the control pipe runs once the multiplayer menu is open
 *  If the player is already at the main menu, it transition to MP menu
 *
 * A lobby member that loses its host asks the matchmaker to check the host. If the host is
 * gone, one member takes the lobby over and the others join it. A match with the game's wave
 * checkpoint is reopened and restarts its current wave, otherwise the new host opens a listen
 * lobby with the lobby's settings.
 */

#pragma once
#include <functional>
#include "Base/Base.hpp"


struct MatchmakingState;


class CMatchmaking final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"matchmaking";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    /** Game thread, after UGameEngine::Tick. Drives HTTP results, discovery, and probes. */
    void On_GameEngineTick(float Delta);

    /**
     * Called from FSocketWin::RecvFrom for every received datagram. Returns true when the
     * datagram is a matchmaking packet, which must then be hidden from UE3.
     */
    bool On_ReceivedDatagram(FSocket* Socket, BYTE const* Data, INT Length, SOCKADDR_IN const* Source);

    /** BlazeGame::UpdateOnlineGame: the host's match settings changed. */
    void On_HostSettingsChanged(USFXOnlineGameSettings* Settings);

    /** BlazeGame::AllowMatchmaking: false once the lobby commits to a match. */
    void On_AllowMatchmaking(bool bAllow);

    /** UObject::ProcessInternal: watches the menu and handles its Invite Friends button. */
    bool On_ScriptCall(UStruct* Function);

    /**
     * BlazeGame::OnConnectionError. Returns true when it handled a client losing its host
     * by showing the stock host-left prompt, whose OK returns to the multiplayer menu.
     */
    bool On_ConnectionError();

    /** Authority removes a lobby player; the target's ClientKickPlayer RPC leaves locally. */
    bool On_KickPlayer(USFXOnlineComponentBlazeGame* Game, FUniqueNetId const& PlayerId);

    /** BlazeGame::GetPlayerCount: the players a new host restoring a match waits for, else 0. */
    int MigrationPlayerCount() const;

    /**
     * Game thread. The multiplayer settings screen saved a matchmaker (empty disables
     * matchmaking) and the access key to send it (empty for none).
     */
    void ApplySettings(std::string const& Url, std::string const& AccessKey);
    /** Game thread: supplies the installed mod identity, not the ASI version. Empty disables matchmaking. */
    bool SetModVersion(std::string const& Version);

    /** True while connected to other players or joining a lobby. */
    bool IsInSession() const;

    /**
     * QueueJoinCode results - MilanNetConfigBridge's JOIN_ constants
     */
    enum class EJoinCodeResult : int
    {
        Queued      = 0,
        InvalidCode = 1,
        Unavailable = 2,
    };

    /**
     * Queues a join on the game thread with the given code
     */
    EJoinCodeResult QueueJoinCode(std::string Code);

    /** How a menu Quick Match ended. The failures map to the script's distinct error prompts. */
    enum class EQuickMatchOutcome
    {
        Matched,            // A host was reserved; travel to it.
        NoMatch,            // The search ended without a usable lobby.
        MatchmakingOff,     // Matchmaking is turned off (or the mod version is unknown, which disables it).
        ServerUnavailable,  // The matchmaker could not be reached or answered with an error.
        AccessKeyRejected,  // The matchmaker refused the configured access key.
    };

    /** Starts a menu Quick Match. Completion runs on the game thread. */
    bool StartQuickMatch(int MapId, int EnemyModeId, int DifficultyId, std::function<void(EQuickMatchOutcome)> Done);
    void CancelQuickMatch();
    std::wstring QuickMatchConnectString() const;
    /** Expanded lobby preference for Quick Match search (MilanNetConfigBridge) */
    void SetQuickMatchExpandedLobbies(bool ExpandedLobbies) noexcept;

private:

    void RegisterCommands();
    void TickLostHost();

    MatchmakingState* mp_state{ nullptr };
    UNetConnection* mp_lostHost{ nullptr };  // Server connection already reported as closed.
    bool m_quickMatchExpandedLobbies{ false };  // Classic lobbies only unless the player opts in
};

static_assert(ComponentType<CMatchmaking>);
