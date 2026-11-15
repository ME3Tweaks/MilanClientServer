Project Milan - client/server code
===================

You may want to visit the [main project repository](https://github.com/ME3Tweaks/ProjectMilan) instead.

## How to build

### Are you building client code?

Run this in Developer PowerShell / Command Prompt for Visual Studio 2026:

```
> cmake --preset VisualStudio
> cmake --build --preset VisualStudio
; and/or open the generated Visual Studio solution:
> .\Build\VisualStudio\Projects\MilanClient.sln
```

> [!IMPORTANT]
> Updating C++ project configuration, which includes changing / adding source files,
> must be done through the CMakeLists.txt file for the project, and not Visual Studio
> itself. Additionally, it is not recommended to *create* new files via Visual Studio
> because it defaults to build root instead of source root. That makes it very easy
> to leave your new file(s) in a temporary, git-ignored build directory.
>

### Are you building server code?

See the [server documentation](./MilanServer/README.md).

### Are you building Network Profiler?

Open and build `./NetworkProfiler/NetworkProfiler.sln`.

> [!NOTE]
> All code emitting debug information relevant to network profiling is built normally
> as a part of `MilanHost` in debug configurations. The Network Profiler application for
> viewing captured information is built in a standalone Visual Studio solution that is
> separate from the rest of this project's build system.
>

## Third-party dependencies

This is a non-authoritative list of external dependencies required to build
parts of this project, in no particular order.

```
MilanHost:
  - fpng - v1.0.6 - The Unlicense - Richard Geldreich, Jr
  - ImGui - v1.93.0 - MIT - Omar Cornut
  - nlohmann/json - v3.12.0 - MIT - Niels Lohmann
  - RenderDoc API - #9f4f0e6 - MIT - Baldur Karlsson & Crytek
  - toml++ - v3.4.0 - MIT - Mark Gillard
  - Zydis - v4.1.0 - MIT - Florian Bernd & Joel Höner

NetworkProfiler:
  - CommunityToolkit.Mvvm - v8.4.0 - MIT - .NET Foundation and contributors
  - Microsoft.Xaml.Behaviors.Wpf - v1.1.135 - MIT - Microsoft
  - ScottPlot.WPF - v5.0.55 - MIT - Scott Harden / Harden Technologies, LLC
```

## License

See the [server documentation](./MilanServer/README.md) for server code license.
