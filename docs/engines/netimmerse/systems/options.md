# LEGO Alpha Team: options

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
    That interface wasn't offered, so neither did anything. Now the ramp
    is applied to each frame shown (brightness 50% to 60% lifted the
    dark parts by about 25 levels), and `GetCaps` reports
    `DDCAPS2_PRIMARYGAMMA`.
  - Music, voice-over and sound effects volumes: the game sets its
    sources' gains (`IA3dSource::SetGain`), which ours applies.
  - Mouse sensitivity and the advanced options (textures, lightmaps,
    backdrop, environment animation, shadows) are the game's own
    business: nothing of ours is involved.
