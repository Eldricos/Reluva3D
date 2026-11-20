# LEGO Alpha Team: options

> **Status:** done · **Checked against:** the original running in Reluva, each option set by hand · **Confidence:** confirmed unless marked

## Overview

The Options screen from the main menu: screen resolution, brightness and
gamma, the music, voice-over and sound effects volumes, mouse sensitivity
and the advanced options.

## How it works

- Done: the game's options (the Options screen from the main menu),
  checked one by one:
  - Screen resolution: the game changes the display mode itself
    (`SetDisplayMode`); ours draws at the new size (800x600 and
    1024x768x32 checked), and the frames shown follow it. The choice is
    kept in `Game.txt` as `Width`, `Height` and `BPP` (all players
    share it), so a run can start at it: `Width  1024`, `Height  768`,
    `BPP  32` there, and the route's points scaled by 1.6.
  - Screen brightness and gamma: the game sets the display's gamma ramp
    through `IDirectDrawGammaControl`, asked of the primary surface.
    Reluva offers that interface and applies the ramp to each frame
    shown (brightness 50% to 60% lifts the dark parts by about 25
    levels), and `GetCaps` reports `DDCAPS2_PRIMARYGAMMA`.
  - Music, voice-over and sound effects volumes: the game sets its
    sources' gains (`IA3dSource::SetGain`), which ours applies.
  - Mouse sensitivity and the advanced options (textures, lightmaps,
    backdrop, environment animation, shadows) are the game's own
    business: nothing of ours is involved.

## In Reluva

Reluva runs the game's own code, unchanged, in the x86 interpreter ([running.md](running.md)); the options are the game's own. Reluva's stand-ins carry them
out: the display mode and the gamma ramp in its DirectDraw
(`src/emulation/x86/directx/`), the gains in its A3D
(`src/emulation/x86/a3d.cpp`). Gamma and brightness applied to each
frame, and the display modes, are in
[KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md).

## Sources and checks

- **How it was checked:** each option set in the game in Reluva, one by
  one (above).
