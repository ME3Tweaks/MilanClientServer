/**
 * @file        MilanHost/Components/CMatchmaking.hpp
 * @header      MilanServer client: lobby registration, invites, endpoint discovery,
 *              and peer probes sent through the UE3 game socket.
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

    /** Starts a menu Quick Match. Completion runs on the game thread. */
    bool StartQuickMatch(int MapId, int EnemyModeId, int DifficultyId, std::function<void(bool)> Done);
    void CancelQuickMatch();
    std::wstring QuickMatchConnectString() const;

private:

    void RegisterCommands();

    MatchmakingState* mp_state{ nullptr };
};

static_assert(ComponentType<CMatchmaking>);
