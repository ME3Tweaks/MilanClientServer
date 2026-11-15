#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "Online/ModVersion.hpp"

namespace fs = std::filesystem;

void Check(bool value, char const* message)
{
    if (!value)
        throw std::runtime_error(message);
}

int main(int argc, char** argv)
try
{
    if (argc == 3 && std::string_view{ argv[1] } == "--inspect")
    {
        auto const result = ModVersion::Resolve(fs::path{ argv[2] }, {});
        Check(!result.Version.empty(), result.Error.data());
        std::cout << "Installed Project Milan version: " << result.Version << "\n";
        std::cout << "Source: " << result.MetadataPath.string() << "\n";
        return 0;
    }

    auto temporary = fs::temp_directory_path();
    if (temporary.filename().empty())
        temporary = temporary.parent_path();
    auto const root = temporary / ("milan-mod-version-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(root.parent_path() == temporary && root.filename().string().starts_with("milan-mod-version-test-"), "unsafe fixture path");
    Check(fs::create_directory(root), "fixture directory already exists");
    struct Cleanup
    {
        fs::path Path;
        ~Cleanup() { std::error_code error; fs::remove_all(Path, error); }
    } cleanup{ root };

    auto const executable = root / "Game" / "LMP" / "Binaries" / "Win64" / "MassEffectMultiplayer.exe";
    auto const metadata = root / "Game" / "LMP" / "BioGame" / "DLC" / "DLC_MOD_ProjectMilan" / "_metacmm.txt";
    fs::create_directories(metadata.parent_path());
    auto write = [&](std::string const& contents)
    {
        std::ofstream output(metadata, std::ios::binary | std::ios::trunc);
        output << contents;
        output.close();
        Check(!output.fail(), "could not write fixture");
    };
    Check(ModVersion::Resolve(executable, {}).Version.empty(), "missing metadata enabled matchmaking");
    write("\xEF\xBB\xBFProject Milan\r\n1.0.14\r\n9999\r\nextra metadata");
    auto const installed = ModVersion::Resolve(executable, {});
    Check(installed.Version == "1.0.14" && installed.MetadataPath == metadata && installed.Error.empty(), "wrong installed version/path");
    Check(installed.From == ModVersion::Source::Metadata, "wrong metadata source");
    Check(ModVersion::Resolve(executable, "config-2").Version == "config-2", "configuration did not override metadata");
    auto const cli = ModVersion::Resolve(executable, "config-2", std::string{ "cli-3" });
    Check(cli.Version == "cli-3" && cli.From == ModVersion::Source::CommandLine, "command line did not override config");
    Check(ModVersion::Resolve(executable, {}, std::string{}).Version.empty(), "empty explicit override fell back to metadata");
    Check(ModVersion::Resolve(executable, "invalid value").Version.empty(), "invalid config fell back to metadata");
    write("Project Milan\n1.0.15");
    Check(ModVersion::Resolve(executable, {}).Version == "1.0.15", "reader cached an old version or rejected final line without newline");
    write("\n1.0.16\n1234");
    Check(ModVersion::Resolve(executable, {}).Version == "1.0.16", "physical line positions were not preserved");
    for (auto const& contents : { std::string{ "Project Milan\n\n1234" }, std::string{ "1.0.14" },
        std::string{ "Project Milan\ninvalid value\n1234" }, std::string{ "Project Milan\n" } + std::string(65, 'a'),
        std::string(4097, 'a') + "\n1.0.14", std::string{ "Project Milan\n1.0" } + '\0' + "14\n1234" })
    {
        write(contents);
        auto const invalid = ModVersion::Resolve(executable, {});
        Check(invalid.Version.empty() && !invalid.Error.empty(), "malformed metadata enabled matchmaking");
    }
    write("Project Milan\r\n" + std::string(64, 'a') + "\r\n1234");
    Check(ModVersion::Resolve(executable, {}).Version.size() == 64, "64-character version rejected");
    Check(ModVersion::Resolve(fs::path{ "relative.exe" }, {}).Version.empty(), "relative executable path accepted");
    Check(ModVersion::Resolve(fs::path{}, {}).Version.empty(), "missing executable path accepted");
    std::cout << "Mod version tests passed (installed path, metadata parsing, bounds, fresh reads, and override precedence).\n";
}
catch (std::exception const& error)
{
    std::cerr << "Mod version test failed: " << error.what() << '\n';
    return 1;
}
