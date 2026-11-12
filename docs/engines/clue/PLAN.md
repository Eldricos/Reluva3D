# Clue: plan

## Goal

Reimplement Clue: Murder at Boddy Mansion (and the UK's Cluedo), found in
an install folder or on the CD (US or UK data; 2.0 or 2.2), and played as
the original. How, and why, is under [Decisions](#decisions).

## Phases

Each step can be seen working before the next.

1. **Detection and a data viewer** (done). Finds the game in an install
   folder or on the CD (US or UK data; 2.0 or 2.2). Readers for `PACK`,
   `DDRV`, both image versions and `ANIM`. Viewers for the animations
   (playing), images, sounds and text, described in the README. `reluva-tool`
   commands to list, extract and check the files. Unit tests on made-up
   files.
2. **The mansion** (done, to be checked against the original). Reads
   the objects (`.o`) and `clue.lvl` ([formats/level.md](formats/level.md))
   and draws the house: floors, walls, furniture and the suspects at their
   starting places, sorted by the boxes they fill, the rooms' animations
   playing. Scrolls; an overview of the whole house. The near walls
   see-through, solid or hidden.
3. **Video** (done, [formats/videos.md](formats/videos.md)). An AVI reader and a Cinepak decoder in
   `src/common/video` ([common/cinepak.md](../../common/video/cinepak.md)); the
   videos page plays all 44 with their sound.
4. **The interface** (done, but for the in-game panels). `dialogInfo.res`
   read ([formats/resi.md](formats/resi.md)); the game's
   fonts, dialogs, buttons, sliders and pointer. The game now starts as
   the original: the intro, the main menu over the storm, Start Game
   (locally), the six suspects' players (human with a name, or computer
   at a difficulty), the game variants, Preferences (saved), How to Play
   with its texts, Credits, Exit; then "Entering the mansion", the deal,
   and the mansion with the game's bar and the Esc menu. The in-game panels
   (cards, notepad, suggestion and accusation, disproving) come with the
   rules, as they need them.
5. **The game** (done, to be checked against the original). The board
   read from the level ([formats/board.md](formats/board.md)):
   squares, doors, starting squares, secret passages. The rules as the
   game's own texts give them (`rules.h`): the deal, turns, the die or No
   Die Roll's nine moves, moving, suggestions bringing the suspect in,
   disproving in turn, accusations, being out, the end. Played as
   described in [systems/playing.md](systems/playing.md#in-the-game): moves planned square by square
   and walked, the suggestion and accusation window, the suggestions'
   videos, disproving, the card shown, the solution, the notepad and the
   cards, the board from above, the ticker's sentences and the spoken
   lines. Hot seat: each human sees only their own cards and notes.
   Weapons are cards only: the mansion has no weapon tokens. Computer
   players at three levels (step 6). Unit tests for the board, the
   rules and whole games of computer players.
6. **Computer players** (done, to be compared with more of the
   original's games). Each keeps a notebook of who holds which card
   (`ai.h`) and deduces from what it sees; the set-up screen's three
   levels see and deduce more or less ([systems/ai.md](systems/ai.md)). Unit tests for the
   deductions; `reluva-tool clue board <clue.lvl> <games>` plays mixed
   games of the three levels on the real board. The levels are matched
   to recordings of the original at each.
7. **The rest** (done, to be checked against the original). The rooms'
   hot spots ([systems/sounds.md](systems/sounds.md)), thunder and gusts of wind with the drapes blowing,
   the fire and the fountain heard near them, thunder over the menus'
   storm; saved games ([systems/saves.md](systems/saves.md)). Saved games are Reluva's own, in eight slots as
   the original's Save window and Load Game screen have: the original's
   needn't load. The original's `Prefs.ini` is read the first time
   ([systems/preferences.md](systems/preferences.md)).
8. **Network play** (done, to be tried between real machines). Games
   on the local network and over the internet by address, in the
   original's network screens. Reluva's own protocol, not DirectPlay's:
   Reluva and the original can't play each other
   ([systems/network.md](systems/network.md)).

## Decisions

### Approach

Reimplement the game, as for New Worlds, rather than run `Clue.exe` in
the x86 interpreter as Elite Force's DLLs are. Elite Force's DLLs only
ask a small engine interface of their host; `Clue.exe` is a whole Windows
program built on MFC, DirectDraw, DirectShow, DirectInput, DirectPlay,
Miles and SmartHeap, which would all need standing in for. The game's
rules are the board game's, which are public, and its look and content
are data-driven: the mansion, the menus and dialogs, the text and the
videos are all in files.

Where behaviour isn't in the data (the computer players' play, timings,
which sound plays when), it's found by watching the original run and,
where needed, by reading `Clue.exe`, as CONTRIBUTING.md allows. Eagle's
exported names give the structure.

The engine goes in `src/engines/clue/`, with the decoders likely to be
of use to other games (AVI, Cinepak) in `src/common/`.

