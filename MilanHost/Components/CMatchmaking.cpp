#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cwctype>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <SPI.h>

#include "Components/CCommands.hpp"
#include "Components/CMatchmaking.hpp"
#include "Engine/ExtObject.hpp"
#include "Engine/Hooks.hpp"
#include "Engine/Intercept.hpp"
#include "Online/MatchmakingNet.hpp"
#include "Online/Rendezvous.hpp"
#include "Online/PeerSecurity.hpp"
#include "Online/ModVersion.hpp"
#include "Public/LmpLobby.h"
#include "Shell/ClientConfig.hpp"
#include "Shell/CmdArgs.hpp"
#include "Version/SharedVersion.h"


using json = nlohmann::json;
using Clock = std::chrono::steady_clock;


// ! Constants and helpers.
// ========================================

namespace
{
    // Static evidence only (Ghidra, LE3 11.3.2): UTcpNetDriver releases its FSocketWin at
    // +0x1F8 on cleanup, and FSocketWin keeps its SOCKET after FSocket's fields at +0x1C.
    // LocalPort() validates the handle with getsockname before anything is sent.
    constexpr std::ptrdiff_t k_tcpNetDriverSocketOffset = 0x1F8;
    constexpr std::ptrdiff_t k_socketWinHandleOffset = 0x1C;

    constexpr int k_protocolVersion = 2;
    constexpr int k_requestTimeoutMs = 10'000;
    constexpr int k_longPollTimeoutMs = 35'000;
    constexpr auto k_discoveryRetry = std::chrono::seconds(1);
    constexpr auto k_discoveryRefresh = std::chrono::seconds(20);
    constexpr auto k_discoverySlowRetry = std::chrono::seconds(5);
    constexpr int k_discoveryFastAttempts = 10;
    constexpr auto k_probeInterval = std::chrono::milliseconds(500);
    constexpr auto k_probeWindow = std::chrono::seconds(15);
    constexpr auto k_pollRetryDelay = std::chrono::seconds(2);
    constexpr auto k_stateCheckInterval = std::chrono::milliseconds(250);
    constexpr std::chrono::seconds k_hostRetryBase{ 5 };
    constexpr std::chrono::seconds k_hostRetryMax{ 60 };
    // Tolerates a brief listen-socket gap, e.g. during servertravel; a longer gap means hosting ended.
    constexpr auto k_listenGrace = std::chrono::seconds(5);
    // Host migration: how long a member waits for the matchmaker to pick a new host, how long
    // a new host may take to load its lobby (and the others wait for it), and how long its
    // first report waits for UDP discovery.
    constexpr auto k_migrationDecisionTimeout = std::chrono::seconds(45);
    constexpr auto k_adoptWindow = std::chrono::seconds(60);
    constexpr auto k_adoptReportWait = std::chrono::seconds(3);
    // The lobby a new host opens; origin makes it take the settings from the game settings object.
    constexpr wchar_t const* k_migrationLobbyUrl = L"mplobby?listen?origin=MultiplayerMenu";
    // Player latency (see TickPlayerPing). UNetConnection::AvgLag is the round trip in seconds,
    // averaged once per StatPeriod (1 s) from packet acknowledgements, and 9999 until the first
    // measurement (Ghidra, LE3 11.3.2: UNetConnection::Tick at 0x7ff74ad21d80).
    constexpr std::ptrdiff_t k_netConnectionAvgLagOffset = 0x290;
    constexpr float k_unmeasuredLag = 9999.f;
    constexpr auto k_pingInterval = std::chrono::seconds(1);
    // PRI.Ping is in the engine's units, ms / 4 (1-250). 0 means not measured yet, and 255,
    // which the engine never writes, marks the host (the pause menu shows "Host").
    constexpr unsigned char k_hostPing = 255;
    constexpr std::size_t k_maxInviteCodeLength = 64;
    constexpr int k_maxConsumedPerCall = 256;

    std::string Narrow(wchar_t const* Text)
    {
        std::string Result{};
        for (; Text != nullptr && *Text != L'\0'; ++Text)
            Result.push_back(*Text < 0x80 ? static_cast<char>(*Text) : '?');
        return Result;
    }

    std::wstring Widen(std::string const& Text)
    {
        return std::wstring(Text.begin(), Text.end());
    }

    struct Endpoint
    {
        std::uint32_t               Address{ 0 };       // Network byte order.
        std::uint16_t               Port{ 0 };          // Host byte order.
        std::string                 Kind{};

        bool operator==(Endpoint const& Other) const { return Address == Other.Address && Port == Other.Port; }
        std::string ToString() const { return MatchmakingNet::FormatIPv4(Address, Port); }
    };

    std::optional<Endpoint> ParseCandidate(json const& Candidate)
    {
        std::string const Address = Candidate.at("address").get<std::string>();
        int const Port = Candidate.at("port").get<int>();
        unsigned Parts[4]{};
        char Trailing = 0;
        if (Port < 1 || Port > 65535
            || std::sscanf(Address.c_str(), "%u.%u.%u.%u%c", &Parts[0], &Parts[1], &Parts[2], &Parts[3], &Trailing) != 4
            || Parts[0] > 255 || Parts[1] > 255 || Parts[2] > 255 || Parts[3] > 255)
        {
            return std::nullopt;
        }

        Endpoint Result{};
        auto* const Bytes = reinterpret_cast<std::uint8_t*>(&Result.Address);
        for (int i = 0; i < 4; ++i)
            Bytes[i] = static_cast<std::uint8_t>(Parts[i]);
        Result.Port = static_cast<std::uint16_t>(Port);
        Result.Kind = Candidate.at("kind").get<std::string>();
        return Result;
    }

    std::vector<Endpoint> ParseCandidates(json const& Array, bool const AllowLoopback)
    {
        std::vector<Endpoint> Result{};
        for (auto const& Candidate : Array)
        {
            if (auto Parsed = ParseCandidate(Candidate); Parsed && Result.size() < 8
                && MatchmakingNet::IsSafePeerAddress(Parsed->Address, AllowLoopback))
                Result.push_back(std::move(*Parsed));
        }
        return Result;
    }

    SOCKADDR_IN ToSockaddr(std::uint32_t const Address, std::uint16_t const Port)
    {
        SOCKADDR_IN Result{};
        Result.sin_family = AF_INET;
        Result.sin_port = htons(Port);
        Result.sin_addr.s_addr = Address;
        return Result;
    }

    // The matchmaker's error code, or empty for a transport failure or a body without one.
    std::string ErrorCode(MatchmakingNet::HttpResult const& Result)
    {
        json const Body = json::parse(Result.Body, nullptr, false);
        return Body.is_object() && Body.contains("code") && Body["code"].is_string() ? Body["code"].get<std::string>() : std::string{};
    }

    std::string DescribeFailure(MatchmakingNet::HttpResult const& Result)
    {
        if (Result.Status == 0)
            return Result.Error;

        json const Body = json::parse(Result.Body, nullptr, false);
        if (Body.is_object() && Body.contains("code") && Body["code"].is_string())
            return "HTTP " + std::to_string(Result.Status) + " " + Body["code"].get<std::string>();
        return "HTTP " + std::to_string(Result.Status);
    }

    // Finds "-name=value" on the command line, case-insensitively. Either the value or
    // the whole argument may be quoted.
    std::optional<std::string> CommandLineValue(std::wstring_view const Name)
    {
        std::wstring_view const Args{ CmdArgs::Get() };
        for (std::size_t At = 0; At + Name.size() <= Args.size(); ++At)
        {
            bool const QuotedArgument = At > 0 && Args[At - 1] == L'"';
            if ((At > 0 && !std::iswspace(Args[At - 1]) && !QuotedArgument)
                || ::_wcsnicmp(Args.data() + At, Name.data(), Name.size()) != 0)
            {
                continue;
            }

            std::size_t Begin = At + Name.size();
            bool const QuotedValue = Begin < Args.size() && Args[Begin] == L'"';
            Begin += QuotedValue ? 1 : 0;
            std::size_t End = Begin;
            while (End < Args.size() && Args[End] != L'"' && ((QuotedArgument || QuotedValue) || !std::iswspace(Args[End])))
                ++End;
            return Narrow(std::wstring{ Args.substr(Begin, End - Begin) }.c_str());
        }
        return std::nullopt;
    }

    bool IsInstanceOf(UObject const* const Object, SFXName const& ClassName)
    {
        for (UField const* Class = Object != nullptr ? Object->Class : nullptr; Class != nullptr; Class = Class->SuperField)
        {
            if (Class->Name == ClassName)
                return true;
        }
        return false;
    }
}


// ! Shared state.
// ========================================

enum class EMatchmakingRole { None, Host, Joiner };

struct UdpTicket
{
    Rendezvous::Id16                Id{};
    Rendezvous::Key                 Secret{};
    bool                            Valid{ false };

    static UdpTicket FromJson(json const& Ticket)
    {
        UdpTicket Result{};
        Result.Valid = Rendezvous::DecodeBase64Url(Ticket.at("ticketId").get<std::string>(), Result.Id)
            && Rendezvous::DecodeBase64Url(Ticket.at("secret").get<std::string>(), Result.Secret);
        return Result;
    }
};

struct DiscoveryState
{
    UdpTicket                       Ticket{};
    std::uint64_t                   Sequence{ 0 };
    Clock::time_point               NextSend{};
    int                             Unanswered{ 0 };
    bool                            Answered{ false };
    Endpoint                        Observed{};

    void Restart()
    {
        NextSend = Clock::now();
        Unanswered = 0;
        Answered = false;
        Observed = {};
    }
};

struct PeerPath
{
    std::string                     ReservationId{};
    Rendezvous::Key                 TraversalKey{};
    Rendezvous::Id16                Pair{};
    std::vector<Endpoint>           Candidates{};
    Clock::time_point               ProbeUntil{};
    Clock::time_point               NextProbe{};
    std::uint64_t                   ProbeSequence{ 0 };
    std::optional<Endpoint>         Verified{};
    std::optional<Endpoint>         ProbedBy{};
    bool                            Joined{ false };
};

// The hosted lobby's settings as the game last published them through UpdateOnlineGame.
struct AdmittedPeer
{
    FUniqueNetId PlayerId{};
    UNetDriver* Driver{ nullptr };
    UNetConnection* Connection{ nullptr };
    std::string Proof{};
};

struct HostedSettings
{
    bool                            Known{ false };
    // The game's privacy modes (EMatchPrivacySetting) as the matchmaker sees them: Public is
    // listed and members invite, Friends is unlisted and members invite, Private is unlisted and
    // only the host invites.
    bool                            Private{ false };       // Unlisted: Private or Friends.
    bool                            MemberInvites{ true };  // Members may invite: Public or Friends.
    bool                            AllowMatchmaking{ true };
    bool                            LockedToStart{ false }; // Locked by the lobby committing to a match.
    int                             MapId{ 0 };             // 0 is Random, for maps and enemies alike.
    int                             EnemyModeId{ 0 };
    int                             DifficultyId{ 0 };
    int                             MaxPlayers{ 0 };        // Lobby size a migrated lobby reopens with; 0 keeps the game's.
    std::optional<int>              ResolvedMapId{};        // Random picks, once the lobby commits.
    std::optional<int>              ResolvedEnemyId{};
};

struct AutoHostPolicy
{
    bool                            Suppressed{ false };    // mm.leave while hosting; lasts until hosting ends.
    int                             Failures{ 0 };
    Clock::time_point               NextAttempt{};
    Clock::time_point               ListenLostAt{};
};

using Completion = std::function<void(MatchmakingState&)>;
using Job = std::function<Completion()>;

struct Mailbox
{
    std::mutex                      Mutex{};
    std::condition_variable         Wake{};
    std::deque<Job>                 ControlJobs{};
    std::deque<Job>                 PollJobs{};
    std::deque<Completion>          Completions{};
    bool                            Stop{ false };
};

struct PendingLobbyKick
{
    UNetDriver* Driver{};
    UNetConnection* Connection{};
    FUniqueNetId PlayerId{};
    Clock::time_point CloseAt{};
};

// Presence: while this game is in no lobby and no search, it tells the matchmaker it is
// running, so the server's statistics can count it. Only a matchmaker that asks for it in
// /v1/info gets it. This outlives a session (ResetSession), unlike most of MatchmakingState.
struct PresenceState
{
    std::string                     Id{};               // Random, made up for this run of the game.
    std::string                     Url{};              // The matchmaker the fields below are about.
    bool                            InfoKnown{ false };
    int                             Seconds{ 0 };       // The matchmaker's interval; 0 means it wants none.
    bool                            Reported{ false };  // The matchmaker may hold a report from this game.
    Clock::time_point               Next{};
};

struct MatchmakingState
{
    std::shared_ptr<Mailbox>        Box{ std::make_shared<Mailbox>() };
    std::string                     BaseUrl{};
    std::string                     ModVersion{};
    std::uint64_t                   Generation{ 0 };
    EMatchmakingRole                Role{ EMatchmakingRole::None };
    DWORD                           GameThreadId{ 0 };

    Clock::time_point               NextPingUpdate{};
    Clock::time_point               LastServiceContact{};
    Clock::time_point               NextInviteUiUpdate{};
    std::vector<PendingLobbyKick>    PendingKicks{};
    UWorld*                         PendingKickWorld{ nullptr }; // Local kick: defer travel out of the script/RPC stack.

    // From /v1/info and DNS.
    bool                            InfoReady{ false };
    bool                            AccessKeyRejected{ false };   // The last /v1/info refused the access key.
    std::uint32_t                   ServerAddress{ 0 };
    std::uint16_t                   ServerUdpPort{ 0 };
    int                             HeartbeatSeconds{ 20 };
    int                             ServerMaxPlayersCeiling{ 0 };   // 0 until known.
    PresenceState                   Presence{};

    // Current UE3 game socket.
    FSocket*                        Socket{ nullptr };
    int                             LocalPort{ -1 };

    DiscoveryState                  Discovery{};

    // The lobby: a host's registered one, or the one a member holds a reservation for. A member
    // only knows the invite code when it joined with one.
    std::string                     LobbyId{};
    std::string                     InviteCode{};
    int                             MemberMaxPlayers{ 0 };  // Member: the lobby's size when it reserved.

    // Host.
    std::string                     HostKey{};
    long long                       Revision{ 0 };
    std::string                     LastStateBody{};
    Clock::time_point               NextHeartbeat{};
    Clock::time_point               NextStateCheck{};
    long long                       IntroductionsVersion{ -1 };
    // Set while the game is not listening: the host sends nothing until it listens again, so
    // a game that stopped hosting cannot look alive to a migration check.
    bool                            IntroductionsPaused{ false };
    std::vector<std::string>        LanAddresses{};
    std::map<std::string, PeerPath> Joiners{};
    std::map<std::string, AdmittedPeer> AdmittedMembers{};
    // A new host after migration: its lobby may still be loading until AdoptUntil. The UDP
    // ticket is used once the listen socket exists, and the first report waits (until
    // AdoptReportBy at most) for discovery so the members see the observed endpoint.
    Clock::time_point               AdoptUntil{};
    Clock::time_point               AdoptReportBy{};
    std::optional<UdpTicket>        AdoptTicket{};

    // Joiner. Once joined, the reservation's ID and key identify it as a lobby member.
    std::string                     ReservationId{};
    std::string                     JoinKey{};
    std::string                     SearchId{};
    std::string                     SearchKey{};
    std::wstring                    QuickMatchTarget{};
    std::function<void(CMatchmaking::EQuickMatchOutcome)> QuickMatchDone{};
    PeerPath                        HostPath{};
    bool                            SamePublicAddress{ false };
    bool                            TravelIssued{ false };
    int                             ReportedPort{ -1 };
    bool                            JoinedReported{ false };
    long long                       ReservationVersion{ -1 };
    // Migration: a member that lost its host waits for the matchmaker's decision, then, if
    // another member takes over, for that host's lobby to come up. Both end by the deadline.
    bool                            MigrationPending{ false };
    bool                            AwaitingHostCandidates{ false };
    long long                       MigrationVersion{ -1 };
    Clock::time_point               MigrationDeadline{};

    // Kept by ResetSession: these belong to the game, not to one lobby or reservation.
    HostedSettings                  Hosted{};
    AutoHostPolicy                  AutoHost{};
    std::string                     PendingJoin{};
    bool                            PendingJoinLeftGame{ false };   // Left a lobby or match so PendingJoin can start.
    UWorld*                         MenuWorld{ nullptr };   // World in which the multiplayer menu is showing.
    std::wstring                    PipeName{};
    std::shared_ptr<MatchmakingNet::ControlPipe> Pipe{};
};


// ! Worker threads.
// ========================================

namespace
{
    void WorkerLoop(std::shared_ptr<Mailbox> Box, bool const Poll)
    {
        for (;;)
        {
            Job Next{};
            {
                std::unique_lock Lock{ Box->Mutex };
                auto& Queue = Poll ? Box->PollJobs : Box->ControlJobs;
                Box->Wake.wait(Lock, [&] { return Box->Stop || !Queue.empty(); });
                if (Box->Stop)
                    return;
                Next = std::move(Queue.front());
                Queue.pop_front();
            }

            Completion Done = Next();

            std::lock_guard Lock{ Box->Mutex };
            if (Done)
                Box->Completions.push_back(std::move(Done));
        }
    }

    using HttpDone = std::function<void(MatchmakingState&, MatchmakingNet::HttpResult const&)>;

    // Queues an HTTP request on a worker. The completion runs on the game thread and is
    // dropped if the session changed in the meantime (mm.leave, a new mm.host, ...).
    void PostHttp(MatchmakingState& State, bool const Poll, wchar_t const* const Method, std::string Path,
        std::string Bearer, std::string Body, HttpDone Done, std::chrono::milliseconds const Delay = {})
    {
        std::string const Url = State.BaseUrl;
        std::uint64_t const Generation = State.Generation;
        int const Timeout = Poll ? k_longPollTimeoutMs : k_requestTimeoutMs;

        Job Work = [=]() -> Completion
        {
            if (Delay.count() > 0)
                std::this_thread::sleep_for(Delay);
            MatchmakingNet::HttpResult Result = MatchmakingNet::Request(Url, Method, Path, Bearer, Body, Timeout);
            return [Generation, Done, Result = std::move(Result)](MatchmakingState& S)
            {
                if (Generation != S.Generation)
                    return;
                if (Result.Status >= 200 && Result.Status < 300)
                    S.LastServiceContact = Clock::now();
                else if (Result.Status == 0 || Result.Status == 401)
                    S.LastServiceContact = {};
                try
                {
                    Done(S, Result);
                }
                catch (json::exception const& Exception)
                {
                    MILAN_ERRORW(ELOG_Matchmaking, L"malformed matchmaker response: %S", Exception.what());
                }
            };
        };

        std::lock_guard Lock{ State.Box->Mutex };
        (Poll ? State.Box->PollJobs : State.Box->ControlJobs).push_back(std::move(Work));
        State.Box->Wake.notify_all();
    }

    std::optional<json> SuccessJson(MatchmakingNet::HttpResult const& Result, char const* What)
    {
        if (Result.Status < 200 || Result.Status >= 300)
        {
            MILAN_WARNW(ELOG_Matchmaking, L"%S failed: %S", What, DescribeFailure(Result).c_str());
            return std::nullopt;
        }
        json Body = json::parse(Result.Body, nullptr, false);
        if (Body.is_discarded() || !Body.is_object())
        {
            MILAN_WARNW(ELOG_Matchmaking, L"%S returned invalid JSON", What);
            return std::nullopt;
        }
        return Body;
    }
}


// ! Game-side helpers.
// ========================================

namespace
{
    UGameEngine* GameEngine()
    {
        return (GEngine != nullptr && *GEngine != nullptr) ? static_cast<UGameEngine*>(*GEngine) : nullptr;
    }

    // A connecting client's pending level owns the driver until the map loads; after
    // that, and for a listen server, it is the world's driver.
    UNetDriver* FindGameDriver()
    {
        UGameEngine* const Engine = GameEngine();
        if (Engine != nullptr && Engine->GPendingLevel != nullptr && Engine->GPendingLevel->NetDriver != nullptr)
            return Engine->GPendingLevel->NetDriver;
        if (GWorld != nullptr && *GWorld != nullptr)
            return (*GWorld)->NetDriver;
        return nullptr;
    }

    FSocket* SocketOf(UNetDriver* const Driver)
    {
        if (Driver == nullptr || !Driver->IsA(UTcpNetDriver::StaticClass()))
            return nullptr;
        return *reinterpret_cast<FSocket**>(reinterpret_cast<std::uint8_t*>(Driver) + k_tcpNetDriverSocketOffset);
    }

    std::uintptr_t HandleOf(FSocket* const Socket)
    {
        return *reinterpret_cast<std::uintptr_t const*>(reinterpret_cast<std::uint8_t const*>(Socket) + k_socketWinHandleOffset);
    }

    bool IsListenServer()
    {
        UNetDriver* const Driver = (GWorld != nullptr && *GWorld != nullptr) ? (*GWorld)->NetDriver : nullptr;
        UGameEngine* const Engine = GameEngine();
        return Driver != nullptr && Driver->ServerConnection == nullptr
            && (Engine == nullptr || Engine->GPendingLevel == nullptr);
    }

    int ConnectedClientCount()
    {
        UNetDriver* const Driver = (GWorld != nullptr && *GWorld != nullptr) ? (*GWorld)->NetDriver : nullptr;
        return Driver != nullptr ? static_cast<int>(Driver->ChildConnections.Count()) : 0;
    }

    UWorld* CurrentWorld()
    {
        return GWorld != nullptr ? *GWorld : nullptr;
    }

    // The host's GameInfo is the lobby's until the match travels to its mission map.
    bool HostIsInLobby()
    {
        static SFXName const k_lobbyGameInfo{ L"SFXGameInfoMP_Lobby", 0 };
        UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
        AWorldInfo* const WorldInfo = Engine != nullptr ? Engine->GetCurrentWorldInfo() : nullptr;
        return WorldInfo == nullptr || WorldInfo->Game == nullptr || IsInstanceOf(WorldInfo->Game, k_lobbyGameInfo);
    }

    // The lobby size: UE3 admits players up to GameInfo.MaxPlayers. Milan's SFXGameInfoMP.InitGame
    // sets it on every map from the host's chosen size (the connection counts in
    // SFXOnlineGameSettings), capped at MaxPlayersAllowed (config MaxLobbySize).
    int HostMaxPlayers()
    {
        UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
        AWorldInfo* const WorldInfo = Engine != nullptr ? Engine->GetCurrentWorldInfo() : nullptr;
        return (WorldInfo != nullptr && WorldInfo->Game != nullptr && WorldInfo->Game->MaxPlayers > 0)
            ? WorldInfo->Game->MaxPlayers : 4;
    }

    // The largest lobby size this game allows (GameInfo.MaxPlayersAllowed), or 0 without a game.
    int CurrentMaxPlayersAllowed()
    {
        UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
        AWorldInfo* const WorldInfo = Engine != nullptr ? Engine->GetCurrentWorldInfo() : nullptr;
        return (WorldInfo != nullptr && WorldInfo->Game != nullptr) ? WorldInfo->Game->MaxPlayersAllowed : 0;
    }

    // The level is changing: a pending ServerTravel (WorldInfo.NextURL, during its countdown), a
    // seamless transition, or a hard travel. A joiner arriving now would follow a level that is going away.
    bool IsLevelTransitioning()
    {
        UGameEngine* const Engine = GameEngine();
        if (Engine == nullptr || Engine->GPendingLevel != nullptr || Engine->TravelURL.Length() > 0)
            return true;
        AWorldInfo* const WorldInfo = Engine->GetCurrentWorldInfo();
        return WorldInfo != nullptr && (WorldInfo->NextURL.Length() > 0 || WorldInfo->IsInSeamlessTravel());
    }

    bool RunConsoleCommand(std::wstring const& Command)
    {
        UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
        if (Engine == nullptr || Engine->GamePlayers.Count() == 0)
            return false;
        ULocalPlayer* const Player = Engine->GamePlayers(0);
        if (Player == nullptr || Player->Actor == nullptr)
            return false;
        Player->Actor->ConsoleCommand(FString{ Command.c_str() }, TRUE);
        return true;
    }

    // The local player's controller, or null before one exists.
    ABioPlayerController* LocalBioPlayer()
    {
        UGameEngine* const Engine = GameEngine();
        if (Engine == nullptr || Engine->GamePlayers.Count() == 0 || Engine->GamePlayers(0) == nullptr
            || Engine->GamePlayers(0)->Actor == nullptr || !Engine->GamePlayers(0)->Actor->IsA(ABioPlayerController::StaticClass()))
        {
            return nullptr;
        }
        return static_cast<ABioPlayerController*>(Engine->GamePlayers(0)->Actor);
    }

    // Shows @p Message in the stock HUD hint box. ShowHint creates the box and its timer from
    // stringrefs, so its text is then replaced with ours.
    bool ShowHint(std::wstring const& Message, float const Seconds)
    {
        ABioPlayerController* const Player = LocalBioPlayer();
        USFXGUIInteraction* const Gui = Player != nullptr ? Player->GetSFXUIController() : nullptr;
        if (Gui == nullptr)
            return false;

        constexpr unsigned char k_bottomHint = 2;
        Gui->eventShowHint(0, 0, Seconds, 0, k_bottomHint, FALSE, TRUE, Player);
        if (Gui->m_aHintStack.Empty() || Gui->m_aHintStack(0) == nullptr)
            return false;
        FString const Text{ Message.c_str() };
        Gui->m_aHintStack(0)->eventAS_ShowHintMessage(Text, Text, FALSE, k_bottomHint);
        return true;
    }

    // Queues the game's modal message box with an OK button, the way
    // SFXPlayerController.NotifyInviteFailed reports a failed platform invite.
    bool ShowMessageBox(std::wstring const& Message)
    {
        ABioPlayerController* const Player = LocalBioPlayer();
        USFXGUIInteraction* const Gui = Player != nullptr ? Player->GetSFXUIController() : nullptr;
        if (Gui == nullptr)
            return false;

        // The game's own name, so a later invite failure replaces this box rather than stacking.
        static SFXName const k_inviteFailedBox{ L"InviteFailedMsg", 0 };
        constexpr int k_priority = 2;
        FBioMessageBoxOptionalParams Params{};
        Params.srAText = Player->srOK;
        Params.bModal = TRUE;
        FScriptDelegate const NoCallback{};
        return Gui->QueueNamedMessageBoxEx(k_inviteFailedBox, k_priority, FString{ Message.c_str() }, NoCallback, 0, Player, FALSE, &Params);
    }

    // Text the player sees. The string refs are Project Milan's own, in its DLC TLK
    // (ProjectMilan/Pipelines/Mod/TLK/DLC_MOD_ProjectMilan_INT.xml); the English is used when a
    // TLK does not have one, e.g. with an older mod build. {0} is replaced with an argument.
    struct LocalizedText
    {
        int             StrRef;
        wchar_t const*  English;
    };

    namespace Text
    {
        constexpr LocalizedText InviteCode{ 49875700, L"Invite Code: {0}" };
        constexpr LocalizedText InviteCodeCopied{ 49875701, L"Invite Code: {0} (join link copied)" };
        constexpr LocalizedText InviteCodeUnavailable{ 49875702, L"Invite code unavailable (matchmaker not connected)" };
        constexpr LocalizedText InviteNotFound{ 49875703, L"This invite is no longer valid. The host may have left, or the lobby has closed." };
        constexpr LocalizedText LobbyFull{ 49875704, L"This lobby is full." };
        constexpr LocalizedText LobbyNotJoinable{ 49875705, L"This lobby is not accepting players right now. It may be changing hosts or finishing a match. Try again shortly." };
        constexpr LocalizedText VersionMismatch{ 49875706, L"The host is running a different version of the game. You both need the same version to play together." };
        constexpr LocalizedText AddressLimit{ 49875707, L"Too many joins from your network are in progress. Wait a minute and try again." };
        constexpr LocalizedText ServerBusy{ 49875708, L"The matchmaking server is busy. Try again later." };
        constexpr LocalizedText ServerUnreachable{ 49875709, L"Could not reach the matchmaking server. Check your connection and try again." };
        constexpr LocalizedText JoinFailed{ 49875710, L"Could not join this lobby ({0})." };
        constexpr LocalizedText HostUnreachable{ 49875711, L"Could not reach the host of this lobby. Try again shortly." };
        constexpr LocalizedText ServerConnectFailed{ 49875712, L"Could not connect to the matchmaking server at {0}. Try again later." };
        constexpr LocalizedText MatchmakingOff{ 49875713, L"Cannot join this lobby: matchmaking is turned off." };
        constexpr LocalizedText NewHostRejoining{ 49875714, L"Another player is the new host. Rejoining..." };
        constexpr LocalizedText LostHostSearching{ 49875715, L"Lost the connection to the host. Looking for a new host..." };
        constexpr LocalizedText InvitesOff{ 49875716, L"Invites are disabled when the lobby size is set to 1." };
        constexpr LocalizedText InvitesHostOnly{ 49875717, L"Only the host can invite players to a private lobby." };
        constexpr LocalizedText AccessKeyRejected{ 49875718, L"The matchmaking server's password is missing or wrong. Check it in Options > Multiplayer Settings." };
    }

    // Looks @p Text up in the loaded TLKs the way script does (SFXGame.GetSimpleString).
    std::wstring Localize(LocalizedText const& Text, std::wstring const& Argument = {})
    {
        std::wstring Result{};
        UClass* const GameClass = ASFXGame::StaticClass();
        if (auto* const Game = GameClass != nullptr ? static_cast<ASFXGame*>(GameClass->ClassDefaultObject) : nullptr)
            Result = Game->GetSimpleString(Text.StrRef, FALSE).Chars();
        // A string no loaded TLK has comes back as its ID with a $ prefix, e.g. "$49875700".
        if (Result.empty() || Result == L"$" + std::to_wstring(Text.StrRef))
            Result = Text.English;

        if (std::size_t const Slot = Result.find(L"{0}"); Slot != std::wstring::npos)
            Result.replace(Slot, 3, Argument);
        return Result;
    }

    // Percent-encodes everything but RFC 3986's unreserved characters.
    std::string PercentEncode(std::string const& Text)
    {
        constexpr char k_hex[] = "0123456789ABCDEF";
        std::string Result{};
        for (unsigned char const C : Text)
        {
            if ((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '-' || C == '.' || C == '_' || C == '~')
            {
                Result.push_back(static_cast<char>(C));
            }
            else
            {
                Result.push_back('%');
                Result.push_back(k_hex[C >> 4]);
                Result.push_back(k_hex[C & 0xF]);
            }
        }
        return Result;
    }

    // Scheme and host compare without case, and a trailing slash is the same URL.
    bool IsDefaultMatchmaker(std::string MatchmakerUrl)
    {
        while (!MatchmakerUrl.empty() && MatchmakerUrl.back() == '/')
            MatchmakerUrl.pop_back();
        return ::_stricmp(MatchmakerUrl.c_str(), LMP_LOBBY_DEFAULT_MATCHMAKER_URL) == 0;
    }

    // An https page, so chat apps make it clickable; it opens me3tweaksmodmanager://lmpjoin with the same
    // query, and Mod Manager starts LMP if needed and joins through the control pipe. The default
    // matchmaker is left out to keep links short.
    std::string JoinLink(std::string const& MatchmakerUrl, std::string const& InviteCode)
    {
        std::string Link = "https://me3tweaks.com/modmanager/protocol/lmpjoin?code=" + PercentEncode(InviteCode);
        if (!IsDefaultMatchmaker(MatchmakerUrl))
            Link += "&server=" + PercentEncode(MatchmakerUrl);
        return Link;
    }

    // Worker thread. Every failure is a return value: the clipboard may be held open by another
    // process, or be unavailable altogether (e.g. a locked session).
    bool WriteClipboardText(std::wstring const& Text)
    {
        // A message-only window owns the data; SetClipboardData may refuse when there is no owner.
        HWND const Owner = ::CreateWindowExW(0, L"STATIC", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        bool Opened = false;
        for (int Attempt = 0; Attempt < 10; ++Attempt)
        {
            if ((Opened = ::OpenClipboard(Owner) != FALSE))
                break;
            ::Sleep(20);
        }

        bool Copied = false;
        if (Opened)
        {
            SIZE_T const Bytes = (Text.size() + 1) * sizeof(wchar_t);
            HGLOBAL const Memory = ::GlobalAlloc(GMEM_MOVEABLE, Bytes);
            if (void* const Data = Memory != nullptr ? ::GlobalLock(Memory) : nullptr)
            {
                std::memcpy(Data, Text.c_str(), Bytes);
                ::GlobalUnlock(Memory);
                // On success the clipboard owns the memory.
                Copied = ::EmptyClipboard() && ::SetClipboardData(CF_UNICODETEXT, Memory) != nullptr;
            }
            if (!Copied && Memory != nullptr)
                ::GlobalFree(Memory);
            ::CloseClipboard();
        }

        if (Owner != nullptr)
            ::DestroyWindow(Owner);
        return Copied;
    }

    // Returns whether @p Text was copied within @p Wait. The copy runs on its own thread, so
    // a clipboard that is stuck (EmptyClipboard messages the previous owner, which may hang)
    // costs the game thread at most @p Wait; a copy that finishes later still lands.
    bool CopyToClipboard(std::wstring Text, std::chrono::milliseconds const Wait)
    {
        try
        {
            auto Result = std::make_shared<std::promise<bool>>();
            std::future<bool> Copied = Result->get_future();
            std::thread{ [Text = std::move(Text), Result]
            {
                bool Done = false;
                try
                {
                    Done = WriteClipboardText(Text);
                }
                catch (...)
                {
                }
                Result->set_value(Done);
            } }.detach();
            return Copied.wait_for(Wait) == std::future_status::ready && Copied.get();
        }
        catch (...)
        {
            // E.g. the thread could not start.
            return false;
        }
    }

    // A client whose connection to its host closed, still in that host's world.
    bool HasLostHostConnection()
    {
        UWorld* const World = CurrentWorld();
        UNetConnection* const Server = (World != nullptr && World->NetDriver != nullptr) ? World->NetDriver->ServerConnection : nullptr;
        UGameEngine* const Engine = GameEngine();
        return Server != nullptr && Server->ConnectionState == USOCK_Closed && Engine != nullptr && Engine->GPendingLevel == nullptr;
    }

    // Reports a lost host the way UE3 did; CMatchmaking::On_ConnectionError then shows the
    // stock host-left prompt.
    void ReportConnectionLost()
    {
        UGameEngine* const Engine = GameEngine();
        if (Engine == nullptr || Engine->GameViewport == nullptr || !Engine->GameViewport->IsA(USFXGameViewportClient::StaticClass()))
            return;
        auto* const Viewport = static_cast<USFXGameViewportClient*>(Engine->GameViewport);
        Viewport->NotifyConnectionError(Viewport->Localize(L"Errors", L"ConnectionFailed", L"Engine"),
            Viewport->Localize(L"Errors", L"ConnectionFailed_Title", L"Engine"));
    }

    // A lobby opened with origin=MultiplayerMenu takes its settings from the online game
    // settings (SFXLobbyFlow.InitMatchSettings), so a new host puts the lobby's there first.
    // EMatchPrivacySetting.
    constexpr unsigned char k_privacyPublic = 0;
    constexpr unsigned char k_privacyPrivate = 1;
    constexpr unsigned char k_privacyFriends = 2;

    unsigned char PrivacyMode(HostedSettings const& H)
    {
        return !H.Private ? k_privacyPublic : H.MemberInvites ? k_privacyFriends : k_privacyPrivate;
    }

    wchar_t const* PrivacyName(HostedSettings const& H)
    {
        return !H.Private ? L"public" : H.MemberInvites ? L"friends" : L"private";
    }

    bool ApplyLobbySettings(HostedSettings const& H)
    {
        for (TypedObjectIterator<USFXOnlineComponentBlazeGame, true, false> Iter{}; Iter; ++Iter)
        {
            USFXOnlineGameSettings* const Settings = (*Iter)->GameSettings;
            if (Settings == nullptr)
                continue;

            // Random has no package name; the lobby reads that back as map 0.
            if (H.MapId > 0)
                Settings->mME3MapName = Settings->GetMapByID(H.MapId).PackageName;
            else
                Settings->mME3MapName = L"";
            Settings->EnemyType = H.EnemyModeId;
            Settings->Difficulty = static_cast<unsigned char>(H.DifficultyId);
            // The lobby size is the two connection counts together. Milan's SetPrivacy keeps that
            // total and splits it by privacy, and SFXGameInfoMP.InitGame applies it to the lobby.
            if (H.MaxPlayers > 0)
            {
                Settings->NumPublicConnections = H.MaxPlayers;
                Settings->NumPrivateConnections = 0;
            }
            Settings->eventSetPrivacy(PrivacyMode(H));
            return true;
        }
        return false;
    }

    float AverageLag(UNetConnection const* const Connection)
    {
        return *reinterpret_cast<float const*>(reinterpret_cast<std::uint8_t const*>(Connection) + k_netConnectionAvgLagOffset);
    }

    // A round trip in PRI.Ping units, or 0 while it is not measured yet.
    unsigned char PingFromLag(float const Lag)
    {
        if (!(Lag >= 0.f) || Lag >= k_unmeasuredLag)
            return 0;
        return static_cast<unsigned char>(std::clamp(std::lround(Lag * 1000.f / 4.f), 1L, 250L));
    }

    std::wstring DescribePing(unsigned char const Ping)
    {
        if (Ping == k_hostPing)
            return L"host";
        return Ping == 0 ? L"not measured" : std::to_wstring(Ping * 4) + L" ms";
    }

    APlayerReplicationInfo* LocalPRI()
    {
        UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
        if (Engine == nullptr || Engine->GamePlayers.Count() == 0 || Engine->GamePlayers(0) == nullptr
            || Engine->GamePlayers(0)->Actor == nullptr)
        {
            return nullptr;
        }
        return Engine->GamePlayers(0)->Actor->PlayerReplicationInfo;
    }

    bool SamePlayerId(FUniqueNetId const& A, FUniqueNetId const& B)
    {
        return A.Uid.A == B.Uid.A && A.Uid.B == B.Uid.B;
    }

    // Never dereference a stored connection until it is found in the current driver's
    // connections with the same player ID. Travel/disconnect can destroy either object.
    void TickPendingKicks(MatchmakingState& S, Clock::time_point const Now)
    {
        UWorld* const World = CurrentWorld();
        UNetDriver* const Driver = World != nullptr ? World->NetDriver : nullptr;
        // Closing a connection can update the roster and queue another kick. Work on a
        // separate list so those callbacks cannot invalidate the pending-kick iteration.
        std::vector<PendingLobbyKick> Pending = std::exchange(S.PendingKicks, {});
        for (PendingLobbyKick const& Kick : Pending)
        {
            if (Driver == nullptr || Driver != Kick.Driver || Driver->ServerConnection != nullptr)
                continue;
            for (UNetConnection* const Client : Driver->ChildConnections)
            {
                if (Client != Kick.Connection)
                    continue;
                APlayerReplicationInfo* const PRI = Client->Actor != nullptr ? Client->Actor->PlayerReplicationInfo : nullptr;
                if (Client->ConnectionState == USOCK_Closed || PRI == nullptr || !SamePlayerId(PRI->UniqueId, Kick.PlayerId))
                    break;
                if (Now < Kick.CloseAt)
                {
                    S.PendingKicks.push_back(Kick);
                    break;
                }
                MILAN_INFOW(ELOG_Matchmaking, L"vote kick: enforcing removal of %s (%d-%d)",
                    *PRI->PlayerName, Kick.PlayerId.Uid.A, Kick.PlayerId.Uid.B);
                UNetConnection_Close_hook(Client);
                break;
            }
        }
    }

    bool SendDatagram(FSocket* const Socket, std::span<std::uint8_t const> const Data,
        std::uint32_t const Address, std::uint16_t const Port)
    {
        if (Socket == nullptr || FSocketWin_SendTo_orig == nullptr)
            return false;
        SOCKADDR_IN Destination = ToSockaddr(Address, Port);
        INT Sent = 0;
        return (*FSocketWin_SendTo_orig)(Socket, const_cast<BYTE*>(Data.data()), static_cast<INT>(Data.size()), &Sent, &Destination) != 0
            && Sent == static_cast<INT>(Data.size());
    }
}


// ! Session flows.
// ========================================

namespace
{
    // Ends the current lobby or reservation. Completions still in flight are dropped.
    // Migration keeps the server info: it moves between sessions on the same matchmaker.
    void ResetSession(MatchmakingState& S, bool const KeepServerInfo = false)
    {
        MatchmakingState Next{};
        Next.Box = S.Box;
        Next.BaseUrl = S.BaseUrl;
        Next.ModVersion = S.ModVersion;
        Next.Generation = S.Generation + 1;
        Next.GameThreadId = S.GameThreadId;
        // Kicks belong to these UE3 connections, even if matchmaking resets its session.
        Next.PendingKicks = std::move(S.PendingKicks);
        Next.PendingKickWorld = S.PendingKickWorld;
        Next.Presence = S.Presence;
        if (KeepServerInfo)
        {
            Next.ServerMaxPlayersCeiling = S.ServerMaxPlayersCeiling;
            Next.InfoReady = S.InfoReady;
            Next.ServerAddress = S.ServerAddress;
            Next.ServerUdpPort = S.ServerUdpPort;
            Next.HeartbeatSeconds = S.HeartbeatSeconds;
        }
        Next.Socket = S.Socket;
        Next.LocalPort = S.LocalPort;
        Next.Hosted = S.Hosted;
        Next.AutoHost = S.AutoHost;
        Next.PendingJoin = std::move(S.PendingJoin);
        Next.PendingJoinLeftGame = S.PendingJoinLeftGame;
        Next.MenuWorld = S.MenuWorld;
        Next.PipeName = std::move(S.PipeName);
        Next.Pipe = std::move(S.Pipe);
        S = std::move(Next);
    }

    // Fetches /v1/info and resolves the server for UDP, then continues with @p Next.
    void FetchInfo(MatchmakingState& S, std::function<void(MatchmakingState&)> Next,
        std::function<void(MatchmakingState&)> Failed = {})
    {
        std::string const Url = S.BaseUrl;
        std::uint64_t const Generation = S.Generation;

        Job Work = [=]() -> Completion
        {
            MatchmakingNet::HttpResult Result = MatchmakingNet::Request(Url, L"GET", "/v1/info", "", "", k_requestTimeoutMs);
            std::uint32_t Address = 0;
            std::string ResolveError{};
            bool const Resolved = MatchmakingNet::ResolveUrlHostIPv4(Url, Address, ResolveError);

            return [=](MatchmakingState& State)
            {
                if (Generation != State.Generation)
                    return;
                try
                {
                    std::optional<json> const Info = SuccessJson(Result, "server info");
                    State.AccessKeyRejected = !Info && Result.Status == 401 && ErrorCode(Result) == "access_key_invalid";
                    if (!Info)
                    {
                        if (Failed) Failed(State);
                        return;
                    }
                    if (Info->at("protocolVersion").get<int>() != k_protocolVersion)
                    {
                        MILAN_ERRORW(ELOG_Matchmaking, L"matchmaker protocol %d is not supported (need %d)",
                            Info->at("protocolVersion").get<int>(), k_protocolVersion);
                        if (Failed) Failed(State);
                        return;
                    }
                    if (!Resolved)
                    {
                        MILAN_ERRORW(ELOG_Matchmaking, L"%S", ResolveError.c_str());
                        if (Failed) Failed(State);
                        return;
                    }

                    State.ServerAddress = Address;
                    State.ServerUdpPort = static_cast<std::uint16_t>(Info->at("udpPort").get<int>());
                    State.HeartbeatSeconds = std::max(5, Info->at("heartbeatSeconds").get<int>());
                    State.ServerMaxPlayersCeiling = Info->value("maxPlayersCeiling", 0);
                    State.Presence.Url = Url;
                    State.Presence.InfoKnown = true;
                    State.Presence.Seconds = Info->value("presenceSeconds", 0);
                    State.Presence.Next = {};
                    State.InfoReady = true;
                    if (State.ServerUdpPort == 0)
                        MILAN_WARNW(ELOG_Matchmaking, L"matchmaker has UDP discovery disabled; no observed endpoints");
                    Next(State);
                }
                catch (json::exception const& Exception)
                {
                    MILAN_ERRORW(ELOG_Matchmaking, L"malformed server info: %S", Exception.what());
                    if (Failed) Failed(State);
                }
            };
        };

        std::lock_guard Lock{ S.Box->Mutex };
        S.Box->ControlJobs.push_back(std::move(Work));
        S.Box->Wake.notify_all();
    }

    // The lobby locks matchmaking while it commits to a match. That lock ends once the match
    // is running, so it can be joined in progress; a lock set during the match (its end) holds.
    // Nobody can join while the level changes. Fullness is the matchmaker's check.
    bool IsJoinable(HostedSettings const& H, bool const InLobby)
    {
        return (H.AllowMatchmaking || (H.LockedToStart && !InLobby)) && !IsLevelTransitioning();
    }

    // The lobby size the matchmaker is told: the game's, capped at the matchmaker's ceiling, which
    // rejects every report of a larger lobby. Raising the game's MaxLobbySize past the server's
    // MaxPlayersCeiling is a configuration mismatch, logged once per size.
    int ReportedMaxPlayers(MatchmakingState const& S)
    {
        int const MaxPlayers = HostMaxPlayers();
        if (S.ServerMaxPlayersCeiling <= 0 || MaxPlayers <= S.ServerMaxPlayersCeiling)
            return MaxPlayers;

        static int s_warnedSize = 0;
        if (s_warnedSize != MaxPlayers)
        {
            s_warnedSize = MaxPlayers;
            MILAN_WARNW(ELOG_Matchmaking, L"lobby size %d is above the matchmaker's limit of %d players; advertising %d",
                MaxPlayers, S.ServerMaxPlayersCeiling, S.ServerMaxPlayersCeiling);
        }
        return S.ServerMaxPlayersCeiling;
    }

    // Bind admission to a connection the game accepted. Counts and peer probes
    // alone cannot establish that a reservation holder actually joined the game.
    void RefreshAdmittedMembers(MatchmakingState& S)
    {
        if (IsLevelTransitioning())
            return;
        UWorld* const World = CurrentWorld();
        UNetDriver* const Driver = World != nullptr ? World->NetDriver : nullptr;
        if (Driver == nullptr || Driver->ServerConnection != nullptr)
            return;
        std::map<std::string, AdmittedPeer> active;
        for (UNetConnection* const Client : Driver->ChildConnections)
        {
            APlayerReplicationInfo* const PRI = Client != nullptr && Client->Actor != nullptr ? Client->Actor->PlayerReplicationInfo : nullptr;
            if (Client == nullptr || Client->ConnectionState != USOCK_Open || PRI == nullptr)
                continue;
            if (std::any_of(S.PendingKicks.begin(), S.PendingKicks.end(), [&](PendingLobbyKick const& kick)
                { return kick.Driver == Driver && kick.Connection == Client && SamePlayerId(kick.PlayerId, PRI->UniqueId); }))
                continue;
            bool known = false;
            for (auto const& [Id, Peer] : S.AdmittedMembers)
            {
                if (Peer.Driver == Driver && Peer.Connection == Client && SamePlayerId(Peer.PlayerId, PRI->UniqueId))
                {
                    active.emplace(Id, Peer);
                    known = true;
                    break;
                }
            }
            if (known)
                continue;
            std::string const url = Client->RequestURL.Length() <= 4096 ? Narrow(*Client->RequestURL) : std::string{};
            auto const id = PeerSecurity::UrlOption(url, "MilanMember");
            auto const proof = PeerSecurity::UrlOption(url, "MilanProof");
            if (!id || !proof || id->size() != 22 || proof->size() != 32)
                continue;
            std::string expected;
            auto const previous = S.AdmittedMembers.find(std::string{ *id });
            if (previous != S.AdmittedMembers.end() && SamePlayerId(previous->second.PlayerId, PRI->UniqueId))
                expected = previous->second.Proof;
            else if (auto const pending = S.Joiners.find(std::string{ *id }); pending != S.Joiners.end())
                expected = Rendezvous::AdmissionProof(pending->second.TraversalKey);
            if (!PeerSecurity::EqualProof(*proof, expected))
                continue;
            active.emplace(std::string{ *id }, AdmittedPeer{ PRI->UniqueId, Driver, Client, expected });
            if (previous == S.AdmittedMembers.end())
                MILAN_INFOW(ELOG_Matchmaking, L"listen host confirmed member %S", std::string{ *id }.c_str());
        }
        for (auto const& [Id, Peer] : S.AdmittedMembers)
        {
            MILAN_UNUSED(Peer);
            if (!active.contains(Id))
                MILAN_INFOW(ELOG_Matchmaking, L"listen host revoked member %S", Id.c_str());
        }
        S.AdmittedMembers = std::move(active);
    }

    // Wave progress is not read yet; an in-progress match reports wave 0.
    json BuildLobbyState(MatchmakingState const& S)
    {
        HostedSettings const& H = S.Hosted;
        int const MaxPlayers = ReportedMaxPlayers(S);
        bool const InLobby = HostIsInLobby();
        json State{
            { "isPrivate", H.Private },
            { "memberInvites", H.MemberInvites },
            { "joinable", IsJoinable(H, InLobby) },
            { "mapId", H.MapId },
            { "enemyModeId", H.EnemyModeId },
            { "difficultyId", H.DifficultyId },
            { "playerCount", std::min(MaxPlayers, 1 + ConnectedClientCount()) },
            { "maxPlayers", MaxPlayers },
            { "phase", InLobby ? "Lobby" : "InProgress" },
            { "waveNumber", 0 },
            { "gamePort", S.LocalPort },
            { "lanAddresses", S.LanAddresses },
            { "memberIds", json::array() },
        };
        for (auto const& [Id, Peer] : S.AdmittedMembers)
        {
            MILAN_UNUSED(Peer);
            if (State["memberIds"].size() >= static_cast<std::size_t>(MaxPlayers - 1))
                break;
            State["memberIds"].push_back(Id);
        }
        if (H.ResolvedMapId)
            State["resolvedMapId"] = *H.ResolvedMapId;
        if (H.ResolvedEnemyId)
            State["resolvedEnemyId"] = *H.ResolvedEnemyId;
        return State;
    }

    // Registration failures never affect the hosted game; they only delay the next attempt.
    void RetryHostLater(MatchmakingState& S)
    {
        AutoHostPolicy& P = S.AutoHost;
        auto const Delay = std::min(k_hostRetryMax, k_hostRetryBase * (1 << std::min(P.Failures, 4)));
        if (P.Failures++ == 0)
            MILAN_WARNW(ELOG_Matchmaking, L"could not register this game with the matchmaker; hosting is unaffected, retrying in the background");
        P.NextAttempt = Clock::now() + Delay;
        ResetSession(S);
    }

    bool IsHosting(MatchmakingState const& S)
    {
        return S.Socket != nullptr && IsListenServer();
    }

    bool WasReplaced(MatchmakingNet::HttpResult const& Result)
    {
        return Result.Status == 409 && ErrorCode(Result) == "host_replaced";
    }

    // The matchmaker moved the lobby to another member after this game stopped answering, so
    // its players are gone. It stays unregistered until it stops hosting (or mm.host).
    void HandleReplaced(MatchmakingState& S)
    {
        MILAN_WARNW(ELOG_Matchmaking, L"another player took over lobby %S; this game no longer hosts it", S.LobbyId.c_str());
        S.AutoHost.Suppressed = true;
        ResetSession(S);
    }

    void PollIntroductions(MatchmakingState& S, std::chrono::milliseconds Delay = {});

    // The host only polls while it is listening, because a poll shows the matchmaker the host
    // is alive. The per-tick host work resumes a paused poll.
    void ContinueIntroductions(MatchmakingState& S, std::chrono::milliseconds const Delay = {})
    {
        if (IsHosting(S))
            PollIntroductions(S, Delay);
        else
            S.IntroductionsPaused = true;
    }

    void ApplyIntroductions(MatchmakingState& S, json const& List)
    {
        S.IntroductionsVersion = List.at("version").get<long long>();

        std::map<std::string, PeerPath> Updated{};
        for (auto const& Introduction : List.at("introductions"))
        {
            std::string const Id = Introduction.at("reservationId").get<std::string>();
            bool const Known = S.Joiners.contains(Id);
            PeerPath Path = Known ? S.Joiners[Id] : PeerPath{};
            Path.ReservationId = Id;
            Path.Joined = Introduction.at("joined").get<bool>();

            if (!Known)
            {
                if (!Rendezvous::DecodeBase64Url(Introduction.at("traversalKey").get<std::string>(), Path.TraversalKey))
                {
                    MILAN_WARNW(ELOG_Matchmaking, L"introduction %S has a malformed traversal key", Id.c_str());
                    continue;
                }
                Path.Pair = Rendezvous::DerivePairId(Path.TraversalKey);
                MILAN_INFOW(ELOG_Matchmaking, L"joiner reserved a slot (reservation %S)", Id.c_str());
            }

            std::vector<Endpoint> Candidates = ParseCandidates(Introduction.at("joinerCandidates"),
                reinterpret_cast<std::uint8_t const*>(&S.ServerAddress)[0] == 127);
            if (Candidates != Path.Candidates)
            {
                for (auto const& Candidate : Candidates)
                {
                    MILAN_UNUSED(Candidate);
                    MILAN_INFOW(ELOG_Matchmaking, L"joiner %S candidate %S (%S)", Id.c_str(), Candidate.ToString().c_str(), Candidate.Kind.c_str());
                }
                Path.Candidates = std::move(Candidates);
                Path.ProbeUntil = Clock::now() + k_probeWindow;
                Path.NextProbe = Clock::now();
            }
            Updated.emplace(Id, std::move(Path));
        }

        for (auto const& [Id, Path] : S.Joiners)
            if (!Updated.contains(Id))
                MILAN_INFOW(ELOG_Matchmaking, L"reservation %S ended", Id.c_str());
        S.Joiners = std::move(Updated);
    }

    void PollIntroductions(MatchmakingState& S, std::chrono::milliseconds const Delay)
    {
        std::string const Path = "/v1/lobbies/" + S.LobbyId + "/introductions/poll?after=" + std::to_string(S.IntroductionsVersion);
        PostHttp(S, true, L"POST", Path, S.HostKey, "", [](MatchmakingState& State, MatchmakingNet::HttpResult const& Result)
        {
            if (Result.Status == 404)
            {
                MILAN_WARNW(ELOG_Matchmaking, L"lobby is gone from the matchmaker; registering it again");
                ResetSession(State);
                return;
            }
            if (WasReplaced(Result))
            {
                HandleReplaced(State);
                return;
            }
            if (std::optional<json> const List = SuccessJson(Result, "introductions poll"))
            {
                ApplyIntroductions(State, *List);
                ContinueIntroductions(State);
                return;
            }
            ContinueIntroductions(State, k_pollRetryDelay);
        }, Delay);
    }

    void SendHeartbeat(MatchmakingState& S)
    {
        json State = BuildLobbyState(S);
        std::string const Content = State.dump();
        if (Content != S.LastStateBody)
        {
            ++S.Revision;
            S.LastStateBody = Content;
        }
        State["revision"] = S.Revision;
        S.NextHeartbeat = Clock::now() + std::chrono::seconds(S.HeartbeatSeconds);

        PostHttp(S, false, L"PUT", "/v1/lobbies/" + S.LobbyId + "/state", S.HostKey, State.dump(),
            [](MatchmakingState& Session, MatchmakingNet::HttpResult const& Result)
            {
                if (Result.Status == 404)
                {
                    MILAN_WARNW(ELOG_Matchmaking, L"lobby expired on the matchmaker; registering it again");
                    ResetSession(Session);
                    return;
                }
                if (WasReplaced(Result))
                {
                    HandleReplaced(Session);
                    return;
                }
                SuccessJson(Result, "lobby heartbeat");
            });
    }

    void StartHost(MatchmakingState& S)
    {
        if (S.ModVersion.empty())
        {
            if (S.AutoHost.Failures++ == 0)
                MILAN_WARNW(ELOG_Matchmaking, L"mod version is unknown; local hosting continues but matchmaking registration is disabled. Set [milan.game] version or mm.version.");
            S.AutoHost.NextAttempt = Clock::now() + k_hostRetryMax;
            return;
        }
        ResetSession(S);
        S.Role = EMatchmakingRole::Host;
        S.LanAddresses = MatchmakingNet::LanIPv4Addresses(4);

        FetchInfo(S, [](MatchmakingState& State)
        {
            json State0 = BuildLobbyState(State);
            State.LastStateBody = State0.dump();
            State.Revision = 1;
            State0["revision"] = State.Revision;
            json const Request{ { "gameVersion", State.ModVersion }, { "state", State0 } };

            PostHttp(State, false, L"POST", "/v1/lobbies", "", Request.dump(), [](MatchmakingState& Session, MatchmakingNet::HttpResult const& Result)
            {
                std::optional<json> const Created = SuccessJson(Result, "lobby creation");
                if (!Created)
                {
                    RetryHostLater(Session);
                    return;
                }

                Session.LobbyId = Created->at("lobbyId").get<std::string>();
                Session.HostKey = Created->at("hostKey").get<std::string>();
                Session.InviteCode = Created->at("inviteCode").get<std::string>();
                Session.Discovery.Ticket = UdpTicket::FromJson(Created->at("udpTicket"));
                Session.Discovery.Restart();
                Session.NextHeartbeat = Clock::now() + std::chrono::seconds(Session.HeartbeatSeconds);
                Session.AutoHost.Failures = 0;

                MILAN_INFOW(ELOG_Matchmaking, L"lobby registered (%s); invite code %S (game port %d)",
                    PrivacyName(Session.Hosted), Session.InviteCode.c_str(), Session.LocalPort);
                ContinueIntroductions(Session);
            });
        }, [](MatchmakingState& State) { RetryHostLater(State); });
    }

    std::optional<Endpoint> ChooseTravelTarget(std::vector<Endpoint> const& Candidates, bool const SamePublicAddress)
    {
        auto const IsLoopback = [](Endpoint const& Candidate)
        {
            return reinterpret_cast<std::uint8_t const*>(&Candidate.Address)[0] == 127;
        };

        // A loopback observation only occurs when the host contacted a matchmaker on its
        // own machine. It reaches the host only from that machine (the local two-instance test).
        auto const Find = [&](char const* Kind) -> std::optional<Endpoint>
        {
            for (auto const& Candidate : Candidates)
                if (Candidate.Kind == Kind && (SamePublicAddress || !IsLoopback(Candidate)))
                    return Candidate;
            return std::nullopt;
        };

        if (SamePublicAddress)
            if (auto Observed = Find("PublicObserved"); Observed && IsLoopback(*Observed))
                return Observed;

        // UE3 connects to exactly one address. Prefer the LAN path for a housemate so
        // router hairpinning is not required; otherwise the observed public mapping.
        if (SamePublicAddress)
            if (auto Lan = Find("Lan"))
                return Lan;
        if (auto Observed = Find("PublicObserved"))
            return Observed;
        if (auto Reported = Find("PublicReported"))
            return Reported;
        // Left with a loopback-registered host, whose LAN addresses are all that reach it from elsewhere.
        return Find("Lan");
    }

    void PollReservation(MatchmakingState& S, std::chrono::milliseconds Delay = {});
    bool TravelToHost(MatchmakingState& S, bool QuickMatch);
    void FailMigration(MatchmakingState& S, wchar_t const* Why);
    void Leave(MatchmakingState& S);

    void PollReservation(MatchmakingState& S, std::chrono::milliseconds const Delay)
    {
        std::string const Path = "/v1/reservations/" + S.ReservationId + "/poll?after=" + std::to_string(S.ReservationVersion);
        PostHttp(S, true, L"POST", Path, S.JoinKey, "", [](MatchmakingState& State, MatchmakingNet::HttpResult const& Result)
        {
            if (Result.Status == 404)
            {
                if (State.AwaitingHostCandidates)
                    FailMigration(State, L"the new host's lobby closed");
                else if (!State.JoinedReported)
                    MILAN_WARNW(ELOG_Matchmaking, L"reservation ended before the join completed");
                return;
            }
            std::optional<json> const Status = SuccessJson(Result, "reservation poll");
            if (!Status)
            {
                PollReservation(State, k_pollRetryDelay);
                return;
            }

            State.ReservationVersion = Status->at("version").get<long long>();
            State.SamePublicAddress = Status->at("samePublicAddress").get<bool>();
            std::vector<Endpoint> Candidates = ParseCandidates(Status->at("hostCandidates"),
                reinterpret_cast<std::uint8_t const*>(&State.ServerAddress)[0] == 127);
            if (Candidates != State.HostPath.Candidates)
            {
                for (auto const& Candidate : Candidates)
                {
                    MILAN_UNUSED(Candidate);
                    MILAN_INFOW(ELOG_Matchmaking, L"host candidate %S (%S)", Candidate.ToString().c_str(), Candidate.Kind.c_str());
                }
                State.HostPath.Candidates = std::move(Candidates);
                State.HostPath.ProbeUntil = Clock::now() + k_probeWindow;
                State.HostPath.NextProbe = Clock::now();
            }

            // A new host after migration has reported from its lobby, so it can be joined.
            if (State.AwaitingHostCandidates && !State.HostPath.Candidates.empty())
            {
                State.AwaitingHostCandidates = false;
                MILAN_INFOW(ELOG_Matchmaking, L"the new host's lobby is up");
                if (!TravelToHost(State, false))
                    FailMigration(State, L"the new host has no usable address");
                return;
            }
            // Nothing more to learn once in the host's game, and the poll worker is shared.
            if (!State.JoinedReported)
                PollReservation(State);
        }, Delay);
    }

    using QuickMatchOutcome = CMatchmaking::EQuickMatchOutcome;

    void FinishQuickMatch(MatchmakingState& S, QuickMatchOutcome const Outcome)
    {
        auto Done = std::move(S.QuickMatchDone);
        S.QuickMatchDone = {};
        if (Done)
            Done(Outcome);
    }

    // A failed request to the matchmaker is a refused key when /v1/info said so; otherwise it is unavailable.
    QuickMatchOutcome QuickMatchServerFailure(MatchmakingState const& S)
    {
        return S.AccessKeyRejected ? QuickMatchOutcome::AccessKeyRejected : QuickMatchOutcome::ServerUnavailable;
    }

    // Takes a reservation's credentials and what is known of the path to its host.
    bool TakeReservation(MatchmakingState& S, json const& Reservation)
    {
        S.ReservationId = Reservation.at("reservationId").get<std::string>();
        S.JoinKey = Reservation.at("joinKey").get<std::string>();
        if (auto const Lobby = Reservation.find("lobby"); Lobby != Reservation.end() && Lobby->is_object())
        {
            S.LobbyId = Lobby->value("lobbyId", std::string{});
            S.MemberMaxPlayers = Lobby->value("maxPlayers", 0);
        }
        S.Discovery.Ticket = UdpTicket::FromJson(Reservation.at("udpTicket"));
        if (!Rendezvous::DecodeBase64Url(Reservation.at("traversalKey").get<std::string>(), S.HostPath.TraversalKey))
        {
            MILAN_ERRORW(ELOG_Matchmaking, L"reservation has a malformed traversal key");
            return false;
        }
        S.HostPath.ReservationId = S.ReservationId;
        S.HostPath.Pair = Rendezvous::DerivePairId(S.HostPath.TraversalKey);
        S.HostPath.Candidates = ParseCandidates(Reservation.at("hostCandidates"),
            reinterpret_cast<std::uint8_t const*>(&S.ServerAddress)[0] == 127);
        S.HostPath.ProbeUntil = Clock::now() + k_probeWindow;
        S.HostPath.NextProbe = Clock::now();
        S.SamePublicAddress = Reservation.at("samePublicAddress").get<bool>();

        for (auto const& Candidate : S.HostPath.Candidates)
        {
            MILAN_UNUSED(Candidate);
            MILAN_INFOW(ELOG_Matchmaking, L"host candidate %S (%S)", Candidate.ToString().c_str(), Candidate.Kind.c_str());
        }
        return true;
    }

    bool TravelToHost(MatchmakingState& S, bool const QuickMatch)
    {
        std::optional<Endpoint> const Target = ChooseTravelTarget(S.HostPath.Candidates, S.SamePublicAddress);
        if (!Target)
        {
            MILAN_ERRORW(ELOG_Matchmaking, L"reservation has no usable host candidates");
            return false;
        }

        MILAN_INFOW(ELOG_Matchmaking, L"joining via %S (%S, same public address: %d)",
            Target->ToString().c_str(), Target->Kind.c_str(), S.SamePublicAddress ? 1 : 0);
        std::string const Proof = Rendezvous::AdmissionProof(S.HostPath.TraversalKey);
        if (Proof.empty())
            return false;
        std::string const TravelUrl = Target->ToString() + "?MilanMember=" + S.ReservationId + "?MilanProof=" + Proof;
        if (QuickMatch)
        {
            S.QuickMatchTarget = Widen(TravelUrl);
            S.TravelIssued = true; // The script callback issues the travel command.
            PollReservation(S);
            FinishQuickMatch(S, QuickMatchOutcome::Matched);
        }
        else
        {
            S.TravelIssued = RunConsoleCommand(L"open " + Widen(TravelUrl));
            if (!S.TravelIssued)
                MILAN_ERRORW(ELOG_Matchmaking, L"no local player controller to run 'open'");
            PollReservation(S);
        }
        return true;
    }

    bool AcceptReservation(MatchmakingState& S, json const& Reservation, bool const QuickMatch)
    {
        return TakeReservation(S, Reservation) && TravelToHost(S, QuickMatch);
    }

    void PollSearch(MatchmakingState& S, std::chrono::milliseconds Delay = {});

    void HandleSearchStatus(MatchmakingState& S, json const& Status)
    {
        std::string const State = Status.at("state").get<std::string>();
        if (State == "Matched")
        {
            if (!AcceptReservation(S, Status.at("reservation"), true))
                FinishQuickMatch(S, QuickMatchOutcome::NoMatch);
        }
        else if (State == "Searching")
        {
            int const Wait = std::clamp(Status.value("pollAfterMilliseconds", 2000), 100, 10000);
            PollSearch(S, std::chrono::milliseconds{ Wait });
        }
        else
        {
            MILAN_INFOW(ELOG_Matchmaking, L"quick match found no lobby");
            FinishQuickMatch(S, QuickMatchOutcome::NoMatch);
        }
    }

    void PollSearch(MatchmakingState& S, std::chrono::milliseconds const Delay)
    {
        PostHttp(S, true, L"POST", "/v1/searches/" + S.SearchId + "/poll", S.SearchKey, "",
            [](MatchmakingState& State, MatchmakingNet::HttpResult const& Result)
            {
                if (std::optional<json> const Status = SuccessJson(Result, "quick match poll"))
                    HandleSearchStatus(State, *Status);
                else
                    FinishQuickMatch(State, QuickMatchOutcome::ServerUnavailable);
            }, Delay);
    }

    void StartSearch(MatchmakingState& S, int const MapId, int const EnemyModeId, int const DifficultyId,
        std::function<void(QuickMatchOutcome)> Done)
    {
        // An empty URL is matchmaking turned off (Multiplayer Settings or mm.server off); an unknown mod version disables it too.
        if (S.BaseUrl.empty() || S.ModVersion.empty())
        {
            MILAN_WARNW(ELOG_Matchmaking, S.BaseUrl.empty()
                ? L"quick match is unavailable: matchmaking is turned off"
                : L"quick match requires a known mod version; set [milan.game] version or mm.version");
            if (Done)
                Done(QuickMatchOutcome::MatchmakingOff);
            return;
        }
        ResetSession(S);
        S.Role = EMatchmakingRole::Joiner;
        S.QuickMatchDone = std::move(Done);
        MILAN_INFOW(ELOG_Matchmaking, L"quick match search: map %d, enemy %d, difficulty %d", MapId, EnemyModeId, DifficultyId);
        FetchInfo(S, [MapId, EnemyModeId, DifficultyId](MatchmakingState& State)
        {
            json const Request{
                { "gameVersion", State.ModVersion },
                { "mapId", MapId },
                { "enemyModeId", EnemyModeId },
                { "difficultyId", DifficultyId },
                { "widen", true },
            };
            PostHttp(State, false, L"POST", "/v1/searches", "", Request.dump(),
                [](MatchmakingState& Session, MatchmakingNet::HttpResult const& Result)
                {
                    std::optional<json> const Created = SuccessJson(Result, "quick match search");
                    if (!Created)
                    {
                        FinishQuickMatch(Session, QuickMatchServerFailure(Session));
                        return;
                    }
                    Session.SearchId = Created->at("searchId").get<std::string>();
                    Session.SearchKey = Created->at("searchKey").get<std::string>();
                    HandleSearchStatus(Session, Created->at("status"));
                });
        }, [](MatchmakingState& State) { FinishQuickMatch(State, QuickMatchServerFailure(State)); });
    }

    // What to tell the player when the matchmaker refuses an invite code. The codes are
    // MilanServer's Failures.
    std::wstring RedeemFailureMessage(MatchmakingNet::HttpResult const& Result)
    {
        std::string const Code = ErrorCode(Result);
        if (Code == "invite_not_found")
            return Localize(Text::InviteNotFound);
        if (Code == "lobby_full")
            return Localize(Text::LobbyFull);
        if (Code == "lobby_not_joinable")
            return Localize(Text::LobbyNotJoinable);
        if (Code == "version_mismatch")
            return Localize(Text::VersionMismatch);
        if (Code == "address_limit")
            return Localize(Text::AddressLimit);
        if (Code == "server_full")
            return Localize(Text::ServerBusy);
        if (Code == "access_key_invalid")
            return Localize(Text::AccessKeyRejected);
        if (Result.Status == 0)
            return Localize(Text::ServerUnreachable);
        return Localize(Text::JoinFailed, Widen(DescribeFailure(Result)));
    }

    void ReportJoinFailure(std::wstring const& Message)
    {
        if (!ShowMessageBox(Message))
            MILAN_WARNW(ELOG_Matchmaking, L"could not show the join failure: %s", Message.c_str());
    }

    void StartJoin(MatchmakingState& S, std::string InviteCode)
    {
        if (S.ModVersion.empty())
        {
            ReportJoinFailure(L"The installed Project Milan version could not be identified. Set the mod version before using matchmaking.");
            return;
        }
        ResetSession(S);
        S.Role = EMatchmakingRole::Joiner;
        S.InviteCode = InviteCode;

        // A failed join ends the session, so the game does not look like it is still joining,
        // and tells the player why: the join may have come from a link or Discord.
        FetchInfo(S, [InviteCode](MatchmakingState& State)
        {
            json const Request{ { "inviteCode", InviteCode }, { "gameVersion", State.ModVersion } };
            PostHttp(State, false, L"POST", "/v1/invites/redeem", "", Request.dump(), [](MatchmakingState& Session, MatchmakingNet::HttpResult const& Result)
            {
                std::optional<json> const Reservation = SuccessJson(Result, "invite redemption");
                if (!Reservation)
                {
                    ResetSession(Session);
                    ReportJoinFailure(RedeemFailureMessage(Result));
                    return;
                }

                if (!AcceptReservation(Session, *Reservation, false))
                {
                    Leave(Session); // Releases the reservation.
                    // Usually a reservation without a host address this game can reach.
                    ReportJoinFailure(Localize(Text::HostUnreachable));
                }
            });
        }, [](MatchmakingState& State)
        {
            std::wstring const Server = Widen(State.BaseUrl);
            bool const bKeyRejected = State.AccessKeyRejected;
            bool const bDefaultServer = State.BaseUrl == k_defaultMatchmakerUrl;
            ResetSession(State);
            // The default server's address is not shown to players.
            if (bKeyRejected)
                ReportJoinFailure(Localize(Text::AccessKeyRejected));
            else if (bDefaultServer)
                ReportJoinFailure(Localize(Text::ServerUnreachable));
            else
                ReportJoinFailure(Localize(Text::ServerConnectFailed, Server));
        });
    }

    void Leave(MatchmakingState& S)
    {
        if (S.Role == EMatchmakingRole::Host && !S.LobbyId.empty())
        {
            PostHttp(S, false, L"DELETE", "/v1/lobbies/" + S.LobbyId, S.HostKey, "", [](MatchmakingState&, MatchmakingNet::HttpResult const&) {});
            MILAN_INFOW(ELOG_Matchmaking, L"closing lobby %S", S.LobbyId.c_str());
        }
        else if (S.Role == EMatchmakingRole::Joiner && !S.SearchId.empty() && !S.JoinedReported)
        {
            // Cancelling the search also releases its unjoined reservation.
            PostHttp(S, false, L"DELETE", "/v1/searches/" + S.SearchId, S.SearchKey, "", [](MatchmakingState&, MatchmakingNet::HttpResult const&) {});
        }
        else if (S.Role == EMatchmakingRole::Joiner && !S.ReservationId.empty() && !S.JoinedReported)
        {
            PostHttp(S, false, L"DELETE", "/v1/reservations/" + S.ReservationId, S.JoinKey, "", [](MatchmakingState&, MatchmakingNet::HttpResult const&) {});
        }
        else if (S.Role == EMatchmakingRole::Joiner && !S.ReservationId.empty())
        {
            // A lobby member: the matchmaker stops waiting for it in a host migration.
            PostHttp(S, false, L"DELETE", "/v1/members/" + S.ReservationId, S.JoinKey, "", [](MatchmakingState&, MatchmakingNet::HttpResult const&) {});
        }

        // The delete requests above complete under the new generation and are ignored.
        ResetSession(S);
    }

    // Leaves the lobby and shows the host-left prompt; On_ConnectionError finds the session
    // already ended. Only a game still in the lost host's world gets the prompt.
    void FailMigration(MatchmakingState& S, wchar_t const* const Why)
    {
        MILAN_UNUSED(Why);
        MILAN_WARNW(ELOG_Matchmaking, L"host migration failed: %s", Why);
        Leave(S);
        if (HasLostHostConnection())
            ReportConnectionLost();
    }

    // This game hosts the lobby now. It opens a listen lobby with the lobby's settings; the
    // per-tick host work reports once that lobby listens, which completes the handoff.
    void AdoptLobby(MatchmakingState& S, json const& Handoff)
    {
        json const& Lobby = Handoff.at("state");
        HostedSettings Settings{};
        Settings.Known = true;
        Settings.Private = Lobby.at("isPrivate").get<bool>();
        Settings.MemberInvites = Lobby.value("memberInvites", !Settings.Private);
        Settings.MapId = Lobby.at("mapId").get<int>();
        Settings.EnemyModeId = Lobby.at("enemyModeId").get<int>();
        Settings.DifficultyId = Lobby.at("difficultyId").get<int>();
        Settings.MaxPlayers = Lobby.value("maxPlayers", 0);
        std::string LobbyId = Handoff.at("lobbyId").get<std::string>();
        std::string HostKey = Handoff.at("hostKey").get<std::string>();
        std::string InviteCode = Handoff.at("inviteCode").get<std::string>();
        long long const Revision = Handoff.at("revision").get<long long>();
        int const HeartbeatSeconds = Handoff.at("heartbeatSeconds").get<int>();
        int const ExpectedPlayers = Handoff.at("expectedPlayers").get<int>();
        UdpTicket const Ticket = UdpTicket::FromJson(Handoff.at("udpTicket"));

        ResetSession(S, true);
        S.Role = EMatchmakingRole::Host;
        S.LobbyId = std::move(LobbyId);
        S.HostKey = std::move(HostKey);
        S.InviteCode = std::move(InviteCode);
        // The first report changes the state, so it goes out as the next revision.
        S.Revision = Revision;
        S.HeartbeatSeconds = std::max(5, HeartbeatSeconds);
        S.LanAddresses = MatchmakingNet::LanIPv4Addresses(4);
        S.IntroductionsPaused = true;
        S.AdoptUntil = Clock::now() + k_adoptWindow;
        S.AdoptTicket = Ticket;
        S.Hosted = Settings;
        S.AutoHost = {};

        MILAN_UNUSED(ExpectedPlayers);
        MILAN_INFOW(ELOG_Matchmaking, L"this game is the new host of lobby %S (%d player(s) expected); opening the lobby",
            S.LobbyId.c_str(), ExpectedPlayers);
        if (!ApplyLobbySettings(Settings))
            MILAN_WARNW(ELOG_Matchmaking, L"no online game settings to carry the lobby's settings over; the lobby starts with its defaults");
        if (!RunConsoleCommand(std::wstring{ L"open " } + k_migrationLobbyUrl))
        {
            MILAN_ERRORW(ELOG_Matchmaking, L"no local player controller to open the lobby");
            FailMigration(S, L"could not open the lobby");
        }
    }

    // Another member hosts the lobby now. The reservation has no host candidates until that
    // host reports from its new lobby; PollReservation then travels.
    void RejoinLobby(MatchmakingState& S, json const& Reservation)
    {
        // Still the same lobby, so a member that joined with its invite code keeps it.
        std::string InviteCode = std::move(S.InviteCode);
        ResetSession(S, true);
        S.Role = EMatchmakingRole::Joiner;
        S.InviteCode = std::move(InviteCode);
        if (!TakeReservation(S, Reservation))
        {
            FailMigration(S, L"the matchmaker sent an invalid reservation");
            return;
        }

        MILAN_INFOW(ELOG_Matchmaking, L"another player is the new host; rejoining with reservation %S", S.ReservationId.c_str());
        ShowHint(Localize(Text::NewHostRejoining), 10.f);
        if (!S.HostPath.Candidates.empty())
        {
            if (!TravelToHost(S, false))
                FailMigration(S, L"the new host has no usable address");
            return;
        }
        S.AwaitingHostCandidates = true;
        S.MigrationDeadline = Clock::now() + k_adoptWindow;
        PollReservation(S);
    }

    void PollMember(MatchmakingState& S, std::chrono::milliseconds Delay = {});

    void HandleMigrationStatus(MatchmakingState& S, json const& Status)
    {
        S.MigrationVersion = Status.at("version").get<long long>();
        std::string const State = Status.at("state").get<std::string>();
        if (State == "NewHost")
            AdoptLobby(S, Status.at("host"));
        else if (State == "JoinNewHost")
            RejoinLobby(S, Status.at("reservation"));
        else if (State == "HostAlive")
            FailMigration(S, L"the host still answers the matchmaker, so this game lost its own connection");
        else
            PollMember(S);
    }

    void PollMember(MatchmakingState& S, std::chrono::milliseconds const Delay)
    {
        std::string const Path = "/v1/members/" + S.ReservationId + "/poll?after=" + std::to_string(S.MigrationVersion);
        PostHttp(S, true, L"POST", Path, S.JoinKey, "", [](MatchmakingState& State, MatchmakingNet::HttpResult const& Result)
        {
            if (Result.Status == 404)
            {
                FailMigration(State, L"the lobby is gone from the matchmaker");
                return;
            }
            if (std::optional<json> const Status = SuccessJson(Result, "migration poll"))
                HandleMigrationStatus(State, *Status);
            else
                PollMember(State, k_pollRetryDelay);
        }, Delay);
    }

    // A lobby member that lost its host asks the matchmaker to check the host and, if it is
    // gone, to hand the lobby to one of the members. Returns false for a game that is not a
    // member, which just reports the lost connection.
    bool BeginMigration(MatchmakingState& S)
    {
        if (S.Role != EMatchmakingRole::Joiner || !S.JoinedReported || S.ReservationId.empty() || S.BaseUrl.empty()
            || S.MigrationPending || S.AwaitingHostCandidates)
        {
            return false;
        }

        S.MigrationPending = true;
        S.MigrationVersion = -1;
        S.MigrationDeadline = Clock::now() + k_migrationDecisionTimeout;
        MILAN_INFOW(ELOG_Matchmaking, L"lost the host; asking the matchmaker to check it");
        ShowHint(Localize(Text::LostHostSearching), 10.f);

        json const Report{ { "canHost", true } };
        PostHttp(S, false, L"POST", "/v1/members/" + S.ReservationId + "/host-lost", S.JoinKey, Report.dump(),
            [](MatchmakingState& State, MatchmakingNet::HttpResult const& Result)
            {
                if (std::optional<json> const Status = SuccessJson(Result, "host-lost report"))
                    HandleMigrationStatus(State, *Status);
                else
                    FailMigration(State, L"the matchmaker did not take the report");
            });
        return true;
    }

    // Host, join and search keys are bearer secrets, so they need TLS outside the local network.
    void WarnIfPlainHttp(std::string const& Url)
    {
        if (MatchmakingNet::IsPlainHttpToRemote(Url))
            MILAN_WARNW(ELOG_Matchmaking, L"matchmaker %S uses plain http://, so lobby and join keys cross the network unencrypted; use https://",
                Url.c_str());
    }

    // An empty URL disables matchmaking. A hosted game re-registers on the new server by itself.
    void SetMatchmaker(MatchmakingState& S, std::string Url, wchar_t const* const Source)
    {
        MILAN_UNUSED(Source);
        if (Url == S.BaseUrl)
            return;

        Leave(S); // Its requests capture the previous URL.
        S.BaseUrl = std::move(Url);
        S.AutoHost = {};
        if (S.BaseUrl.empty())
            MILAN_INFOW(ELOG_Matchmaking, L"matchmaking disabled (%s)", Source);
        else
            MILAN_INFOW(ELOG_Matchmaking, L"matchmaker set to %S (%s)", S.BaseUrl.c_str(), Source);
        WarnIfPlainHttp(S.BaseUrl);
    }

    void QueueJoin(MatchmakingState& S, std::string Code, wchar_t const* const Source)
    {
        MILAN_UNUSED(Source);
        S.PendingJoin = std::move(Code);
        S.PendingJoinLeftGame = false;
        MILAN_INFOW(ELOG_Matchmaking, L"join %S queued (%s); it starts once the multiplayer menu is open",
            S.PendingJoin.c_str(), Source);
    }

    // Checks a matchmaker change and/or join from outside the game thread. Returns null when
    // it is valid, or what is wrong with it.
    char const* ValidateExternalRequest(std::optional<std::string> const& Url, std::optional<std::string> const& Code)
    {
        if (!Url && !Code)
            return "send matchmakerUrl, inviteCode, or both";
        if (Url && !Url->empty() && !MatchmakingNet::IsValidBaseUrl(*Url))
            return "matchmakerUrl must be an http:// or https:// URL, or empty to disable matchmaking";
        // Codes go into a JSON body and the log, so they are kept to printable ASCII.
        if (Code && (Code->empty() || Code->size() > k_maxInviteCodeLength
            || !std::all_of(Code->begin(), Code->end(), [](char const C) { return C > 0x20 && C < 0x7F; })))
        {
            return "inviteCode must be 1-64 printable ASCII characters";
        }
        return nullptr;
    }

    // Hands a validated request to the game thread. Returns false once the game is shutting down.
    bool QueueExternalRequest(Mailbox& Box, std::optional<std::string> Url, std::optional<std::string> Code,
        wchar_t const* const Source)
    {
        std::lock_guard Lock{ Box.Mutex };
        if (Box.Stop)
            return false;
        Box.Completions.push_back([Url = std::move(Url), Code = std::move(Code), Source](MatchmakingState& S)
        {
            if (Url)
                SetMatchmaker(S, *Url, Source);
            if (Code)
                QueueJoin(S, *Code, Source);
        });
        return true;
    }

    // Runs on the control pipe's thread, so it only validates and hands the request to the game thread.
    std::string HandleControlRequest(std::shared_ptr<Mailbox> const& Box, std::string const& Text)
    {
        auto const Reply = [](char const* const Error)
        {
            return (Error == nullptr ? json{ { "ok", true } } : json{ { "ok", false }, { "error", Error } }).dump();
        };

        try
        {
            json const Request = json::parse(Text, nullptr, false);
            if (!Request.is_object())
                return Reply("request must be a JSON object");

            std::optional<std::string> Url{};
            std::optional<std::string> Code{};
            if (auto const Field = Request.find("matchmakerUrl"); Field != Request.end())
            {
                if (!Field->is_string())
                    return Reply("matchmakerUrl must be a string");
                Url = Field->get<std::string>();
            }
            if (auto const Field = Request.find("inviteCode"); Field != Request.end())
            {
                if (!Field->is_string())
                    return Reply("inviteCode must be a string");
                Code = Field->get<std::string>();
            }
            if (char const* const Error = ValidateExternalRequest(Url, Code))
                return Reply(Error);

            if (!QueueExternalRequest(*Box, std::move(Url), std::move(Code), L"control pipe"))
                return Reply("the game is shutting down");
            return Reply(nullptr);
        }
        catch (std::exception const&)
        {
            return Reply("could not process the request");
        }
    }
}


// ! LMP.Lobby SPI service (Public/LmpLobby.h).
// ========================================

namespace
{
    // Other ASIs call in from any thread, so they only see copies the game thread publishes here.
    // Deliberately leaked: callers may still hold the table during static destruction.
    struct LobbyService
    {
        std::mutex                  Mutex{};
        std::shared_ptr<Mailbox>    Box{};          // Null until Initialize and after Terminate.
        LmpLobbyState               State{};
        std::uint64_t               Generation{ 0 };
        std::vector<std::pair<LmpLobbyInviteHandler, void*>> InviteHandlers{};
    };

    constexpr std::size_t k_maxInviteHandlers = 16;

    LobbyService& TheLobbyService()
    {
        static LobbyService* const s_service = new LobbyService{};
        return *s_service;
    }

    // The version 1 layout. Later versions only append, so this stays the minimum callers pass.
    constexpr std::uint32_t k_minLobbyStateSize = sizeof(LmpLobbyState);

    // Leaves the field empty rather than handing out a truncated value.
    template<std::size_t N>
    void CopyField(char (&Field)[N], std::string const& Value)
    {
        if (Value.size() < N)
            std::memcpy(Field, Value.c_str(), Value.size() + 1);
        else
            Field[0] = '\0';
    }

    bool SameLobbyState(LmpLobbyState const& A, LmpLobbyState const& B)
    {
        return A.Role == B.Role && A.Flags == B.Flags && A.PlayerCount == B.PlayerCount && A.MaxPlayers == B.MaxPlayers
            && std::strcmp(A.MatchmakerUrl, B.MatchmakerUrl) == 0 && std::strcmp(A.LobbyId, B.LobbyId) == 0
            && std::strcmp(A.InviteCode, B.InviteCode) == 0;
    }

    // Game thread.
    LmpLobbyState BuildLobbyServiceState(MatchmakingState const& S)
    {
        LmpLobbyState Out{};
        Out.StructSize = sizeof(Out);
        Out.Role = S.Role == EMatchmakingRole::Host ? LMP_LOBBY_ROLE_HOST
            : S.Role == EMatchmakingRole::Joiner ? LMP_LOBBY_ROLE_MEMBER : LMP_LOBBY_ROLE_NONE;
        CopyField(Out.MatchmakerUrl, S.BaseUrl);
        CopyField(Out.LobbyId, S.LobbyId);
        CopyField(Out.InviteCode, S.InviteCode);

        if (IsHosting(S))
        {
            int const MaxPlayers = HostMaxPlayers();
            bool const InLobby = HostIsInLobby();
            Out.Flags |= LMP_LOBBY_FLAG_CONNECTED | LMP_LOBBY_FLAG_HOSTING;
            Out.MaxPlayers = MaxPlayers;
            Out.PlayerCount = std::min(MaxPlayers, 1 + ConnectedClientCount());
            if (!InLobby)
                Out.Flags |= LMP_LOBBY_FLAG_IN_PROGRESS;
            if (S.Hosted.Known && S.Hosted.Private)
                Out.Flags |= LMP_LOBBY_FLAG_PRIVATE;
            // The matchmaker's rule for redeeming an invite: joinable, no host handoff underway, and
            // a free slot after the players and the reservations still on their way in.
            int Reserved = 0;
            for (auto const& Joiner : S.Joiners)
                Reserved += S.AdmittedMembers.contains(Joiner.first) ? 0 : 1;
            bool const HandingOff = S.AdoptUntil != Clock::time_point{} || S.AdoptReportBy != Clock::time_point{};
            if (S.Role == EMatchmakingRole::Host && !S.LobbyId.empty() && S.Hosted.Known && !HandingOff
                && IsJoinable(S.Hosted, InLobby) && Out.PlayerCount + Reserved < MaxPlayers)
            {
                Out.Flags |= LMP_LOBBY_FLAG_JOINABLE;
            }
        }
        else
        {
            UWorld* const World = CurrentWorld();
            UNetConnection* const Server = (World != nullptr && World->NetDriver != nullptr) ? World->NetDriver->ServerConnection : nullptr;
            UGameEngine* const Engine = GameEngine();
            UEngine* const BaseEngine = (GEngine != nullptr) ? *GEngine : nullptr;
            AWorldInfo* const WorldInfo = BaseEngine != nullptr ? BaseEngine->GetCurrentWorldInfo() : nullptr;
            if (Server != nullptr && Server->ConnectionState != USOCK_Closed && Engine != nullptr && Engine->GPendingLevel == nullptr)
            {
                Out.Flags |= LMP_LOBBY_FLAG_CONNECTED;
                if (WorldInfo != nullptr && WorldInfo->GRI != nullptr)
                    Out.PlayerCount = static_cast<std::int32_t>(WorldInfo->GRI->PRIArray.Count());
                if (S.Role == EMatchmakingRole::Joiner)
                    Out.MaxPlayers = S.MemberMaxPlayers;
            }
        }

        if (!S.PendingJoin.empty() || (S.Role == EMatchmakingRole::Joiner && !S.InviteCode.empty() && !S.JoinedReported))
            Out.Flags |= LMP_LOBBY_FLAG_JOIN_PENDING;
        return Out;
    }

    // Game thread, once per tick.
    void PublishLobbyServiceState(MatchmakingState const& S)
    {
        LmpLobbyState const Next = BuildLobbyServiceState(S);
        LobbyService& Service = TheLobbyService();
        std::lock_guard Lock{ Service.Mutex };
        if (Service.Generation != 0 && SameLobbyState(Next, Service.State))
            return;
        Service.State = Next;
        ++Service.Generation;
    }

    std::uint32_t LobbyService_GetState(LmpLobbyState* const State)
    {
        if (State == nullptr || State->StructSize < k_minLobbyStateSize)
            return LMP_LOBBY_INVALID_ARGUMENT;

        LobbyService& Service = TheLobbyService();
        LmpLobbyState Copy{};
        {
            std::lock_guard Lock{ Service.Mutex };
            Copy = Service.State;
        }
        Copy.StructSize = std::min<std::uint32_t>(State->StructSize, sizeof(LmpLobbyState));
        std::memcpy(State, &Copy, Copy.StructSize);
        return LMP_LOBBY_OK;
    }

    std::uint64_t LobbyService_GetStateGeneration()
    {
        LobbyService& Service = TheLobbyService();
        std::lock_guard Lock{ Service.Mutex };
        return Service.Generation;
    }

    std::uint32_t LobbyService_RequestJoin(char const* const MatchmakerUrl, char const* const InviteCode)
    {
        try
        {
            if (InviteCode == nullptr)
                return LMP_LOBBY_INVALID_ARGUMENT;

            // Bounded reads: anything longer than its limit is rejected without reading all of it.
            std::optional<std::string> Url{};
            if (MatchmakerUrl != nullptr && *MatchmakerUrl != '\0')
            {
                std::size_t const UrlLength = ::strnlen(MatchmakerUrl, LMP_LOBBY_URL_MAX);
                if (UrlLength >= LMP_LOBBY_URL_MAX)
                    return LMP_LOBBY_INVALID_ARGUMENT;
                Url.emplace(MatchmakerUrl, UrlLength);
            }
            std::optional<std::string> Code{ std::in_place, InviteCode, ::strnlen(InviteCode, k_maxInviteCodeLength + 1) };
            if (ValidateExternalRequest(Url, Code) != nullptr)
                return LMP_LOBBY_INVALID_ARGUMENT;

            std::shared_ptr<Mailbox> Box{};
            {
                LobbyService& Service = TheLobbyService();
                std::lock_guard Lock{ Service.Mutex };
                Box = Service.Box;
            }
            if (Box == nullptr || !QueueExternalRequest(*Box, std::move(Url), std::move(Code), L"LMP.Lobby service"))
                return LMP_LOBBY_SHUTTING_DOWN;
            return LMP_LOBBY_OK;
        }
        catch (...)
        {
            // Never let an exception cross into the calling ASI.
            return LMP_LOBBY_FAILED;
        }
    }

    std::uint32_t LobbyService_AddInviteHandler(LmpLobbyInviteHandler const Handler, void* const Context)
    {
        try
        {
            if (Handler == nullptr)
                return LMP_LOBBY_INVALID_ARGUMENT;
            LobbyService& Service = TheLobbyService();
            std::lock_guard Lock{ Service.Mutex };
            auto& Handlers = Service.InviteHandlers;
            if (std::find(Handlers.begin(), Handlers.end(), std::pair{ Handler, Context }) != Handlers.end())
                return LMP_LOBBY_INVALID_ARGUMENT;
            if (Handlers.size() >= k_maxInviteHandlers)
                return LMP_LOBBY_LIMIT_REACHED;
            Handlers.emplace_back(Handler, Context);
            return LMP_LOBBY_OK;
        }
        catch (...)
        {
            return LMP_LOBBY_FAILED;
        }
    }

    std::uint32_t LobbyService_RemoveInviteHandler(LmpLobbyInviteHandler const Handler, void* const Context)
    {
        LobbyService& Service = TheLobbyService();
        std::lock_guard Lock{ Service.Mutex };
        auto& Handlers = Service.InviteHandlers;
        auto const Found = std::find(Handlers.begin(), Handlers.end(), std::pair{ Handler, Context });
        if (Found == Handlers.end())
            return LMP_LOBBY_INVALID_ARGUMENT;
        Handlers.erase(Found);
        return LMP_LOBBY_OK;
    }

    LmpLobbyService const k_lobbyServiceTable{
        &LobbyService_GetState,
        &LobbyService_GetStateGeneration,
        &LobbyService_RequestJoin,
        &LobbyService_AddInviteHandler,
        &LobbyService_RemoveInviteHandler,
    };

    // Game thread. Calls each invite handler outside the lock, so handlers can use the service,
    // and returns whether any showed its own UI.
    bool RunInviteHandlers(MatchmakingState const& S, std::string const& JoinLink)
    {
        std::vector<std::pair<LmpLobbyInviteHandler, void*>> Handlers{};
        {
            LobbyService& Service = TheLobbyService();
            std::lock_guard Lock{ Service.Mutex };
            Handlers = Service.InviteHandlers;
        }

        LmpLobbyState const State = BuildLobbyServiceState(S);
        bool Shown = false;
        for (auto const& [Handler, Context] : Handlers)
        {
            try
            {
                Shown |= Handler(&State, JoinLink.c_str(), Context) == LMP_LOBBY_INVITE_SHOWN;
            }
            catch (...)
            {
                MILAN_WARNW(ELOG_Matchmaking, L"an invite handler threw an exception");
            }
        }
        return Shown;
    }

    USFXGUIMovie* FocusedPauseMovie()
    {
        ABioPlayerController* const Player = LocalBioPlayer();
        USFXGUIInteraction* const Gui = Player != nullptr ? Player->GetSFXUIController() : nullptr;
        if (Gui == nullptr)
            return nullptr;

        USFXGUIMovie* const Movie = Gui->GetFocusMovieForPlayerController(Player);
        static SFXName const k_pauseMenu{ L"SFXGUI_MPPauseMenu", 0 };
        if (Movie == nullptr || Movie->Class == nullptr || Movie->Class->Name != k_pauseMenu || !Movie->IsOpen())
            return nullptr;

        return Movie;
    }

    // A created lobby or reservation alone does not prove the service is still reachable.
    // The next request can fail after it goes down, so the button times out after contact stops.
    bool CanInvite(MatchmakingState const& S, Clock::time_point const Now)
    {
        if (S.BaseUrl.empty() || S.LastServiceContact == Clock::time_point{}
            || Now - S.LastServiceContact > std::chrono::seconds{ 60 })
            return false;

        if (S.Role == EMatchmakingRole::Host && !S.InviteCode.empty() && IsListenServer())
            return 1 + ConnectedClientCount() < HostMaxPlayers();

        if (S.Role == EMatchmakingRole::Joiner && S.JoinedReported && !S.ReservationId.empty()
            && !S.MigrationPending)
        {
            UWorld* const World = CurrentWorld();
            UNetConnection* const Server = World != nullptr && World->NetDriver != nullptr
                ? World->NetDriver->ServerConnection : nullptr;
            if (Server == nullptr || Server->ConnectionState == USOCK_Closed)
                return false;
            UEngine* const Engine = GEngine != nullptr ? *GEngine : nullptr;
            AWorldInfo* const WorldInfo = Engine != nullptr ? Engine->GetCurrentWorldInfo() : nullptr;
            if (S.MemberMaxPlayers <= 0 || WorldInfo == nullptr || WorldInfo->GRI == nullptr)
                return false;
            return static_cast<int>(WorldInfo->GRI->PRIArray.Count()) < S.MemberMaxPlayers;
        }

        return false;
    }

    void UpdatePauseInviteAvailability(MatchmakingState& S, Clock::time_point const Now)
    {
        if (Now < S.NextInviteUiUpdate)
            return;
        S.NextInviteUiUpdate = Now + std::chrono::seconds{ 1 };
        if (USFXGUIMovie* const Movie = FocusedPauseMovie())
        {
            Movie->SetVariableBool(FString{ L"screen.Menu.ButtonsMC.InviteEnabled" }, CanInvite(S, Now));
            Movie->ActionScriptVoid(FString{ L"screen.Menu.ButtonsMC.ApplyInviteEnabled" });
        }
    }

    // The pause movie sits above the stock HUD hint layer. Put invite feedback
    // in its own authored text field while that movie has focus.
    bool ShowPauseInviteFeedback(std::wstring const& Message, float const Seconds)
    {
        USFXGUIMovie* const Movie = FocusedPauseMovie();
        if (Movie == nullptr)
            return false;

        Movie->SetVariableString(FString{ L"screen.Menu.ButtonsMC.InviteFeedback.text" }, FString{ Message.c_str() });
        Movie->SetVariableNumber(FString{ L"screen.Menu.ButtonsMC.InviteFeedbackDuration" }, Seconds * 1000.f);
        Movie->ActionScriptVoid(FString{ L"screen.Menu.ButtonsMC.ShowInviteFeedback" });
        return true;
    }

    void ShowInviteHint(std::wstring const& Message, float const Seconds)
    {
        if (!ShowPauseInviteFeedback(Message, Seconds) && !ShowHint(Message, Seconds))
            MILAN_WARNW(ELOG_Matchmaking, L"invite hint box was not created");
    }

    // Game thread. Shares the lobby's invite code (S.InviteCode): copies the join link, lets the
    // invite handlers show their own UI, and shows the code if none did.
    void ShareInvite(MatchmakingState const& S)
    {
        // Copied whatever else shows, so the player can still paste it if a handler's
        // invite UI cannot reach the friend they want.
        std::string const Link = JoinLink(S.BaseUrl, S.InviteCode);
        bool const Copied = CopyToClipboard(Widen(Link), std::chrono::milliseconds{ 250 });
        if (!Copied)
            MILAN_WARNW(ELOG_Matchmaking, L"could not copy the join link to the clipboard");

        if (!RunInviteHandlers(S, Link))
            ShowInviteHint(Localize(Copied ? Text::InviteCodeCopied : Text::InviteCode, Widen(S.InviteCode)), 10.f);
    }

    // A member that joined through a search never saw the invite code. The matchmaker hands it
    // out while the lobby lets its members invite (Public, not Private), and it is
    // asked each time because the host can rotate the code or change the mode.
    void RequestMemberInvite(MatchmakingState& S)
    {
        PostHttp(S, false, L"GET", "/v1/members/" + S.ReservationId + "/invite", S.JoinKey, "",
            [](MatchmakingState& Session, MatchmakingNet::HttpResult const& Result)
        {
            if (Result.Status == 403 && ErrorCode(Result) == "invites_host_only")
            {
                ShowInviteHint(Localize(Text::InvitesHostOnly), 5.f);
                return;
            }

            std::optional<json> const Body = SuccessJson(Result, "invite code request");
            std::string Code = Body ? Body->value("inviteCode", std::string{}) : std::string{};
            if (Code.empty())
            {
                ShowInviteHint(Localize(Text::InviteCodeUnavailable), 3.f);
                return;
            }
            Session.InviteCode = std::move(Code);
            ShareInvite(Session);
        });
    }

    void RegisterLobbyService(::LESDK::Initializer& Context, std::shared_ptr<Mailbox> Box)
    {
        {
            LobbyService& Service = TheLobbyService();
            std::lock_guard Lock{ Service.Mutex };
            Service.Box = std::move(Box);
        }

        ISharedProxyInterface* const Proxy = Context.ProxyInstance;
        unsigned long ProxyVersion = 0;
        if (Proxy == nullptr || Proxy->GetVersion(&ProxyVersion) != SPIReturn::Success || ProxyVersion < SPI_VERSION_V4)
        {
            MILAN_WARNW(ELOG_Matchmaking, L"the proxy has no service registry; other ASIs cannot use %S", LMP_LOBBY_SERVICE_NAME);
            return;
        }

        SPIReturn const Result = Proxy->RegisterService(LMP_LOBBY_SERVICE_NAME, LMP_LOBBY_SERVICE_VERSION, &k_lobbyServiceTable);
        if (Result != SPIReturn::Success)
            MILAN_WARNW(ELOG_Matchmaking, L"could not register %S: %s", LMP_LOBBY_SERVICE_NAME, SPIReturnToString(Result));
    }

    // The table stays registered, so it reports an empty state and refuses joins from here on.
    void RetireLobbyService()
    {
        LobbyService& Service = TheLobbyService();
        std::lock_guard Lock{ Service.Mutex };
        Service.Box.reset();
        Service.State = LmpLobbyState{};
        Service.State.StructSize = sizeof(LmpLobbyState);
        ++Service.Generation;
    }
}


// ! Per-tick work.
// ========================================

namespace
{
    void TrackGameSocket(MatchmakingState& S)
    {
        FSocket* const Socket = SocketOf(FindGameDriver());
        if (Socket == S.Socket)
            return;

        S.Socket = Socket;
        S.LocalPort = Socket != nullptr ? MatchmakingNet::LocalPort(HandleOf(Socket)) : -1;
        if (Socket != nullptr && S.LocalPort < 0)
        {
            MILAN_ERRORW(ELOG_Matchmaking, L"game socket handle at +0x%X failed getsockname; not using it", (unsigned)k_socketWinHandleOffset);
            S.Socket = nullptr;
            return;
        }

        if (Socket != nullptr)
            MILAN_INFOW(ELOG_Matchmaking, L"game socket 0x%p bound to local port %d", (void*)Socket, S.LocalPort);
        else if (S.Role != EMatchmakingRole::None)
            MILAN_INFOW(ELOG_Matchmaking, L"game socket closed");

        // A new socket means a new NAT mapping.
        S.Discovery.Restart();
    }

    void TickDiscovery(MatchmakingState& S, Clock::time_point const Now)
    {
        DiscoveryState& D = S.Discovery;
        if (S.Socket == nullptr || !S.InfoReady || S.ServerUdpPort == 0 || !D.Ticket.Valid || Now < D.NextSend)
            return;

        std::array<std::uint8_t, Rendezvous::k_discoveryRequestLength> Packet{};
        Rendezvous::WriteDiscoveryRequest(Packet, D.Ticket.Id, ++D.Sequence, D.Ticket.Secret);
        if (!SendDatagram(S.Socket, Packet, S.ServerAddress, S.ServerUdpPort))
            MILAN_WARNW(ELOG_Matchmaking, L"discovery send failed");

        if (D.Answered)
        {
            D.NextSend = Now + k_discoveryRefresh;
        }
        else if (++D.Unanswered <= k_discoveryFastAttempts)
        {
            D.NextSend = Now + k_discoveryRetry;
        }
        else
        {
            if (D.Unanswered == k_discoveryFastAttempts + 1)
                MILAN_WARNW(ELOG_Matchmaking, L"no discovery response from %S yet; is the UDP port reachable?",
                    MatchmakingNet::FormatIPv4(S.ServerAddress, S.ServerUdpPort).c_str());
            D.NextSend = Now + k_discoverySlowRetry;
        }
    }

    void TickProbes(MatchmakingState& S, PeerPath& Path, Rendezvous::EDirection const Direction, Clock::time_point const Now)
    {
        if (S.Socket == nullptr || Path.Verified || Path.Candidates.empty() || Now >= Path.ProbeUntil || Now < Path.NextProbe)
            return;

        std::array<std::uint8_t, Rendezvous::k_probeLength> Packet{};
        Rendezvous::WriteProbe(Packet, Rendezvous::EProbeType::Probe, Direction, Path.Pair, ++Path.ProbeSequence, Path.TraversalKey);
        for (auto const& Candidate : Path.Candidates)
            SendDatagram(S.Socket, Packet, Candidate.Address, Candidate.Port);
        Path.NextProbe = Now + k_probeInterval;
    }

    void TickJoiner(MatchmakingState& S, Clock::time_point const Now)
    {
        bool const Migrating = S.MigrationPending || S.AwaitingHostCandidates;
        if (Migrating && Now >= S.MigrationDeadline)
        {
            FailMigration(S, S.MigrationPending ? L"the matchmaker did not choose a new host in time"
                : L"the new host's lobby did not come up in time");
            return;
        }

        // A member that is no longer in (or traveling to) a host's game left it, e.g. for the menu.
        UGameEngine* const Engine = GameEngine();
        UNetDriver* const WorldDriver = (GWorld != nullptr && *GWorld != nullptr) ? (*GWorld)->NetDriver : nullptr;
        bool const Traveling = Engine == nullptr || Engine->GPendingLevel != nullptr || Engine->TravelURL.Length() > 0;
        if ((S.JoinedReported || Migrating) && !Traveling && (WorldDriver == nullptr || WorldDriver->ServerConnection == nullptr))
        {
            MILAN_INFOW(ELOG_Matchmaking, L"left the host's game; leaving the matchmaker lobby");
            Leave(S);
            return;
        }

        if (!S.TravelIssued)
            return;

        // Once UE3 has a socket, tell the host how to reach it on the LAN too.
        if (S.Socket != nullptr && S.LocalPort != S.ReportedPort && !S.ReservationId.empty())
        {
            S.ReportedPort = S.LocalPort;
            json const Request{ { "gamePort", S.LocalPort }, { "lanAddresses", MatchmakingNet::LanIPv4Addresses(4) } };
            PostHttp(S, false, L"PUT", "/v1/reservations/" + S.ReservationId + "/candidates", S.JoinKey, Request.dump(),
                [](MatchmakingState&, MatchmakingNet::HttpResult const& Result)
                {
                    if (Result.Status != 204)
                        MILAN_WARNW(ELOG_Matchmaking, L"reporting LAN candidates failed: %S", DescribeFailure(Result).c_str());
                });
        }

        // A member rejoining after migration is still in the lost host's world until it travels.
        if (!S.JoinedReported && !Traveling && WorldDriver != nullptr && WorldDriver->ServerConnection != nullptr
            && WorldDriver->ServerConnection->ConnectionState != USOCK_Closed)
        {
            S.JoinedReported = true;
            MILAN_INFOW(ELOG_Matchmaking, L"joined the host's game; releasing the reservation");
            PostHttp(S, false, L"POST", "/v1/reservations/" + S.ReservationId + "/joined", S.JoinKey, "",
                [](MatchmakingState&, MatchmakingNet::HttpResult const&) {});
        }
    }

    // Registers a hosted game once its settings are known, and closes the lobby when hosting ends.
    std::string NewPresenceId()
    {
        static constexpr char k_hex[] = "0123456789abcdef";
        std::random_device Device{};
        std::string Id(32, '0');
        for (std::size_t i = 0; i < Id.size(); i += 8)
        {
            std::uint32_t const Value = Device();
            for (std::size_t j = 0; j < 8; ++j)
                Id[i + j] = k_hex[(Value >> (4 * j)) & 0xF];
        }
        return Id;
    }

    // Reports this game to the matchmaker every Presence.Seconds while nothing else counts it:
    // a registered lobby counts its host and members, and a search counts its player. That
    // leaves the menus and a 1-player lobby. The interval comes from the /v1/info that hosting,
    // joining and searching read anyway, so a game that has done none of them sends nothing,
    // and a matchmaker that does not ask for presence never gets a report.
    void TickPresence(MatchmakingState& S, Clock::time_point const Now)
    {
        constexpr std::chrono::seconds k_infoRetry{ 300 };
        PresenceState& P = S.Presence;
        if (P.Url != S.BaseUrl)
        {
            // Another matchmaker: its interval is not known yet, and it holds no report.
            std::string Id = std::move(P.Id);
            P = {};
            P.Id = std::move(Id);
            P.Url = S.BaseUrl;
        }
        if (S.BaseUrl.empty())
            return;

        if (S.Role != EMatchmakingRole::None)
        {
            // Counted by its lobby or search from here on, so the report is withdrawn rather
            // than left to expire and count this player twice.
            if (P.Reported)
            {
                P.Reported = false;
                PostHttp(S, false, L"DELETE", "/v1/presence/" + P.Id, "", "", [](MatchmakingState&, MatchmakingNet::HttpResult const&) {});
            }
            P.Next = {};
            return;
        }

        if (Now < P.Next)
            return;

        if (!P.InfoKnown)
        {
            // FetchInfo records the interval whenever this game hosts, joins or searches. The
            // one case that reads no /v1/info is a 1-player lobby, which is hosted without
            // being registered, so it is read here. A failure is retried at this pace.
            if (!IsListenServer())
                return;
            P.Next = Now + k_infoRetry;
            FetchInfo(S, [](MatchmakingState&) {});
            return;
        }
        if (P.Seconds <= 0)
            return;

        if (P.Id.empty())
            P.Id = NewPresenceId();
        P.Next = Now + std::chrono::seconds{ std::clamp(P.Seconds, 30, 3600) };
        P.Reported = true;
        PostHttp(S, false, L"PUT", "/v1/presence/" + P.Id, "", "", [](MatchmakingState& Session, MatchmakingNet::HttpResult const& Result)
        {
            // The matchmaker turned presence off since /v1/info was read.
            if (Result.Status == 404)
            {
                Session.Presence.Seconds = 0;
                Session.Presence.Reported = false;
            }
        });
    }

    void TickHosting(MatchmakingState& S, Clock::time_point const Now)
    {
        AutoHostPolicy& P = S.AutoHost;
        if (S.Socket == nullptr || !IsListenServer())
        {
            P.Suppressed = false;
            if (S.Role != EMatchmakingRole::Host)
            {
                // The next hosted game publishes its own settings when its lobby starts.
                S.Hosted = {};
                return;
            }
            if (S.AdoptUntil != Clock::time_point{})
            {
                // A new host's lobby is still loading.
                if (Now < S.AdoptUntil)
                    return;
                MILAN_WARNW(ELOG_Matchmaking, L"the new lobby did not start listening in time; giving the matchmaker lobby up");
                Leave(S);
                S.Hosted = {};
                S.AutoHost = {};
                return;
            }
            if (P.ListenLostAt == Clock::time_point{})
            {
                P.ListenLostAt = Now;
            }
            else if (Now - P.ListenLostAt >= k_listenGrace)
            {
                MILAN_INFOW(ELOG_Matchmaking, L"no longer hosting; closing the matchmaker lobby");
                Leave(S);
                S.Hosted = {};
                S.AutoHost = {};
            }
            return;
        }

        P.ListenLostAt = {};

        // A lobby of 1 player takes no invites, so it has no matchmaker lobby. The host can only
        // pick 1 while alone; raising the size registers the lobby again below.
        bool const Solo = HostMaxPlayers() <= 1;
        if (Solo && S.Role == EMatchmakingRole::Host && S.AdoptUntil == Clock::time_point{})
        {
            MILAN_INFOW(ELOG_Matchmaking, L"lobby size is 1 player; closing the matchmaker lobby");
            Leave(S);
            return;
        }
        if (S.Role == EMatchmakingRole::Host || Solo || P.Suppressed || S.BaseUrl.empty() || !S.Hosted.Known || Now < P.NextAttempt)
            return;

        // A joiner that is now listening has left that game and is hosting its own.
        if (S.Role == EMatchmakingRole::Joiner)
            Leave(S);
        StartHost(S);
    }

    // Host traffic for a listening game. Nothing goes out while it is not listening, so a game
    // that stopped hosting cannot answer a migration check; TickHosting ends the lobby if that lasts.
    void TickHostedLobby(MatchmakingState& S, Clock::time_point const Now)
    {
        if (S.AdoptUntil != Clock::time_point{})
        {
            // A new host's lobby is up: discovery starts on its listen socket.
            S.AdoptUntil = {};
            S.AdoptReportBy = Now + k_adoptReportWait;
            if (S.AdoptTicket)
                S.Discovery.Ticket = *std::exchange(S.AdoptTicket, std::nullopt);
            S.Discovery.Restart();
            MILAN_INFOW(ELOG_Matchmaking, L"the new lobby is listening on port %d", S.LocalPort);
        }
        if (S.AdoptReportBy != Clock::time_point{})
        {
            // Its first report completes the handoff and sends the members to its candidates,
            // so it waits briefly for the observed endpoint.
            bool const DiscoveryDone = S.Discovery.Answered || S.ServerUdpPort == 0 || !S.InfoReady;
            if (!DiscoveryDone && Now < S.AdoptReportBy)
                return;
            S.AdoptReportBy = {};
            S.NextHeartbeat = Now;
            MILAN_INFOW(ELOG_Matchmaking, L"taking over lobby %S; invite code %S", S.LobbyId.c_str(), S.InviteCode.c_str());
        }

        if (S.IntroductionsPaused)
        {
            S.IntroductionsPaused = false;
            PollIntroductions(S);
        }

        RefreshAdmittedMembers(S);
        // Setting changes go out promptly; otherwise the full state doubles as the heartbeat.
        bool Due = Now >= S.NextHeartbeat;
        if (!Due && Now >= S.NextStateCheck)
        {
            S.NextStateCheck = Now + k_stateCheckInterval;
            Due = BuildLobbyState(S).dump() != S.LastStateBody;
        }
        if (Due)
            SendHeartbeat(S);
        for (auto& [Id, Path] : S.Joiners)
            TickProbes(S, Path, Rendezvous::EDirection::HostToJoiner, Now);
    }

    // The host writes each player's round trip into that player's replicated PRI.Ping, which
    // reaches every other player. Ping never replicates to its owner, so each client writes its
    // own locally. The engine's ServerUpdatePing is blocked (On_ScriptCall), because it only
    // measures while a pawn moves and would overwrite these values.
    void TickPlayerPing(MatchmakingState& S, Clock::time_point const Now)
    {
        if (Now < S.NextPingUpdate)
            return;
        S.NextPingUpdate = Now + k_pingInterval;

        UWorld* const World = CurrentWorld();
        UNetDriver* const Driver = World != nullptr ? World->NetDriver : nullptr;
        if (Driver == nullptr)
            return;

        auto const Write = [](APlayerReplicationInfo* const PRI, unsigned char const Ping, bool const Replicate)
        {
            if (PRI == nullptr || PRI->Ping == Ping)
                return;
            PRI->Ping = Ping;
            // Script assignments mark an actor for replication; native writes do not.
            if (Replicate)
                PRI->bNetDirty = TRUE;
        };

        if (IsListenServer())
        {
            for (UNetConnection* const Client : Driver->ChildConnections)
            {
                if (Client != nullptr && Client->Actor != nullptr)
                    Write(Client->Actor->PlayerReplicationInfo, PingFromLag(AverageLag(Client)), true);
            }
            Write(LocalPRI(), k_hostPing, true);
        }
        else if (Driver->ServerConnection != nullptr && Driver->ServerConnection->ConnectionState != USOCK_Closed)
        {
            Write(LocalPRI(), PingFromLag(AverageLag(Driver->ServerConnection)), false);
        }
    }

    // What the lobby's Exit (SFXGUI_MPLobby.ConfirmExitLobby) and the pause menu's Exit Match
    // (SFXGUI_MPPauseMenu.ConfirmExitPopupCallback) do: the match saves accomplishments, then
    // both leave the online game and disconnect, which returns to the multiplayer menu.
    void LeaveCurrentGame(MatchmakingState& S)
    {
        static SFXName const k_lobbyGri{ L"SFXGRIMP_Lobby", 0 };
        auto* const Engine = static_cast<USFXEngine*>(GameEngine());
        AWorldInfo* const WorldInfo = Engine != nullptr ? Engine->GetCurrentWorldInfo() : nullptr;
        bool const InMatch = WorldInfo != nullptr && WorldInfo->GRI != nullptr && !IsInstanceOf(WorldInfo->GRI, k_lobbyGri);
        if (InMatch && Engine->AccomplishmentManager != nullptr) {
            Engine->AccomplishmentManager->Save();
        }

        TypedObjectIterator<USFXOnlineComponentBlazeGame, true, false> Iter{};
        if (Iter)
        {
            (*Iter)->LeaveGame();
        }

        Leave(S);
        if (!RunConsoleCommand(L"disconnect"))
            MILAN_ERRORW(ELOG_Matchmaking, L"no local player controller to run 'disconnect'");
    }

    // External joins wait for the multiplayer menu: that is where direct-IP travel is known to work.
    // A player in a lobby or match (hosting, joined, or still connecting) leaves it first.
    void TickPendingJoin(MatchmakingState& S)
    {
        if (S.PendingJoin.empty())
            return;

        if (FindGameDriver() != nullptr)
        {
            // Not mid-travel (e.g. the lobby starting its match), unless still connecting to a host.
            UGameEngine* const Engine = GameEngine();
            bool const Connecting = Engine != nullptr && Engine->GPendingLevel != nullptr;
            if (!S.PendingJoinLeftGame && (Connecting || !IsLevelTransitioning()))
            {
                MILAN_INFOW(ELOG_Matchmaking, L"leaving the current %s to join %S", IsListenServer() ? L"game (hosting)" : L"game",
                    S.PendingJoin.c_str());
                S.PendingJoinLeftGame = true;
                LeaveCurrentGame(S);
            }
            return;
        }
        if (S.MenuWorld == nullptr || S.MenuWorld != CurrentWorld())
            return;

        S.PendingJoinLeftGame = false;
        std::string const Code = std::exchange(S.PendingJoin, {});
        if (S.BaseUrl.empty())
        {
            MILAN_ERRORW(ELOG_Matchmaking, L"cannot join %S: no matchmaker is configured", Code.c_str());
            ReportJoinFailure(Localize(Text::MatchmakingOff));
            return;
        }
        MILAN_INFOW(ELOG_Matchmaking, L"multiplayer menu is open; joining %S via %S", Code.c_str(), S.BaseUrl.c_str());
        Leave(S);
        StartJoin(S, Code);
    }

    void DrainCompletions(MatchmakingState& S)
    {
        std::deque<Completion> Ready{};
        {
            std::lock_guard Lock{ S.Box->Mutex };
            Ready.swap(S.Box->Completions);
        }
        for (auto& Done : Ready)
            Done(S);
    }
}


// ! CMatchmaking implementation.
// ========================================

bool CMatchmaking::Initialize(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);

    mp_state = new MatchmakingState{};
    mp_state->BaseUrl = (gp_clientConfig != nullptr) ? std::string{ gp_clientConfig->MatchmakerUrl.c_str() } : std::string{ k_defaultMatchmakerUrl };
    std::wstring executable(32768, L'\0');
    DWORD const executableLength = ::GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (executableLength == 0 || executableLength >= executable.size())
        executable.clear();
    else
        executable.resize(executableLength);
    auto version = ModVersion::Resolve(std::filesystem::path{ executable },
        gp_clientConfig != nullptr ? std::string_view{ gp_clientConfig->ModVersion.c_str() } : std::string_view{},
        CommandLineValue(L"-mmversion="));
    if (!version.Version.empty())
    {
        mp_state->ModVersion = std::move(version.Version);
        if (version.From == ModVersion::Source::Metadata)
            MILAN_INFOW(ELOG_Matchmaking, L"mod version %S from %s", mp_state->ModVersion.c_str(), version.MetadataPath.c_str());
        else
            MILAN_INFOW(ELOG_Matchmaking, L"mod version %S from %s", mp_state->ModVersion.c_str(),
                version.From == ModVersion::Source::CommandLine ? L"-mmversion" : L"[milan.game] version");
    }
    else
        MILAN_WARNW(ELOG_Matchmaking, L"mod version is unknown: %S (metadata: %s). Matchmaking is disabled; check the installed _metacmm.txt or set [milan.game] version / mm.version.",
            version.Error.data(), version.MetadataPath.empty() ? L"explicit override or executable path unavailable" : version.MetadataPath.c_str());
    // Tied to the configured server, so -mmserver naming another one sends no key.
    if (gp_clientConfig != nullptr)
        MatchmakingNet::SetAccessKey(mp_state->BaseUrl, gp_clientConfig->SelectedAccessKey().c_str());

    if (std::optional<std::string> Url = CommandLineValue(L"-mmserver="))
    {
        if (Url->empty() || MatchmakingNet::IsValidBaseUrl(*Url))
            mp_state->BaseUrl = std::move(*Url);
        else
            MILAN_ERRORW(ELOG_Matchmaking, L"-mmserver must be an http:// or https:// URL (or empty); keeping %S", mp_state->BaseUrl.c_str());
    }
    WarnIfPlainHttp(mp_state->BaseUrl);
    if (std::optional<std::string> Code = CommandLineValue(L"-mmjoin="); Code && !Code->empty())
        QueueJoin(*mp_state, std::move(*Code), L"command line");

    std::thread{ WorkerLoop, mp_state->Box, false }.detach();
    std::thread{ WorkerLoop, mp_state->Box, true }.detach();

    // Per process, so each of several running instances can be addressed.
    std::string PipeError{};
    mp_state->PipeName = L"MilanHost." + std::to_wstring(::GetCurrentProcessId());
    mp_state->Pipe = MatchmakingNet::StartControlPipe(mp_state->PipeName,
        [Box = mp_state->Box](std::string const& Request) { return HandleControlRequest(Box, Request); }, PipeError);
    if (mp_state->Pipe == nullptr)
        MILAN_WARNW(ELOG_Matchmaking, L"control pipe unavailable: %S", PipeError.c_str());

    RegisterLobbyService(Context, mp_state->Box);
    RegisterCommands();

    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized (matchmaker %S, control pipe %s)", k_typeName,
        mp_state->BaseUrl.empty() ? "disabled" : mp_state->BaseUrl.c_str(), mp_state->Pipe ? mp_state->PipeName.c_str() : L"none");
    return true;
}

void CMatchmaking::On_HostSettingsChanged(USFXOnlineGameSettings* const Settings)
{
    if (mp_state == nullptr || Settings == nullptr)
        return;

    // Random has no package name, so it has no master map ID; its setting is 0.
    int const MapId = std::max(0, Settings->mME3MapName.Length() > 0 ? Settings->eventGetMasterMapID(Settings->mME3MapName) : 0);
    int const EnemyModeId = Settings->EnemyType;

    HostedSettings& H = mp_state->Hosted;
    if (H.AllowMatchmaking)
    {
        H.MapId = MapId;
        H.EnemyModeId = EnemyModeId;
        H.ResolvedMapId.reset();
        H.ResolvedEnemyId.reset();
    }
    else
    {
        // CommitSettings disallows matchmaking first, then publishes its Random picks.
        H.ResolvedMapId = MapId;
        H.ResolvedEnemyId = EnemyModeId;
    }
    H.DifficultyId = Settings->Difficulty;
    H.Private = Settings->mPrivacySetting != k_privacyPublic;
    H.MemberInvites = Settings->mPrivacySetting != k_privacyPrivate;
    H.Known = true;
}

void CMatchmaking::On_AllowMatchmaking(bool const bAllow)
{
    if (mp_state == nullptr)
        return;
    mp_state->Hosted.AllowMatchmaking = bAllow;
    mp_state->Hosted.LockedToStart = !bAllow && HostIsInLobby();
}

// SFXGameViewportClient.NotifyConnectionError shows the host-left prompt only when
// GM_IsInMultiplayerGame() is true, and without Blaze's game-flow events LMP never leaves
// MMS_PreMatch. So a client that lost its host gets the same handling here. A failed
// connection attempt still has its pending level and is left to the engine.
bool CMatchmaking::On_ConnectionError()
{
    UGameEngine* const Engine = GameEngine();
    UWorld* const World = CurrentWorld();
    if (Engine == nullptr || World == nullptr || World->NetDriver == nullptr || World->NetDriver->ServerConnection == nullptr
        || Engine->GPendingLevel != nullptr || Engine->GamePlayers.Count() == 0)
    {
        return false;
    }

    ULocalPlayer* const Player = Engine->GamePlayers(0);
    if (Player == nullptr || Player->Actor == nullptr || !Player->Actor->IsA(ASFXPlayerController::StaticClass()))
        return false;

    MILAN_INFOW(ELOG_Matchmaking, L"lost the connection to the host");
    if (mp_state != nullptr && mp_state->Role == EMatchmakingRole::Joiner)
        Leave(*mp_state);

    static_cast<ASFXPlayerController*>(Player->Actor)->OnGameConnectionLost();
    static_cast<USFXEngine*>(Engine)->eNetworkErrorStatus = 1; // ErrorStatus_DisplayingPrompt
    return true;
}

// Like mm.server: a hosted lobby re-registers on the new server by itself.
void CMatchmaking::ApplySettings(std::string const& Url, std::string const& AccessKey)
{
    if (mp_state == nullptr)
        return;
    if (!Url.empty())
        MatchmakingNet::SetAccessKey(Url, AccessKey);
    SetMatchmaker(*mp_state, Url, L"multiplayer settings");
}

bool CMatchmaking::SetModVersion(std::string const& Version)
{
    if (mp_state == nullptr || (!Version.empty() && !PeerSecurity::IsModVersion(Version)))
        return false;
    if (mp_state->ModVersion == Version)
        return true;
    Leave(*mp_state);
    mp_state->ModVersion = Version;
    mp_state->AutoHost.Failures = 0;
    mp_state->AutoHost.NextAttempt = Clock::now();
    MILAN_INFOW(ELOG_Matchmaking, L"matchmaking mod identity: %S", Version.empty() ? "unknown (matchmaking disabled)" : Version.c_str());
    return true;
}

bool CMatchmaking::IsInSession() const
{
    UGameEngine* const Engine = GameEngine();
    UWorld* const World = CurrentWorld();
    UNetDriver* const Driver = World != nullptr ? World->NetDriver : nullptr;
    if (Engine != nullptr && Engine->GPendingLevel != nullptr)
        return true;
    if (Driver != nullptr && (Driver->ServerConnection != nullptr || Driver->ChildConnections.Count() > 0))
        return true;
    return mp_state != nullptr && (mp_state->Role == EMatchmakingRole::Joiner || !mp_state->PendingJoin.empty());
}

bool CMatchmaking::On_KickPlayer(USFXOnlineComponentBlazeGame* const Game, FUniqueNetId const& PlayerId)
{
    UWorld* const World = CurrentWorld();
    UGameEngine* const Engine = GameEngine();
    AWorldInfo* const WorldInfo = Engine != nullptr ? Engine->GetCurrentWorldInfo() : nullptr;
    static SFXName const k_lobbyGri{ L"SFXGRIMP_Lobby", 0 };
    if (mp_state == nullptr || Game == nullptr || World == nullptr || World->NetDriver == nullptr
        || WorldInfo == nullptr || WorldInfo->GRI == nullptr || !IsInstanceOf(WorldInfo->GRI, k_lobbyGri)
        || (PlayerId.Uid.A == 0 && PlayerId.Uid.B == 0))
    {
        return false;
    }

    MatchmakingState& S = *mp_state;
    APlayerReplicationInfo* const Local = LocalPRI();
    if (Local != nullptr && SamePlayerId(Local->UniqueId, PlayerId))
    {
        Game->SetKickedOutOfGame(TRUE);
        if (IsListenServer())
        {
            // Stop heartbeats without deleting the lobby: the remaining members can
            // report the departed host and use the existing host-migration path.
            ResetSession(S, true);
        }
        else
        {
            // An intentional kick must release membership before the connection closes,
            // so it cannot be treated as a lost host or rejoin during host migration.
            Leave(S);
        }
        S.PendingKickWorld = World;
        MILAN_INFOW(ELOG_Matchmaking, L"vote kick: local player %s is leaving the lobby", *Local->PlayerName);
        return true;
    }

    // A client may only handle its own directed ClientKickPlayer notification.
    if (!IsListenServer() || UNetConnection_Close_orig == nullptr)
        return false;
    UNetDriver* const Driver = World->NetDriver;
    for (UNetConnection* const Client : Driver->ChildConnections)
    {
        APlayerController* const PC = Client != nullptr ? Client->Actor : nullptr;
        APlayerReplicationInfo* const PRI = PC != nullptr ? PC->PlayerReplicationInfo : nullptr;
        if (Client == nullptr || Client->ConnectionState == USOCK_Closed || PRI == nullptr || !SamePlayerId(PRI->UniqueId, PlayerId))
            continue;
        for (PendingLobbyKick const& Kick : S.PendingKicks)
        {
            if (Kick.Driver == Driver && Kick.Connection == Client && SamePlayerId(Kick.PlayerId, PlayerId))
                return true;
        }
        UFunction* const Notify = UObject::FindObject<UFunction>(L"Function SFXGameMPContent.SFXPlayerControllerMP.ClientKickPlayer");
        if (Notify == nullptr)
        {
            MILAN_ERRORW(ELOG_Matchmaking, L"vote kick: ClientKickPlayer RPC was not found");
            return false;
        }
        // Let the reliable RPC reach the target and release its lobby membership. Close
        // the host's connection after one second even if the target ignores the RPC.
        S.PendingKicks.push_back({ Driver, Client, PlayerId, Clock::now() + std::chrono::seconds{ 1 } });
        for (auto it = S.AdmittedMembers.begin(); it != S.AdmittedMembers.end();)
        {
            if (SamePlayerId(it->second.PlayerId, PlayerId))
            {
                S.Joiners.erase(it->first);
                it = S.AdmittedMembers.erase(it);
            }
            else
                ++it;
        }
        S.NextHeartbeat = Clock::now();
        struct { FUniqueNetId kickedPlayer; } Parms{ PlayerId };
        PC->ProcessEvent(Notify, &Parms);
        MILAN_INFOW(ELOG_Matchmaking, L"vote kick: removing %s (%d-%d)", *PRI->PlayerName, PlayerId.Uid.A, PlayerId.Uid.B);
        return true;
    }
    return false;
}

bool CMatchmaking::On_ScriptCall(UStruct* const Function)
{
    if (mp_state == nullptr || Function == nullptr)
        return true;

    // Cheap name checks first; this runs for every script call.
    static SFXName const k_show{ L"ShowMultiplayerMenu", 0 };
    static SFXName const k_hide{ L"HideMultiplayerMenu", 0 };
    static SFXName const k_invite{ L"InviteFriends", 0 };
    static SFXName const k_serverUpdatePing{ L"ServerUpdatePing", 0 };
    static FunctionNamedIntercept s_show{};
    static FunctionNamedIntercept s_hide{};
    static FunctionNamedIntercept s_invite{};
    static FunctionNamedIntercept s_pauseInvite{};
    static FunctionNamedIntercept s_serverUpdatePing{};
    // Runs on the host when a client reports its ping; TickPlayerPing owns PRI.Ping instead.
    if (Function->Name == k_serverUpdatePing && s_serverUpdatePing.Applies(Function, L"Engine", L"PlayerController", L"ServerUpdatePing"))
        return false;
    if (Function->Name == k_show && s_show.Applies(Function, L"SFXGameMPContent", L"SFXLobbyFlow", L"ShowMultiplayerMenu"))
        mp_state->MenuWorld = CurrentWorld();
    else if (Function->Name == k_hide && s_hide.Applies(Function, L"SFXGameMPContent", L"SFXLobbyFlow", L"HideMultiplayerMenu"))
        mp_state->MenuWorld = nullptr;
    else if (Function->Name == k_invite &&
        (s_invite.Applies(Function, L"SFXGameMPContent", L"SFXGUI_MPLobby", L"InviteFriends") ||
         s_pauseInvite.Applies(Function, L"SFXGameMPContent", L"SFXGUI_MPPauseMenu", L"InviteFriends")))
    {
        // The original function opens the platform invite UI, which is unavailable on PC.
        MatchmakingState& S = *mp_state;
        // The lobby disables the button at size 1; this covers any other way in.
        if (IsListenServer() && HostMaxPlayers() <= 1)
            ShowInviteHint(Localize(Text::InvitesOff), 5.f);
        else if (S.Role == EMatchmakingRole::Host && !S.InviteCode.empty() && IsListenServer()
            && CanInvite(S, Clock::now()))
            ShareInvite(S);
        // A player in someone else's lobby asks the matchmaker for the code.
        else if (S.Role == EMatchmakingRole::Joiner && S.JoinedReported && !S.ReservationId.empty()
            && CanInvite(S, Clock::now()))
            RequestMemberInvite(S);
        else
            ShowInviteHint(Localize(Text::InviteCodeUnavailable), 3.f);
        return false;
    }
    return true;
}

bool CMatchmaking::StartQuickMatch(int const MapId, int const EnemyModeId, int const DifficultyId,
    std::function<void(EQuickMatchOutcome)> Done)
{
    if (mp_state == nullptr || !Done)
        return false;
    Leave(*mp_state);
    StartSearch(*mp_state, MapId, EnemyModeId, DifficultyId, std::move(Done));
    return true;
}

void CMatchmaking::CancelQuickMatch()
{
    if (mp_state != nullptr && mp_state->Role == EMatchmakingRole::Joiner
        && (mp_state->QuickMatchDone || !mp_state->SearchId.empty()) && !mp_state->JoinedReported)
        Leave(*mp_state);
}

std::wstring CMatchmaking::QuickMatchConnectString() const
{
    return mp_state != nullptr ? mp_state->QuickMatchTarget : std::wstring{};
}

void CMatchmaking::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);

    if (mp_state == nullptr)
        return;

    RetireLobbyService();
    if (mp_state->Pipe != nullptr)
        MatchmakingNet::StopControlPipe(*mp_state->Pipe);

    // Workers keep the mailbox alive through their own shared_ptr and exit on Stop.
    // A worker blocked in WinHTTP finishes its request first; it never touches this state.
    {
        std::lock_guard Lock{ mp_state->Box->Mutex };
        mp_state->Box->Stop = true;
        mp_state->Box->Wake.notify_all();
    }
    delete std::exchange(mp_state, nullptr);
}

void CMatchmaking::RegisterCommands()
{
    auto& Commands = COMPONENT(CCommands);

    // Hosted games register automatically; this re-registers now, e.g. after mm.leave.
    Commands.RegisterCommand(L"mm", L"host", [this](wchar_t const* const Params) -> bool
    {
        MILAN_UNUSED(Params);
        if (!IsListenServer() || mp_state->Socket == nullptr)
        {
            MILAN_WARNW(ELOG_Matchmaking, L"mm.host: start hosting a match first (no listen socket yet)");
            return true;
        }
        if (!mp_state->Hosted.Known)
        {
            MILAN_WARNW(ELOG_Matchmaking, L"mm.host: the lobby has not published its match settings yet");
            return true;
        }
        Leave(*mp_state);
        mp_state->AutoHost = {};
        return true;
    });

    Commands.RegisterCommand(L"mm", L"join", [this](wchar_t const* const Params) -> bool
    {
        std::string const Code = Narrow(Params);
        if (Code.empty())
        {
            MILAN_WARNW(ELOG_Matchmaking, L"usage: mm.join <invite code>");
            return true;
        }
        mp_state->PendingJoin.clear();
        Leave(*mp_state);
        StartJoin(*mp_state, Code);
        return true;
    });

    // Also stops automatic registration until this game stops hosting.
    Commands.RegisterCommand(L"mm", L"leave", [this](wchar_t const* const Params) -> bool
    {
        MILAN_UNUSED(Params);
        mp_state->AutoHost.Suppressed = IsListenServer();
        mp_state->PendingJoin.clear();
        Leave(*mp_state);
        return true;
    });

    // mm.server shows the URL, mm.server <url> sets it, and mm.server off disables matchmaking.
    Commands.RegisterCommand(L"mm", L"server", [this](wchar_t const* const Params) -> bool
    {
        std::string Url = Narrow(Params);
        Url.erase(0, Url.find_first_not_of(' '));
        Url.erase(Url.find_last_not_of(' ') + 1);
        if (Url == "off")
            SetMatchmaker(*mp_state, {}, L"console");
        else if (!Url.empty() && !MatchmakingNet::IsValidBaseUrl(Url))
            MILAN_WARNW(ELOG_Matchmaking, L"mm.server: expected an http:// or https:// URL, or 'off'");
        else if (!Url.empty())
            SetMatchmaker(*mp_state, std::move(Url), L"console");
        MILAN_INFOW(ELOG_Matchmaking, L"matchmaker: %S", mp_state->BaseUrl.empty() ? "disabled" : mp_state->BaseUrl.c_str());
        return true;
    });

    Commands.RegisterCommand(L"mm", L"version", [this](wchar_t const* const Params) -> bool
    {
        std::string value = Narrow(Params);
        auto first = value.find_first_not_of(' ');
        if (first != std::string::npos)
        {
            value = value.substr(first, value.find_last_not_of(' ') - first + 1);
            if (!SetModVersion(value == "off" ? std::string{} : value))
                MILAN_WARNW(ELOG_Matchmaking, L"mm.version: expected 1-64 printable ASCII characters without spaces, or off");
        }
        if (mp_state != nullptr)
            MILAN_INFOW(ELOG_Matchmaking, L"mod version: %S", mp_state->ModVersion.empty() ? "unknown" : mp_state->ModVersion.c_str());
        return true;
    });

    Commands.RegisterCommand(L"mm", L"status", [this](wchar_t const* const Params) -> bool
    {
        MILAN_UNUSED(Params);
        MatchmakingState const& S = *mp_state;
        static constexpr wchar_t const* k_roles[] = { L"none", L"host", L"joiner" };
        MILAN_INFOW(ELOG_Matchmaking, L"role %s, matchmaker %S, game socket port %d", k_roles[static_cast<int>(S.Role)],
            S.BaseUrl.empty() ? "disabled" : S.BaseUrl.c_str(), S.LocalPort);
        UNetDriver* const Driver = FindGameDriver();
        MILAN_UNUSED(Driver);
        MILAN_INFOW(ELOG_Matchmaking, L"  net driver %s, listen server %d, UDP socket %s, multiplayer menu %d, control pipe %s",
            Driver != nullptr ? *Driver->Class->GetName() : L"none", IsListenServer() ? 1 : 0,
            SocketOf(Driver) != nullptr ? L"found" : L"not found", S.MenuWorld != nullptr && S.MenuWorld == CurrentWorld() ? 1 : 0,
            S.Pipe ? S.PipeName.c_str() : L"none");
        if (IsListenServer())
        {
            MILAN_INFOW(ELOG_Matchmaking, L"  lobby size %d (largest allowed %d, matchmaker limit %d)", HostMaxPlayers(),
                CurrentMaxPlayersAllowed(), S.ServerMaxPlayersCeiling);
        }
        if (S.Hosted.Known)
        {
            [[maybe_unused]] bool const InLobby = HostIsInLobby();
            MILAN_INFOW(ELOG_Matchmaking, L"  hosted settings: %s, map %d, enemy %d, difficulty %d, %s, %s",
                PrivacyName(S.Hosted), S.Hosted.MapId, S.Hosted.EnemyModeId, S.Hosted.DifficultyId,
                IsJoinable(S.Hosted, InLobby) ? L"joinable" : L"not joinable", InLobby ? L"lobby" : L"in progress");
        }
        UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
        AWorldInfo* const WorldInfo = Engine != nullptr ? Engine->GetCurrentWorldInfo() : nullptr;
        if (Driver != nullptr && WorldInfo != nullptr && WorldInfo->GRI != nullptr)
        {
            for (APlayerReplicationInfo* const PRI : WorldInfo->GRI->PRIArray)
            {
                if (PRI != nullptr)
                    MILAN_INFOW(ELOG_Matchmaking, L"  player %s: ping %s", *PRI->PlayerName, DescribePing(PRI->Ping).c_str());
            }
        }
        if (!S.Presence.InfoKnown)
            MILAN_INFOW(ELOG_Matchmaking, L"  presence: not known yet (learned when this game first hosts, joins or searches)");
        else if (S.Presence.Seconds <= 0)
            MILAN_INFOW(ELOG_Matchmaking, L"  presence: off (the matchmaker does not ask for it)");
        else
            MILAN_INFOW(ELOG_Matchmaking, L"  presence: every %d s while in no lobby or search%s", S.Presence.Seconds,
                S.Presence.Reported ? L" (reporting)" : L"");
        if (S.AutoHost.Suppressed)
            MILAN_INFOW(ELOG_Matchmaking, L"  automatic registration paused by mm.leave until hosting ends (mm.host resumes)");
        else if (S.AutoHost.Failures > 0)
            MILAN_INFOW(ELOG_Matchmaking, L"  registration failed %d time(s); retrying in the background", S.AutoHost.Failures);
        if (!S.PendingJoin.empty())
            MILAN_INFOW(ELOG_Matchmaking, L"  join %S waiting for the multiplayer menu", S.PendingJoin.c_str());
        if (S.Discovery.Answered)
            MILAN_INFOW(ELOG_Matchmaking, L"  observed endpoint %S", S.Discovery.Observed.ToString().c_str());
        if (S.MigrationPending)
            MILAN_INFOW(ELOG_Matchmaking, L"  lost the host; waiting for the matchmaker to choose a new one");
        else if (S.AwaitingHostCandidates)
            MILAN_INFOW(ELOG_Matchmaking, L"  waiting for the new host's lobby to come up");
        else if (S.AdoptUntil != Clock::time_point{})
            MILAN_INFOW(ELOG_Matchmaking, L"  new host; waiting for this game's lobby to listen");
        if (S.Role == EMatchmakingRole::Host)
        {
            MILAN_INFOW(ELOG_Matchmaking, L"  lobby %S, invite %S, revision %lld", S.LobbyId.c_str(), S.InviteCode.c_str(), S.Revision);
            for (auto const& [Id, Path] : S.Joiners)
                MILAN_INFOW(ELOG_Matchmaking, L"  joiner %S: %zu candidates, verified %S, joined %d", Id.c_str(), Path.Candidates.size(),
                    Path.Verified ? Path.Verified->ToString().c_str() : "no", Path.Joined ? 1 : 0);
        }
        else if (S.Role == EMatchmakingRole::Joiner)
        {
            MILAN_INFOW(ELOG_Matchmaking, L"  reservation %S, verified host %S, joined %d", S.ReservationId.c_str(),
                S.HostPath.Verified ? S.HostPath.Verified->ToString().c_str() : "no", S.JoinedReported ? 1 : 0);
        }
        return true;
    });
}

// UE3's UWorld::TickNetClient reported a closed server connection through
// SetProgress(PMT_ConnectionFailure). LE3's UWorld::Tick no longer calls it, because ME3
// learned of a departed host from Blaze, so a client never noticed. A lobby member asks the
// matchmaker for a new host first; otherwise, or if that fails, the loss is reported the same
// way as UE3 did, once per connection, and On_ConnectionError shows the host-left prompt.
void CMatchmaking::TickLostHost()
{
    UWorld* const World = CurrentWorld();
    UNetConnection* const Server = (World != nullptr && World->NetDriver != nullptr) ? World->NetDriver->ServerConnection : nullptr;
    if (Server == nullptr || Server->ConnectionState != USOCK_Closed)
    {
        mp_lostHost = nullptr;
        return;
    }

    UGameEngine* const Engine = GameEngine();
    if (Server == mp_lostHost || Engine == nullptr || Engine->GPendingLevel != nullptr || Engine->TravelURL.Length() > 0
        || Engine->GameViewport == nullptr || !Engine->GameViewport->IsA(USFXGameViewportClient::StaticClass()))
    {
        return;
    }

    mp_lostHost = Server;
    if (mp_state != nullptr && BeginMigration(*mp_state))
        return;
    MILAN_INFOW(ELOG_Matchmaking, L"connection to the host closed; reporting a connection failure");
    ReportConnectionLost();
}

void CMatchmaking::On_GameEngineTick(float const Delta)
{
    MILAN_UNUSED(Delta);
    if (mp_state != nullptr && mp_state->PendingKickWorld != nullptr)
    {
        UWorld* const KickedWorld = std::exchange(mp_state->PendingKickWorld, nullptr);
        if (KickedWorld == CurrentWorld())
        {
            if (!RunConsoleCommand(L"disconnect"))
                MILAN_ERRORW(ELOG_Matchmaking, L"vote kick: no local player controller to disconnect");
            return;
        }
    }
    TickLostHost();
    if (mp_state == nullptr)
        return;

    MatchmakingState& S = *mp_state;
    S.GameThreadId = ::GetCurrentThreadId();

    DrainCompletions(S);
    TrackGameSocket(S);

    Clock::time_point const Now = Clock::now();
    TickPendingKicks(S, Now);
    TickPlayerPing(S, Now);
    TickHosting(S, Now);
    TickPresence(S, Now);
    TickPendingJoin(S);
    TickDiscovery(S, Now);
    UpdatePauseInviteAvailability(S, Now);

    if (S.Role == EMatchmakingRole::Host && !S.LobbyId.empty())
    {
        if (IsHosting(S))
            TickHostedLobby(S, Now);
    }
    else if (S.Role == EMatchmakingRole::Joiner)
    {
        TickJoiner(S, Now);
        TickProbes(S, S.HostPath, Rendezvous::EDirection::JoinerToHost, Now);
    }

    PublishLobbyServiceState(S);
}

bool CMatchmaking::On_ReceivedDatagram(FSocket* const Socket, BYTE const* const Data, INT const Length, SOCKADDR_IN const* const Source)
{
    if (Length <= 0)
        return false;

    Rendezvous::EPacket const Kind = Rendezvous::Classify(Data, static_cast<std::size_t>(Length));
    if (Kind == Rendezvous::EPacket::None)
        return false;

    // Our datagrams are always hidden from UE3, but state is only touched on the game thread.
    if (mp_state == nullptr || Source == nullptr || ::GetCurrentThreadId() != mp_state->GameThreadId)
        return true;

    MatchmakingState& S = *mp_state;
    std::span<std::uint8_t const> const Packet{ Data, static_cast<std::size_t>(Length) };
    Endpoint const From{ Source->sin_addr.s_addr, ntohs(Source->sin_port), {} };

    if (Kind == Rendezvous::EPacket::DiscoveryResponse)
    {
        std::uint64_t Sequence = 0;
        Endpoint Observed{};
        if (!S.Discovery.Ticket.Valid || !(From.Address == S.ServerAddress && From.Port == S.ServerUdpPort)
            || !Rendezvous::ReadDiscoveryResponse(Packet, S.Discovery.Ticket.Id, S.Discovery.Ticket.Secret, Sequence, Observed.Address, Observed.Port)
            || Sequence > S.Discovery.Sequence)
        {
            return true;
        }

        bool const Changed = !S.Discovery.Answered || !(Observed == S.Discovery.Observed);
        S.Discovery.Answered = true;
        S.Discovery.Unanswered = 0;
        S.Discovery.Observed = Observed;
        if (Changed)
        {
            // Matching ports on a local test show the request left the UE3 socket itself.
            MILAN_INFOW(ELOG_Matchmaking, L"discovery: server saw %S for game socket port %d (%s)",
                Observed.ToString().c_str(), S.LocalPort,
                Observed.Port == S.LocalPort ? L"same port" : L"port remapped by NAT");
        }
        return true;
    }

    Rendezvous::ProbeHeader Header{};
    if (!Rendezvous::PeekProbe(Packet, Header))
        return true;

    Rendezvous::EDirection const Expected = S.Role == EMatchmakingRole::Host ? Rendezvous::EDirection::JoinerToHost
        : Rendezvous::EDirection::HostToJoiner;
    Rendezvous::EDirection const Reply = S.Role == EMatchmakingRole::Host ? Rendezvous::EDirection::HostToJoiner
        : Rendezvous::EDirection::JoinerToHost;

    PeerPath* Path = nullptr;
    if (S.Role == EMatchmakingRole::Host)
    {
        for (auto& [Id, Candidate] : S.Joiners)
            if (Candidate.Pair == Header.Pair)
                Path = &Candidate;
    }
    else if (S.Role == EMatchmakingRole::Joiner && !S.ReservationId.empty() && S.HostPath.Pair == Header.Pair)
    {
        Path = &S.HostPath;
    }

    bool const IsProbe = Header.Type == Rendezvous::EProbeType::Probe;
    if (Path == nullptr || (IsProbe ? Header.Direction != Expected : Header.Direction != Reply)
        || !Rendezvous::VerifyProbe(Packet, Path->TraversalKey))
    {
        return true;
    }

    if (IsProbe)
    {
        std::array<std::uint8_t, Rendezvous::k_probeLength> Ack{};
        Rendezvous::WriteProbe(Ack, Rendezvous::EProbeType::Ack, Expected, Path->Pair, Header.Sequence, Path->TraversalKey);
        SendDatagram(Socket, Ack, From.Address, From.Port);
        if (!Path->ProbedBy || !(*Path->ProbedBy == From))
        {
            Path->ProbedBy = From;
            MILAN_INFOW(ELOG_Matchmaking, L"probe received from %S (reservation %S)", From.ToString().c_str(), Path->ReservationId.c_str());
        }
    }
    else if (Header.Sequence <= Path->ProbeSequence && !Path->Verified)
    {
        Path->Verified = From;
        MILAN_INFOW(ELOG_Matchmaking, L"path verified: %S answered our probe (reservation %S)", From.ToString().c_str(), Path->ReservationId.c_str());
    }
    return true;
}
