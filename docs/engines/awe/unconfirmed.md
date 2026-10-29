# Unconfirmed items (Murder on the Orient Express)

What Reluva does from reading `motoe.exe`, or from inference, without it
having been seen to be so in the running game or a recording. Each is
the best reading there is; none is known to be wrong. When one is
settled, move it to the format page it belongs to and take it off here.

## Read from the code, not watched

- **The order of two dialogue updates within a frame.** The Povent runner
  and the pending action (+0x320) are updated in a fixed order in the
  frame's update, with no branch between them. A difference would last
  one frame. Two breakpoints in the original (under Wine, or x64dbg on
  Windows) would settle it; left until the other AWE games give a
  reason to run the original under a debugger.
  [dialogue.md](../../formats/awe/dialogue.md), section 9.10.
- **The wheel and the dialogue's generic extras.** Reluva's wheel steps
  through the choices as shown, the generic extras among them; that the
  game's skips the extras is possible but not checked.
  [dialogue.md](../../formats/awe/dialogue.md), section 3.
- **The x87's precision.** Reluva's path-finder matches the game's code
  point for point when the interpreter keeps the x87 at 53 bits, Windows'
  default for a program. That the game leaves it there is assumed.
  [ac2.md](../../formats/awe/ac2.md).

## Meanings inferred

- **The "met them here" tags.** The code sets a tag per scene,
  conversation and part when talking, and sets those of a scene to 2 when
  it's left; read as "met this visit" (1) and "met on an earlier visit"
  (2), choosing `gen_regreet1` or `gen_regreet2`. The code is sure; the
  reading is likely. [dialogue.md](../../formats/awe/dialogue.md),
  section 2.2.
- **The dialogue font.** "calisto mt 12, shadow, dialogue" from
  `fonts.txt` is taken as the dialogue's; the recordings agree by eye.
  [dialogue.md](../../formats/awe/dialogue.md), section 6.
- **The save's thumbnail format.** Written by D3DX, probably as a 32-bit
  BMP; Reluva writes and reads it so and the game's own saves load.
  [saves.md](../../formats/awe/saves.md).
- **Fields loaded but never read.** The common `b54`, `b58`, `b68`,
  `SPRT`'s `dc8`, the item's `1610` and `1612`, the camera's `238`,
  `23a` and `1240`: nothing in the executable reads them, so what they
  were for can't be told. [ac2.md](../../formats/awe/ac2.md).

## Checked only against recordings

These were compared with the walkthrough videos, whose encoding shifts
colours and brightness, so small differences can't be told apart from
the video's:

- the lit characters' colours (the specular brings the dress and skin
  nearer the recording's; the skin is still a little paler in Reluva);
- the dialogue text's brightness (a shade brighter in the recordings);
- fade lengths (0.27 to 0.47 s in a recording, as straight ramps).
