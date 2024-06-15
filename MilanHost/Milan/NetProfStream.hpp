/**
 * @file        MilanHost/Milan/NetProfStream.hpp
 * @header      Declarations for Milan Network Profiler's capture stream.
 */

#pragma once

#include <mutex>

#include <LESDK/Headers.hpp>
#include "Containers/string.hpp"
#include "Containers/unordered_map.hpp"
#include "Containers/vector.hpp"
#include "Milan/Base.hpp"
#include "Milan/Streams.hpp"
#include "Proxies/Engine.hpp"


enum class NPRecord : unsigned char
{
    Dummy = 10,

    TieredStringLookup,
    FrameStart,
    LevelStart,
    RemoteFunction,
    ActorReplication,
    PropertyReplication,
    ControlMessage,
    SessionEvent,
    SocketCall,
    SendBunch,

    MAX,  // Higher boundary for valid NPRecord values, exclusive.
};

// We use high-order to mark record types.
static_assert(static_cast<int>(NPRecord::MAX) < 0x80);


class NetworkCapture final : public NonCopyable, public AllocateThroughEngine
{
    /** Sync primitive locked by public methods. */
    std::mutex Mutex;

    /** Output stream for writing the capture. */
    IOutputStream* Stream;

    /** Array of unique serialized strings. */
    game::vector<::game::wstring> StringTable;
    /** Mapping from string hash to index into @ref StringTable. */
    game::unordered_map<::game::wstring, size_t> StringLookup;
    /** Number of items in @ref StringTable already serialized to a "tiered string lookup" record. */
    size_t StringTierOffset;

public:

    NetworkCapture();
    ~NetworkCapture();

    void FlushRecords();

    void CaptureFrameStart(float Delta);
    void CaptureLevelStart(FURL* Url);
    void CaptureRemoteFunction(UNetConnection* Connection, AActor* Actor, UFunction* Function, INT Bits);
    void CaptureActorReplication(UNetConnection* Connection, AActor* Actor);
    void CapturePropertyReplication(UNetConnection* Connection, UProperty* Property, INT Bits);
    void CaptureControlMessage(UNetConnection* Connection, bool bPendingLevel, ENetMsg Type, FString const& Params);
    void CaptureSessionEvent(UNetConnection* Connection, FString Event, FString Description);
    void CaptureSocketCall(SOCKADDR_IN* Remote, bool bSending, INT Bits);
    void CaptureSendBunch(UChannel* Channel, INT Bits);

private:

    void RecordStringTable_NoLock();

    NetworkCapture& operator<<(NPRecord Type);

    NetworkCapture& operator<<(bool Value);
    NetworkCapture& operator<<(short Value);
    NetworkCapture& operator<<(int Value);
    NetworkCapture& operator<<(long long Value);
    NetworkCapture& operator<<(float Value);

    NetworkCapture& operator<<(UNetConnection* Connection);
    NetworkCapture& operator<<(SOCKADDR_IN* Socket);
    NetworkCapture& operator<<(FString& String);
    NetworkCapture& operator<<(FString const& String);

    NetworkCapture& operator<<(SFXName Name);
    NetworkCapture& operator<<(UObject* Object);

public:

    static NetworkCapture* Global();
    static void GlobalInitialize();
    static void GlobalClose();

private:

    /** Global instance. */
    static NetworkCapture* GlobalInstance;
    static std::mutex GlobalMutex;
};


#define MILAN_ENABLE_NETPROF 1

#if defined(MILAN_ENABLE_NETPROF) && MILAN_ENABLE_NETPROF == 1

    #define NETPROF(Call) \
        do { \
            if (::NetworkCapture::Global() != nullptr) { \
                ::NetworkCapture::Global()-> Call ; \
            } \
        } while (false)

#else

    #define NETPROF(Call) \
        do { } while (false)

#endif
