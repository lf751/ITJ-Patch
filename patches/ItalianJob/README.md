# The Italian Job — compatibility fixes and display options

Fixes for the Windows PC release of **The Italian Job** (2002 PC port of the 2001 game), developed with the Vibe reverse-engineering toolkit.

## Download and install

Download `ItalianJob-Fixes-<version>.zip` from this repository's **Releases** page. Extract it beside the game's `TIJ.exe`, close the game, then run **TIJ Fixes.exe** once.

Use **TIJ Options.exe** to choose a supported resolution. **Apply & Play** starts `TIJ.exe`, which switches display modes before launching missions. Directly launching a mission uses the current Windows desktop resolution.

The installer includes the tested DxWrapper v1.8.8600.25 and the HUD ASI, and configures the wrapper in the frontend and `code/exes`. RTX Remix itself is optional and is installed separately. Existing `d3d9.dll`, `.trex`, Remix configuration and save progress are preserved. An existing different `d3d8.dll` or DxWrapper DLL is rejected instead of being overwritten. An existing custom-DLL loader entry also requires manual integration.

**Supported build:** the 85 `stgNNN.exe` mission binaries whose normalized hashes are listed in [`installer/missions.tsv`](installer/missions.tsv). Both original and already-fixed copies are accepted. Other releases or independently modified mission binaries are rejected before installation. The options app additionally checks the frontend's display-mode instruction layout. No game executable or save is included in the download.

## Included fixes

| Component | Change |
| --- | --- |
| Mission loading | Marks only the RenderWare `_rwcseg` section executable, fixing the observed execute-access crashes. |
| Remix menus and rapid clicking | Removes the single camera-render call that injects a Windows left-button down/up pair every frame. |
| Widescreen HUD | `TIJHudFix.asi` reanchors the complete speedometer and needle to the right edge without resizing them. Other HUD panels and pause/menu layouts are unchanged. |
| Resolution options | A small .NET Framework app edits the frontend's guarded native-resolution checks and only the saved display-mode choice. |

The options app shows the resolution selector, detected resolution, **Apply**, and **Apply & Play**. It creates no backups. The installer also creates no backups; game saves are not replaced or reset. Re-running the installer is safe for recognized builds.

Resolution changes use the original game's customized 640×480 mode slot. The game's own menu may continue calling this slot "640×480". Reapply through the options app if you change the in-game display setting.

## RTX Remix

Install a compatible x86 RTX Remix runtime separately in the game folder and mission directory as appropriate for your setup. This package supplies the D3D8 → D3D9 converter, not the Remix renderer or replacement assets. It does not copy graphics presets, texture tags or experimental input settings between the frontend and missions.

The HUD ASI loads from `code/exes/dxwrapper.ini` through `LoadCustomDllPath = TIJHudFix.asi`. To disable only the HUD correction, close the game and remove that filename from the setting. Mission compatibility fixes remain installed.

## Source layout

- `hud-asi/` — x86 ASI source, guarded signatures, anchor calculation and tests.
- `options-app/` — compact Windows Forms resolution program.
- `installer/` — game-file patcher and normalized mission hash manifest.
- `release-tools/` — repeatable Windows build, tests and packaging scripts.

Research logs, game binaries, backups, captures, local paths and build output are excluded from Git and the release ZIP.

## Build locally

Requires Windows, Visual Studio C++ x86 tools, Windows .NET Framework C# compiler, PowerShell and Internet access for the pinned DxWrapper download.

```powershell
./patches/ItalianJob/release-tools/build-release.ps1 -Version local
./patches/ItalianJob/release-tools/test-release.ps1
```

The ZIP and SHA-256 checksum appear in `patches/ItalianJob/dist/`. The build verifies the third-party archive hash, builds the x86 ASI and programs, runs anchor tests, and checks the exact package contents. The test script verifies installation, save/INI preservation, idempotence and rejection of unsupported binaries or conflicting wrappers. For an additional isolated-copy test against your installed game:

```powershell
./patches/ItalianJob/release-tools/test-release.ps1 -GameRoot 'D:\Path\To\Game'
```

## GitHub Actions and releases

The **The Italian Job - Build and Release** workflow builds/tests this branch and uploads a downloadable artifact on relevant branch pushes. To publish a release, push an `italian-job-v*` tag pointing at the finished commit:

```powershell
git tag italian-job-v1.0.0
git push origin italian-job-v1.0.0
```

Only a successful tagged build publishes the ZIP and checksum to Releases. No tag or release is created by merely pushing the branch. Manual workflow dispatch is also configured; GitHub requires the workflow to be present on the default branch before exposing manual dispatch, so use the branch-push artifact or tag trigger until then.

## Validation and limits

- Mission-loading and synthetic-click fixes were applied across all 85 missions; the user confirmed the fixes worked.
- The HUD ASI's signatures match all 85 mission executables. Actual ASI loading and corrected speedometer placement were verified in STG055 with RTX Remix at 2560×1440. Other missions/resolutions have not each been played.
- The resolution tool passed isolated actual-game read/apply/reapply/revert tests, save-data preservation and unsupported-layout rejection. Live display-mode switching through the app is not claimed as fully tested.
- The installer uses only the known build's hashes. It will not silently adapt unknown executable versions.

## References and licenses

- [PCGamingWiki resolution notes](https://www.pcgamingwiki.com/wiki/The_Italian_Job)
- [Caper partial decompilation](https://github.com/dashr9230/Caper)
- [DxWrapper v1.8.8600.25](https://github.com/elishacloud/dxwrapper/releases/tag/v1.8.8600.25)

Project code follows the repository's MIT license. DxWrapper's license and third-party notices are included in `licenses/DxWrapper.txt` in the release. Game and RTX Remix assets are not redistributed.
