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

The recordings were calibrated before comparing: the prerendered
background a frame shows is in the install, so fitting the frame's
pixels against it (where nothing covers it) gives the recording's colour
transform, per channel (see
[reverse-engineering.md](reverse-engineering.md)). The walkthrough comes
out about 1.10 to 1.15 x the true value, less 10 to 13, varying a little
from part to part: about 10 to 20 % more contrast, most likely the
capture's video range read as full range. What's left after that:

- **The dialogue text.** Its colour is the right `fonts.txt` one (a
  second speaker's line comes out nearest colour 9, as the game's rule
  gives) and as bright as Reluva's within what the video can show: thin
  coloured text is smeared by the video's quarter-resolution colour, and
  the method's own error there (25 % or more in green and blue) is as
  large as any difference. The "shade brighter" look is the recording's
  contrast.

## Settled since

Moved to their format pages; kept here so the reasoning can be found:

- **The characters' colours:** in Salon_02 (6B, Part 7 at 6:22, the
  frame's own fit at a correlation of 0.997), Reluva's frame put through
  the recording's transform and encoding, Antoinette, Hubbard and the
  Count agree with the recording within 0 to 15 % per channel, part by
  part. The Count had come out 1.6 to 1.9 times brighter in the
  recording, sitting upright where Reluva had him slumped: a bug in
  reading Granny curves with no knots, found by posing his `sitidle1`
  with granny2.dll (`awe-pose`) and fixed.
  [gr2.md](../../formats/awe/gr2.md).
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
- **Fade lengths:** the recordings' 0.27 to 0.47 s are the game's fade
  of at most one step a frame (0.32 s at the least; the 0.27 s within
  the video's measurement); listed with the known differences.
  [known-differences.md](known-differences.md).
- **The save's thumbnail:** a 24-bit BMP. D3DX's BMP writer in the game
  writes only 24- and 8-bit files; Reluva wrote 32-bit and now writes
  24. [saves.md](../../formats/awe/saves.md).
