#include <format>
#include <filesystem>
#include <span>
#include <vector>
#include <tomlplusplus/toml.hpp>

#include "Base/IOUtils.hpp"
#include "Shell/ClientConfig.hpp"


#define MLN_CONFIG(in, path, type)                                      \
    [&]() -> type {                                                     \
        auto option = in.at_path(path).value<type>();                   \
        if (!option) { throw std::make_pair(path, #type); }             \
        return option.value();                                          \
    } ()

#define MLN_CONFIG_OPT(in, path, type, alt)                             \
    in.at_path(path).value<type>().value_or(alt)


bool ClientConfig::TryRead(std::wstring_view const InPath, game::string& OutError) noexcept
{
    OutError.clear();

    try
    {
        // A fresh install has no MilanHost.toml yet. Keep the defaults available to the
        // settings bridge; its first save creates the file. Existing unreadable or invalid
        // files must still report an error rather than being silently replaced.
        std::error_code FileError;
        bool const Exists = std::filesystem::exists(std::filesystem::path{ InPath }, FileError);
        if (FileError)
        {
            OutError = FileError.message().c_str();
            return false;
        }
        if (!Exists)
            return true;
        toml::table const Table = toml::parse_file(InPath);

        ModVersion          = MLN_CONFIG_OPT(Table, "milan.game.version", std::string, std::string{}).c_str();

        // Older files only have url, where an empty url disabled matchmaking.
        std::string const Url = MLN_CONFIG_OPT(Table, "milan.matchmaker.url", std::string, std::string{ k_defaultMatchmakerUrl });
        bool const bDefaultUrl = Url == k_defaultMatchmakerUrl;
        MatchmakerEnabled   = MLN_CONFIG_OPT(Table, "milan.matchmaker.enabled", bool, !Url.empty());
        MatchmakerCustom    = MLN_CONFIG_OPT(Table, "milan.matchmaker.custom", bool, !Url.empty() && !bDefaultUrl);
        MatchmakerCustomUrl = bDefaultUrl ? "" : Url.c_str();
        MatchmakerPassword  = MLN_CONFIG_OPT(Table, "milan.matchmaker.password", std::string, std::string{}).c_str();
        MatchmakerUrl       = SelectedMatchmakerUrl();

        // Build notices list
        if (toml::table const* Notices = Table.at_path("milan.notices").as_table())
        {
            for (auto&& [Server, Id] : *Notices)
            {
                if (auto const Value = Id.value<std::string>()) {
                    SeenNotices.emplace_back(game::string{ std::string{ Server.str() }.c_str() }, game::string{ Value->c_str() });
                }
            }
        }

#ifndef _FINAL_RELEASE
        if (toml::array const* LoggingSuppressedArray = Table.at_path("milan.logging.suppressed").as_array())
        {
            LoggingSuppressed.reserve(LoggingSuppressedArray->size());
            for (auto&& LoggingSuppressedValue : *LoggingSuppressedArray)
            {
                auto const String = LoggingSuppressedValue.value<std::string>();
                if (!String) { throw std::make_pair("milan.logging.suppressed[..]", "std::string"); }
                LoggingSuppressed.emplace_back(String->c_str());
            }
        }
#endif // ! _FINAL_RELEASE

        return true;
    }
    catch (std::pair<char const*, char const*> const Exception)
    {
        auto const [KeyName, TypeName] = Exception;
        OutError.append("required key '").append(KeyName).append("' (").append(TypeName).append(") not found");
        return false;
    }
    catch (toml::parse_error const& Exception)
    {
        OutError.append(Exception.description());
        if (auto const& Source = Exception.source().begin; Source)
            OutError.append(" (at ").append(std::to_string(Source.line))
            .append(":").append(std::to_string(Source.column)).append(")");
        return false;
    }
}

game::string ClientConfig::SelectedMatchmakerUrl() const
{
    if (!MatchmakerEnabled)
        return {};
    if (MatchmakerCustom && !MatchmakerCustomUrl.empty())
        return MatchmakerCustomUrl;
    return k_defaultMatchmakerUrl;
}

game::string ClientConfig::SelectedAccessKey() const
{
    return MatchmakerEnabled && MatchmakerCustom && !MatchmakerCustomUrl.empty() ? MatchmakerPassword : game::string{};
}


namespace
{
    std::string_view TrimToml(std::string_view Text)
    {
        auto const First = Text.find_first_not_of(" \t\r");
        if (First == std::string_view::npos)
            return {};
        return Text.substr(First, Text.find_last_not_of(" \t\r") - First + 1);
    }

    // The table a "[name]" line opens, or empty for any other line.
    std::string_view TomlTableName(std::string_view Line)
    {
        Line = TrimToml(Line);
        if (Line.empty() || Line.front() != '[')
            return {};
        auto const Close = Line.find(']');
        return Close == std::string_view::npos ? std::string_view{} : TrimToml(Line.substr(1, Close - 1));
    }

    // The key a "key = value" line sets, or empty for any other line.
    std::string_view TomlLineKey(std::string_view Line)
    {
        Line = TrimToml(Line);
        auto const Equals = Line.find('=');
        if (Line.empty() || Line.front() == '#' || Line.front() == '[' || Equals == std::string_view::npos)
            return {};
        return TrimToml(Line.substr(0, Equals));
    }

    std::string TomlBasicString(std::string_view const Value)
    {
        std::string Out{ "\"" };
        for (char const C : Value)
        {
            switch (C)
            {
            case '"':  Out += "\\\""; break;
            case '\\': Out += "\\\\"; break;
            case '\t': Out += "\\t"; break;
            case '\n': Out += "\\n"; break;
            case '\r': Out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(C) < 0x20 || C == 0x7F)
                    Out += std::format("\\u{:04X}", static_cast<unsigned>(static_cast<unsigned char>(C)));
                else
                    Out += C;
            }
        }
        return Out += '"';
    }
}

namespace
{
    struct TomlEntry
    {
        std::string                 Key;        // As written in the file, quoted if it has to be
        std::string                 Value;      // A TOML value, e.g. true or a quoted string
        std::string                 Alias{};    // Another spelling of Key that also matches
    };

    // Writes the file at InSourcePath to InTargetPath with Values set in the [SectionName] table,
    // keeping comments and other settings
    bool TryWriteTomlSection(std::wstring_view const InSourcePath, std::wstring_view const InTargetPath, std::string_view const SectionName, std::span<TomlEntry const> const Values, game::string& OutError) noexcept
    {
        OutError.clear();

        try
        {
            // An unreadable existing file must not be replaced by one holding only these keys
            ::game::vector<byte> Buffer{};
            std::error_code FileError{};
            if (!::ReadFileFull(InSourcePath, Buffer) && std::filesystem::exists(std::filesystem::path{ InSourcePath }, FileError))
            {
                OutError = "could not read the existing file";
                return false;
            }

            std::vector<bool> Written(Values.size());

            std::string const Original(reinterpret_cast<char const*>(Buffer.data()), Buffer.size());
            char const* const Newline = Original.find("\r\n") != std::string::npos ? "\r\n" : "\n";

            std::vector<std::string> Lines{};
            for (std::size_t Begin = 0; Begin < Original.size();)
            {
                std::size_t End = Original.find('\n', Begin);
                if (End == std::string::npos)
                    End = Original.size();
                std::string Line = Original.substr(Begin, End - Begin);
                if (!Line.empty() && Line.back() == '\r')
                    Line.pop_back();
                Lines.push_back(std::move(Line));
                Begin = End + 1;
            }

            std::size_t Section = Lines.size();
            for (std::size_t i = 0; i < Lines.size(); ++i)
            {
                if (TomlTableName(Lines[i]) == SectionName)
                {
                    Section = i;
                    break;
                }
            }

            if (Section == Lines.size())
            {
                if (!Lines.empty() && !TrimToml(Lines.back()).empty())
                    Lines.emplace_back();
                Lines.emplace_back("[" + std::string{ SectionName } + "]");
                Section = Lines.size() - 1;
            }

            std::size_t SectionEnd = Section + 1;
            while (SectionEnd < Lines.size() && TomlTableName(Lines[SectionEnd]).empty())
                ++SectionEnd;

            std::size_t InsertAt = Section + 1;
            for (std::size_t i = Section + 1; i < SectionEnd; ++i)
            {
                if (!TrimToml(Lines[i]).empty())
                    InsertAt = i + 1;

                std::string_view const Key = TomlLineKey(Lines[i]);
                for (std::size_t v = 0; v < Values.size(); ++v)
                {
                    if (!Key.empty() && (Key == Values[v].Key || Key == Values[v].Alias))
                    {
                        Lines[i] = Values[v].Key + " = " + Values[v].Value;
                        Written[v] = true;
                    }
                }
            }

            for (std::size_t v = 0; v < Values.size(); ++v)
            {
                if (!Written[v]) {
                    Lines.insert(Lines.begin() + static_cast<std::ptrdiff_t>(InsertAt++), Values[v].Key + " = " + Values[v].Value);
                }
            }

            std::string Text{};
            for (std::string const& Line : Lines)
                Text.append(Line).append(Newline);

            // Never replace the file with one the next start could not read.
            static_cast<void>(toml::parse(Text));

            if (!::TryWriteFileFullAtomic(InTargetPath, std::span<byte const>{ reinterpret_cast<byte const*>(Text.data()), Text.size() }))
            {
                OutError = "could not write the file";
                return false;
            }
            return true;
        }
        catch (toml::parse_error const& Exception)
        {
            OutError.append("the updated file would not parse: ").append(Exception.description());
            return false;
        }
        catch (std::exception const& Exception)
        {
            OutError.append(Exception.what());
            return false;
        }
    }
}

bool ClientConfig::TryWriteMatchmaker(std::wstring_view const InSourcePath, std::wstring_view const InTargetPath, game::string& OutError) const noexcept
{
    try
    {
        TomlEntry const Values[]
        {
            { "enabled",  MatchmakerEnabled ? "true" : "false" },
            { "custom",   MatchmakerCustom ? "true" : "false" },
            { "url",      TomlBasicString(MatchmakerCustomUrl) },
            { "password", TomlBasicString(MatchmakerPassword) },
        };
        return TryWriteTomlSection(InSourcePath, InTargetPath, "milan.matchmaker", Values, OutError);
    }
    catch (std::exception const& Exception)
    {
        OutError = Exception.what();
        return false;
    }
}

game::string ClientConfig::SeenNotice(std::string_view const Server) const
{
    for (auto const& [Url, Id] : SeenNotices)
    {
        if (std::string_view{ Url.c_str() } == Server)
            return Id;
    }
    return {};
}

void ClientConfig::RememberNotice(std::string_view const Server, std::string_view const Id)
{
    game::string const Value{ std::string{ Id }.c_str() };
    for (auto& [Url, Seen] : SeenNotices)
    {
        if (std::string_view{ Url.c_str() } == Server)
        {
            Seen = Value;
            return;
        }
    }
    SeenNotices.emplace_back(game::string{ std::string{ Server }.c_str() }, Value);
}

bool ClientConfig::TryWriteSeenNotice(std::wstring_view const InSourcePath, std::wstring_view const InTargetPath, std::string_view const Server,
    game::string& OutError) const noexcept
{
    try
    {
        TomlEntry const Values[]{ { TomlBasicString(Server), TomlBasicString(SeenNotice(Server)), "'" + std::string{ Server } + "'" } };
        return TryWriteTomlSection(InSourcePath, InTargetPath, "milan.notices", Values, OutError);
    }
    catch (std::exception const& Exception)
    {
        OutError = Exception.what();
        return false;
    }
}

ClientConfig* gp_clientConfig = nullptr;
game::string* gp_clientConfigError = nullptr;

namespace
{
    // Older releases kept the file in the game's Binaries\Win64 folder
    constexpr wchar_t const* k_legacyConfigPath = L"./MilanHost.toml";
}

std::wstring GetConfigPath()
{
    return ::GetLMPSaveFolder() + L"\\MilanHost.toml";
}

std::wstring GetLoadConfigPath()
{
    std::wstring const Path = GetConfigPath();
    std::error_code Error{};
    // Does new file exist?
    if (!std::filesystem::exists(Path, Error) && !Error && std::filesystem::exists(k_legacyConfigPath, Error))
    {
        // No, use old path
        return k_legacyConfigPath;
    }
    // Use new path
    return Path;
}

void InitGlobalConfig(wchar_t const* const Filename)
{
    gp_clientConfig = new (std::nothrow) ClientConfig;
    gp_clientConfigError = new (std::nothrow) game::string;

    if (!gp_clientConfig || !gp_clientConfigError)
        return;

    // Errors will be reported separately...
    if (!gp_clientConfig->TryRead(Filename, *gp_clientConfigError))
        delete std::exchange(gp_clientConfig, nullptr);
}

void CleanupGlobalConfig()
{
    if (gp_clientConfig != nullptr)
        delete std::exchange(gp_clientConfig, nullptr);

    if (gp_clientConfigError != nullptr)
        delete std::exchange(gp_clientConfigError, nullptr);
}
