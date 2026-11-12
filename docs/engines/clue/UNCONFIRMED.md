# Clue: unconfirmed items

What Reluva does that isn't yet confirmed against the original, by
`Clue.exe`'s code or by recordings, or where the two disagree. Each says
what Reluva does now, what is known, and what would settle it. Items to
look at in a playthrough are in [TESTING.md](TESTING.md); what Reluva
knowingly does differently is in [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md).

## Read from the code, not seen running

### Code and recordings disagree

#### Computer players naming themselves

- **Reluva:** as the code: a suspect and a weapon not known to be held,
  by chance (0x42D8A0). A player that doesn't hold its own card may name
  itself: about one suggestion in five at every level.
- **Known:** every step of the choice was traced and matches (the cards
  known held 0x42CFF0, the "holds one of" lists 0x42D020 and 0x42CCE0,
  who adds to them 0x418D00, the deal 0x41C1C0, the pick 0x42D050, the
  levels at the player's +0xCC). Nothing in the code keeps a player off
  its own suspect.
- **Recordings:** the VM recording's experts made 15 suggestions before
  they knew the answer's suspect and none named itself (9% of Reluva's
  games look like that); the Steam Deck recording's normal players named
  themselves 3 times in about 30 (about one game in eight of Reluva's);
  novices often, as Reluva's.
- **To settle it:** more recorded games against normal or expert players,
  their suggestions counted (`reluva-tool clue board <clue.lvl> 300 expert 6`
  gives Reluva's figures to set them against).

#### The weather's pace

- **Reluva:** the code's 300-second cycle (0x41C690) on the real clock:
  light gusts from 60 s, heavy 120-180 s, a big thunder at 150 s, and so on.
- **Known:** the cycle's clock is fed the real time between game ticks
  (0x4186A0), videos and menus included.
- **Recording** ("gameplay", YouTube `dq0uYzGq2ok`): the heavy gusts last
  about 80 s and come back about every 6½ minutes, where the code gives
  60 s and 5 minutes. The gaps may be the video's cuts, or stretches the
  sound search missed.
- **To settle it:** a recording of an unbroken game with a clock on screen,
  the gusts timed by ear.

### Built from the code, not yet seen working

These follow `Clue.exe` closely but haven't been tried in a real game; the
checks are in [TESTING.md](TESTING.md).

- Walking about your own room (0x423B5B, 0x424900), and in a network game
  seen by everyone (sent as the original sends the click, 0x41BD90), the
  way the original's way-finder takes (0x41A0C0).
- Hot spots: only on your turn, only in your room, hit by the box's
  outline; the credits portraits reacting to the pointer; clicks on the
  board from above.
- Animation speeds and play modes (once, round, back and forth) and the
  footsteps on the walks' key-5000 frames.
- The fountain's water drawn see-through by its parts' flags.
- The menus' lightning on a click, and the music by screen.
- The in-game Preferences and How to Play windows (dialogs 37-39): their
  sliders and the rules' scroll knob.

### Not traced, no visible effect known

- **Dialog controls' fields 12, 13 and 20** ([resi.md](formats/resi.md)):
  the same in every control of a dialog; EagleM's reader copies the block
  whole (0x1001C070). Nothing in the game is known to depend on them.
- **Key 1800** on the hall's `r_logo` ([level.md](formats/level.md)):
  `Clue.exe` never reads it.

## Seen running, not traced in the code

### The board from above (`UI:Aoverhead`)

Placed by EagleM.dll, not by Clue.exe.

- **Reluva:** at (208, 5), where a recording shows it; its squares 13
  pixels, column 9 and row 9 at (8, 15) in it.
- **Known:** the class (about 0x43CF90) draws it itself; its window's place
  wasn't traced. The recording's match was to the pixel.

## Inferred

### The loser's window (dialog 40)

Placed by EagleM.dll, not by Clue.exe.

- **Reluva:** at (205, 11), where `Clue.exe` puts the exit window (0x435C7F)
  and where recordings show the Esc menu, its Save window and its
  Preferences.
- **Known:** it's the same size (231 × 395); no recording shows it.
- **To settle it:** a screenshot of a wrong accusation's window.

## Guessed

None yet.

## Settled

None yet.
