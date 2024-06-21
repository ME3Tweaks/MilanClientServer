MilanClientServer
===================

You need to have the following installed when building on Windows:

   * Visual Studio 2022 with at least C++ workload and Windows 10 / 11 SDK.
      * You also need ".NET desktop development" workload to build the network profiler GUI.
   * Reasonably up-to-date `stable-x86_64-pc-windows-msvc` Rust toolchain.

Use the [official rustup-init tool](https://www.rust-lang.org/tools/install)
to install Rust, when prompted go with the stable channel and standard installation.
This will install compiler, standard library, and `cargo` - the build system and dependency
management tool.


## How to build:

### Are you building client code?

```
> cmake --preset VisualStudio
> .\Build\VisualStudio\Projects\MilanClient.sln
```

Note that the client `MilanHost` ASI will invoke `cargo` as a part of its
build process, but will only build the little subset of Rust code it requires.
These commands **must** be run in Developer PowerShell / Command Prompt for VS 2022.

> [!IMPORTANT]
> Updating C++ project configuration, which includes changing / adding source files,
> must be done through the CMakeLists.txt file for the project, and not Visual Studio
> itself. Additionally, it is not recommended to *create* new files via Visual Studio
> because it defaults to build root instead of source root. That makes it very easy
> to leave your new file(s) in a temporary, git-ignored build directory.
>


### Are you building server code?

```
> cargo build [--release] [-p 'server_*']
```

### Are you building Network Profiler?

Open and build `./NetworkProfiler/NetworkProfiler.sln`.

> [!NOTE]
> All code emitting debug information relevant to network profiling is built normally
> as a part of `MilanHost` in debug configurations. The Network Profiler application for
> viewing captured information is built in a standalone Visual Studio solution that is
> separate from the rest of this project's build system.
>


## Repository structure:

This listing assumes the standard CMake preset (`VisualStudio`).

```
.cargo/                     - cargo configuration (required!)
Build/                      - output directory
  Cargo/                    - output for rust code
  VisualStudio/             - output for c++ code
    Projects/               - generated visual studio solution and projects
    {DEBUG|RELESE|FINAL}/   - directories for built artifacts
    MilanBuildConfig.cmake  - workspace-wide build configuration
  Crates/                   - local rust libraries
MilanCore/                  - the rust library and a cmake target for linking it into c++ code
  client_core/              - ecsm implementation as a non-system library
  client_ffi/               - system library wrapping the ecsm interface for ffi
  milan_ecsm_ffi.h          - auto-generated ffi bindings (git-ignored!)
MilanHost/                  - the main milan client asi, written in c++
MilanServer/                - server executable(s)
NetworkProfiler/            - gui network profiler application
ThirdParty/                 - dependencies provided via submodules
Utilities/                  - smaller asi plugins and/or tools
Cargo.toml                  - workspace for all rust code in this project
CMakeLists.txt              - workspace for all c++ code in this project
CMakePresets.json           - cmake configuration / build presets
```


## Third-party dependencies:

This is a non-authoritative list of external software dependencies required to build
all or parts of this project, in no particular order.

```
General:
  - Corrosion - v0.4.7 - MIT - Andrew Gaspar
  - ImGui - v1.90.5 - MIT - Omar Cornut
  - ImPlot - #f156599 - MIT - Evan Pezent
  - Zydis - v4.1.0 - MIT - Florian Bernd & Joel Höner

Network Profiler:
  - CommunityToolkit.Mvvm - v8.2.2 - MIT - .NET Foundation and contributors
  - Microsoft.Xaml.Behaviors.Wpf - v1.1.122 - MIT - Microsoft
  - ScottPlot.WPF - v5.0.35 - MIT - Scott Harden / Harden Technologies, LLC
```
