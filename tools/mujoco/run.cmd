@echo off
setlocal
rem Prefer PowerShell 7; the fallback ships with Windows.
set "psExe=%ProgramFiles%\PowerShell\7\pwsh.exe"
if not exist "%psExe%" set "psExe=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
rem Bypass applies to this process only; no persistent execution policy is changed.
"%psExe%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" %*
set "runExit=%ERRORLEVEL%"
echo.
if not "%runExit%"=="0" echo [ERROR] MuJoCo launcher failed. See the output above.
rem Keep the console visible after a double-click. Commands with arguments do not pause.
if "%~1"=="" pause
exit /b %runExit%
