# LEGO Alpha Team: unconfirmed items

What Reluva does that hasn't been checked against the original yet, and
what the format documents still mark as a guess. Each item says what
would confirm it. Known differences are in
[known-differences.md](known-differences.md).

## Gameplay

- **Every mission played through by hand.** All 51 Adventure missions are
  solved in automated runs, but by loading the game's own solved layouts
  (`<space>_SOLVED.PUZ`, [alphateam.md](alphateam.md), phase 4), not by
  placing the parts with the mouse. Only Mission One (M1010) has been
  placed and solved by hand. To confirm: play each mission
  ([testing.md](testing.md)).
- **Dragging, rotating and dropping every kind of part** (tiles, cords,
  belts, ropes and the rest) by hand. The training missions teach each
  kind; only Mission One's Direction Tiles have been placed by hand.
- **The later training missions** (M0020 Electric Cords and Sockets, M0030
  Belts, M0040 Ropes) come up in the Adventure where the original
  playthrough shows them, but have only been run to their goal, not
  played.
- **Instant Replay, Next Mission and Start Over** have been seen working
  on Mission One and M1085 only.
- **Hints:** the hint menu and its counter ("Hints left: 2") have been
  seen; using hints hasn't been checked.
- **The ending** (M4070 solved: "The Victory Party" and the credits) has
  been seen in an automated run's frames, not played or heard.

## Pictures and sound

- **Side by side with the original on a period machine or a virtual
  machine.** Comparisons so far are with recordings of the original
  (960x720, about eight hours, given by the game owner): each mission's
  opening looks the same (rooms, textures, lighting, text). Pixel-level
  differences from a real card's drawing (see
  [known-differences.md](known-differences.md)) haven't been measured.
- **The camera** at Mission One's end matches the original's once the
  player's camera input is matched. Other missions' cameras haven't been
  compared shot for shot.
- **3D sound** follows A3D's documented distance model; the volumes and
  pans haven't been compared by ear with A3D hardware.
- **Options:** screen resolution, brightness, gamma and the three volumes
  were checked. Mouse sensitivity and the advanced options (textures,
  lightmaps, backdrop, environment animation, shadows) are the game's own
  code; their effect hasn't been compared with the original.
- **800x600 and 640x480 in 32 bits**, and 1024x768 in 16 bits, have been
  set but not played through.

## Formats

Fields the format documents still mark as guesses:

- **`.pac` archives** ([formats/pac.md](formats/pac.md)): the entry that
  `Ambient.pac` and `LMap.pac` list for themselves, empty, is probably the
  packing tool's own output file. (The field once guessed to be flags is
  the data offset's high half, from the game's code.)
- **`.DDF` fonts** ([formats/ddf.md](formats/ddf.md)): settled from the
  game's code: the fields once read as a line height, descent, em height
  and space advance are each page's ascent and descent. Only why one font
  (Times New Roman Italic) has a page-1 descent one more than its glyphs'
  is open.
- **The install** ([formats/install.md](formats/install.md)):
  `Launcher.exe` is guessed to be the CD's `AutoPlay.exe` menu (same
  size; neither is in the copy studied, so they haven't been compared);
  other languages presumably ship `Locale_<xx>.pac`. Only the English
  version has been seen. (`BaseClassComp.dll` and `BehaviorComp.dll`'s
  roles are now known from their exports.)

The `.FIN` model format's former guesses have all been settled from the
game's own `LoadBinary` code ([formats/fin.md](formats/fin.md)).

## Systems

- **Windows other than the developer's:** built and played on Windows with
  Visual Studio 2022 (x64) and on Linux with GCC. Other compilers,
  32-bit builds, macOS and ARM (where the JIT isn't available and the
  interpreter runs) haven't been played on.
- **Displays faster than 60 Hz:** the original's speed follows the
  display (its flips wait for it); Reluva always paces to 60 Hz. Whether
  the original plays differently at 75 or 85 Hz is unknown.
