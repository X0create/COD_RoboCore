param(
    [switch]$Headless,
    [switch]$Check,
    [ValidateSet('Chassis', 'SingleMotor')]
    [string]$Demo = 'Chassis',
    [ValidateSet('lf_rb', 'lb_rf')]
    [string]$Diagonal = 'lf_rb',
    [string]$Snapshot = '',
    [string]$PythonExe = 'C:\Develop\Anaconda\envs\mujoco\python.exe',
    [string]$ClionDir = (Join-Path $env:LOCALAPPDATA 'Programs\CLion')
)
$ErrorActionPreference = 'Stop'
$repoDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildDir = Join-Path $repoDir 'build\mujoco-windows'
# 用 CLion 的电脑编译工具，不能使用 PATH 中面向 STM32 的工具链。
$cmakeExe = Join-Path $ClionDir 'bin\cmake\win\x64\bin\cmake.exe'
$gccExe = Join-Path $ClionDir 'bin\mingw\bin\gcc.exe'
$ninjaExe = Join-Path $ClionDir 'bin\ninja\win\x64\ninja.exe'
foreach ($toolPath in @($PythonExe, $cmakeExe, $gccExe, $ninjaExe)) {
    if (-not (Test-Path -LiteralPath $toolPath -PathType Leaf)) {
        throw "Missing tool: $toolPath. Set -PythonExe or -ClionDir to your installation."
    }
}
& $cmakeExe -S $PSScriptRoot -B $buildDir -G Ninja `
    "-DCMAKE_C_COMPILER=$gccExe" "-DCMAKE_MAKE_PROGRAM=$ninjaExe" '-DCMAKE_BUILD_TYPE=Release'
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $cmakeExe --build $buildDir
if ($LASTEXITCODE -ne 0) { throw 'Control DLL build failed' }
$scriptName = if ($Demo -eq 'Chassis') { 'sentry_chassis.py' } else { 'single_motor.py' }
$pythonArgs = @('-B', (Join-Path $PSScriptRoot $scriptName))
if ($Demo -eq 'Chassis') {
    $pythonArgs += @('--diagonal', $Diagonal)
    if ($Snapshot) { $pythonArgs += @('--snapshot', $Snapshot) }
}
if ($Headless -or $Check) { $pythonArgs += '--headless' }
if ($Check) { $pythonArgs += '--check' }
& $PythonExe @pythonArgs
if ($LASTEXITCODE -ne 0) { throw 'MuJoCo demo failed' }
