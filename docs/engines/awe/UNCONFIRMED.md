# Unconfirmed items (Murder on the Orient Express)

What Reluva does from reading `motoe.exe`, or from inference, without it
having been seen to be so in the running game or a recording. Each is
the best reading there is; none is known to be wrong. When one is
settled, move it to the format page it belongs to and take it off here.

## Read from the code, not watched

- **A walk eases out the gesture under way.** The game's walk start
  (`0x441160`) calls `0x4048f0`, which eases every finite gesture out
  over 0.3 s, once the path is set; Reluva does the same, so no one
  slides along in an idle variation. Read, not watched. The one slide
  left on the route is Hardman's in Sirkeci.Market_05 (`walka 10 17` in
  the fountain talk): he has no walk file, so the game, by the same
  code, moves him at 150 units a second with no walk (`0x4422d9`).
  [characters.md](characters.md#walking-a-path).
- **A new game from the menu starts afresh.** `reset` (`0x411aa0`)
  empties the conversations heard (`[0x498810]+0x2b4`), and going to the
  main menu changes the place (`0x422640`), so Play drops the scenes
  kept (`0x4229a5` -> `0x423430`). Read from the code; in Reluva, a game
  left for the menu and started again was checked by hand.
  [characters.md](characters.md#leaving-a-scene-and-coming-back),
  [dialogue.md](dialogue.md), section 7.
- **The interface's last row of one letter.** `0x414720` draws a wrapped
  text's last row only if it's longer than one character (`0x414b34`),
  though `0x414c30` counts it in the height. Reluva does the same; no
  text in the install is known to end that way.
  [interface.md](interface.md).

## Meanings inferred

- **Fields loaded but never read.** The common `b54`, `b58`, `b68`,
  `SPRT`'s `dc8`, the item's `1610` and `1612`, the camera's `238`,
  `23a` and `1240`: nothing in the executable reads them, so what they
  were for can't be told. [ac2.md](ac2.md).

## Checked only against recordings

The recordings were calibrated before comparing: the prerendered
background a frame shows is in the install, so fitting the frame's
pixels against it (where nothing covers it) gives the recording's colour
transform, per channel (see
[REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)). The walkthrough comes
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

- **The order of the dialogue's updates within a frame:** read in full
  from 0x43d430 (the Povent runner, then what waited on it in the same
  frame, the pause, the Prevent, the line's events, the box's slide, the
  line's end). Reluva's differed in four small ways and now follows it.
  [dialogue.md](dialogue.md), section 9, item 10.

- **A character's routine when its scene is shown again:** started
  afresh, as Reluva does. The game's switch-on at scene entry
  (`0x440690`) empties the character's runner (`0x409760`) and runs the
  first `IDLE` script whose condition holds from its beginning.
  [characters.md](characters.md#leaving-a-scene-and-coming-back).

- **The characters' colours:** in Salon_02 (6B, Part 7 at 6:22, the
  frame's own fit at a correlation of 0.997), Reluva's frame put through
  the recording's transform and encoding, Antoinette, Hubbard and the
  Count agree with the recording within 0 to 15 % per channel, part by
  part. The Count had come out 1.6 to 1.9 times brighter in the
  recording, sitting upright where Reluva had him slumped: a bug in
  reading Granny curves with no knots, found by posing his `sitidle1`
  with granny2.dll (`awe pose-check`) and fixed.
  [gr2.md](gr2.md).
- **The x87's precision:** 24 bits, not 53. Direct3D 8's `CreateDevice`
  sets it, the game doesn't ask it not to, and the path-finder is made
  after. At 53 bits 4 of 22,480 test paths differed from the game's; the
  port now rounds as the game's FPU does and all agree.
  [ac2.md](ac2.md).
- **The wheel and the generic extras:** the game's steps walk only the
  part's own threads and the goodbye, passing over the extras; with
  nothing highlighted and no goodbye, a step back highlights nothing.
  Reluva now does both. [dialogue.md](dialogue.md),
  section 3.
- **The "met them here" tags:** the greetings' own words confirm it:
  `gen_regreet1`'s "I don't think I'm quite done yet", `gen_regreet2`'s
  "Something has occurred to me since I left you".
  [dialogue.md](dialogue.md), section 2.2.
- **The dialogue font:** the code measures and draws the lines with font
  1, the second row of `fonts.txt`. [dialogue.md](dialogue.md),
  section 6.
- **Fade lengths:** the recordings' 0.27 to 0.47 s are the game's fade
  of at most one step a frame (0.32 s at the least; the 0.27 s within
  the video's measurement); listed with the known differences.
  [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md).
- **The save's thumbnail:** a 24-bit BMP. D3DX's BMP writer in the game
  writes only 24- and 8-bit files; Reluva wrote 32-bit and now writes
  24. [saves.md](saves.md).
