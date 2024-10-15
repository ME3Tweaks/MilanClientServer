#include <format>
#include <tomlplusplus/toml.hpp>

#include "Milan/ClientConfig.hpp"


#define MLN_CONFIG(in, path, type)                                      \
    [&]() -> type {                                                     \
        auto option = in.at_path(path).value<type>();                   \
        if (!option) { throw std::make_pair(L"" path, L"" #type); }     \
        return option.value();                                          \
    } ()

#define MLN_CONFIG_OPT(in, path, type, alt)                             \
    in.at_path(path).value<type>().value_or(alt)


bool ClientConfig::TryRead(std::wstring_view const InPath, FString& OutError)
{
    OutError.Clear();

    try
    {
        toml::table const Table = toml::parse_file(InPath);

        ConnectHost         = MLN_CONFIG(Table, "milan.connect.host", std::string);
        ConnectPort         = MLN_CONFIG(Table, "milan.connect.port", int);
        LoggingVerbose      = MLN_CONFIG_OPT(Table, "milan.logging.verbose", bool, false);

        return true;
    }
    catch (std::pair<wchar_t const*, wchar_t const*> const Exception)
    {
        auto const [KeyName, TypeName] = Exception;
        OutError.AppendFormat(L"required key '%s' (%s) not found", KeyName, TypeName);
        return false;
    }
    catch (toml::parse_error const& Exception)
    {
        OutError.AppendUtf8(Exception.description().data());
        if (auto const& Source = Exception.source().begin; Source)
            OutError.AppendFormat(L" (at {}:{})", Source.line, Source.column);
        return false;
    }
}
