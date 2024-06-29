/**
 * @file        MilanHost/Milan/GuidCacheUtils.hpp
 * @brief       Utilities for debugging GuidCache issues.
 */

#pragma once

#include <mutex>
#include <optional>
#include <LESDK/Headers.hpp>

#include "Containers/string.hpp"
#include "Containers/unordered_map.hpp"
#include "Milan/Base.hpp"
#include "Proxies/Engine.hpp"


// TODO: Move this to LExSDKv2!
template<>
struct std::hash<SFXName>
{
    std::size_t operator()(SFXName const& Value) const
    {
        static_assert(sizeof(SFXName) == sizeof(std::size_t));
        return *reinterpret_cast<std::size_t const*>(&Value);
    }
};


class GuidCacheOverride final
    : public NonCopyable
    , public AllocateThroughEngine
{
public:

    explicit GuidCacheOverride(char const* InPath);

    std::optional<FGuid>    Get(SFXName Package);
    void                    UpdateLookup();

private:

    std::mutex                                  Mutex;
    ::game::string                              Source{};
    ::game::unordered_map<SFXName, FGuid>       Lookup{};
};
