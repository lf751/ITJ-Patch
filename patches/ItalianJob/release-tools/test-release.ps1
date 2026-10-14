param([string]$GameRoot)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$build = Get-ChildItem (Join-Path $project 'dist') -Directory -Filter 'build-*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (!$build) { throw 'Build the release first.' }
$fixture = Join-Path $build.FullName ('installer-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $fixture | Out-Null
$bytes = New-Object byte[] 1536
function Put16($offset, $value) { [BitConverter]::GetBytes([uint16]$value).CopyTo($bytes, $offset) }
function Put32($offset, $value) { [BitConverter]::GetBytes([uint32]$value).CopyTo($bytes, $offset) }
$bytes[0] = 77; $bytes[1] = 90; Put32 60 128; Put32 128 17744; Put16 132 332; Put16 134 2; Put16 148 224
[Text.Encoding]::ASCII.GetBytes('.text').CopyTo($bytes, 376)
[Text.Encoding]::ASCII.GetBytes('_rwcseg').CopyTo($bytes, 416)
Put32 452 3221225536
$callOffset = 544
$call = [byte[]](232,17,34,51,68); $call.CopyTo($bytes, $callOffset)
$normalized = $bytes.Clone(); [BitConverter]::GetBytes([uint32]3758096448).CopyTo($normalized,452)
544..548 | ForEach-Object { $normalized[$_] = 144 }
$hash = [Security.Cryptography.SHA256]::Create()
try { $digest = [BitConverter]::ToString($hash.ComputeHash($normalized)).Replace('-','').ToLowerInvariant() } finally { $hash.Dispose() }
[IO.File]::WriteAllBytes((Join-Path $fixture 'mission.bin'), $bytes)
"stg001.exe`t544`te811223344`t$digest" | Set-Content (Join-Path $fixture 'missions.tsv') -Encoding ascii
$testCode = @'
using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using ItalianJobRelease;
class ReleaseTests {
 static void Require(bool b,string message) { if(!b) throw new Exception(message); }
 static string Fingerprint(string root) { using(var h=SHA256.Create()) return string.Join("|",Directory.GetFiles(root,"*",SearchOption.AllDirectories).OrderBy(p=>p).Select(p=>p+":"+Convert.ToBase64String(h.ComputeHash(File.ReadAllBytes(p))))); }
 static int Main(string[] args) {
  try {
   string root=Path.Combine(args[0],"game");Directory.CreateDirectory(Path.Combine(root,"code","exes"));
   string mission=Path.Combine(root,"code","exes","stg001.exe"), ini=Path.Combine(root,"code","exes","dxwrapper.ini");
   byte[] original=File.ReadAllBytes(Path.Combine(args[0],"mission.bin"));File.WriteAllBytes(mission,original);
   File.WriteAllBytes(Path.Combine(root,"TIJ.exe"),new byte[]{77,90});
   byte[] save=Enumerable.Range(0,352).Select(i=>(byte)(i%251)).ToArray();File.WriteAllBytes(Path.Combine(root,"res_data.dat"),save);
   File.WriteAllText(ini,"[General]\nLoadCustomDllPath =\n[Compatibility]\nD3d8to9 = 0\n[User]\nKeepMe = Yes\n");
   File.WriteAllText(Path.Combine(root,"code","exes","rtx.conf"),"user-authored Remix settings\n");
   Installer.Apply(root);byte[] patched=File.ReadAllBytes(mission);
   Require(patched.Skip(544).Take(5).All(b=>b==144),"Click injection not removed.");
   Require((BitConverter.ToUInt32(patched,452)&0x20000000)!=0,"RenderWare code not executable.");
   Require(File.ReadAllBytes(Path.Combine(root,"res_data.dat")).SequenceEqual(save),"Save was modified.");
   Require(File.ReadAllText(ini).Contains("KeepMe = Yes"),"INI setting lost.");
   Require(File.ReadAllText(Path.Combine(root,"dxwrapper.ini")).Contains("LoadCustomDllPath = TIJHudFix.asi"),"Frontend ASI loader missing.");
   Require(File.ReadAllBytes(Path.Combine(root,"TIJHudFix.asi")).SequenceEqual(File.ReadAllBytes(Path.Combine(root,"code","exes","TIJHudFix.asi"))),"Frontend and mission ASIs differ.");
   Require(File.ReadAllText(Path.Combine(root,"code","exes","rtx.conf"))=="user-authored Remix settings\n","Remix settings lost.");
   Require(!Directory.Exists(Path.Combine(root,"options-backups")),"Backups created.");
   string before=Fingerprint(root);Installer.Apply(root);Require(before==Fingerprint(root),"Reinstall not idempotent.");
   byte[] unsupported=(byte[])original.Clone();unsupported[1400]=3;File.WriteAllBytes(mission,unsupported);before=Fingerprint(root);
   bool rejected=false;try{Installer.Apply(root);}catch(InvalidDataException){rejected=true;}Require(rejected&&before==Fingerprint(root),"Unsupported version partially modified installation.");
   File.WriteAllBytes(mission,original);File.WriteAllText(Path.Combine(root,"d3d8.dll"),"foreign wrapper");before=Fingerprint(root);rejected=false;
   try{Installer.Apply(root);}catch(InvalidOperationException){rejected=true;}Require(rejected&&before==Fingerprint(root),"Foreign wrapper overwritten or partial patch applied.");
   File.Delete(Path.Combine(root,"d3d8.dll"));
   File.WriteAllText(Path.Combine(root,"dxwrapper.ini"),"[General]\nLoadCustomDllPath = OtherPlugin.dll\n");before=Fingerprint(root);rejected=false;
   try{Installer.Apply(root);}catch(InvalidOperationException){rejected=true;}Require(rejected&&before==Fingerprint(root),"Frontend custom loader overwritten or partial patch applied.");
   Console.WriteLine("PASS: installation, execute flag, click fix, save/config preservation, idempotence, unsupported build rejection, foreign-wrapper rejection.");return 0;
  }catch(Exception e){Console.Error.WriteLine(e);return 1;}
 }
}
'@
$testSource = Join-Path $fixture 'ReleaseTests.cs'; $testCode | Set-Content $testSource -Encoding ascii
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$resources = @(
    ('/resource:' + (Join-Path $fixture 'missions.tsv') + ',missions.tsv'),
    ('/resource:' + (Join-Path $build.FullName 'wrapper\dxwrapper.dll') + ',dxwrapper.dll'),
    ('/resource:' + (Join-Path $build.FullName 'wrapper\Stub\d3d8.dll') + ',d3d8.dll'),
    ('/resource:' + (Join-Path $build.FullName 'TIJHudFix.asi') + ',TIJHudFix.asi')
)
$testExe = Join-Path $fixture 'ReleaseTests.exe'
& $csc /nologo /target:exe /main:ReleaseTests ('/out:' + $testExe) /reference:System.Windows.Forms.dll /reference:System.Drawing.dll @resources (Join-Path $project 'installer\Installer.cs') (Join-Path $project 'options-app\GameOptions.cs') $testSource
if ($LASTEXITCODE -ne 0) { throw 'Installer tests failed to compile.' }
& $testExe $fixture
if ($LASTEXITCODE -ne 0) { throw 'Installer tests failed.' }
# Exercise the actual packaged options app without requiring proprietary game files in CI.
$optionsFixture = Join-Path $fixture 'options-game'
New-Item -ItemType Directory -Force $optionsFixture | Out-Null
$displayBytes = New-Object byte[] 512
for ($i = 0; $i -lt 6; $i++) {
    $widths = @(640,640,800,800,1024,1024); $heights = @(480,480,600,600,768,768)
    $offset = 64 + $i * 53
    ([byte[]](129,125,204)).CopyTo($displayBytes, $offset - 3)
    [BitConverter]::GetBytes([int]$widths[$i]).CopyTo($displayBytes, $offset)
    ([byte[]](117,44,129,125,208)).CopyTo($displayBytes, $offset + 4)
    [BitConverter]::GetBytes([int]$heights[$i]).CopyTo($displayBytes, $offset + 9)
    ([byte[]](117,35,131,125,200)).CopyTo($displayBytes, $offset + 13)
    $displayBytes[$offset + 18] = if ($i % 2 -eq 0) { 16 } else { 32 }
}
[IO.File]::WriteAllBytes((Join-Path $optionsFixture 'TIJ.exe'), $displayBytes)
[IO.File]::WriteAllBytes((Join-Path $optionsFixture 'res_data.dat'), (New-Object byte[] 352))
$options = Join-Path $build.FullName 'package\TIJ Options.exe'
$report = Join-Path $fixture 'options-ci-test.txt'
$process = Start-Process -FilePath $options -ArgumentList @('--self-test', ('"' + $optionsFixture + '"'), ('"' + $report + '"')) -Wait -PassThru
if ($process.ExitCode -ne 0) { throw (Get-Content $report -Raw) }
Get-Content $report
if ($GameRoot) {
    $gameFixture = Join-Path $fixture 'actual-game'
    New-Item -ItemType Directory -Force (Join-Path $gameFixture 'code\exes') | Out-Null
    Copy-Item (Join-Path $GameRoot 'TIJ.exe'),(Join-Path $GameRoot 'res_data.dat') $gameFixture
    Copy-Item (Join-Path $GameRoot 'code\exes\stg*.exe') (Join-Path $gameFixture 'code\exes')
    $originalMissions = Join-Path $GameRoot '.diagnostics\original-mission-exes'
    if (Test-Path -LiteralPath $originalMissions) {
        Copy-Item (Join-Path $originalMissions 'stg*.exe') (Join-Path $gameFixture 'code\exes') -Force
    }
    $installer = Join-Path $build.FullName 'package\TIJ Fixes.exe'
    $process = Start-Process -FilePath $installer -ArgumentList @('--apply', ('"' + $gameFixture + '"')) -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw 'Packaged installer failed on the isolated actual game copy.' }
    $process = Start-Process -FilePath $installer -ArgumentList @('--apply', ('"' + $gameFixture + '"')) -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw 'Packaged installer reapply failed.' }
    $options = Join-Path $build.FullName 'package\TIJ Options.exe'
    $report = Join-Path $fixture 'options-test.txt'
    $process = Start-Process -FilePath $options -ArgumentList @('--self-test', ('"' + $gameFixture + '"'), ('"' + $report + '"')) -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw (Get-Content $report -Raw) }
    Get-Content $report
    Write-Output 'PASS: packaged executables installed/reapplied against an isolated copy of all 85 actual missions.'
}
