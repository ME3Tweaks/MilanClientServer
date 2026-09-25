#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Components/CCommands.hpp"
#include "Components/CMatchmaking.hpp"
#include "Engine/Hooks.hpp"
#include "Online/MatchmakingNet.hpp"
#include "Online/Rendezvous.hpp"
#include "Shell/ClientConfig.hpp"
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

    constexpr int k_protocolVersion = 1;
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

    std::string GameVersion()
    {
        return std::string{ "milanhost-" } + VERSION_STRING_A;
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

    std::vector<Endpoint> ParseCandidates(json const& Array)
    {
        std::vector<Endpoint> Result{};
        for (auto const& Candidate : Array)
        {
            if (auto Parsed = ParseCandidate(Candidate); Parsed && Result.size() < 8)
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

    std::string DescribeFailure(MatchmakingNet::HttpResult const& Result)
    {
        if (Result.Status == 0)
            return Result.Error;

        json const Body = json::parse(Result.Body, nullptr, false);
        if (Body.is_object() && Body.contains("code") && Body["code"].is_string())
            return "HTTP " + std::to_string(Result.Status) + " " + Body["code"].get<std::string>();
        return "HTTP " + std::to_string(Result.Status);
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

struct MatchmakingState
{
    std::shared_ptr<Mailbox>        Box{ std::make_shared<Mailbox>() };
    std::string                     BaseUrl{};
    std::uint64_t                   Generation{ 0 };
    EMatchmakingRole                Role{ EMatchmakingRole::None };
    DWORD                           GameThreadId{ 0 };

    // From /v1/info and DNS.
    bool                            InfoReady{ false };
    std::uint32_t                   ServerAddress{ 0 };
    std::uint16_t                   ServerUdpPort{ 0 };
    int                             HeartbeatSeconds{ 20 };

    // Current UE3 game socket.
    FSocket*                        Socket{ nullptr };
    int                             LocalPort{ -1 };

    DiscoveryState                  Discovery{};

    // Host.
    std::string                     LobbyId{};
    std::string                     HostKey{};
    std::string                     InviteCode{};
    long long                       Revision{ 0 };
    std::string                     LastStateBody{};
    Clock::time_point               NextHeartbeat{};
    Clock::time_point               NextStateCheck{};
    long long                       IntroductionsVersion{ -1 };
    std::vector<std::string>        LanAddresses{};
    std::map<std::string, PeerPath> Joiners{};

    // Joiner.
    std::string                     ReservationId{};
    std::string                     JoinKey{};
    std::string                     SearchId{};
    std::string                     SearchKey{};
    std::wstring                    QuickMatchTarget{};
    std::function<void(bool)>       QuickMatchDone{};
    PeerPath                        HostPath{};
    bool                            TravelIssued{ false };
    int                             ReportedPort{ -1 };
    bool                            JoinedReported{ false };
    long long                       ReservationVersion{ -1 };
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
    void ResetSession(MatchmakingState& S)
    {
        std::string const BaseUrl = S.BaseUrl;
        std::shared_ptr<Mailbox> const Box = S.Box;
        std::uint64_t const Generation = S.Generation + 1;
        DWORD const GameThreadId = S.GameThreadId;
        FSocket* const Socket = S.Socket;
        int const LocalPort = S.LocalPort;

        S = MatchmakingState{};
        S.Box = Box;
        S.BaseUrl = BaseUrl;
        S.Generation = Generation;
        S.GameThreadId = GameThreadId;
        S.Socket = Socket;
        S.LocalPort = LocalPort;
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

    // Lobby state is a placeholder until the menu integration reads the real settings.
    json BuildLobbyState(MatchmakingState const& S)
    {
        json State{
            { "isPrivate", false },
            { "joinable", true },
            { "mapId", 0 },
            { "enemyModeId", 0 },
            { "difficultyId", 0 },
            { "playerCount", std::min(4, 1 + ConnectedClientCount()) },
            { "maxPlayers", 4 },
            { "phase", "Lobby" },
            { "waveNumber", 0 },
            { "gamePort", S.LocalPort },
            { "lanAddresses", S.LanAddresses },
        };
        return State;
    }

    void PollIntroductions(MatchmakingState& S, std::chrono::milliseconds Delay = {});

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

            std::vector<Endpoint> Candidates = ParseCandidates(Introduction.at("joinerCandidates"));
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
                MILAN_WARNW(ELOG_Matchmaking, L"lobby is gone from the matchmaker; run mm.host again");
                ResetSession(State);
                return;
            }
            if (std::optional<json> const List = SuccessJson(Result, "introductions poll"))
            {
                ApplyIntroductions(State, *List);
                PollIntroductions(State);
                return;
            }
            PollIntroductions(State, k_pollRetryDelay);
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
                    MILAN_WARNW(ELOG_Matchmaking, L"lobby expired on the matchmaker; run mm.host again");
                    ResetSession(Session);
                    return;
                }
                SuccessJson(Result, "lobby heartbeat");
            });
    }

    void StartHost(MatchmakingState& S)
    {
        ResetSession(S);
        S.Role = EMatchmakingRole::Host;
        S.LanAddresses = MatchmakingNet::LanIPv4Addresses(4);

        FetchInfo(S, [](MatchmakingState& State)
        {
            json State0 = BuildLobbyState(State);
            State.LastStateBody = State0.dump();
            State.Revision = 1;
            State0["revision"] = State.Revision;
            json const Request{ { "gameVersion", GameVersion() }, { "state", State0 } };

            PostHttp(State, false, L"POST", "/v1/lobbies", "", Request.dump(), [](MatchmakingState& Session, MatchmakingNet::HttpResult const& Result)
            {
                std::optional<json> const Created = SuccessJson(Result, "lobby creation");
                if (!Created)
                    return;

                Session.LobbyId = Created->at("lobbyId").get<std::string>();
                Session.HostKey = Created->at("hostKey").get<std::string>();
                Session.InviteCode = Created->at("inviteCode").get<std::string>();
                Session.Discovery.Ticket = UdpTicket::FromJson(Created->at("udpTicket"));
                Session.Discovery.Restart();
                Session.NextHeartbeat = Clock::now() + std::chrono::seconds(Session.HeartbeatSeconds);

                MILAN_INFOW(ELOG_Matchmaking, L"lobby registered; invite code %S (game port %d)", Session.InviteCode.c_str(), Session.LocalPort);
                PollIntroductions(Session);
            });
        });
    }

    std::optional<Endpoint> ChooseTravelTarget(std::vector<Endpoint> const& Candidates, bool const SamePublicAddress)
    {
        auto const Find = [&](char const* Kind) -> std::optional<Endpoint>
        {
            for (auto const& Candidate : Candidates)
                if (Candidate.Kind == Kind)
                    return Candidate;
            return std::nullopt;
        };

        // A loopback observation only occurs when the host contacted a matchmaker on
        // this same machine; use it for the local two-instance test.
        if (SamePublicAddress)
            if (auto Observed = Find("PublicObserved"); Observed &&
                reinterpret_cast<std::uint8_t const*>(&Observed->Address)[0] == 127)
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
        return Candidates.empty() ? std::nullopt : std::optional<Endpoint>{ Candidates.front() };
    }

    void PollReservation(MatchmakingState& S, std::chrono::milliseconds Delay = {});

    void PollReservation(MatchmakingState& S, std::chrono::milliseconds const Delay)
    {
        std::string const Path = "/v1/reservations/" + S.ReservationId + "/poll?after=" + std::to_string(S.ReservationVersion);
        PostHttp(S, true, L"POST", Path, S.JoinKey, "", [](MatchmakingState& State, MatchmakingNet::HttpResult const& Result)
        {
            if (Result.Status == 404)
            {
                if (!State.JoinedReported)
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
            std::vector<Endpoint> Candidates = ParseCandidates(Status->at("hostCandidates"));
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
            PollReservation(State);
        }, Delay);
    }

    void FinishQuickMatch(MatchmakingState& S, bool const Success)
    {
        auto Done = std::move(S.QuickMatchDone);
        S.QuickMatchDone = {};
        if (Done)
            Done(Success);
    }

    bool AcceptReservation(MatchmakingState& S, json const& Reservation, bool const QuickMatch)
    {
        S.ReservationId = Reservation.at("reservationId").get<std::string>();
        S.JoinKey = Reservation.at("joinKey").get<std::string>();
        S.Discovery.Ticket = UdpTicket::FromJson(Reservation.at("udpTicket"));
        if (!Rendezvous::DecodeBase64Url(Reservation.at("traversalKey").get<std::string>(), S.HostPath.TraversalKey))
        {
            MILAN_ERRORW(ELOG_Matchmaking, L"reservation has a malformed traversal key");
            return false;
        }
        S.HostPath.ReservationId = S.ReservationId;
        S.HostPath.Pair = Rendezvous::DerivePairId(S.HostPath.TraversalKey);
        S.HostPath.Candidates = ParseCandidates(Reservation.at("hostCandidates"));
        S.HostPath.ProbeUntil = Clock::now() + k_probeWindow;
        S.HostPath.NextProbe = Clock::now();
        bool const SamePublicAddress = Reservation.at("samePublicAddress").get<bool>();

        for (auto const& Candidate : S.HostPath.Candidates)
        {
            MILAN_UNUSED(Candidate);
            MILAN_INFOW(ELOG_Matchmaking, L"host candidate %S (%S)", Candidate.ToString().c_str(), Candidate.Kind.c_str());
        }

        std::optional<Endpoint> const Target = ChooseTravelTarget(S.HostPath.Candidates, SamePublicAddress);
        if (!Target)
        {
            MILAN_ERRORW(ELOG_Matchmaking, L"reservation has no usable host candidates");
            return false;
        }

        MILAN_INFOW(ELOG_Matchmaking, L"joining via %S (%S, same public address: %d)",
            Target->ToString().c_str(), Target->Kind.c_str(), SamePublicAddress ? 1 : 0);
        if (QuickMatch)
        {
            S.QuickMatchTarget = Widen(Target->ToString());
            S.TravelIssued = true; // The script callback issues the travel command.
            PollReservation(S);
            FinishQuickMatch(S, true);
        }
        else
        {
            S.TravelIssued = RunConsoleCommand(L"open " + Widen(Target->ToString()));
            if (!S.TravelIssued)
                MILAN_ERRORW(ELOG_Matchmaking, L"no local player controller to run 'open'");
            PollReservation(S);
        }
        return true;
    }

    void PollSearch(MatchmakingState& S, std::chrono::milliseconds Delay = {});

    void HandleSearchStatus(MatchmakingState& S, json const& Status)
    {
        std::string const State = Status.at("state").get<std::string>();
        if (State == "Matched")
        {
            if (!AcceptReservation(S, Status.at("reservation"), true))
                FinishQuickMatch(S, false);
        }
        else if (State == "Searching")
        {
            int const Wait = std::clamp(Status.value("pollAfterMilliseconds", 2000), 100, 10000);
            PollSearch(S, std::chrono::milliseconds{ Wait });
        }
        else
        {
            MILAN_INFOW(ELOG_Matchmaking, L"quick match found no lobby");
            FinishQuickMatch(S, false);
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
                    FinishQuickMatch(State, false);
            }, Delay);
    }

    void StartSearch(MatchmakingState& S, int const MapId, int const EnemyModeId, int const DifficultyId,
        std::function<void(bool)> Done)
    {
        ResetSession(S);
        S.Role = EMatchmakingRole::Joiner;
        S.QuickMatchDone = std::move(Done);
        MILAN_INFOW(ELOG_Matchmaking, L"quick match search: map %d, enemy %d, difficulty %d", MapId, EnemyModeId, DifficultyId);
        FetchInfo(S, [MapId, EnemyModeId, DifficultyId](MatchmakingState& State)
        {
            json const Request{
                { "gameVersion", GameVersion() },
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
                        FinishQuickMatch(Session, false);
                        return;
                    }
                    Session.SearchId = Created->at("searchId").get<std::string>();
                    Session.SearchKey = Created->at("searchKey").get<std::string>();
                    HandleSearchStatus(Session, Created->at("status"));
                });
        }, [](MatchmakingState& State) { FinishQuickMatch(State, false); });
    }

    void StartJoin(MatchmakingState& S, std::string InviteCode)
    {
        ResetSession(S);
        S.Role = EMatchmakingRole::Joiner;

        FetchInfo(S, [InviteCode](MatchmakingState& State)
        {
            json const Request{ { "inviteCode", InviteCode }, { "gameVersion", GameVersion() } };
            PostHttp(State, false, L"POST", "/v1/invites/redeem", "", Request.dump(), [](MatchmakingState& Session, MatchmakingNet::HttpResult const& Result)
            {
                std::optional<json> const Reservation = SuccessJson(Result, "invite redemption");
                if (!Reservation)
                    return;

                AcceptReservation(Session, *Reservation, false);
            });
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

        // The delete requests above complete under the new generation and are ignored.
        ResetSession(S);
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

    void TickJoiner(MatchmakingState& S)
    {
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

        UGameEngine* const Engine = GameEngine();
        UNetDriver* const WorldDriver = (GWorld != nullptr && *GWorld != nullptr) ? (*GWorld)->NetDriver : nullptr;
        if (!S.JoinedReported && Engine != nullptr && Engine->GPendingLevel == nullptr
            && WorldDriver != nullptr && WorldDriver->ServerConnection != nullptr)
        {
            S.JoinedReported = true;
            MILAN_INFOW(ELOG_Matchmaking, L"joined the host's game; releasing the reservation");
            PostHttp(S, false, L"POST", "/v1/reservations/" + S.ReservationId + "/joined", S.JoinKey, "",
                [](MatchmakingState&, MatchmakingNet::HttpResult const&) {});
        }
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
    mp_state->BaseUrl = (gp_clientConfig != nullptr) ? std::string{ gp_clientConfig->MatchmakerUrl.c_str() } : std::string{ "http://127.0.0.1:5080" };

    std::thread{ WorkerLoop, mp_state->Box, false }.detach();
    std::thread{ WorkerLoop, mp_state->Box, true }.detach();

    RegisterCommands();

    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized (matchmaker %S)", k_typeName, mp_state->BaseUrl.c_str());
    return true;
}

bool CMatchmaking::StartQuickMatch(int const MapId, int const EnemyModeId, int const DifficultyId,
    std::function<void(bool)> Done)
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

    Commands.RegisterCommand(L"mm", L"host", [this](wchar_t const* const Params) -> bool
    {
        MILAN_UNUSED(Params);
        if (!IsListenServer() || mp_state->Socket == nullptr)
        {
            MILAN_WARNW(ELOG_Matchmaking, L"mm.host: start hosting a match first (no listen socket yet)");
            return true;
        }
        Leave(*mp_state);
        StartHost(*mp_state);
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
        Leave(*mp_state);
        StartJoin(*mp_state, Code);
        return true;
    });

    Commands.RegisterCommand(L"mm", L"leave", [this](wchar_t const* const Params) -> bool
    {
        MILAN_UNUSED(Params);
        Leave(*mp_state);
        return true;
    });

    Commands.RegisterCommand(L"mm", L"server", [this](wchar_t const* const Params) -> bool
    {
        std::string const Url = Narrow(Params);
        if (!Url.empty())
        {
            Leave(*mp_state);
            mp_state->BaseUrl = Url;
        }
        MILAN_INFOW(ELOG_Matchmaking, L"matchmaker: %S", mp_state->BaseUrl.c_str());
        return true;
    });

    Commands.RegisterCommand(L"mm", L"status", [this](wchar_t const* const Params) -> bool
    {
        MILAN_UNUSED(Params);
        MatchmakingState const& S = *mp_state;
        static constexpr wchar_t const* k_roles[] = { L"none", L"host", L"joiner" };
        MILAN_INFOW(ELOG_Matchmaking, L"role %s, matchmaker %S, game socket port %d", k_roles[static_cast<int>(S.Role)], S.BaseUrl.c_str(), S.LocalPort);
        UNetDriver* const Driver = FindGameDriver();
        MILAN_UNUSED(Driver);
        MILAN_INFOW(ELOG_Matchmaking, L"  net driver %s, listen server %d, UDP socket %s",
            Driver != nullptr ? *Driver->Class->GetName() : L"none", IsListenServer() ? 1 : 0,
            SocketOf(Driver) != nullptr ? L"found" : L"not found");
        if (S.Discovery.Answered)
            MILAN_INFOW(ELOG_Matchmaking, L"  observed endpoint %S", S.Discovery.Observed.ToString().c_str());
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

void CMatchmaking::On_GameEngineTick(float const Delta)
{
    MILAN_UNUSED(Delta);
    if (mp_state == nullptr)
        return;

    MatchmakingState& S = *mp_state;
    S.GameThreadId = ::GetCurrentThreadId();

    DrainCompletions(S);
    TrackGameSocket(S);

    Clock::time_point const Now = Clock::now();
    TickDiscovery(S, Now);

    if (S.Role == EMatchmakingRole::Host && !S.LobbyId.empty())
    {
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
    else if (S.Role == EMatchmakingRole::Joiner)
    {
        TickJoiner(S);
        TickProbes(S, S.HostPath, Rendezvous::EDirection::JoinerToHost, Now);
    }
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
