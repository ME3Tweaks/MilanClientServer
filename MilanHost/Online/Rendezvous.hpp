/**
 * @file        MilanHost/Online/Rendezvous.hpp
 * @header      Matchmaking datagrams that share the UE3 game socket.
 *
 * Discovery (magic "MLNR") matches MilanServer/MilanServer/Discovery.cs.
 * Peer probes (magic "MLNP") are exchanged directly between host and joiner,
 * authenticated with the reservation's traversal key. Probe layout, 56 bytes:
 *   0  4  magic "MLNP"         4  1  version (1)      5  1  type (1 probe, 2 ack)
 *   6  1  direction (1 joiner->host, 2 host->joiner)  7  1  zero
 *   8 16  pair ID = HMAC-SHA256(key, "milan-pair-id")[0..16]
 *  24  8  sequence (an ack echoes the probe's)        32  8  zero
 *  40 16  HMAC-SHA256(key, bytes 0..39)[0..16]
 * Every datagram here has a fixed length and magic, so the receive hook can
 * pull it out before UE3 parses it.
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>


namespace Rendezvous
{
    constexpr std::size_t k_discoveryRequestLength = 64;
    constexpr std::size_t k_discoveryResponseLength = 56;
    constexpr std::size_t k_probeLength = 56;

    using Key = std::array<std::uint8_t, 32>;
    using Id16 = std::array<std::uint8_t, 16>;

    enum class EPacket { None, DiscoveryResponse, Probe };
    enum class EProbeType : std::uint8_t { Probe = 1, Ack = 2 };
    enum class EDirection : std::uint8_t { JoinerToHost = 1, HostToJoiner = 2 };

    struct ProbeHeader
    {
        EProbeType                  Type{};
        EDirection                  Direction{};
        Id16                        Pair{};
        std::uint64_t               Sequence{ 0 };
    };

    /** Decodes unpadded base64url that must fill @p Out exactly. */
    bool DecodeBase64Url(std::string_view Text, std::span<std::uint8_t> Out);

    /** Identifies our datagrams by length, magic, version, and type. Does not authenticate. */
    EPacket Classify(std::uint8_t const* Data, std::size_t Length);

    void WriteDiscoveryRequest(std::span<std::uint8_t, k_discoveryRequestLength> Out,
        Id16 const& TicketId, std::uint64_t Sequence, Key const& Secret);

    bool ReadDiscoveryResponse(std::span<std::uint8_t const> Packet, Id16 const& TicketId, Key const& Secret,
        std::uint64_t& OutSequence, std::uint32_t& OutAddressNetworkOrder, std::uint16_t& OutPort);

    Id16 DerivePairId(Key const& TraversalKey);

    void WriteProbe(std::span<std::uint8_t, k_probeLength> Out, EProbeType Type, EDirection Direction,
        Id16 const& Pair, std::uint64_t Sequence, Key const& TraversalKey);

    /** Parses the probe header without checking the tag. */
    bool PeekProbe(std::span<std::uint8_t const> Packet, ProbeHeader& Out);

    bool VerifyProbe(std::span<std::uint8_t const> Packet, Key const& TraversalKey);
}
