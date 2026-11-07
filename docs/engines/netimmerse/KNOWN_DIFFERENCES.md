# LEGO Alpha Team: known differences from the original

Where Reluva is known to behave differently from the game running on a
Windows machine of its time. Each one is either deliberate, or a
consequence of how Reluva runs the game; none stops a mission being
played. Things that may differ but haven't been checked are in
[UNCONFIRMED.md](UNCONFIRMED.md).

Reluva runs the game's own program and DLLs unchanged
([alphateam.md](alphateam.md), phase 3). So the game's logic, physics,
camera, menus and saving are the original's own code. The differences are
in what Reluva supplies in place of Windows, DirectX, Aureal A3D and the
hardware.

## Pictures

- **Drawn in software.** Direct3D 7 is a software rasterizer
  (`src/common/x86/directx/raster.cpp`) following the DirectX 7
  documentation's fixed-function pipeline, not a graphics card. A card of
  2000 drew with its own quirks, which ours doesn't copy:
  - no dithering in 16-bit colour: colours are rounded to the nearest
    5-6-5 value;
  - the mip level is chosen once per triangle, not per pixel;
  - bilinear filtering only (no card-specific trilinear or anisotropic
    filtering);
  - pixel centres at whole coordinates with a top-left fill rule, as the
    documentation has it.
- **Floating-point precision** is Direct3D's: the FPU is put in single
  precision when the device is made, as Direct3D 7 does without
  `DDSCL_FPUPRESERVE`. Found because at double precision the camera at
  Mission One's end framed the skull door wrongly. As on the processor, that
  precision rounds additions, subtractions, multiplications, divisions
  and square roots, not `sin`, `cos`, `tan` and the other transcendental
  instructions (Intel SDM vol. 1, 8.1.5.2), whose results are worked out
  with the host's maths library (in doubles, where the x87 has 64 bits).
- **Gamma and brightness** are applied to each frame shown, in place of
  the display's gamma ramp.
- **Display modes:** the game's choice of resolution and colour depth
  (640x480 to 1024x768, 16 or 32 bits) is drawn at that size. In exclusive fullscreen, a mode change takes the
  desktop's size, as DirectDraw does.

## Timing and speed

- **At most 60 frames a second.** The game draws as fast as it can and
  relies on `Flip` waiting for the display. Ours paces flips to a 60 Hz
  display whatever the real one runs at.
- **Slower than the original on its own hardware** in busy scenes. The
  game's code runs in an x86 emulator with native code generation (JIT)
  and the drawing is done on the processor: about 45 frames a second in
  missions at 1024x768x32 on a 2020s desktop, lower in wide camera shots
  with much on screen, and dips while the attract sequence loads its next
  level ([alphateam.md](alphateam.md), "Speed").
- **The processor's speed** as the game measures it
  (`DDCaps::CalcProcSpeed`, RDTSC across a `Sleep`) reads as 1 GHz, so it
  recommends full detail (shadows, environment animation) where a period
  machine might not.

## Sound

Aureal A3D is stood in for (`src/common/x86/a3d.cpp`) from its public
SDK documentation, through Reluva's mixer:

- 3D sounds get a volume from their distance (A3D's inverse-distance
  model, with each source's minimum and maximum distance) and a stereo
  pan from their side of the listener.
- **Not heard:** A3D's geometry (occlusion and reflections), materials,
  sound cones, Doppler, EQ and its HRTF positioning (sounds behind you
  sound like sounds beside you). `IsFeatureAvailable` reports none of
  them, so the game takes the paths it takes on a card without them.

## Starting and leaving

- **The intro movie** (`lmiintro.avi`) only plays if it has been copied
  from the CD into the install folder (the installer leaves it on the
  CD). A key or click skips it, as in the original; its very last frame
  isn't shown.
- **Quitting Reluva** while the game runs (closing the window, Ctrl+F5)
  asks first, and doesn't save: the game itself saves only through Exit
  Game and as an Adventure moves on, as it does on Windows.
- **F12** pauses the game and opens Reluva's data viewer, which the
  original doesn't have.

## Not supported, and not needed

- Structured exception handling (`RaiseException`, `RtlUnwind`) ends the
  run instead of being dispatched. The game hasn't been seen to use it.
- `AutoLoadPuzzle` in `Config.txt` (skip the menus straight to a
  mission) gives a black screen. That's the game's own code: the key is a
  leftover the shipped game doesn't support (traced in
  [alphateam.md](alphateam.md)).
- MMX and SSE instructions aren't emulated: the game's DLLs don't use them.
