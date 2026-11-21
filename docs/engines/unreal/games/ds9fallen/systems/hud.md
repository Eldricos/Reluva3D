# The Fallen: the HUD and its canvas

> **Status:** done · **Code:** `src/engines/unreal/vm/vm_canvas.cpp` · **Checked against:** the code (Engine.dll's Canvas natives) and a recording · **Confidence:** from the code unless marked

## Overview

The game's own HUD is drawn: its panels, numbers, communicator, in the
game's fonts ([font.md](../formats/font.md)), by its own scripts through
the Canvas natives.

## How it works

The engine hands the player a Canvas object each frame, before the world
is drawn (`PreRender`) and after (`PostRender`), and the player hands it
to its HUD. `vm_canvas.cpp` does the same: the Maps page asks for it once
the world's drawn (`ScriptLevel::renderHud`). The canvas is made ready
first: `Reset`, its size and clip the screen's, its origin and pen at 0.
What the Canvas natives draw is kept as a list for the page:
- **tiles:** a texture's rectangle (in texels) at a rectangle of the
  screen (in pixels), with a colour (`DrawColor`) and a style (`Style`:
  normal, masked, translucent, modulated);
- **actors:** drawn over what's there, each in the view (the frame,
  below) it was asked in, and in its order among the tiles.

The natives:
- **`DrawTile(Tex, XL, YL, U, V, UL, VL)`** at the pen (`OrgX + CurX`,
  `OrgY + CurY`); the pen moves on by its width. `DrawTileClipped` is cut
  to the clip (`OrgX` to `OrgX + ClipX`...), its texture rectangle cut
  with it. Canvas's own `DrawIcon`, `DrawRect` and `DrawPattern` are
  script, on top of these.
- **`DrawText(Text, CR)`** draws at the pen in the canvas's font
  ([font.md](../formats/font.md)), a tile a letter. Each letter's
  width plus `SpaceX` moves it on, and the tallest letter plus `SpaceY`
  makes a line. The text wraps between words at the clip's right edge,
  each line centred in the clip with `bCenter`; a wrapped line starts
  back at the pen's column, not the clip's edge. The pen moves past it
  (with CR, to the next line's start), and `CurYL` keeps the line's height.
- **`DrawTextClipped`** draws one line, cut to the clip, with '&' (a
  menu's hot key) left out if asked. `DrawScaledText` scales the letters.
- **`StrLen`** measures text wrapped as `DrawText` would; `TextSize` on
  one line.
- **`SetRenderFrame(X, Y, W, H, Location, Rotation, FOV, ClearZ)`** sets
  the frame the next actors are drawn in: a rectangle of the screen and a
  camera, as given (Engine.dll's `execSetRenderFrame` hands them to
  `FSceneNode::ComputeRenderCoords`), with the field of view given or 90.
  It also sets the viewing player's `FovAngle` to that field of view, and
  clears the depth with ClearZ. It lasts until the next one or the end of
  the frame; each frame starts in the world's view. The HUD's
  `ResetCanvas` sets the whole screen with the HUD actor's own place (the
  camera's, `UpdateLocation`) and rotation, and the `FovAngle` that
  `SaveCanvas` kept. The scripts draw no actor in it after that and
  project nothing in it, so it shows nothing. What it does is put back
  the `FovAngle` that a portrait's or the tricorder's frame changed,
  which `CTV_Hud`'s debug labels read.
- **Tiles in a frame** are placed from its corner and cut to it, and the
  canvas's clip (`ClipX`, `ClipY`) becomes its size, as the scripts
  expect. The info pad's documents are drawn from 0, 0 and scroll
  inside their window. The star map's blocks scroll inside theirs, scaled
  by the clip the HUD saved before setting the frame (`saveClipX`). The
  tricorder takes `GetScreenCoords` as a share of `ClipX`.
- **`DrawActor`, `DrawClippedActor`** draw the actor where it is, in the
  current frame (`DrawClippedActor` in its own rectangle, the frame's
  camera), the depth cleared first if asked; the page draws it, hidden or
  not. Its draw type, style and glow are taken as they are at the call:
  the HUD's speaker portrait (`DS9_Hud_Mesh_Actor`) sets them only around
  it. The HUD uses these for the communicator's portrait (the speaker's
  head, a camera 78 units in front of it at 10°), the tricorder's dots
  and markers, beacons and the beam-down cloud.
- **`GetScreenCoords(Location, X, Y, Scale)`** is where a place falls in
  the current frame's view, from the frame's corner as tiles are placed;
  false behind it.
- `DrawPortal` isn't drawn: no script calls it.

The Fallen's HUD (`DS9_Hud`) is built from textures that draw themselves
(`Texture.Draw(Canvas)`, script). Each HUD texture keeps where it goes in
properties it borrows:
- `Specular` and `Friction`: its place, in 800 × 600 units;
- `PrimeCount`: the corner or edge that place is from;
- `Alpha`: its style.

The HUD's animated textures are FliTextures ([texture.md](../formats/texture.md)),
played by script (`DS9_Hud.SetFlick`): `Mode` 0 stops, 1 plays once, 2
loops. Each time a FliTexture is drawn it plays on by the real time
since it was last drawn (so on while paused): its `FrameTime` grows by
that times `Rate`, and `FrameNum` moves on a frame each 1/30 s of that. Played once, it stops back on frame 1 with
`Mode` 0, which `FlickPlaying` waits for. A tile draws the frame
`FrameNum` says (the communicator's bar sliding open, the meters).

That's why textures need their own stored properties
([script-vm.md](script-vm.md)): with the
defaults, every piece of the HUD was off the screen.

On the Maps page, while playing, the tiles are drawn over the world in
screen pixels:
- see-through texels stay so in every style, as letters are drawn masked
  however they blend;
- translucent tiles are added, modulated ones multiplied in.

On M04_KiraL1 the HUD shows:
- the medical panel bottom left: its meter and Kira's health (100) in the
  HUD's number font;
- the weapon panel bottom right: the phaser rifle's picture and its
  ammunition (150);
- F1, the communicator: the Starfleet badge and the crew list (O'Brien,
  Dax, Bashir) with its selection marker.
- F1, then Fire: hails the selected crewmate. The communicator's bar
  slides open and the answer is written in its box beside their face
  (O'Brien: "You'll need to reinitialize the EPS taps...").

The keys:
- **Passed to the game** while playing: F1 to F12 and the number keys,
  as the game binds them, on press and release.
- **Aliases** are followed: `ShowObjectives` is `Button bShowObjectives`.
- **A `Button`** holds the player's input flag while the key is down.
- **An exec function** is called as the key goes down, with its argument
  if it has one (`SwitchWeapon 2`).

With the game's HUD drawn, the page leaves the objectives and subtitles
to it and puts its status line between the HUD's panels.

## In Reluva

`vm/vm_canvas.cpp` works the Canvas natives and keeps what they draw for
the page (`ScriptLevel::renderHud`).

## Sources and checks

- **Where the facts come from:** Engine.dll's Canvas natives, read for
  what they do, and the HUD's scripts.
- **How it was checked:** in play, and against the recordings
  ([research/playthrough.md](../research/playthrough.md)).
