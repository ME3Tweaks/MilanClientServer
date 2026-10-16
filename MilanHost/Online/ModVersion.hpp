#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "Online/PeerSecurity.hpp"

namespace ModVersion
{
    enum class Source { Metadata, Configuration, CommandLine };

    struct Result
    {
        std::string Version{};
        std::filesystem::path MetadataPath{};
        Source From{ Source::Metadata };
        std::string_view Error{};
    };

    // Bound each physical line; an oversized/malformed file must not be read in full.
    inline bool ReadLine(std::istream& input, std::string& line, std::size_t maximum)
    {
        line.clear();
        for (;;)
        {
            int const c = input.get();
            if (c == std::char_traits<char>::eof())
            {
                if (input.bad())
                    return false;
                break;
            }
            if (c == '\n')
                break;
            if (line.size() >= maximum)
                return false;
            line.push_back(static_cast<char>(c));
        }
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return true;
    }

    // Temporary source until AutoTOC publishes the registry. Only the DLC identity
    // is fixed: always read the installed version, never the ASI build version.
    inline Result Resolve(std::filesystem::path const& executable, std::string_view configured,
        std::optional<std::string> const& commandLine = std::nullopt)
    {
        Result result{};
        if (commandLine)
        {
            result.From = Source::CommandLine;
            result.Version = *commandLine;
        }
        else if (!configured.empty())
        {
            result.From = Source::Configuration;
            result.Version = configured;
        }
        else
        {
            auto const gameRoot = executable.parent_path().parent_path().parent_path();
            if (!executable.is_absolute() || gameRoot.empty() || gameRoot == executable.root_path())
            {
                result.Error = "could not resolve the running game's root from its executable";
                return result;
            }
            result.MetadataPath = gameRoot / L"BioGame" / L"DLC" / L"DLC_MOD_ProjectMilan" / L"_metacmm.txt";
            std::ifstream input(result.MetadataPath, std::ios::binary);
            if (!input.is_open())
            {
                result.Error = "could not open Project Milan's _metacmm.txt";
                return result;
            }
            // The UTF-8 name (optionally BOM-prefixed) is line 1; version is line 2.
            // Never skip empty lines or substitute the installer build on line 3.
            std::string name;
            if (!ReadLine(input, name, 4096) || !ReadLine(input, result.Version, 65))
            {
                result.Version.clear();
                result.Error = "Project Milan's _metacmm.txt has unreadable or oversized header lines";
                return result;
            }
        }
        if (!PeerSecurity::IsModVersion(result.Version))
        {
            result.Version.clear();
            result.Error = "mod version must contain 1-64 printable ASCII characters without spaces";
        }
        return result;
    }
}
