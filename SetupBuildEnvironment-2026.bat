mkdir Build
"%ProgramFiles%\CMake\bin\cmake.exe" -S %~dp0 -B Build --preset "windows-clang-debug"
del /f /q $Liberty.slnx
mklink $Liberty.slnx Build\Liberty.slnx
