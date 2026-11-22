# Star Trek: Birth of the Federation: the main menu's music and sounds

> **Status:** partial · **Code:** `src/engines/botf/menu_audio.cpp` · **Checked against:** a recording of the game with sound, the main menu and every screen it opens · **Confidence:** confirmed unless marked

## Overview

Under the main menu and every screen it opens, the game plays music from
`music.snd`; its controls, and going from screen to screen, play effects
from `sfx.snd` and speech from the speech archive (`english.snd`)
([formats/snd.md](../formats/snd.md)). Which sound goes with what was found
by looking for each of the archives' sounds in a recording of the game
with sound, and matching where each is heard with what the recording shows
happening.

## How it works

### The music

`music.snd`'s first piece, its five segments (entries 0-4, 20 to 28
seconds each), one after another with nothing between them. It begins
with segment 0 or 1, and then plays only 2, 3 and 4, never one twice in a
row: two recordings, two and seven minutes long, play 0, 3, 2, 4, 3, 2
and 1, 2, 4, 2, 3, 2, 4, 3, 4, 3, 2, 3, 2, 4, 2, 4, 3, each segment
starting where the one before ends. Reluva picks the first and each next
at random among those *(inferred)*. The music
doesn't stop or start again going from screen to screen.

### The sounds

The recording's audio runs 0.3 seconds behind its picture, the same
every time; the times here are the picture's.

| When | Sound | Archive entry |
|---|---|---|
| The main menu's buttons | none | |
| Opening the game setup screen | the click, and speech, 1.7 seconds | `sfx.snd` 87, `english.snd` 1362 |
| Its Accept, opening the empire selection screen | 0.5 seconds, and speech, 2.2 seconds | `sfx.snd` 50, `english.snd` 1361 |
| An empire chosen | an effect and speech of its own, by `race.rst`'s order | `sfx.snd` 60-64, `english.snd` 318-322 |
| Opening Save Game | the click | `sfx.snd` 87 |
| A slot chosen on Save Game | the click, and speech, 1.6 seconds | `english.snd` 722 |
| Save Game's Accept, back to the game | as going back to the menu, and speech | `sfx.snd` 50, `english.snd` 723 |
| Opening Options or Load Game | a click, 0.1 seconds | `sfx.snd` 87 |
| Opening the Hall of Fame | 1.2 seconds | `sfx.snd` 75 |
| Opening Multiplayer Setup | none | |
| Back to the main menu, from any of them | 0.5 seconds | `sfx.snd` 50 |
| A control on the game setup screen | a short click, 0.03 seconds | `sfx.snd` 66 |
| A button on Options, Load Game, Multiplayer Setup | the click | `sfx.snd` 87 |
| A saved game's slot chosen on Load Game | the click, and speech, 1.4 seconds | `english.snd` 323 |
| The voice volume let go | speech, a different line each time | `english.snd` 351, 621, 725 heard |
| The video volume let go | 0.7 seconds | `sfx.snd` 162 |
| The effects volume let go | as going back to the menu | `sfx.snd` 50 |
| The music volume let go | none: the music playing changes | |

- **The same sound in several entries:** `sfx.snd` 50, 55 and 97 hold the
  same sound, as do 66, 137 and 142, and 87 and 107. Which the game plays
  isn't known; Reluva plays the first *(guessed)*.
- **The voice volume's lines:** four slider moves, three different lines.
  Reluva picks one of the three heard at random *(guessed: the game's
  choice may be wider)*.
- **Opening the game setup screen's click** is in one recording and not
  the other; Reluva plays it.
- **Not played yet:** Retire's screen (a click on Retire, then `sfx.snd`
  53, 80 and 112 on it), which needs a game.
- **Two recordings:** the first, of the main menu's screens; the second,
  of a new game set up, each empire chosen in turn, the game started,
  saved, and retired. Starting the game from the empire selection screen
  makes no sound.

### Volumes

The options' volumes, 0 to 127 (`Stbof.ini`, [game-files.md](game-files.md)),
set the music's, the speech's and the effects' loudness. Reluva scales the
mixer's as recorded at 127 and in proportion below *(inferred)*. While the
options are open their volumes apply as they're set, so a sample, and the
music, are heard at the new one; Cancel puts the old ones back. The video
volume's sample plays at the video volume, and at the effects' too, as
Reluva's mixer has no kind of sound for movies.

## In Reluva

`MenuAudio` (`src/engines/botf/menu_audio.cpp`) opens the three archives
where `GameFiles` finds them (`music.snd` on the CD for a minimum install),
queues the music's segments two ahead, and plays the effects and speech
the screens ask for (`UiSound`, `ui/layout_screen.h`) and the engine's
moves between screens. Without an audio device, or an archive, that part
is silent. The unit tests (`botf.unit.screens`: "menu music") cover the
music's order.

## Sources and checks

- **Where the facts come from:** a recording of the English retail game
  by the user (2 minutes 12 seconds, 30 frames a second, 48 kHz stereo),
  November 2026: the main menu, the game setup screen, Options (each
  volume moved), the Hall of Fame, Load Game, Multiplayer Setup, each
  opened and left.
- **How it was found:** every one of `sfx.snd`'s 163 sounds, the speech
  archive's 1,364 and the first 4 seconds of `music.snd`'s 151 segments,
  decoded by Reluva, looked for in the recording's sound by normalized
  cross-correlation (0.6 or more for a match; the effects matched at 0.8
  to 0.99, the speech at 0.7 to 0.9, as the music plays under them). The
  music was followed two seconds at a time through the whole recording.
  The screens' changes were found in its picture, and the moves sliders
  made by the pointer's place.
- **How it was checked:** Reluva's `MenuAudio`, playing the menu's sounds
  over its music from the user's archives into a recording, looked for in
  that recording the same way: each sound where it was played, the music's
  segments one after another.

## Open questions

- How the game picks the music's next segment, and the first; whether
  `music.tbl` says ([formats/stbof-res.md](../formats/stbof-res.md)).
- Which of the same sound's entries the game plays, and the voice
  volume's lines.
- The empire selection screen's sounds, and Save Game's.
- Whether the game's volumes scale as Reluva's do.
