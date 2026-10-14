# Italian Job UI anchor ASI

Installed as `TIJHudFix.asi` beside both `TIJ.exe` and the mission executables in `code/exes`. Each folder's `dxwrapper.ini` loads it through:

```ini
LoadCustomDllPath = TIJHudFix.asi
```

## Behavior

The game already preserves the aspect of Rt2d sprites and text. This patch corrects their horizontal placement in the original 640-by-480 coordinate system:

- The speedometer face, bezel and needle move together to the right edge.
- The pause-menu transform is centred, preserving panel, label and volume-control spacing. Fullscreen rectangles in other pause states span the viewport.
- Frontend menu text, highlights, selection graphics, custom textures, volume controls and language feedback are centred. Fade overlays span the viewport; full-screen artwork uses the same centred 4:3 rectangle as selection overlays and masks, with black side bars on widescreen displays.

At 2560-by-1440, the right-edge adjustment is 640 pixels and the centre adjustment is 320 pixels. Both are zero at 4:3. Dimensions, vertical positions, the world camera and the damage indicator are preserved.

## Guards and validation

Signatures identify the supported render scopes and resolution globals before any calls are redirected. Mission pause signatures match all 85 supported missions. Frontend patterns are specific to the supplied TIJ.exe build and tolerate the resolution tool's changes. Code is also scanned in the frontend's `.text` and `_rwcseg` sections, which are marked as data in this release. Unknown or ambiguous layouts are left untouched.

The speedometer ASI was tested in STG055 with RTX Remix at 2560-by-1440. A temporary pause transform correction at that resolution was visually confirmed by the user. Frontend ASI loading and its active text path were traced at the same resolution. The user confirmed the revised shared 4:3 frontend artwork layout looks correct. Additional pause states have not all been visually verified. Native x86 builds use `/W4 /WX`; shared anchor tests cover 4:3, 1080p, 1440p, 4K and ultrawide, including fullscreen rectangle alignment.

No game executable is changed on disk by the ASI. Clear `LoadCustomDllPath` in either folder to disable that folder's UI correction.

## Build

Use `release-tools/build-release.ps1` as described in the project README. Local traces, research files and build output are excluded from the repository and package.