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

> **Important**
>
> Updating C++ project configuration, which includes changing / adding source files,
> must be done through the CMakeLists.txt file for the project, and not Visual Studio
> itself. Additionally, it is not recommended to *create* new files via Visual Studio
> because it defaults to build root instead of source root. That makes it very easy
> to leave your new file(s) in a temporary, git-ignored build directory.


### Are you building server code?

```
> cargo build [--release] [-p 'server_*']
```

### Are you building Network Profiler?

All code emitting debug information relevant to network profiling is built normally
as a part of `MilanHost` in debug configurations. The Network Profiler application to
view captured information is built in a standalone Visual Studio solution that is
separate from the rest of the build system.

Open and build `./NetworkProfiler/NetworkProfiler.sln`.


## Repository structure:

This listing assumes you use the standard CMake preset (`VisualStudio`).

   - `.cargo/` - Cargo configuration (**required**).
   - `Build/` - Output directory.
     - `Cargo/` - Output for Rust code.
     - `VisualStudio/` - Output for C++ code.
       - `Projects/` - Generated Visual Studio solution and projects.
       - `{DEBUG|RELESE|FINAL}/` - Built artifacts.
   - `Crates/` - Local Rust libraries for client core and/or server.
   - `MilanCore/` - Rust library and CMake target for linking it into C++ code.
     - `client_core/` - ECSM implementation as a non-system library.
     - `client_ffi/` - System library wrapping ECSM interface for FFI.
     - `milan_ecsm_ffi.h` - The FFI bindings **auto-generated** at build time.
   - `MilanHost/` - The main Milan Client ASI, written in C++.
   - `MilanServer/` - Server executable(s).
   - `NetworkProfiler/` - Network Profiler application.
   - `ThirdParty/` - Dependencies provided via **submodules** (except for ImGui).
   - `Utilities/` - Smaller ASIs and/or tools.
   - `Cargo.toml` - Workspace for all Rust code in this project.
   - `CMakeLists.txt` - Workspace for all C++ code in this project.
   - `CMakePresets.json` - CMake configuration / build presets.
