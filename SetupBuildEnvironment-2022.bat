mkdir Build
"%ProgramFiles%\CMake\bin\cmake.exe" -S %~dp0 -B Build --preset "windows-clang-debug" -G "Visual Studio 17 2022"
del /f /q $Liberty.sln
mklink $Liberty.sln Build\Liberty.sln
