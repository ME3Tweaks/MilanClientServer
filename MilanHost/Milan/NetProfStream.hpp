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


enum class NPRecord : unsigned char
{
    TieredStringLookup,
    ActorReplication,
    PropertyReplication,

    MAX,  // Higher boundary for valid NPRecord values, exclusive.
};

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
    /** Index of the last item in @ref StringTable that has been serialized to a "tiered string lookup" record. */
    size_t StringTierOffset;

public:

    NetworkCapture();
    ~NetworkCapture();

    void FlushRecords();

    void CaptureActorReplication(UNetConnection* Connection, AActor* Actor);
    void CapturePropertyReplication(UNetConnection* Connection, UProperty* Property, INT Bits);

private:

    void RecordStringTable_NoLock();

    NetworkCapture& operator<<(NPRecord Type);
    NetworkCapture& operator<<(UNetConnection* Connection);
    NetworkCapture& operator<<(FString& String);
    NetworkCapture& operator<<(FString const& String);
    NetworkCapture& operator<<(INT Value);

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
