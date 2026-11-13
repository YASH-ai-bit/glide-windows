param([switch]$TestsOnly)
$ErrorActionPreference = 'Stop'
$glideRoot = $PSScriptRoot
$glideVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$glideVs = & $glideVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $glideVs) { throw 'MSVC C++ build tools are required.' }
$glideVars = Join-Path $glideVs 'VC\Auxiliary\Build\vcvars64.bat'
$glideEnvironment = & cmd.exe /d /c ('call "' + $glideVars + '" >nul && set')
foreach ($glideLine in $glideEnvironment) {
    if ($glideLine -match '^(PATH|INCLUDE|LIB|LIBPATH)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$glideOut = Join-Path $glideRoot 'artifacts\native'
[void](New-Item -ItemType Directory -Path $glideOut -Force)
$glideSources = @(Join-Path $glideRoot 'native\glide.cpp')
$glideLibraries = @('user32.lib','gdi32.lib','shell32.lib','dwmapi.lib','hid.lib','advapi32.lib','comctl32.lib')
$glideFlags = @('/nologo','/std:c++17','/EHsc','/O2','/MT','/W4','/DUNICODE','/D_UNICODE','/utf-8')
if (-not $TestsOnly) {
    & cl.exe @glideFlags ('/Fo:' + (Join-Path $glideOut 'Glide.obj')) ('/Fe:' + (Join-Path $glideOut 'Glide.exe')) @glideSources /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED ('/MANIFESTINPUT:' + (Join-Path $glideRoot 'native\app.manifest')) @glideLibraries
    if ($LASTEXITCODE -ne 0) { throw 'Native application compilation failed.' }
}
& cl.exe @glideFlags /DGLIDE_TEST_BUILD ('/Fo:' + (Join-Path $glideOut 'Tests.obj')) ('/Fe:' + (Join-Path $glideOut 'Glide.Tests.exe')) (Join-Path $glideRoot 'native\tests.cpp') /link /SUBSYSTEM:CONSOLE @glideLibraries
if ($LASTEXITCODE -ne 0) { throw 'Native tests compilation failed.' }
Write-Output ('Native binaries: ' + $glideOut)
