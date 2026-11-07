# Testing (Clue: Murder at Boddy Mansion)

How Reluva's Clue engine is checked: the automated checks, what they
cover and don't, and a list of things to try in a playthrough by hand.

## Automated checks

### Unit tests

`ctest --test-dir build` runs the engine's suites (`tests/engines/clue/unit/`,
`reluva-tests clue` alone) among the others, on files made up in the tests
and needing no game files, so they run in CI:

| Suite | What it checks |
|---|---|
| `clue.unit.detection` | Finding the game: an install's language by `Sounds/end turn.wav` (the UK's alone have it), the CD as both, a `Clue.exe` not in the table as an unknown release |
| `clue.unit.formats` | Packed archives, images, animations (their rate and play flags), the game's folders (an install's, the CD's), objects, levels, `dialogInfo.res` |
| `clue.unit.mansion` | What's drawn in front of what, the hot spots' boxes, lighting, a mansion built from a level |
| `clue.unit.frontend` | A dialog drawn and clicked, typing in its boxes; the original's `Prefs.ini` |
| `clue.unit.board` | The board a level lays out: squares, doors, passages, moves |
| `clue.unit.rules` | Dealing, suggestions, disproving, accusations; the notebook; computer players at each level |
| `clue.unit.saves` | A game saved and read back |
| `clue.unit.network` | Two machines over the in-memory network: finding a game, joining, the lobby, chat, the start, every action in one order (walks about a room among them), a network losing about a third of its packets, a player gone |

### The basic engine test: no game files

`clue.engine` (`tests/engines/clue/engine.cpp`) makes a small install of
its own and plays it through the engine as the launcher starts it, so a
change anywhere (in `src/common/`, say) that alters what the engine does is
caught. CI runs it on Linux and Windows with the rest.

- **The install:** a stand-in `Clue.exe` beside the ticker's sentences
  (found by the fallback detection, which looks for the files whatever
  their contents), a `dialogInfo.res` of its own with the menus a game
  goes through (each a column of buttons), a short Cinepak intro and deal
  with sound, and the unit tests' small board as `clue.lvl`, with floors
  to draw.
- **The play:** the intro; New Game, a game here; Miss Scarlet a person,
  Colonel Mustard and Mrs. White computer players (the game needs three),
  Start; the deal; the game, saved in the first slot as "Test" from the Esc
  menu, and left; the saved game loaded from the main menu; back to the
  launcher.
- **The record** (`tests/engines/clue/data/engine-record.txt`): what's
  detected, every screen shown (a checksum), the window's titles, and the
  saved game's name and a checksum of the game it holds.
- **When it fails:** it prints each line, with the old value beside any
  that changed. If the change is meant, `reluva-tests clue.engine --update`,
  review the record's diff, and commit it with the change.

### The game-files test: with your own copy of the game

Point `RELUVA_CLUE` at the game's folder (an install, the one holding
`Clue.exe` and `Data/Classic`, or the CD or a copy of it) and run the steps
labelled `gamedata` (`tests/engines/clue/gamedata.cmake`):

    RELUVA_CLUE=/path/to/Clue ctest --test-dir build -L gamedata -R clue

On Windows: `set RELUVA_CLUE=C:\Games\Clue`, then
`ctest --test-dir build -C Release -L gamedata -R clue`. Without the variable
the steps show as skipped.

| Step | What it runs |
|---|---|
| `clue.gamedata.release` | What the game is (`detect`): an install's one language, the CD's two |
| `clue.gamedata.files` | Every image, animation, object and dialog read, the mansion built from `clue.lvl`, every picture the dialogs name found (`clue check`) |
| `clue.gamedata.levels` | `clue.lvl`'s placements, each one (`clue level -v`) |
| `clue.gamedata.boards` | The board it lays out, and 200 games of computer players on it, the same games every time (`clue board`) |
| `clue.gamedata.play` | The game itself, headless, as below (`clue game-run`; on the CD, the US's and the UK's) |

`reluva-tool clue game-run <game folder> [-uk] [-shots <folder>]` plays the game
on a System with no window whose clock moves only as the game waits, so
every run is the same: the intro skipped, a new game of Miss Scarlet (a
person) and two computer players set up in the menus by clicking the game's
own buttons where `dialogInfo.res` puts them, the deal, ten seconds of the
game, saved as "Reluva" from the Esc menu, left, and loaded again from the
main menu. It prints a checksum of every screen shown and of the saved
game; `-uk` plays the UK's game (Cluedo) where the folder has it; `-shots`
writes a screen a second as PNG files, to look at. On the CD the files step
also checks the UK's files (`clue check -uk`).

Each step's output is kept in the build (`gamedata/clue/<step>/`), the
previous run's beside it, and its checksum and summary line go in the
record, `tests/engines/clue/data/gamedata/<release>/<step>.txt`. Records so
far, made on Linux: `english-2.2` (the US install patched to 2.2),
`english-2.0` (the US install as the CD installs it) and `2.0-cd` (the CD
itself, both languages). A UK install's would go in `uk-english-2.0` or
`uk-english-2.2`; none has been seen yet. Another release's go in a folder of their own,
with `RELUVA_GAMEDATA_UPDATE=1`. See
[DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests) for the shared helpers.

### What the automated checks don't cover

Sound and music as heard, timing against the original, network play
between two real machines, and anything that needs a person's eye on the
screen against the original: those are what the playthrough below is for.
The reverse-engineering harnesses (`tools/clue-re/`, see
[REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)) aren't built or run by
the tests.

## A playthrough by hand

A checklist for playing Reluva's Clue (and, where it says so, the original
beside it) to confirm what was built from `Clue.exe`'s code but hasn't been
seen working, and to collect what [UNCONFIRMED.md](UNCONFIRMED.md) still
needs. Highest value first within each part.

**Have running:** a screen recording with sound, for anything about timing,
motion or sound; screenshots are enough for positions. Note the recording's
time with each finding. **[shot]** = a screenshot is enough; **[rec]** =
needs a recording; **[orig]** = compare with the original game.

**Set up:** the game data from the CD and the 2.2 patch; the CD's music
ripped (`reluva-tool cue-rip CLUE.cue <game folder>`) for the music checks.

### 1. The board and moving

- **Walking about your room** [rec]: on your turn, in a room, click its
  floor in a few places, round the furniture. The suspect should walk
  there without using a move; a move you had started planning is dropped
  (with `unting`). Clicking a door's square starts a move as before. Try it
  before and after rolling. [orig] Compare the way taken round furniture:
  straight steps before slanting ones, never cutting a corner.
  In a network game, the other players should see the same walk.
- **Move numbers** [rec]: plan a move square by square. Each number should
  appear on its square with a `ting`; clicking back to an earlier square
  takes the later numbers away with an `unting`; a square you can't go to
  gives `nogo`.
- **Secret passages** [rec]: take one from a corner room; the suspect
  should come out of the other end's passage, facing out of it.

### 2. Hot spots

- **Reactions** [rec]: on your turn, in a room, click its furniture that
  reacts (the bear and the rhino in the billiard room, the piano, the
  phonograph, the stove, the globe, the grandfather clock ...). Each
  should play one of its reactions with its sound, and not again until it
  has finished.
- **When they don't react** [rec]: clicking them on a computer player's
  turn, from the corridor, or in a room you're not in should do nothing
  (the click is then a move's, or nothing).
- **The credits portraits** [rec]: in the study and the ballroom, the two
  small portraits in the credits painting should react when the pointer
  passes over them, with no click.
- **The board from above** [rec]: with the board shown, click where a hot
  spot of your room is drawn; it should react.

### 3. Sounds and music

- **Speed** [rec] [orig]: every game sound (clicks, steps, voices,
  thunder, the menus' thunder) plays 1/64 faster and higher than the
  file. Compare a voice line ("... cannot disprove") with the original.
- **Weather** [rec]: start a game and leave it running for six minutes
  without menus. Light gusts with rain should start at 1:00, stop at 1:15,
  start at 1:30, turn heavy at 2:00, a big thunder at 2:30, light again at
  3:00, stop at 3:30, light at 3:45, stop at 4:00, and the cycle starts
  again at 5:00; the drapes blow with the gusts. Small and middle thunders
  come by chance during the gusts. [orig] Time the same in the original
  (see [UNCONFIRMED.md](UNCONFIRMED.md#the-weathers-pace)).
- **The fire and the fountain** [rec]: walk past the library's fire and
  the conservatory's fountain; each should fade in and out with distance
  and pan with the view.
- **Footsteps** [rec]: each suspect's steps should fall as its feet come
  down (two a walking cycle), men's and women's different.
- **Music by screen** [rec] (needs the ripped tracks): the menus with the
  storm play no CD music; the character picker starts track 5, which plays
  on through the menus; leaving the main menu's Preferences stops it; the
  loading screen plays its own loop; the game starts on track 2 and goes
  2, 3, 4, 2 ...; after a suggestion's video one of 2-4 starts by chance.
- **Volumes** [rec]: the music slider changes only the music, the master
  slider the game's sounds and voices; the videos (the intro, the deal,
  the suggestions) play at full whatever either says. At the default settings the music should sit
  about as loud against the effects as in the original [orig].

### 4. Pictures and animation

- **Animation speeds** [rec] [orig]: the suspects walk at 13-16 frames a
  second, their idles go to the end and back (4-8 a second), the fires
  flicker at 8, the menus' storm runs at 15.
- **The fountain** [shot]: its water should show the floor through it.
- **The menus' lightning** [rec]: each click in a menu with the storm
  flashes the four corner pictures with a thunderclap for about a second
  before the click acts.
- **The screen's frames** [shot]: the left and right frames start at y 5
  (just under the top one) and meet the bar; the ticker's text scrolls
  between x 41 and 599.

### 5. The windows over the game

- **Esc menu** [shot]: the Esc menu, its Save window, Preferences, How to
  Play and the exit window all at the same place (left 205, top 11).
- **In-game Preferences** [rec]: each switch, both volume sliders (the
  change heard at once), Continue back to the game, Esc back to the Esc
  menu; the game going on behind it.
- **In-game How to Play** [rec]: each of the seven topics; the text's
  arrows and scroll knob; Back to the topics; Continue back to the game.
- **The loser's window** [shot] [orig]: make a wrong accusation and note
  where its window is (see [UNCONFIRMED.md](UNCONFIRMED.md#the-losers-window-dialog-40)).

### 6. The computer players

- **Self-naming** [rec] [orig]: in a game against five experts (and one
  against normal players), note every suggestion from the ticker: who
  suggests whom, and when each kind of the answer seems settled. Count
  those where a player names itself before that. The more games the
  better (see [UNCONFIRMED.md](UNCONFIRMED.md#computer-players-naming-themselves)).
- **Pace** [rec]: a computer player's turn without a suggestion takes 5-9 s
  from one "..., it is your turn." to the next; with one, about 20 s.
