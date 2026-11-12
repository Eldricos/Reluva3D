# Action sequence data

> **Status:** decoded · **Code:** `src/engines/stharbinger/game/arcade.cpp`

## Overview

The files the action sequences use besides their videos: the targets'
sprites, the panel, explosions and torpedoes, and the music and voice cues.

## Layout

* **Sprites** (`SPRITES\<NAME>\*.RLE`, every file of the folder in name order):
  RLE animations whose first 8 bytes hold the target's data instead of the scene
  animations' fixed `00 00 01 01 0A FF 00 00`:

  | Offset | Contents |
  |---|---|
  | 0 | u16 video frame at which the sprite's frame 0 is shown |
  | 2 | hits needed |
  | 3 | damage per shot at the player (0..100 scale) |
  | 4 | fire period in video frames |
  | 5 | gun: FF never fires, 2 alternates muzzles 0 and 1, otherwise muzzle 0 |
  | 6 | u16 id: below 100 an independent target (fires after `id` frames); 100 and up a group sharing its hits (fires after `id - 100`); from 200 the boss |

  In the frame headers, bytes 9..20 are the muzzle offsets (`s16 x[3], y[3]`)
  from the frame's top-left; frame x/y are relative to the video rectangle.
* **`PANEL\RUNSH.RLE`**: the shield indicator (frame by damage), drawn at its
  own screen position. `RUNGUN.RLE` is loaded but never shown.
  `PANEL\<NAME>STAT.RLE`: static shown before the death video.
* **`EXPLODE\EXPL*.RLE`**: in name order the large (`EXPLLG01..05`) and small
  (`EXPLSM01..05`) explosions.
* **`TORPEDOS\`**: `T29L/T17L/T09L` and `T29R/T17R/T09R` are the torpedo pairs
  from the left and right launchers at three distances (one frame per direction,
  0..9), `T5ALL` the last stage.
* **`SPRITES\PDOOR.RLE`**: the plan sequence's door; its first u16 is the frame
  it appears.
* **`SOUND\<NAME>.MUS`** (text): a number used to time the ending, then segment
  suffixes: `NAME + suffix + .RAW` play in order and loop; the `tag` segment
  plays once near the end.
* **`VOICE\<NAME>.VCE`** (text): pairs of lines, a sound name and either a
  video frame number or `d<n>` (when damage first reaches `95 - 5 * (n / 5)`).

## Sources and checks

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).
