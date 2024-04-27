MilanClientServer
===================

You need to have the following installed:

   * Visual Studio 2022 with at least C++ workload.
   * Reasonably up-to-date `stable-x86_64-pc-windows-msvc` Rust toolchain.

Use the [official rustup-init](https://www.rust-lang.org/tools/install) tool
to install Rust, when prompted go with the stable channel and standard installation.
This will install compiler, standard library, and `cargo` - the build system and dependency
management tool.


## How to build:

**Are you building client code?**

```
> cmake --preset VisualStudio
> .\Build\VisualStudio\Projects\MilanClient.sln
```

Note that the client `MilanHost` ASI will invoke `cargo` as a part of its
build process, but will only build the little subset of Rust code it requires.
These commands **must** be run in Developer PowerShell / Command Prompt for VS 2022.

**Are you building server code?**

```
> cargo build [--release] [-p 'server_*']
```


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
   - `ThirdParty/` - Dependencies provided via **submodules** (except for ImGui).
   - `Utilities/` - Smaller ASIs and/or tools.
   - `Cargo.toml` - Workspace for all Rust code in this project.
   - `CMakeLists.txt` - Workspace for all C++ code in this project.
   - `CMakePresets.json` - CMake configuration / build presets.
