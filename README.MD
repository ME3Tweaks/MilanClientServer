MilanClientServer
===================

> [!WARNING]
> This document, and especially build instructions, may be a bit out of date.
> The primary working branch now and for the foreseeable future is `server`.
>

You need to have the following installed when building on Windows:

   * Visual Studio 2022 with at least C++ workload and Windows 10 / 11 SDK.
      * You also need ".NET desktop development" workload to build the network profiler GUI.


## How to build:

### Are you building client code?

```
> cmake --preset VisualStudio
> .\Build\VisualStudio\Projects\MilanClient.sln
```

These commands **must** be run in Developer PowerShell / Command Prompt for VS 2022.

> [!IMPORTANT]
> Updating C++ project configuration, which includes changing / adding source files,
> must be done through the CMakeLists.txt file for the project, and not Visual Studio
> itself. Additionally, it is not recommended to *create* new files via Visual Studio
> because it defaults to build root instead of source root. That makes it very easy
> to leave your new file(s) in a temporary, git-ignored build directory.
>


### Are you building server code?

(TODO: Document server building.)

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
Build/                      - output directory, can safely be deleted between builds
  VisualStudio/             - generated visual studio solution and projects
    {DEBUG|RELEASE|FINAL}/  - output for c++ code
MilanHost/                  - the main milan client asi, written in c++
MilanServer/                - server executable(s)
NetworkProfiler/            - gui network profiler application
Patching/                   - index and git diffs for locally patching submodules
ThirdParty/                 - dependencies provided via submodules
CMakeLists.txt              - workspace for all c++ code in this project
CMakePresets.json           - cmake configuration / build presets
MilanBuildConfig.cmake      - workspace-wide build configuration
MilanPatchHooks.cmake       - cmake logic for locally patching submodules
MilanTargetUtils.cmake      - auxiliary logic for adding cmake targets
```


## Third-party dependencies:

This is a non-authoritative list of external software dependencies required to build
all or parts of this project, in no particular order.

```
General:
  - ImGui - v1.91.3 - MIT - Omar Cornut
  - RenderDoc API - #9f4f0e6 - MIT - Baldur Karlsson & Crytek
  - toml++ - v3.4.0 - MIT - Mark Gillard
  - Zydis - v4.1.0 - MIT - Florian Bernd & Joel Höner

Network Profiler:
  - CommunityToolkit.Mvvm - v8.4.0 - MIT - .NET Foundation and contributors
  - Microsoft.Xaml.Behaviors.Wpf - v1.1.135 - MIT - Microsoft
  - ScottPlot.WPF - v5.0.53 - MIT - Scott Harden / Harden Technologies, LLC
```
