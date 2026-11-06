# Clue: known differences from the original

Where Reluva knowingly does something other than what `Clue.exe` 2.2 does.
Some are choices (the original's way can't work today, or wasn't wanted);
some follow the evidence over the code where the two disagree. Things that
may differ but haven't been settled are in
[unconfirmed.md](unconfirmed.md). The main notes: [clue.md](../clue.md).

## By choice

| What | The original | Reluva | Why |
|---|---|---|---|
| Network play | DirectPlay, and the MSN Internet Gaming Zone for games over the internet | Its own protocol over UDP: games on the local network, or joined by the host's address ("Internet \| by address" in the Connection menu) | The Gaming Zone is gone and DirectPlay isn't portable. See [clue.md, Network play](../clue.md#network-play). |
| Saved games | The original's own format | Reluva's own format, eight slots in a `saves` folder beside its preferences | Not compatible with the original's saves, and not meant to be. |
| Settings in network games | One machine's preferences | Each player's own | Chosen when network play was designed. |
| CD music | Played from the disc (Miles' "redbook" functions) | Played from the tracks ripped once into the game's folder (`track02.wav` ... `track05.wav` or `.mp3`; `reluva-tool cue-rip` makes them from a BIN/CUE image) | Reluva can't read the tracks from the files on the disc. Without them the game is silent, as the original is without its CD. |
| Preferences file | `Prefs.ini` beside the game | `preferences.ini` in Reluva's folder for the game, read from the original's `Prefs.ini` the first time | Reluva doesn't write into the game's folder. |

## The evidence over the code

| What | `Clue.exe`'s code | Recordings of the original | Reluva |
|---|---|---|---|
| The loading's music (`load music loop22khz.wav`) | Played through the same stream function as the wind, which sets the rate to 65/64 of the file's (0x4331F0) | Plays at the file's own rate (measured 1.000, against 1.016 for every other game sound) | At the file's rate, as the recording |

Everything else follows the code, where the two were set side by side;
where they still disagree (the computer players naming themselves, the
weather's pace), Reluva follows the code and the question is in
[unconfirmed.md](unconfirmed.md).
