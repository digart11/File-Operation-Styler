param(
    [string]$CompilerRoot = "$env:ProgramFiles\Windhawk\Compiler",
    [string]$ManifestTool = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.26100.0\x64\mt.exe"
)
$ErrorActionPreference = 'Stop'
$testOutput = Join-Path $env:TEMP 'FileOperationStyler-overflow-tests'
New-Item -ItemType Directory -Force -Path $testOutput | Out-Null
$testExe = Join-Path $testOutput 'transfer-viewport-tests.exe'
$compiler = Join-Path $CompilerRoot 'bin\clang++.exe'
$testFlags = @(
    '-x', 'c++', '-std=c++23', '-target', 'x86_64-w64-mingw32',
    '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
    '-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008',
    '-D__USE_MINGW_ANSI_STDIO=0', '-DWH_MOD', '-DWH_EDITING',
    '-include', 'windhawk_api.h', '-Wall', '-Wextra',
    '-Wno-unused-parameter', '-Wno-missing-field-initializers',
    '-Wno-cast-function-type-mismatch', '-static'
)
& $compiler @testFlags (Join-Path $PSScriptRoot 'transfer-viewport-tests.cpp') `
    -o $testExe -lcomctl32 -lgdi32 -lgdiplus -lshlwapi -ladvapi32 -luxtheme
if ($LASTEXITCODE) { throw 'Viewport test compilation failed' }
# comctl32 subclass exports require the same v6 activation context as Explorer.
$manifestPath = Join-Path $testOutput 'tests.manifest'
@'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><assemblyIdentity version="1.0.0.0" processorArchitecture="amd64" name="FileOperationStyler.ViewportTests" type="win32"/><dependency><dependentAssembly><assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls" version="6.0.0.0" processorArchitecture="amd64" publicKeyToken="6595b64144ccf1df" language="*"/></dependentAssembly></dependency></assembly>
'@ | Set-Content -LiteralPath $manifestPath -Encoding UTF8
& $ManifestTool -nologo -manifest $manifestPath "-outputresource:$testExe;#1"
if ($LASTEXITCODE) { throw 'Viewport test manifest embedding failed' }
& $testExe
if ($LASTEXITCODE) { throw "Viewport tests failed: $LASTEXITCODE" }
