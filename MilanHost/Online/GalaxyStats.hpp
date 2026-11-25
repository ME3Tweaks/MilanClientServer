#pragma once

// Lobby Galaxy at War counts from the matchmaker (MilanServer GET /v1/stats/galaxy/{gameVersion})
// This file covers Where to fetch them and how to read them

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace GalaxyStats
{
    constexpr std::string_view k_defaultPath = "/v1/stats/galaxy";
    constexpr int k_minRefreshSeconds = 60;
    constexpr int k_maxRefreshSeconds = 3600;
    constexpr std::size_t k_maxMaps = 256;
    constexpr std::size_t k_maxUrlLength = 512;

    struct Target
    {
        std::string Origin{};   // scheme://host[:port]
        std::string Path{};     // Starts with '/'

        bool operator==(Target const&) const = default;
    };

    struct MapCount
    {
        int MapId{ 0 };
        int Lobbies{ 0 };
        int Players{ 0 };
    };

    // Snapshot is per-game version stats
    struct Snapshot
    {
        int Players{ 0 };
        int RefreshSeconds{ k_minRefreshSeconds };
        std::vector<MapCount> Maps{};
        int FullPopulation{ 0 };    // 0: no tint
        float MaxBaseTint{ 0.f };
    };

    // Prefix must be lowercase
    inline bool StartsWithNoCase(std::string_view const Text, std::string_view const Prefix)
    {
        return Text.size() >= Prefix.size() && std::equal(Prefix.begin(), Prefix.end(), Text.begin(),
            [](char const Expected, char const Actual) { return (Actual >= 'A' && Actual <= 'Z' ? Actual + 32 : Actual) == Expected; });
    }

    // Where /v1/info's galaxyStatsUrl points, as the server can send user to a different cached endpoint
    inline std::optional<Target> Resolve(std::string_view const Matchmaker, std::optional<std::string_view> const Advertised)
    {
        if (Matchmaker.empty())
        {
            return std::nullopt;
        }
        std::string_view const Url = Advertised.value_or(k_defaultPath);
        if (Url.empty() || Url.size() > k_maxUrlLength)
        {
            return std::nullopt;
        }
        for (char const C : Url)
        {
            // The game version is appended as a path segment, so no query or fragment
            if (C <= ' ' || C >= '\x7F' || C == '?' || C == '#')
            {
                return std::nullopt;
            }
        }

        if (Url.front() == '/')
        {
            if (Url.size() > 1 && Url[1] == '/')
            {
                return std::nullopt;
            }
            return Target{ std::string{ Matchmaker }, std::string{ Url } };
        }

        bool const Secure = StartsWithNoCase(Url, "https://");
        if (!Secure && !StartsWithNoCase(Url, "http://"))
        {
            return std::nullopt;
        }
        if (!Secure && StartsWithNoCase(Matchmaker, "https://"))
        {
            return std::nullopt;
        }

        std::size_t const HostStart = Url.find("://") + 3;
        std::size_t const PathStart = Url.find('/', HostStart);
        std::string_view const Origin = Url.substr(0, PathStart);
        if (Origin.size() <= HostStart || Origin.find_first_of("?#@") != std::string_view::npos)
        {
            return std::nullopt;
        }
        return Target{ std::string{ Origin }, PathStart == std::string_view::npos ? std::string{ "/" } : std::string{ Url.substr(PathStart) } };
    }

    // Builds the stats path for this game version
    inline std::optional<std::string> VersionPath(std::string_view Path, std::string_view const EncodedVersion)
    {
        if (EncodedVersion.empty() || EncodedVersion == "." || EncodedVersion == "..")
        {
            return std::nullopt;
        }
        while (!Path.empty() && Path.back() == '/')
        {
            Path.remove_suffix(1);
        }
        std::string Result{ Path };
        Result += '/';
        Result += EncodedVersion;
        return Result;
    }

    // Reads a GalaxyStats body. Counts are clamped
    inline std::optional<Snapshot> Parse(nlohmann::json const& Body)
    {
        if (!Body.is_object())
        {
            return std::nullopt;
        }
        auto const Count = [](nlohmann::json const& Object, char const* const Key) -> std::optional<int>
            {
                auto const Found = Object.find(Key);
                if (Found == Object.end() || !Found->is_number_integer())
                {
                    return std::nullopt;
                }
                // Clamp to 1 million
                return static_cast<int>(std::clamp<long long>(Found->get<long long>(), 0, 1'000'000));
            };

        Snapshot Result{};
        std::optional<int> const Players = Count(Body, "players");
        auto const Maps = Body.find("maps");
        if (!Players || Maps == Body.end() || !Maps->is_array())
        {
            // Malformed data! Abort
            return std::nullopt;
        }
        Result.Players = *Players;
        Result.RefreshSeconds = std::clamp(Count(Body, "refreshSeconds").value_or(k_minRefreshSeconds), k_minRefreshSeconds, k_maxRefreshSeconds);

        for (nlohmann::json const& Map : *Maps)
        {
            if (Result.Maps.size() == k_maxMaps)
            {
                break;
            }
            if (!Map.is_object())
            {
                continue;
            }

            // Unpack map data
            std::optional<int> const Id = Count(Map, "mapId");
            std::optional<int> const Lobbies = Count(Map, "lobbies");
            std::optional<int> const MapPlayers = Count(Map, "players");
            if (Id && *Id > 0 && Lobbies && MapPlayers)
            {
                Result.Maps.push_back({ *Id, *Lobbies, *MapPlayers });
            }
        }

        if (auto const Tint = Body.find("tint"); Tint != Body.end() && Tint->is_object())
        {
            // Unpack optional tint data for GAW
            std::optional<int> const Full = Count(*Tint, "fullPopulation");
            auto const MaxBase = Tint->find("maxBaseTint");
            if (Full && *Full > 0 && MaxBase != Tint->end() && MaxBase->is_number())
            {
                double const Value = MaxBase->get<double>();
                Result.FullPopulation = *Full;
                Result.MaxBaseTint = std::isfinite(Value) ? static_cast<float>(std::clamp(Value, 0.0, 1.0)) : 0.f;
            }
        }
        return Result;
    }
}
