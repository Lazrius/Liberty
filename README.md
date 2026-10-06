![Liberty](/Docs/Banner.png)
[![Build Status]][actions] [![Discord Badge]][discord]

[Build Status]: https://github.com/HaydnTrigg/Liberty/actions/workflows/main.yml/badge.svg
[actions]: https://github.com/HaydnTrigg/Liberty/actions
[Discord Badge]: https://img.shields.io/badge/Discord-PC/Xbox%20Decompilation-blue?color=%237289DA&logo=discord&logoColor=%23FFFFFF
[discord]: https://discord.gg/v3xcYgHvNZ

This is a prototype export from Excalibur. A new delink based matching decomp coming soon.

> [!IMPORTANT]
> Visual Studio 2022
> * Desktop Development with C++
> * C++ MFC (x86 x x64)
> * C++ Clang Compiler for Windows
> * MSBuild support for LLVM (clang-cl) toolset

## Build Instructions

For both Windows and Linux you need copy DATA from Freelancer to the root directory (where you clone this repo).

### Windows:

* Run SetupBuildEnvironment-2026.bat or run cmake with one of the Windows presets
* Build Project (Pray)

### Linux

- Setup [msvc-wine](https://github.com/mstorsjo/msvc-wine). Recommend installing it to /opt/msvc
  - Ensure that it has MFC installed with it, it doesn't by default
- Ensure that clang-cl, LLVM, and associated utilities are installed
- Run `cmake --preset linux-clang-wine-debug -S $PWD -B $PWD/build/linux-clang-wine-debug`
- Run `cmake --build $PWD/build/linux-clang-wine-debug`
- (Pray even harder than you would for Windows)