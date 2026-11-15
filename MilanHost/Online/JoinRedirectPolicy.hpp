#pragma once

#include <cstdint>
#include <optional>

namespace JoinRedirect::Policy
{
    struct Address
    {
        std::uint32_t   Ip{ 0 };    // Network byte order.
        std::uint16_t   Port{ 0 };  // Host byte order.

        constexpr bool operator==(Address const&) const = default;
    };

    // Where a pending join's UE3 connection should send to
    constexpr std::optional<Address> Target(Address const Current, bool const HostAnswered, std::optional<Address> const Verified, std::optional<Address> const ProbedBy)
    {
        if (HostAnswered)
            return std::nullopt;
        std::optional<Address> const Proven = Verified ? Verified : ProbedBy;
        if (!Proven || *Proven == Current)
            return std::nullopt;
        return Proven;
    }
}
