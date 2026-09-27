// Desync snapshots only exist in DEBUG builds; see CDesync.hpp.
#ifdef _MLN_DEBUG

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>
#include <tomlplusplus/toml.hpp>

#include "Components/CCommands.hpp"
#include "Components/CDesync.hpp"


using json = nlohmann::json;

namespace
{
    constexpr int k_formatVersion = 1;
    constexpr unsigned long long k_cpfNet = 0x20;
    constexpr int k_maxStructDepth = 6;
    constexpr float k_helloInterval = 10.0f;

    constexpr wchar_t const* k_desyncConfigFilename = L"./MilanDesync.toml";
    constexpr std::wstring_view k_mutatePrefix = L"MilanDesync ";

    // Actor properties written for every actor even when they are not replicated.
    constexpr std::string_view k_coreProperties[] = {
        "Location", "Rotation", "Velocity", "Physics", "Role", "RemoteRole", "bHidden", "bTearOff",
        "bCollideActors", "bBlockActors", "Base", "Owner", "DrawScale",
    };

    /** Layout of any TArray in memory. */
    struct ScriptArray
    {
        BYTE*   Data;
        int     Count;
        int     Max;
    };

    std::string Utf8(wchar_t const* const Text, int const Length)
    {
        if (Text == nullptr || Length <= 0)
            return {};

        int const Size = ::WideCharToMultiByte(CP_UTF8, 0, Text, Length, nullptr, 0, nullptr, nullptr);
        std::string Result(static_cast<size_t>(std::max(Size, 0)), '\0');
        if (Size > 0)
            ::WideCharToMultiByte(CP_UTF8, 0, Text, Length, Result.data(), Size, nullptr, nullptr);
        return Result;
    }

    std::string Utf8(FString const& Text)
    {
        return Utf8(Text.Chars(), static_cast<int>(Text.Length()));
    }

    std::wstring Wide(std::string_view const Text)
    {
        if (Text.empty())
            return {};

        int const Size = ::MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), nullptr, 0);
        std::wstring Result(static_cast<size_t>(std::max(Size, 0)), L'\0');
        if (Size > 0)
            ::MultiByteToWideChar(CP_UTF8, 0, Text.data(), static_cast<int>(Text.size()), Result.data(), Size);
        return Result;
    }

    std::string NameOf(UObject const* const Object)
    {
        return Utf8(Object->Name.ToString());
    }

    std::string BasicNameOf(UObject const* const Object)
    {
        return Utf8(Object->Name.ToString(SFXName::k_formatBasic));
    }

    double Round3(float const Value)
    {
        return std::round(static_cast<double>(Value) * 1000.0) / 1000.0;
    }

    SFXName const& MessageType()
    {
        static SFXName const Name{ L"MilanDesync", 0 };
        return Name;
    }

    UWorld* CurrentWorld()
    {
        return (GWorld != nullptr) ? *GWorld : nullptr;
    }

    UNetDriver* WorldDriver()
    {
        UWorld* const World = CurrentWorld();
        return World != nullptr ? World->NetDriver : nullptr;
    }

    bool IsClient()
    {
        UNetDriver* const Driver = WorldDriver();
        return Driver != nullptr && Driver->ServerConnection != nullptr;
    }

    bool IsHost()
    {
        UNetDriver* const Driver = WorldDriver();
        return Driver != nullptr && Driver->ServerConnection == nullptr;
    }

    char const* SideName()
    {
        return IsClient() ? "client" : IsHost() ? "host" : "standalone";
    }

    AWorldInfo* CurrentWorldInfo()
    {
        UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
        return Engine != nullptr ? Engine->GetCurrentWorldInfo() : nullptr;
    }

    APlayerController* LocalController()
    {
        UEngine* const Engine = (GEngine != nullptr) ? *GEngine : nullptr;
        if (Engine == nullptr || Engine->GamePlayers.Count() == 0 || Engine->GamePlayers(0) == nullptr)
            return nullptr;
        return Engine->GamePlayers(0)->Actor;
    }

    int PlayerIdOf(APlayerController const* const Controller)
    {
        return (Controller != nullptr && Controller->PlayerReplicationInfo != nullptr)
            ? Controller->PlayerReplicationInfo->PlayerID : -1;
    }

    /** Maps each actor with an open channel on the connection to that channel's index. */
    std::unordered_map<AActor const*, int> ChannelsOf(UNetConnection* const Connection)
    {
        std::unordered_map<AActor const*, int> Channels{};
        if (Connection == nullptr)
            return Channels;

        for (UChannel* const Channel : Connection->OpenChannels)
        {
            if (Channel == nullptr)
                continue;
            auto* const ActorChannel = Channel->Cast<UActorChannel>();
            if (ActorChannel != nullptr && ActorChannel->Actor != nullptr)
                Channels[ActorChannel->Actor] = Channel->ChannelIndex;
        }
        return Channels;
    }

    /**
     * Writes an object reference in a form both machines can compare. Actors are written by
     * local name ("@Name") and mapped to the other side by the diff tool; components are
     * written relative to their owning actor; assets are written by full path.
     */
    json ObjectRef(UObject* const Object)
    {
        if (Object == nullptr)
            return nullptr;

        if (Object->IsA<AActor>())
            return "@" + NameOf(Object);

        if (Object->Outer != nullptr && Object->Outer->IsA<AActor>())
            return "@" + NameOf(Object->Outer) + "/" + BasicNameOf(Object->Class);

        static SFXName const k_transient{ L"Transient", 0 };
        UPackage const* const Package = Object->GetOutermost();
        if (Package != nullptr && Package->Name == k_transient)
            return "~" + BasicNameOf(Object->Class);

        return Utf8(Object->GetFullName());
    }

    bool IsSkippedPropertyClass(UClass const* const Class)
    {
        return Class == UDelegateProperty::StaticClass()
            || Class == UMapProperty::StaticClass()
            || Class == UInterfaceProperty::StaticClass();
    }

    json ReadProperty(UProperty* Property, BYTE const* Data, int Depth, int ArrayLimit);

    json ReadElement(UProperty* const Property, BYTE const* const Data, int const Depth, int const ArrayLimit)
    {
        UClass* const Class = Property->Class;

        if (Class == UBoolProperty::StaticClass())
            return (*reinterpret_cast<DWORD const*>(Data) & static_cast<UBoolProperty*>(Property)->BitMask) != 0;

        if (Class == UByteProperty::StaticClass())
        {
            BYTE const Value = *Data;
            UEnum const* const Enum = static_cast<UByteProperty*>(Property)->Enum;
            if (Enum != nullptr && Value < Enum->Names.Count())
                return Utf8(Enum->Names(Value).ToString(SFXName::k_formatBasic));
            return Value;
        }

        if (Class == UIntProperty::StaticClass() || Class == UStringRefProperty::StaticClass())
            return *reinterpret_cast<int const*>(Data);

        if (Class == UFloatProperty::StaticClass())
            return Round3(*reinterpret_cast<float const*>(Data));

        if (Class == UNameProperty::StaticClass())
            return Utf8(reinterpret_cast<SFXName const*>(Data)->ToString());

        if (Class == UStrProperty::StaticClass())
            return Utf8(*reinterpret_cast<FString const*>(Data));

        if (Property->IsA<UObjectProperty>())
            return ObjectRef(*reinterpret_cast<UObject* const*>(Data));

        if (Class == UStructProperty::StaticClass())
        {
            if (Depth >= k_maxStructDepth)
                return "<deep>";

            json Object = json::object();
            UStruct* const Struct = static_cast<UStructProperty*>(Property)->Struct;
            for (UProperty* Inner = Struct != nullptr ? Struct->PropertyLink : nullptr; Inner != nullptr; Inner = Inner->PropertyLinkNext)
            {
                if (!IsSkippedPropertyClass(Inner->Class))
                    Object[BasicNameOf(Inner)] = ReadProperty(Inner, Data + Inner->Offset, Depth + 1, ArrayLimit);
            }
            return Object;
        }

        if (Class == UArrayProperty::StaticClass())
        {
            if (Depth >= k_maxStructDepth)
                return "<deep>";

            auto const* const Array = reinterpret_cast<ScriptArray const*>(Data);
            UProperty* const Inner = static_cast<UArrayProperty*>(Property)->Inner;
            int const Count = std::min(Array->Count, ArrayLimit);

            json Items = json::array();
            for (int Index = 0; Index < Count && Inner != nullptr && Array->Data != nullptr; ++Index)
                Items.push_back(ReadElement(Inner, Array->Data + static_cast<size_t>(Index) * Inner->ElementSize, Depth + 1, ArrayLimit));

            if (Array->Count > ArrayLimit)
                return json{ { "count", Array->Count }, { "head", std::move(Items) } };
            return Items;
        }

        return "<" + BasicNameOf(Class) + ">";
    }

    json ReadProperty(UProperty* const Property, BYTE const* const Data, int const Depth, int const ArrayLimit)
    {
        if (Property->ArrayDim <= 1)
            return ReadElement(Property, Data, Depth, ArrayLimit);

        json Items = json::array();
        for (int Index = 0; Index < Property->ArrayDim; ++Index)
            Items.push_back(ReadElement(Property, Data + static_cast<size_t>(Index) * Property->ElementSize, Depth, ArrayLimit));
        return Items;
    }

    UProperty* FindProperty(UClass* const Class, SFXName const Name)
    {
        for (UProperty* Property = Class->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
        {
            if (Property->Name == Name)
                return Property;
        }
        return nullptr;
    }

    /** Follows a dotted path like "Mesh.PhysicsWeight" through object properties. */
    json ReadPath(UObject* const Root, std::vector<SFXName> const& Segments, int const ArrayLimit)
    {
        UObject* Current = Root;
        for (size_t Index = 0; Index < Segments.size(); ++Index)
        {
            if (Current == nullptr)
                return nullptr;

            UProperty* const Property = FindProperty(Current->Class, Segments[Index]);
            if (Property == nullptr)
                return "<missing>";

            BYTE const* const Data = reinterpret_cast<BYTE const*>(Current) + Property->Offset;
            if (Index + 1 == Segments.size())
                return ReadProperty(Property, Data, 0, ArrayLimit);

            if (!Property->IsA<UObjectProperty>())
                return "<not an object>";

            Current = *reinterpret_cast<UObject* const*>(Data);
        }
        return nullptr;
    }

    bool ShouldCapture(AActor const* const Actor, bool const bClient)
    {
        if (Actor == nullptr || Actor->bStatic)
            return false;

        // Clients keep their own authoritative actors of replicated classes, which catches
        // local spawns that duplicate or outlive the host's copy.
        if (bClient)
            return Actor->Role != ROLE_Authority || Actor->RemoteRole != ROLE_None;

        return Actor->RemoteRole != ROLE_None;
    }
}


// ! CDesync implementation.
// ========================================

bool CDesync::Initialize(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);

    LoadConfig();
    RegisterCommands();

    MILAN_DEBUGW(ELOG_Startup, L"component '%s' initialized", k_typeName);
    return true;
}

void CDesync::Terminate(::LESDK::Initializer& Context)
{
    MILAN_UNUSED(Context);
    MILAN_DEBUGW(ELOG_Teardown, L"component '%s' terminating", k_typeName);
    CloseFile();
}

void CDesync::RegisterCommands()
{
    auto& Commands = COMPONENT(CCommands);

    Commands.RegisterCommand(L"desync", L"snap",
        [this](wchar_t const*)
        {
            if (IsClient())
                RequestHostSnapshot();
            else
                mp_hostSnapOrigin = "manual";
            return true;
        });

    Commands.RegisterCommand(L"desync", L"start",
        [this](wchar_t const* const Params)
        {
            if (IsClient())
            {
                MILAN_WARNW(ELOG_Desync, L"desync.start runs on the host; use desync.snap to request a single snapshot");
                return true;
            }

            LoadConfig();
            float const Requested = (Params != nullptr && *Params != L'\0') ? std::wcstof(Params, nullptr) : 0.0f;
            m_interval = Requested > 0.0f ? Requested : m_defaultInterval;
            m_timer = 0.0f;
            mp_hostSnapOrigin = "timer";

            MILAN_INFOW(ELOG_Desync, L"desync: snapshots every %.2f s", m_interval);
            return true;
        });

    Commands.RegisterCommand(L"desync", L"stop",
        [this](wchar_t const*)
        {
            m_interval = 0.0f;
            MILAN_INFOW(ELOG_Desync, L"desync: periodic snapshots stopped");
            return true;
        });

    Commands.RegisterCommand(L"desync", L"mark",
        [this](wchar_t const* const Params)
        {
            if (!OpenFile())
                return true;

            AWorldInfo* const WorldInfo = CurrentWorldInfo();
            json Mark{
                { "type", "mark" },
                { "side", SideName() },
                { "afterSeq", m_lastSequence },
                { "time", WorldInfo != nullptr ? Round3(WorldInfo->TimeSeconds) : 0.0 },
                { "note", Utf8(Params, static_cast<int>(std::wcslen(Params))) },
            };
            WriteLine(Mark.dump(-1, ' ', false, json::error_handler_t::replace));

            // A mark usually means "the bug is on screen now", so capture it on both sides.
            if (IsClient())
                RequestHostSnapshot();
            else
                mp_hostSnapOrigin = "mark";

            MILAN_INFOW(ELOG_Desync, L"desync: mark after seq %d: %s", m_lastSequence, Params);
            return true;
        });

    Commands.RegisterCommand(L"desync", L"reload",
        [this](wchar_t const*)
        {
            LoadConfig();
            return true;
        });

    Commands.RegisterCommand(L"desync", L"close",
        [this](wchar_t const*)
        {
            m_interval = 0.0f;
            CloseFile();
            return true;
        });
}

void CDesync::LoadConfig()
{
    m_extraConfig.clear();
    m_plans.clear();
    m_extrasByClass.clear();
    mp_cacheWorld = nullptr;

    if (::GetFileAttributesW(k_desyncConfigFilename) == INVALID_FILE_ATTRIBUTES)
    {
        MILAN_DEBUGW(ELOG_Desync, L"desync: no %s, using defaults", k_desyncConfigFilename);
        return;
    }

    try
    {
        toml::table const Table = toml::parse_file(k_desyncConfigFilename);

        m_defaultInterval = static_cast<float>(Table.at_path("desync.interval").value_or(1.0));
        m_arrayLimit = std::max(0, static_cast<int>(Table.at_path("desync.array_limit").value_or(16)));
        m_bFull = Table.at_path("desync.full").value_or(false);

        if (toml::table const* const Extra = Table.at_path("desync.extra").as_table())
        {
            for (auto&& [Key, Value] : *Extra)
            {
                toml::array const* const Paths = Value.as_array();
                if (Paths == nullptr)
                    continue;

                auto& Target = m_extraConfig[Wide(Key.str())];
                for (auto&& Path : *Paths)
                {
                    if (auto const String = Path.value<std::string>())
                        Target.push_back(*String);
                }
            }
        }

        MILAN_INFOW(ELOG_Desync, L"desync: loaded %s (interval %.2f, array limit %d, full %d, %zu extra classes)",
            k_desyncConfigFilename, m_defaultInterval, m_arrayLimit, m_bFull, m_extraConfig.size());
    }
    catch (toml::parse_error const& Exception)
    {
        MILAN_ERRORW(ELOG_Desync, L"desync: failed to parse %s: %S", k_desyncConfigFilename, Exception.description().data());
    }
}

bool CDesync::OpenFile()
{
    if (m_file.is_open())
        return true;

    SYSTEMTIME Time{};
    ::GetLocalTime(&Time);

    char Buffer[128]{};
    std::snprintf(Buffer, sizeof(Buffer), "desync-%04d%02d%02d-%02d%02d%02d-%d.jsonl",
        Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond, ::GetGameInstanceOrdinal());

    m_fileName = Buffer;
    m_file.open(m_fileName, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!m_file.is_open())
    {
        MILAN_ERRORW(ELOG_Desync, L"desync: failed to open %S", m_fileName.c_str());
        return false;
    }

    m_classesWritten.clear();

    json Header{
        { "type", "header" },
        { "version", k_formatVersion },
        { "instance", ::GetGameInstanceOrdinal() },
    };
    WriteLine(Header.dump());

    MILAN_INFOW(ELOG_Desync, L"desync: writing %S", m_fileName.c_str());
    return true;
}

void CDesync::CloseFile()
{
    if (!m_file.is_open())
        return;

    m_file.close();
    MILAN_INFOW(ELOG_Desync, L"desync: closed %S", m_fileName.c_str());
}

void CDesync::WriteLine(std::string const& Line)
{
    m_file << Line << '\n';
    m_file.flush();
}

std::vector<CDesync::PropertyPlan> const& CDesync::PlanFor(UClass* const Class)
{
    if (auto const Found = m_plans.find(Class); Found != m_plans.end())
        return Found->second;

    std::vector<PropertyPlan> Plan{};
    for (UProperty* Property = Class->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
    {
        if (IsSkippedPropertyClass(Property->Class))
            continue;

        bool const bNet = (Property->PropertyFlags & k_cpfNet) != 0;
        std::string Key = BasicNameOf(Property);
        bool const bCore = std::find(std::begin(k_coreProperties), std::end(k_coreProperties), Key) != std::end(k_coreProperties);

        if (bNet || bCore || m_bFull)
            Plan.push_back({ Property, std::move(Key), bNet });
    }

    return m_plans.emplace(Class, std::move(Plan)).first->second;
}

std::vector<CDesync::ExtraPath> const& CDesync::ExtrasFor(UClass* const Class)
{
    if (auto const Found = m_extrasByClass.find(Class); Found != m_extrasByClass.end())
        return Found->second;

    std::vector<ExtraPath> Extras{};
    for (UField* Field = Class; Field != nullptr; Field = Field->SuperField)
    {
        FString const ClassName = Field->Name.ToString(SFXName::k_formatBasic);
        auto const Configured = m_extraConfig.find(std::wstring{ ClassName.Chars(), ClassName.Length() });
        if (Configured == m_extraConfig.end())
            continue;

        for (std::string const& Path : Configured->second)
        {
            ExtraPath Extra{ Path, {} };
            size_t Begin = 0;
            while (Begin <= Path.size())
            {
                size_t const End = std::min(Path.find('.', Begin), Path.size());
                if (End > Begin)
                    Extra.Segments.emplace_back(Wide(std::string_view{ Path }.substr(Begin, End - Begin)).c_str(), 0);
                Begin = End + 1;
            }
            if (!Extra.Segments.empty())
                Extras.push_back(std::move(Extra));
        }
    }

    return m_extrasByClass.emplace(Class, std::move(Extras)).first->second;
}

void CDesync::InvalidateIfWorldChanged(UWorld* const World)
{
    // Map-specific classes unload on travel and their addresses get reused, so anything
    // keyed by UClass* must not outlive the map it was built on.
    std::wstring const Map{ World->URL.Map.Chars(), World->URL.Map.Length() };
    if (World == mp_cacheWorld && Map == m_cacheMap)
        return;

    mp_cacheWorld = World;
    m_cacheMap = Map;
    m_plans.clear();
    m_extrasByClass.clear();
    m_classesWritten.clear();
}

void CDesync::TakeSnapshot(int const Sequence, char const* const Origin)
{
    UWorld* const World = CurrentWorld();
    AWorldInfo* const WorldInfo = CurrentWorldInfo();
    if (World == nullptr || WorldInfo == nullptr || !OpenFile())
        return;

    InvalidateIfWorldChanged(World);

    bool const bClient = IsClient();
    bool const bHost = IsHost();

    json Snapshot{
        { "type", "snap" },
        { "seq", Sequence },
        { "origin", Origin },
        { "side", SideName() },
        { "map", Utf8(World->URL.Map) },
        { "time", Round3(WorldInfo->TimeSeconds) },
        { "realTime", Round3(WorldInfo->RealTimeSeconds) },
    };

    // Channel indices are shared by both ends of a connection, so they pair dynamic actors
    // whose names differ between host and client.
    std::vector<std::pair<std::string, std::unordered_map<AActor const*, int>>> HostChannels{};
    std::unordered_map<AActor const*, int> ClientChannels{};

    if (bHost)
    {
        json Connections = json::array();
        for (UNetConnection* const Connection : WorldDriver()->ChildConnections)
        {
            if (Connection == nullptr)
                continue;

            int const PlayerId = PlayerIdOf(Connection->Actor);
            auto* const TcpConnection = Connection->Cast<UTcpipConnection>();
            Connections.push_back({
                { "playerId", PlayerId },
                { "remote", TcpConnection != nullptr ? Utf8(TcpConnection->FormatRemoteAddress()) : "" },
                { "capable", m_capableConnections.contains(Connection) },
            });
            HostChannels.emplace_back(std::to_string(PlayerId), ChannelsOf(Connection));
        }
        Snapshot["connections"] = std::move(Connections);
    }
    else if (bClient)
    {
        ClientChannels = ChannelsOf(WorldDriver()->ServerConnection);
        Snapshot["playerId"] = PlayerIdOf(LocalController());
    }

    std::vector<std::string> ClassLines{};
    json Actors = json::array();

    for (ULevel* const Level : World->Levels)
    {
        if (Level == nullptr)
            continue;

        for (AActor* const Actor : Level->Actors)
        {
            if (!ShouldCapture(Actor, bClient))
                continue;

            UClass* const Class = Actor->Class;
            std::string const ClassName = BasicNameOf(Class);

            if (!m_classesWritten.contains(Class))
            {
                json NetNames = json::array();
                for (UProperty* Property = Class->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
                {
                    if ((Property->PropertyFlags & k_cpfNet) != 0)
                        NetNames.push_back(BasicNameOf(Property));
                }
                ClassLines.push_back(json{ { "type", "class" }, { "name", ClassName }, { "net", std::move(NetNames) } }.dump());
                m_classesWritten.insert(Class);
            }

            json Entry{
                { "name", NameOf(Actor) },
                { "class", ClassName },
            };

            if (Actor->bNoDelete)
            {
                Entry["noDelete"] = true;
                Entry["path"] = Utf8(Actor->GetFullName());
            }
            if (Actor->bDeleteMe)
                Entry["deleteMe"] = true;

            if (bHost)
            {
                json Channels = json::object();
                for (auto const& [PlayerId, Map] : HostChannels)
                {
                    if (auto const Found = Map.find(Actor); Found != Map.end())
                        Channels[PlayerId] = Found->second;
                }
                if (!Channels.empty())
                    Entry["ch"] = std::move(Channels);
            }
            else if (bClient)
            {
                if (auto const Found = ClientChannels.find(Actor); Found != ClientChannels.end())
                    Entry["ch"] = Found->second;
            }

            FStateFrame const* const StateFrame = Actor->StateFrame;
            if (StateFrame != nullptr && StateFrame->StateNode != nullptr)
                Entry["state"] = BasicNameOf(StateFrame->StateNode);

            json Properties = json::object();
            for (PropertyPlan const& Plan : PlanFor(Class))
                Properties[Plan.Key] = ReadProperty(Plan.Property, reinterpret_cast<BYTE const*>(Actor) + Plan.Property->Offset, 0, m_arrayLimit);
            Entry["p"] = std::move(Properties);

            auto const& Extras = ExtrasFor(Class);
            if (!Extras.empty())
            {
                json ExtraValues = json::object();
                for (ExtraPath const& Extra : Extras)
                    ExtraValues[Extra.Key] = ReadPath(Actor, Extra.Segments, m_arrayLimit);
                Entry["x"] = std::move(ExtraValues);
            }

            Actors.push_back(std::move(Entry));
        }
    }

    size_t const ActorCount = Actors.size();
    Snapshot["actors"] = std::move(Actors);

    for (std::string const& Line : ClassLines)
        WriteLine(Line);
    WriteLine(Snapshot.dump(-1, ' ', false, json::error_handler_t::replace));

    m_lastSequence = Sequence;
    MILAN_INFOW(ELOG_Desync, L"desync: snapshot %d (%S, %S), %zu actors", Sequence, SideName(), Origin, ActorCount);
}

void CDesync::BroadcastSnapshot(int const Sequence)
{
    UNetDriver* const Driver = WorldDriver();
    if (Driver == nullptr)
        return;

    FString const Message = FString::Printf(L"snap %d", Sequence);
    for (UNetConnection* const Connection : Driver->ChildConnections)
    {
        if (Connection == nullptr || Connection->Actor == nullptr || !m_capableConnections.contains(Connection))
            continue;
        Connection->Actor->eventClientMessage(Message, MessageType(), 0.0f);
    }
}

void CDesync::SendHello(float const Delta)
{
    APlayerController* const Controller = LocalController();
    if (Controller == nullptr || Controller->Role != ROLE_AutonomousProxy)
        return;

    // Re-announce periodically too: a new controller can reuse the old one's address after travel.
    m_helloTimer += Delta;
    if (Controller == mp_helloController && m_helloTimer < k_helloInterval)
        return;

    mp_helloController = Controller;
    m_helloTimer = 0.0f;
    Controller->ServerMutate(FString{ L"MilanDesync hello" });
    MILAN_DEBUGW(ELOG_Desync, L"desync: announced to host");
}

void CDesync::RequestHostSnapshot()
{
    APlayerController* const Controller = LocalController();
    if (Controller == nullptr)
        return;

    Controller->ServerMutate(FString{ L"MilanDesync snap" });
    MILAN_INFOW(ELOG_Desync, L"desync: requested snapshot from host");
}

void CDesync::PruneCapableConnections()
{
    UNetDriver* const Driver = WorldDriver();
    std::erase_if(m_capableConnections, [Driver](UNetConnection* const Connection)
        {
            if (Driver == nullptr)
                return true;
            for (UNetConnection* const Live : Driver->ChildConnections)
            {
                if (Live == Connection)
                    return false;
            }
            return true;
        });
}

void CDesync::On_GameEngineTick(float const Delta)
{
    if (IsClient())
    {
        SendHello(Delta);

        for (int const Sequence : m_clientPendingSequences)
            TakeSnapshot(Sequence, "host");
        m_clientPendingSequences.clear();
        return;
    }

    mp_helloController = nullptr;
    PruneCapableConnections();

    if (m_interval > 0.0f)
    {
        m_timer += Delta;
        if (m_timer >= m_interval)
        {
            m_timer = 0.0f;
            if (mp_hostSnapOrigin == nullptr)
                mp_hostSnapOrigin = "timer";
        }
    }

    if (mp_hostSnapOrigin == nullptr)
        return;

    char const* const Origin = std::exchange(mp_hostSnapOrigin, nullptr);
    int const Sequence = ++m_sequence;
    TakeSnapshot(Sequence, Origin);
    if (IsHost())
        BroadcastSnapshot(Sequence);
}

bool CDesync::On_UObject_ProcessEvent(UObject* const Context, UFunction* const Function, void* const Parms)
{
    static SFXName const k_clientMessage{ L"ClientMessage", 0 };
    static SFXName const k_serverMutate{ L"ServerMutate", 0 };

    if (Function->Name == k_clientMessage)
    {
        // Only the receiving client swallows it; on the host this call is the send.
        if (!IsClient() || !Context->IsA<APlayerController>())
            return false;

        auto const* const Params = static_cast<APlayerController_eventClientMessage_Parms const*>(Parms);
        if (!(Params->Type == MessageType()))
            return false;

        int Sequence = 0;
        if (Params->S.Length() > 0 && std::swscanf(Params->S.Chars(), L"snap %d", &Sequence) == 1)
            m_clientPendingSequences.push_back(Sequence);
        return true;
    }

    if (Function->Name == k_serverMutate)
    {
        if (IsClient() || !Context->IsA<APlayerController>())
            return false;

        auto const* const Params = static_cast<APlayerController_execServerMutate_Parms const*>(Parms);
        std::wstring_view const Message{ Params->MutateString.Chars(), Params->MutateString.Length() };
        if (!Message.starts_with(k_mutatePrefix))
            return false;

        std::wstring_view const Request = Message.substr(k_mutatePrefix.size());
        auto* const Controller = static_cast<APlayerController*>(Context);
        auto* const Connection = Controller->Player != nullptr ? Controller->Player->Cast<UNetConnection>() : nullptr;

        if (Request == L"hello" && Connection != nullptr)
        {
            if (m_capableConnections.insert(Connection).second)
                MILAN_INFOW(ELOG_Desync, L"desync: player %d can take snapshots", PlayerIdOf(Controller));
        }
        else if (Request == L"snap")
        {
            mp_hostSnapOrigin = "client";
        }
        return true;
    }

    return false;
}

#endif // _MLN_DEBUG
