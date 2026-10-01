@echo off
rem Keil user command. Keil compiles nothing real; CMake is the only build (ADR 0019, ADR 0052).
rem   build_with_cmake.bat ROBOT build  - Before Build: build h723-ROBOT-debug with CMake in WSL
rem   build_with_cmake.bat ROBOT copy   - After Build:  overwrite the Keil placeholder .axf with the
rem                                       CMake ELF, so Download (F8) and Debug use the GCC firmware
rem ROBOT is the directory name under 01_applic (bench, infantry). An .axf is an ELF file.
setlocal
set ROBOT=%~1
set MODE=%~2
set REPO=%~dp0..\..\..
set ELF=%REPO%\build\h723-%ROBOT%-debug\COD_RoboCore.elf
set AXF=%REPO%\build\keil\%ROBOT%\COD_RoboCore.axf
if "%MODE%"=="copy" goto copy

for /f "usebackq delims=" %%i in (`wsl.exe -d Ubuntu-24.04 wslpath -a "%REPO%"`) do set REPO_WSL=%%i
echo [cmake] building h723-%ROBOT%-debug
wsl.exe -d Ubuntu-24.04 -- bash -lc "cd '%REPO_WSL%' && cmake --preset h723-%ROBOT%-debug > /dev/null && cmake --build --preset h723-%ROBOT%-debug"
if errorlevel 1 (
    echo [cmake] build FAILED, see the messages above
    exit /b 1
)
exit /b 0

:copy
copy /y "%ELF%" "%AXF%" > nul
if errorlevel 1 (
    echo [cmake] copy FAILED: %ELF%
    exit /b 1
)
echo [cmake] %AXF% now holds the CMake ELF
exit /b 0
