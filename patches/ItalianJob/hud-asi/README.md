# Italian Job HUD anchor ASI

Installed as `code/exes/TIJHudFix.asi`. The mission-side `dxwrapper.ini` loads it through:

```ini
LoadCustomDllPath = TIJHudFix.asi
```

## Behavior

Reanchors the speedometer face, bezel and needle to the right edge at the current aspect ratio. It preserves their dimensions and vertical positions. At 2560×1440 the horizontal offset is 640 pixels. At 4:3 the offset is zero. This addresses HUD placement, as clarified by the user, rather than applying a global UI scale.

The correction applies only to three identified horizontal coordinate conversions. The damage indicator, world camera, pause menu and other UI functions are not hooked. Other mission-specific HUD panels have not been corrected or visually tested.

The ASI scans executable sections for the verified gauge and resolution-global signatures. It requires exactly two rectangular gauge matches, one needle match and one resolution initializer. All three UI calls must share their original conversion target. Unsupported layouts are left untouched. No mission executable is changed on disk.

## Validation

- Signatures uniquely matched all 85 installed mission executables; frontend TIJ.exe is deliberately excluded.
- Live Frida capture confirmed the original authored x coordinates 492/556/492 and the 480-unit coordinate system.
- Temporary live correction moved all gauge components together, preserving circular shape and needle alignment.
- Native x86 build passed `/W4 /WX`.
- Shared calculation tested at 640×480, 1024×768, 1920×1080, 2560×1440, 3840×2160 and 3440×1440.
- Deployed ASI verified through DxWrapper's load log, its own first-render log and live call-target inspection.
- Screenshot of actual STG055 at 2560×1440 verified the final ASI's output.
- Other missions and other display resolutions have not each been tested in the running game.

## Disable

Close the game and clear only `TIJHudFix.asi` from `LoadCustomDllPath` in `code/exes/dxwrapper.ini`. No binary restoration is needed.

## Build

Run `../release-tools/build-release.ps1` from the project root as described in the main README. The build discovers Visual Studio, targets x86 with a static runtime, and runs the shared anchor tests. The local `build.cmd` shortcut is not part of the release source.

`patterns.h` contains the checked-in runtime signatures, validated against all 85 supported missions. Local reverse-engineering traces and diagnostic captures are deliberately excluded from this branch.
