param([string]$Version = 'dev', [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[A-Za-z0-9._-]+$') { throw 'Invalid version label.' }
$project = Split-Path $PSScriptRoot -Parent
$repo = Split-Path (Split-Path $project -Parent) -Parent
if (!$OutputDirectory) { $OutputDirectory = Join-Path $project 'dist' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$build = Join-Path $output ('build-' + [guid]::NewGuid().ToString('N'))
$package = Join-Path $build 'package'
New-Item -ItemType Directory -Force $package | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ x86 tools are required.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$hud = Join-Path $project 'hud-asi'
$nativeBuild = Join-Path $build 'native.cmd'
@"
@echo off
call "$vcvars" x86 >nul
if errorlevel 1 exit /b 1
cd /d "$build"
cl /nologo /std:c++17 /O2 /MT /W4 /WX /EHsc /LD /I"$hud" "$hud\TIJHudFix.cpp" /link /OUT:TIJHudFix.asi /IMPLIB:TIJHudFix.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /MT /W4 /WX /EHsc /I"$hud" "$hud\test_anchor.cpp" /Fe:test_anchor.exe
if errorlevel 1 exit /b 1
test_anchor.exe
"@ | Set-Content -LiteralPath $nativeBuild -Encoding ascii
& $nativeBuild
if ($LASTEXITCODE -ne 0) { throw 'HUD build or anchor tests failed.' }
$wrapperZip = Join-Path $output 'dxwrapper-v1.8.8600.25.zip'
$wrapperHash = '3894368EBCE69F348D08E189C2F0818C49AA3B18AD01E06348743BF6A29AF97A'
if (!(Test-Path -LiteralPath $wrapperZip)) { Invoke-WebRequest 'https://github.com/elishacloud/dxwrapper/releases/download/v1.8.8600.25/dxwrapper.zip' -OutFile $wrapperZip }
if ((Get-FileHash -LiteralPath $wrapperZip -Algorithm SHA256).Hash -ne $wrapperHash) { throw 'DxWrapper archive hash mismatch.' }
$wrapper = Join-Path $build 'wrapper'
Expand-Archive -LiteralPath $wrapperZip -DestinationPath $wrapper
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$optionsSource = Join-Path $project 'options-app\GameOptions.cs'
& $csc /nologo /target:winexe /optimize+ ('/out:' + (Join-Path $package 'TIJ Options.exe')) /reference:System.Windows.Forms.dll /reference:System.Drawing.dll $optionsSource
if ($LASTEXITCODE -ne 0) { throw 'Options app build failed.' }
$resources = @(
    ('/resource:' + (Join-Path $project 'installer\missions.tsv') + ',missions.tsv'),
    ('/resource:' + (Join-Path $wrapper 'dxwrapper.dll') + ',dxwrapper.dll'),
    ('/resource:' + (Join-Path $wrapper 'Stub\d3d8.dll') + ',d3d8.dll'),
    ('/resource:' + (Join-Path $build 'TIJHudFix.asi') + ',TIJHudFix.asi')
)
& $csc /nologo /target:winexe /optimize+ /main:ItalianJobRelease.Installer ('/out:' + (Join-Path $package 'TIJ Fixes.exe')) /reference:System.Windows.Forms.dll /reference:System.Drawing.dll @resources (Join-Path $project 'installer\Installer.cs') $optionsSource
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed.' }
Copy-Item -LiteralPath (Join-Path $project 'README.md') -Destination (Join-Path $package 'README.md')
New-Item -ItemType Directory -Path (Join-Path $package 'licenses') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $package 'licenses\Fixes-MIT.txt')
Copy-Item -LiteralPath (Join-Path $wrapper 'License.txt') -Destination (Join-Path $package 'licenses\DxWrapper.txt')
@"
Version: $Version
DxWrapper: v1.8.8600.25
DxWrapper archive SHA256: $wrapperHash
"@ | Set-Content -LiteralPath (Join-Path $package 'VERSION.txt')
$archive = Join-Path $output "ItalianJob-Fixes-$Version.zip"
Compress-Archive -Path (Join-Path $package '*') -DestinationPath $archive -Force
$checksum = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($archive)
$checksum | Set-Content -LiteralPath ($archive + '.sha256') -Encoding ascii
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    $entries = @($zip.Entries.FullName)
    $expected = @('TIJ Options.exe','TIJ Fixes.exe','README.md','VERSION.txt','licenses/Fixes-MIT.txt','licenses/DxWrapper.txt') | Sort-Object
    $actual = @($entries | ForEach-Object { $_.Replace('\','/') }) | Sort-Object
    if (Compare-Object $expected $actual) { throw 'Release archive contains unexpected or missing files.' }
} finally { $zip.Dispose() }
Write-Output "Package: $archive"
Write-Output "Unpacked: $package"
