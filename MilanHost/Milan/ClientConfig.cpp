#include <format>
#include <tomlplusplus/toml.hpp>

#include "Milan/ClientConfig.hpp"


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
        toml::table const Table = toml::parse_file(InPath);

        ConnectHost         = MLN_CONFIG(Table, "milan.connect.host", std::string);
        ConnectPort         = MLN_CONFIG(Table, "milan.connect.port", int);

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

ClientConfig* gp_clientConfig = nullptr;
game::string* gp_clientConfigError = nullptr;

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
