#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace PeerSecurity
{
    using IPv4 = std::array<std::uint8_t, 4>;

    constexpr bool IsModVersion(std::string_view version)
    {
        if (version.empty() || version.size() > 64)
            return false;
        for (char c : version)
            if (c < '!' || c > '~')
                return false;
        return true;
    }

    inline bool EqualProof(std::string_view a, std::string_view b)
    {
        if (a.size() != 32 || b.size() != 32)
            return false;
        unsigned difference = 0;
        for (std::size_t i = 0; i < 32; ++i)
            difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
        return difference == 0;
    }

    constexpr bool IsPrivate(IPv4 const& a)
    {
        return a[0] == 10 || (a[0] == 172 && (a[1] & 0xF0) == 16)
            || (a[0] == 192 && a[1] == 168) || (a[0] == 100 && (a[1] & 0xC0) == 64);
    }

    constexpr std::uint32_t Number(IPv4 const& a)
    {
        return (std::uint32_t{ a[0] } << 24) | (std::uint32_t{ a[1] } << 16)
            | (std::uint32_t{ a[2] } << 8) | a[3];
    }

    constexpr bool OnSubnet(IPv4 const& target, IPv4 const& local, unsigned const prefix)
    {
        if (!IsPrivate(target) || !IsPrivate(local) || prefix == 0 || prefix > 32)
            return false;
        std::uint32_t const mask = 0xFFFFFFFFu << (32 - prefix);
        std::uint32_t const t = Number(target);
        if ((t & mask) != (Number(local) & mask))
            return false;
        // /31 point-to-point and /32 adapter routes have no broadcast address.
        return prefix >= 31 || ((t & ~mask) != 0 && (t & ~mask) != ~mask);
    }

    // UE3 URL options are separated by '?'. Reject duplicates and oversized input;
    // only exact names are accepted so unrelated options cannot attest membership.
    inline std::optional<std::string_view> UrlOption(std::string_view url, std::string_view name)
    {
        if (url.size() > 4096)
            return std::nullopt;
        std::optional<std::string_view> found;
        auto pos = url.find('?');
        while (pos != std::string_view::npos)
        {
            auto const end = url.find('?', pos + 1);
            auto const option = url.substr(pos + 1, end == std::string_view::npos ? end : end - pos - 1);
            if (option.size() > name.size() && option.substr(0, name.size()) == name && option[name.size()] == '=')
            {
                if (found)
                    return std::nullopt;
                found = option.substr(name.size() + 1);
            }
            pos = end;
        }
        return found;
    }
}
