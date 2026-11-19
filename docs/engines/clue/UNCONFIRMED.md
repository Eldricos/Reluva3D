# Clue: unconfirmed items

What Reluva does that isn't yet confirmed against the original, by
`Clue.exe`'s code or by recordings, or where the two disagree. Each says
what Reluva does now, what is known, and what would settle it. Items to
look at in a playthrough are in [TESTING.md](TESTING.md); what Reluva
knowingly does differently is in [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md).

## Read from the code, not seen running

### Code and recordings disagree

Reluva follows the code here; recordings of the original suggest otherwise,
without settling it.

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
  way the original's way-finder takes (0x41A0C0), round the furniture and
  the other suspects.
- Walking into a room from a door (0x4242A2) and out of a secret passage
  (0x425CC0) by the same way-finder.
- Hot spots: only on your turn, only in your room, hit by the box's
  outline; the credits portraits reacting to the pointer; clicks on the
  board from above.
- Animation speeds and play modes (once, round, back and forth) and the
  footsteps on the walks' key-5000 frames.
- The fountain's water drawn see-through by its parts' flags.
- The menus' lightning on a click, and the music by screen.
- The in-game Preferences and How to Play windows (dialogs 37-39): their
  sliders and the rules' scroll knob.

### Read but not used

Fields the original reads from its files and then never uses, as far as
its code has been followed; nothing on screen is known to depend on them.

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

### A UK install

- **Reluva:** an install whose `Data/Classic` has `Sounds/end turn.wav` is
  the UK's (`clue-uk`), and plays as Cluedo
  ([README.md](README.md#identification)).
- **Known:** the CD's `Data/UK` has that file and `Data/US` doesn't; an
  install copies the chosen language's files into `Data/Classic`. Only US
  installs (2.0, and 2.2 patched) have been seen.
- **To settle it:** a UK install's file list, or the game-files test run
  on one (it would make the `uk-english-2.0` or `uk-english-2.2` records).

### The loser's window (dialog 40)

Placed by EagleM.dll, not by Clue.exe.

- **Reluva:** at (205, 11), where `Clue.exe` puts the exit window (0x435C7F)
  and where recordings show the Esc menu, its Save window and its
  Preferences.
- **Known:** it's the same size (231 × 395); no recording shows it.
- **To settle it:** a screenshot of a wrong accusation's window.

## Guessed

### The UK release's full title

- **Reluva:** names the UK's game "Cluedo: Murder at Blackwell Grange" in
  the launcher and the window's title.
- **Known:** the UK's files say "Cluedo" (its `dialogInfo.res`: "Are you
  sure you want to exit Cluedo?", and the menus' pictures); `Clue.exe`
  names itself "Clue / Cluedo". "Murder at Blackwell Grange" is in none of
  the game's files: it's the title the UK release is remembered as having
  been sold under.
- **To settle it:** the UK box, manual or CD label.

## Settled

| Date | What | How |
|---|---|---|
| 2026-11-01 | The game's sounds play 1.6% fast (65/64 of each file's rate) | `Clue.exe` sets each sound's rate to the file's × (x + 1) / 64 with x = 0x40; a recording of the original then matched: its wind loops and thunders were found only at that rate, on the weather's 5-minute cycle ([sounds.md](systems/sounds.md)) |
| 2026-11-01 | The weather is one 300-second cycle, not thunder and wind at random | `Clue.exe` 0x41C690; the recording above has its gusts on that cycle. A playthrough had found the rain starting and stopping at random in Reluva's first version ([sounds.md](systems/sounds.md)) |
| 2026-11-01 | On your turn you can walk about the room you're in | Seen in the original in a playthrough, then found in `Clue.exe` (0x423B5B, 0x424900); its way-finder (0x41A0C0) followed 2026-11-06 ([playing.md](systems/playing.md)) |
| 2026-11-02 | The windows over the game (the Esc menu, Save, Preferences, the exit window) are at (205, 11) | `Clue.exe` places the exit window there (0x435C7F), and recordings show the Esc menu, its Save window and its Preferences there to the pixel ([screen.md](systems/screen.md)) |
| 2026-11-07 | The butler speaks only "..., it is your turn", the die's four prompts, and the disproving and accusing lines | `Clue.exe`'s one voice queue (0x438F80, the prompt chosen at 0x439140) can't give "please move", "choose object" or "end turn"; in three recordings with the voice on, the die's prompts are heard 31 times in all and those three never ([playing.md](systems/playing.md#in-the-game)) |
