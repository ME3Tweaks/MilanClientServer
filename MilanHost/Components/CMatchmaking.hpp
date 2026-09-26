/**
 * @file        MilanHost/Components/CMatchmaking.hpp
 * @header      MilanServer client: lobby registration, invites, endpoint discovery,
 *              and peer probes sent through the UE3 game socket.
 *
 * Hosting never depends on the matchmaker. A hosted game registers itself in the
 * background once its listen server is up; if the matchmaker is unreachable the
 * game carries on and registration is retried.
 *
 * The matchmaker URL comes from config (milan.matchmaker.url), the command line
 * (-mmserver=<url>), the mm.server console command, or the control pipe; an empty
 * URL disables matchmaking. A join code from -mmjoin=<code> or the control pipe
 * runs once the multiplayer menu is open.
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

    /** UObject::ProcessInternal: watches for the multiplayer menu being shown or hidden. */
    void On_ScriptCall(UStruct* Function);

    /**
     * BlazeGame::OnConnectionError. Returns true when it handled a client losing its host
     * by showing the stock host-left prompt, whose OK returns to the multiplayer menu.
     */
    bool On_ConnectionError();

    /** Starts a menu Quick Match. Completion runs on the game thread. */
    bool StartQuickMatch(int MapId, int EnemyModeId, int DifficultyId, std::function<void(bool)> Done);
    void CancelQuickMatch();
    std::wstring QuickMatchConnectString() const;

private:

    void RegisterCommands();
    void TickLostHost();

    MatchmakingState* mp_state{ nullptr };
    UNetConnection* mp_lostHost{ nullptr };  // Server connection already reported as closed.
};

static_assert(ComponentType<CMatchmaking>);
