/**
 * @file        MilanHost/Components/CDesync.hpp
 * @header      Component for capturing replication desync snapshots on host and clients.
 */

#pragma once

#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Base/Base.hpp"
#include "Engine/Hooks.hpp"


/**
 * @brief
 * Writes snapshots of every networked actor to a JSON-lines file, so a host capture and a
 * client capture can be diffed offline by NetworkProfiler.Desync.
 *
 * The host numbers each snapshot and asks clients to take the same one through a reliable
 * PlayerController.ClientMessage RPC. Clients announce themselves and request snapshots
 * through PlayerController.ServerMutate. Both RPCs are swallowed on the receiving side.
 */
class CDesync final : public NonCopyable
{
public:

    static constexpr wchar_t const* k_typeName = L"desync";

    bool Initialize(::LESDK::Initializer& Context);
    void Terminate(::LESDK::Initializer& Context);

    /** Runs at the end of UGameEngine::Tick, after replication for the frame has been sent or received. */
    void On_GameEngineTick(float Delta);

    /** Returns true when the event is one of ours and must not reach the original ProcessEvent. */
    bool On_UObject_ProcessEvent(UObject* Context, UFunction* Function, void* Parms);

    /** One property to write for an object of a given class. */
    struct PropertyPlan
    {
        UProperty*          Property;
        std::string         Key;
        bool                bNet;
    };

    /** One configured extra property path, like "Mesh.PhysicsWeight". */
    struct ExtraPath
    {
        std::string                 Key;
        std::vector<SFXName>        Segments;
    };

private:

    void RegisterCommands();
    void LoadConfig();

    bool OpenFile();
    void CloseFile();
    void WriteLine(std::string const& Line);

    void InvalidateIfWorldChanged(UWorld* World);
    void TakeSnapshot(int Sequence, char const* Origin);
    void BroadcastSnapshot(int Sequence);
    void SendHello(float Delta);
    void RequestHostSnapshot();
    void PruneCapableConnections();

    std::vector<PropertyPlan> const& PlanFor(UClass* Class);
    std::vector<ExtraPath> const& ExtrasFor(UClass* Class);

    std::ofstream                                           m_file{};
    std::string                                             m_fileName{};
    std::unordered_set<UClass*>                             m_classesWritten{};

    std::unordered_map<UClass*, std::vector<PropertyPlan>>  m_plans{};
    std::unordered_map<UClass*, std::vector<ExtraPath>>     m_extrasByClass{};
    UWorld*                                                 mp_cacheWorld{ nullptr };  // World the UClass*-keyed caches were built on.
    std::wstring                                            m_cacheMap{};

    // Settings, read from MilanDesync.toml when a capture starts.
    std::unordered_map<std::wstring, std::vector<std::string>> m_extraConfig{};
    float                                                   m_defaultInterval{ 1.0f };
    int                                                     m_arrayLimit{ 16 };
    bool                                                    m_bFull{ false };

    // Host state.
    int                                                     m_sequence{ 0 };
    char const*                                             mp_hostSnapOrigin{ nullptr };  // Why the next host snapshot is taken; null when none is pending.
    float                                                   m_interval{ 0.0f };
    float                                                   m_timer{ 0.0f };
    std::unordered_set<UNetConnection*>                     m_capableConnections{};

    // Client state.
    std::vector<int>                                        m_clientPendingSequences{};
    int                                                     m_lastSequence{ -1 };
    APlayerController*                                      mp_helloController{ nullptr };
    float                                                   m_helloTimer{ 0.0f };
};

static_assert(ComponentType<CDesync>);
