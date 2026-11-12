# Clue: known differences from the original

Where Reluva knowingly does something other than what `Clue.exe` 2.2 does.
Some are choices (the original's way can't work today, or wasn't wanted);
some follow the evidence over the code where the two disagree. Things that
may differ but haven't been settled are in
[UNCONFIRMED.md](UNCONFIRMED.md). The main notes: [README.md](README.md).

## By choice

| What | The original | Reluva | Why |
|---|---|---|---|
| Network play | DirectPlay, and the MSN Internet Gaming Zone for games over the internet | Its own protocol over UDP: games on the local network, or joined by the host's address ("Internet \| by address" in the Connection menu) | The Gaming Zone is gone and DirectPlay isn't portable. See [network play](systems/network.md). |
| Saved games | The original's own format | Reluva's own format, eight slots in a `saves` folder beside its preferences | Not compatible with the original's saves, and not meant to be. |
| Settings in network games | One machine's preferences | Each player's own | Chosen when network play was designed. |
| CD music | Played from the disc (Miles' "redbook" functions) | Played from the tracks ripped once into the game's folder (`track02.wav` ... `track05.wav` or `.mp3`; `reluva-tool cue-rip` makes them from a BIN/CUE image) | Reluva can't read the tracks from the files on the disc. Without them the game is silent, as the original is without its CD. |
| Preferences file | `Prefs.ini` beside the game | `preferences.ini` in Reluva's folder for the game, read from the original's `Prefs.ini` the first time | Reluva doesn't write into the game's folder. |

### The evidence over the code

| What | `Clue.exe`'s code | Recordings of the original | Reluva |
|---|---|---|---|
| The loading's music (`load music loop22khz.wav`) | Played through the same stream function as the wind, which sets the rate to 65/64 of the file's (0x4331F0) | Plays at the file's own rate (measured 1.000, against 1.016 for every other game sound) | At the file's rate, as the recording |

Everything else follows the code, where the two were set side by side;
where they still disagree (the computer players naming themselves, the
weather's pace), Reluva follows the code and the question is in
[UNCONFIRMED.md](UNCONFIRMED.md).

## Bugs fixed

None. No bug of the original's is known that Reluva leaves out: where
`Clue.exe`'s code does something odd, Reluva does it too. For instance:

- **Three voices never spoken.** "Please move", "choose object" and the
  UK's "end turn" are in `Clue.exe`'s table of voices (0x45E698), but
  nothing plays them; Reluva doesn't either
  ([playing.md](systems/playing.md#in-the-game)).
- **The weather's clock runs on.** The weather's 300-second cycle is fed
  the real time between ticks (0x4186A0), so it goes on through the
  videos and the menus; so does Reluva's
  ([UNCONFIRMED.md](UNCONFIRMED.md#the-weathers-pace)).
- **Saving only between moves.** The Esc menu's Save works only when it's
  a human's turn and nothing is going on, as the original's
  ([saves.md](systems/saves.md)).

## Not yet as the original

| What | The original | Reluva | Why |
|---|---|---|---|
| Walking into a room | From the door to the suspect's place in the room, by its walk grid's way-finder (0x41A0C0), round what stands there | Straight from the door to the place | Not done yet: the way-finder is in Reluva (walks about your own room take it, [playing.md](systems/playing.md)), but walks in from a door don't use it yet. |

### Not yet checked against the original

| What | The original | Reluva | Why |
|---|---|---|---|
| The videos' colours | Decoded by Windows' own Cinepak codec (`iccvid.dll`) through DirectShow, in a 16-bit display mode | Decoded by Reluva's shared Cinepak decoder ([cinepak.md](../../common/video/cinepak.md)) to 24-bit colour, green as Y − U/2 − V with U/2 rounded towards 0 | The decoder Clue had before it moved to the shared one rounded U/2 down instead, so green differed by 1 in 255 wherever U is odd and negative; every other colour, and the sound, were the same in all 44 of Clue's AVIs. Which rounding `iccvid.dll` uses hasn't been looked at, and the original's 16-bit screen drops the lowest bits of green anyway. Against a recording of the original the difference is far below the recording's own (a mean 4 in 255). |

## Additions outside the game

| What | Where |
|---|---|
| A viewer of the game's data: the mansion (scrolled, from above, near walls on or off), the videos, animations, images, sounds and text. **F9** switches to it and back. | [README.md](README.md#using-it) |
| Playing from the CD itself, or a copy of it, as well as from an install: the original has to be installed. Either language of the CD can be played. | [README.md](README.md#identification) |
| A release that isn't in the detection table (another patch) is still found, as an unknown release. | [README.md](README.md#identification) |
| The launcher's master volume, over the game's own sliders. | [preferences.md](systems/preferences.md) |
| `reluva-tool clue` commands: list, extract and check the game's files, a level's placements, games of computer players on the board, and the game played headless (`game-run`). `reluva-tool cue-rip` makes the CD's music tracks from a BIN/CUE image. | [TESTING.md](TESTING.md), [music.md](systems/music.md) |
