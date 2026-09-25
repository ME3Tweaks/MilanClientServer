#include <algorithm>

#include "Online/MatchmakingNet.hpp"
#include "Online/Rendezvous.hpp"


namespace
{
    constexpr std::uint8_t k_version = 1;
    constexpr std::uint8_t k_typeBindingRequest = 1;
    constexpr std::uint8_t k_typeBindingResponse = 2;
    constexpr std::size_t k_tagLength = 16;

    constexpr std::array<std::uint8_t, 4> k_discoveryMagic{ 'M', 'L', 'N', 'R' };
    constexpr std::array<std::uint8_t, 4> k_probeMagic{ 'M', 'L', 'N', 'P' };

    void WriteU64(std::uint8_t* Out, std::uint64_t Value)
    {
        for (int i = 7; i >= 0; --i, Value >>= 8)
            Out[i] = static_cast<std::uint8_t>(Value & 0xFF);
    }

    std::uint64_t ReadU64(std::uint8_t const* In)
    {
        std::uint64_t Value = 0;
        for (int i = 0; i < 8; ++i)
            Value = (Value << 8) | In[i];
        return Value;
    }

    bool HasMagic(std::uint8_t const* Data, std::array<std::uint8_t, 4> const& Magic)
    {
        return Data[0] == Magic[0] && Data[1] == Magic[1] && Data[2] == Magic[2] && Data[3] == Magic[3];
    }

    bool IsZero(std::uint8_t const* Data, std::size_t Length)
    {
        std::uint8_t Accumulator = 0;
        for (std::size_t i = 0; i < Length; ++i)
            Accumulator |= Data[i];
        return Accumulator == 0;
    }

    bool ComputeTag(Rendezvous::Key const& Key, std::span<std::uint8_t const> Data, std::uint8_t* OutTag)
    {
        std::array<std::uint8_t, 32> Full{};
        if (!MatchmakingNet::HmacSha256(Key, Data, Full))
            return false;
        std::copy_n(Full.begin(), k_tagLength, OutTag);
        return true;
    }

    bool CheckTag(Rendezvous::Key const& Key, std::span<std::uint8_t const> Data, std::uint8_t const* Tag)
    {
        std::array<std::uint8_t, k_tagLength> Expected{};
        if (!ComputeTag(Key, Data, Expected.data()))
            return false;

        std::uint8_t Difference = 0;
        for (std::size_t i = 0; i < k_tagLength; ++i)
            Difference |= static_cast<std::uint8_t>(Expected[i] ^ Tag[i]);
        return Difference == 0;
    }

    int Base64UrlValue(char const c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-') return 62;
        if (c == '_') return 63;
        return -1;
    }
}


namespace Rendezvous
{
    bool DecodeBase64Url(std::string_view const Text, std::span<std::uint8_t> const Out)
    {
        if (Text.size() != (Out.size() * 4 + 2) / 3)
            return false;

        std::uint32_t Buffer = 0;
        int Bits = 0;
        std::size_t Written = 0;
        for (char const c : Text)
        {
            int const Value = Base64UrlValue(c);
            if (Value < 0)
                return false;

            Buffer = (Buffer << 6) | static_cast<std::uint32_t>(Value);
            Bits += 6;
            if (Bits >= 8)
            {
                Bits -= 8;
                if (Written == Out.size())
                    return false;
                Out[Written++] = static_cast<std::uint8_t>((Buffer >> Bits) & 0xFF);
            }
        }
        // Leftover bits must be zero padding.
        return Written == Out.size() && (Buffer & ((1u << Bits) - 1)) == 0;
    }

    EPacket Classify(std::uint8_t const* const Data, std::size_t const Length)
    {
        if (Data == nullptr || Length < 8 || Data[4] != k_version)
            return EPacket::None;

        if (Length == k_discoveryResponseLength && HasMagic(Data, k_discoveryMagic) && Data[5] == k_typeBindingResponse)
            return EPacket::DiscoveryResponse;

        if (Length == k_probeLength && HasMagic(Data, k_probeMagic)
            && (Data[5] == static_cast<std::uint8_t>(EProbeType::Probe) || Data[5] == static_cast<std::uint8_t>(EProbeType::Ack)))
        {
            return EPacket::Probe;
        }

        return EPacket::None;
    }

    void WriteDiscoveryRequest(std::span<std::uint8_t, k_discoveryRequestLength> const Out,
        Id16 const& TicketId, std::uint64_t const Sequence, Key const& Secret)
    {
        std::fill(Out.begin(), Out.end(), std::uint8_t{ 0 });
        std::copy(k_discoveryMagic.begin(), k_discoveryMagic.end(), Out.begin());
        Out[4] = k_version;
        Out[5] = k_typeBindingRequest;
        std::copy(TicketId.begin(), TicketId.end(), Out.begin() + 8);
        WriteU64(Out.data() + 24, Sequence);
        ComputeTag(Secret, Out.first(48), Out.data() + 48);
    }

    bool ReadDiscoveryResponse(std::span<std::uint8_t const> const Packet, Id16 const& TicketId, Key const& Secret,
        std::uint64_t& OutSequence, std::uint32_t& OutAddressNetworkOrder, std::uint16_t& OutPort)
    {
        if (Classify(Packet.data(), Packet.size()) != EPacket::DiscoveryResponse
            || !IsZero(Packet.data() + 6, 2) || !IsZero(Packet.data() + 38, 2)
            || !std::equal(TicketId.begin(), TicketId.end(), Packet.begin() + 8)
            || !CheckTag(Secret, Packet.first(40), Packet.data() + 40))
        {
            return false;
        }

        OutSequence = ReadU64(Packet.data() + 24);
        std::copy_n(Packet.data() + 32, 4, reinterpret_cast<std::uint8_t*>(&OutAddressNetworkOrder));
        OutPort = static_cast<std::uint16_t>((Packet[36] << 8) | Packet[37]);
        return true;
    }

    Id16 DerivePairId(Key const& TraversalKey)
    {
        static constexpr std::string_view k_label = "milan-pair-id";
        std::array<std::uint8_t, 32> Full{};
        MatchmakingNet::HmacSha256(TraversalKey,
            std::span{ reinterpret_cast<std::uint8_t const*>(k_label.data()), k_label.size() }, Full);

        Id16 Pair{};
        std::copy_n(Full.begin(), Pair.size(), Pair.begin());
        return Pair;
    }

    void WriteProbe(std::span<std::uint8_t, k_probeLength> const Out, EProbeType const Type, EDirection const Direction,
        Id16 const& Pair, std::uint64_t const Sequence, Key const& TraversalKey)
    {
        std::fill(Out.begin(), Out.end(), std::uint8_t{ 0 });
        std::copy(k_probeMagic.begin(), k_probeMagic.end(), Out.begin());
        Out[4] = k_version;
        Out[5] = static_cast<std::uint8_t>(Type);
        Out[6] = static_cast<std::uint8_t>(Direction);
        std::copy(Pair.begin(), Pair.end(), Out.begin() + 8);
        WriteU64(Out.data() + 24, Sequence);
        ComputeTag(TraversalKey, Out.first(40), Out.data() + 40);
    }

    bool PeekProbe(std::span<std::uint8_t const> const Packet, ProbeHeader& Out)
    {
        if (Classify(Packet.data(), Packet.size()) != EPacket::Probe
            || (Packet[6] != static_cast<std::uint8_t>(EDirection::JoinerToHost) && Packet[6] != static_cast<std::uint8_t>(EDirection::HostToJoiner))
            || Packet[7] != 0 || !IsZero(Packet.data() + 32, 8))
        {
            return false;
        }

        Out.Type = static_cast<EProbeType>(Packet[5]);
        Out.Direction = static_cast<EDirection>(Packet[6]);
        std::copy_n(Packet.data() + 8, Out.Pair.size(), Out.Pair.begin());
        Out.Sequence = ReadU64(Packet.data() + 24);
        return true;
    }

    bool VerifyProbe(std::span<std::uint8_t const> const Packet, Key const& TraversalKey)
    {
        return Packet.size() == k_probeLength && CheckTag(TraversalKey, Packet.first(40), Packet.data() + 40);
    }
}
