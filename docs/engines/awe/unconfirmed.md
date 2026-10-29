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

## Meanings inferred

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

## Settled since

Moved to their format pages; kept here so the reasoning can be found:

- **The x87's precision:** 24 bits, not 53. Direct3D 8's `CreateDevice`
  sets it, the game doesn't ask it not to, and the path-finder is made
  after. At 53 bits 4 of 22,480 test paths differed from the game's; the
  port now rounds as the game's FPU does and all agree.
  [ac2.md](../../formats/awe/ac2.md).
- **The wheel and the generic extras:** the game's steps walk only the
  part's own threads and the goodbye, passing over the extras; with
  nothing highlighted and no goodbye, a step back highlights nothing.
  Reluva now does both. [dialogue.md](../../formats/awe/dialogue.md),
  section 3.
- **The "met them here" tags:** the greetings' own words confirm it:
  `gen_regreet1`'s "I don't think I'm quite done yet", `gen_regreet2`'s
  "Something has occurred to me since I left you".
  [dialogue.md](../../formats/awe/dialogue.md), section 2.2.
- **The dialogue font:** the code measures and draws the lines with font
  1, the second row of `fonts.txt`. [dialogue.md](../../formats/awe/dialogue.md),
  section 6.
- **The save's thumbnail:** a 24-bit BMP. D3DX's BMP writer in the game
  writes only 24- and 8-bit files; Reluva wrote 32-bit and now writes
  24. [saves.md](../../formats/awe/saves.md).
