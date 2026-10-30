# Clue: Murder at Boddy Mansion (1998)

Hasbro Interactive's Clue, developed by EAI. A plan for supporting it,
and what's known so far. Formats: [formats/clue/res.md](../formats/clue/res.md).

## What the game is made of

Source: the retail CD, a clean install from it and the 2.2 patch
(October 2026).

- **`Clue.exe`**: the game, Visual C++ 5 with MFC. It uses DirectDraw
  (16-bit colour; the title screen is 640×480), DirectInput, DirectShow for its videos
  (`AviPlayer.cpp`), the Miles Sound System (`Mss32.dll`), CD audio
  through Miles' "redbook" functions, DirectPlay and the MSN Gaming Zone
  for networked games, and SmartHeap (`SHW32.dll`).
- **`!EagleM.dll`**: EAI's "Eagle" library, used by the game for
  everything below its rules: the data files (`EGDataDrive`,
  `EGDataFile`), images and animations (`EGfxImage`, `EGfxRLEImage`,
  `EGfxAnimation`), fonts, cursors, the screen, and a window system
  (`EGuiWindow`, `EGuiButton`, `EGuiListCtrl`, `EGuiDialogBox`, ...). It
  exports 1,280 C++ functions under their decorated names, so the names
  of its classes and methods are known.
- **`ImageRLE16.dll`**: draws the run-length images, clipped, flipped,
  scaled and sheared.

Data (`Data\Classic\` once installed: the CD's `LangIndepend`,
`ClueSpecific` and `US` folders together, file for file):

| Folder | |
|---|---|
| `Resource` | the mansion's rooms, floors, walls, objects and the six characters: `.res` images, `.a` animations, `.o` objects |
| `Levels/clue.lvl` | the mansion, put together from them |
| `UI` | menus, dialogs, fonts, cursors, the notepad, the ticker |
| `dialogInfo.res` (install folder) | the layout of the menus and dialogs (`RESI`) |
| `Sounds` | WAV, all PCM: effects, and each suspect's spoken lines |
| `Text` | the How to Play pages, the ticker's sentences, credits |
| `Video` | AVI, Cinepak with PCM sound: the intro, the deal, a clip for each suspect with each weapon (`CM_candle.avi` ...) and a `_prison` clip for each suspect |

The CD also has `UK` data: the same sounds and text in British English
("Miss Scarlett", "Rev. Green", "dagger" for "Miss Scarlet", "Mr. Green",
"knife"). The patch replaces `Clue.exe`,
`!EagleM.dll`, `clue.lvl`, `dialogInfo.res` and `small`/`ticker` in `UI`.

The readme describes options for translucency, dynamic lighting,
walk cycles and walls, clickable animated objects in the rooms, hot-seat
play for several people at one computer, and a "no die roll" variant
played with credits.

### Identification

| File | Version | Size | MD5 of first 5000 bytes |
|---|---|---|---|
| `Clue.exe` | 2.0 (CD) | 386,048 | `7ddb4f359cfd8827b4f7b054e95b1cba` |
| `Clue.exe` | 2.2 (patch) | 391,168 | `cb43d1d845b786193353ac221e5f0ac1` |
| `!EagleM.dll` | 2.0 | 248,832 | `b512233497bbc18ed4dd875b0b6ec556` |
| `!EagleM.dll` | 2.2 | 242,688 | `a36d7d82e6ad515aca6307cc5968fc4c` |

## Approach

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

## Plan

Each step can be seen working before the next.

1. **Detection and a data viewer** (done). Finds the game in an install
   folder or on the CD (US or UK data; 2.0 or 2.2). Readers for `PACK`,
   `DDRV`, both image versions and `ANIM`. Viewers for the animations
   (playing), images, sounds and text, described below. `reluva-tool`
   commands to list, extract and check the files. Unit tests on made-up
   files.
2. **The mansion** (done, to be checked against the original). Reads
   the objects (`.o`) and `clue.lvl` ([formats/clue/level.md](../formats/clue/level.md))
   and draws the house: floors, walls, furniture and the suspects at their
   starting places, sorted by the boxes they fill, the rooms' animations
   playing. Scrolls; an overview of the whole house. The near walls
   see-through, solid or hidden.
3. **Video** (done). An AVI reader and a Cinepak decoder in
   `src/common/video` ([formats/video.md](../formats/video.md)); the
   videos page plays all 44 with their sound.
4. **The interface** (done, but for the in-game panels). `dialogInfo.res`
   read ([formats/clue/resi.md](../formats/clue/resi.md)); the game's
   fonts, dialogs, buttons, sliders and pointer. The game now starts as
   the original: the intro, the main menu over the storm, Start Game
   (locally), the six suspects' players (human with a name, or computer
   at a difficulty), the game variants, Preferences (saved), How to Play
   with its texts, Credits, Exit; then "Entering the mansion", the deal,
   and the mansion with the game's bar and the Esc menu. The network
   screens show but don't work yet. The in-game panels
   (cards, notepad, suggestion and accusation, disproving) come with the
   rules, as they need them.
5. **The game** (done, to be checked against the original). The board
   read from the level ([formats/clue/board.md](../formats/clue/board.md)):
   squares, doors, starting squares, secret passages. The rules as the
   game's own texts give them (`rules.h`): the deal, turns, the die or No
   Die Roll's nine moves, moving, suggestions bringing the suspect in,
   disproving in turn, accusations, being out, the end. Played as
   described under "In the game" below: moves planned square by square
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
   levels see and deduce more or less (below). Unit tests for the
   deductions; `reluva-tool clue-board <clue.lvl> <games>` plays mixed
   games of the three levels on the real board. The levels are matched
   to recordings of the original at each.
7. **The rest** (done, to be checked against the original). The rooms'
   hot spots (below), thunder and gusts of wind with the drapes blowing,
   the fire and the fountain heard near them, thunder over the menus'
   storm; saved games. Saved games are Reluva's own, in eight slots as
   the original's Save window and Load Game screen have: the original's
   needn't load. The original's `Prefs.ini` isn't read: Reluva keeps its
   own preferences.
8. **Network play** (later). The original's local-network games, and
   games over the internet by address in place of the Gaming Zone, which
   is gone. Whether to speak DirectPlay's protocol, so as to play against
   the original, is to be decided then.

## The computer players

Each computer player keeps a notebook: for each card, whether each
player and the case file hold it, lack it, or might. What it notes and
how it chooses, by level, matched to the recordings below:

| | Novice | Normal | Expert |
|---|---|---|---|
| Its own cards; cards shown to it | yes | yes | yes |
| Its suggestions nobody could answer: what it doesn't hold is the answer | yes | yes | yes |
| Who couldn't answer its own suggestions lacks their three cards | yes | yes | yes |
| A card is in one place; the case file holds one of each kind | yes | yes | yes |
| Who couldn't answer others' suggestions | | | yes |
| Who answered a suggestion it didn't see holds one of the three | | | yes |
| A wrong accusation: not all three are the answer | | | yes |
| What it names | anyone and anything; itself about a quarter of the time | what it doesn't know | a kind's answer once known; else the unknown card fewest might hold; never itself in doubt |
| Which card it shows | any | one the suggester has seen | that, else one shown to someone else |

The notebook can also count each player's cards; no level does, as
that made experts far quicker than the original's. Computer players
accuse only when they know the answer, so they never accuse wrongly.
They head for the nearest room that may be the answer (once the room
is known, any room), taking a secret passage when the room beyond is
worth it; without the die, they keep three moves to suggest with.

**Measured against the original** (the recordings below; Reluva's
figures from `reluva-tool clue-board` and 300 games each of the same
make-up):

| Level | Original | Reluva |
|---|---|---|
| Novice (3 players) | no accusation in 14 and 12 rounds (two games); named itself 26% | 21.5 rounds a game, 20% won within 12; names itself 26% |
| Normal (6 players) | no accusation in about 10 rounds; named itself 10% | 11.4 rounds, 57% won within 12; 16% |
| Expert (6 players) | won in about 10 rounds; never named itself | 6.0 rounds; 2% |

The experts are still quicker than the one recorded expert game; with
one game to go on, that's left as it is. In mixed games of two of each
level (`clue-board ... 600`), experts won 534, normal players 53,
novices 13.

**The recordings.** Read off each game's ticker, every turn and
suggestion:

- *Expert*: the VM recording (8:20-23:48), five expert players and a
  human.
- *Normal*: "Clue: Murder at Boddy Mansion | Cluedo ... | Steam Deck
  OLED" (YouTube `OesVaOfek9M`, 4:00-24:10): five normal players and a
  human, who won at 24:00. Every computer player suggested on nearly
  every turn, a different room each time; none had accused. They named
  themselves three times in about thirty suggestions.
- *Novice*: "Playthrough 01" and "02" (`kXb0pdCPDuo`, 4:37-21:30, two
  novices and a human; `UCJ_YqBDIOQ`, 1:23-15:40, the same). The humans
  won both; neither novice had accused. Novices named themselves often
  (Colonel Mustard three times, Mrs. Peacock three, Professor Plum three
  of six) and kept naming cards they could know (Colonel Mustard the
  candlestick five times running).

**The original's play.** The VM recording's game (8:20-23:48, Miss
Scarlet human, the others computer players; the answer Miss Scarlet,
the revolver, the billiard room), read off its ticker:

- A computer player suggests on about half its turns; the rest are
  spent walking. No computer player named itself.
- Once a kind is settled, it names that kind's answer and varies the
  rest: from 17:52 on, Mr. Green, Mrs. Peacock, Professor Plum, Colonel
  Mustard and Mrs. White all suggest "Miss Scarlet with the revolver in
  ..." (the hall, the dining room, the library, the study, the
  ballroom, the billiard room), as Reluva's experts do.
- Some repeat a suggestion another made earlier ("Mrs. White suggests
  Mr. Green with the pipe in the hall" at 14:05 and 17:38), so they
  don't take in everything others learn.
- Professor Plum, suggesting in the billiard room with nobody able to
  disprove it (23:24-23:38), accused at once, in the same turn, and was
  right: the game took 15 minutes, about ten rounds.



## Hot spots, weather and sounds

Source: `clue.lvl` and the objects (`.o`), the readme ("Clickable Items
- ... Click on the bear in the Billiard Room, and you may see him dance,
or you could get mauled!"), and `Clue.exe` 2.2's table of sounds.

**Hot spots.** Furniture whose object has states 20, 21 or 22 (`..._ag`,
`..._ab`, `..._an` animations: good, bad, neutral) reacts when clicked,
with one of them by chance: the bear dances or mauls, the piano's lid
opens, the telephone on the lounge's lamp table rings, the stove's pot
boils over, and 25 or so others. The object's timings give each
reaction's sound: time 1500, 1600, 1700 for states 20, 21, 22, the value
a number into `Clue.exe`'s sound table from its 61st entry
(`bear_g.wav` 0, `bear_b.wav` 1, `rhino_b.wav` 2 ... `mnky_b.wav` 21; the
table is a list of pointers to the names at 0x5CA98 in 2.2's file).
Reactions don't touch the game. Reluva takes clicks on hot spots any
time but while a human is planning a move (the clicks are then the
move's); see-through walls don't catch the pointer.

**Weather.** Thunder (`BigThunder4/5`, `MidThunder1-3`, `SmallThunder6-8`)
every 20-60 s; gusts of wind (`wind light gusts`, `wind heavy gusts`)
every 25-75 s, when the drapes (type 12 objects) play their state 23
(light) or 24 (heavy). Over the menus, `mnThndr1`/`mnThndr2` every
12-30 s. How often the original does each is a guess.

**Sounds of things.** The library's fire (`LBfirepl_n.wav`) and the
conservatory's fountain (`fntn_n.wav`) loop while they're near the
view's middle, louder the nearer (full within 150 pixels, none beyond
600).

## Saved games

From the Esc menu, **Save** (when it's a human's turn and nothing is
going on) opens the original's Save window: eight slots; click one,
type its name, OK (or Enter). **Load Game** on the main menu lists them;
choosing one goes into the mansion and on from where it was saved,
without the deal.

They are kept in Reluva's folder for the game, `saves/slot1.sav` ...
`slot8.sav`: text, a header line `reluva-clue-save 1`, the name, then
the game: the players, the rules' state (cards, places, the turn, the
die's random state), each computer player's notebook and random state,
each human's notepad, and where the suspects stand. A save is written
beside the old one and then put in its place.

## Playing

The game opens with its intro (a key or a click skips it), then its
menus, as the original. In the game, the arrow keys move the view
(**Shift**: faster) and **Esc** opens the game's menu. The preferences
are saved in Reluva's folder for the game (`preferences.ini`); "Mansion
Walls" off hides the nearest walls, "Translucency" off makes them solid.

### In the game

As the original, from its recordings and its How to Play texts:

- **The die** (the drawer's first icon, at the bar's right end, on a
  human's turn): rolls; the icon shows the number. Then click the
  squares of the move one at a time, starting next to the suspect. Each
  is numbered on the floor as it's planned, and the squares it can go on
  to are numbered faintly. Clicking one of the move's squares again goes
  back to it. When the die's number is reached, or a room's floor is
  clicked from before its door, the suspect walks there.
- **No Die Roll**: the first icon shows the moves left; plan the move
  the same way and click its last square again (or the icon) to walk.
  Suggesting, accusing and the secret passages cost three moves.
- **Secret passages**: from a corner room at the start of a turn, click
  the passage in the room (or, on the board from above, the room it
  leads to).
- **The bulb** suggests, after entering a room (or when brought into one
  by someone's suggestion): choose a suspect, then a weapon, then
  Suggest. **The handcuffs** accuse, any time in the turn: a suspect, a
  weapon and a room. On the summary, click a picture to choose again;
  Cancel gives up.
- **Ending the turn**: once moved, the first icon is the suspect's pawn;
  click it.
- **The notes and cards** button (the bar's left) opens the cards in
  hand; the notepad icon under them opens the notepad, whose cards icon
  goes back. Each row has a colour box (click: each suspect's colour in
  turn, then none), two marks (click: tick, X, O, tick in O, X in O,
  none) and a triangle that opens a note to type beside the notepad
  (Enter closes it; the triangle shows when a row has one, and pointing
  at it shows the note). Your own cards start in your colour, ticked
  twice; with "Auto Notes" on, a card shown to you is put in the
  colour of who showed it, crossed twice.
- **The board** button shows the board from above, with the suspects'
  pawns; moves can be planned on it too.
- When a human must disprove, their matching cards are shown: click one,
  then Done. A card shown to a human is shown large, with OK.
- **The end.** A right accusation shows the solution; Continue plays the
  murderer's prison clip, then the main menu. A human who accuses wrongly
  while other humans play on is asked whether to go on disproving by
  hand or automatically (the solution isn't shown). When no human is
  left in the game, the solution is shown ("You have accused
  incorrectly", or "Everyone has accused incorrectly") and the game ends.

The computer players take their turns on their own, the camera following
them. The ticker keeps "..., it is your turn." through a human's turn;
the prompts ("please roll the die") are spoken. A sentence stays centred
8 s, then scrolls left, coming round from the right. The suggestions'
videos play in the right column when "Suggest Movies" is on; the spoken
lines play when "Butler Voice" is on; "Walk Animations" off moves the
suspects at once; "Lighting Effects" darkens the house but around the
suspects.

The music, when the CD's tracks have been ripped into the game's folder
(below): track 5 in the menus, looping; tracks 2, 3 and 4 in turn in the
game. Which plays where is a reading of `Clue.exe` (below), to be checked
against the original.

The game's bar, placed where a 640×480 recording of the original shows
it (its pictures matched in the frames): the top and side frames (0, 0),
(0, 4), (635, 4); the ornaments (0, 413), (594, 413); the notes and cards
button (39, 413), the board button (84, 413); the six portraits from
(141, 413), 60 apart (frame 0 as they are, 1 whose turn it is, 2 not
playing); the logo (501, 413); the ticker (0, 462) with its sentence; the
drawer of a human's actions (511, 413), out over the logo, its three
icons at (535, 420), (574, 420) and (606, 420): the die (`UI:Arolldiebtn`,
then `UI:Arolled` with the number, or `UI:Anoroll` with the moves left,
then the suspect's pawn `UI:A<sca>end` to end the turn), the bulb, the
handcuffs. The left column (5, 5): the cards window and the notepad. The
right column (434, 4): the choice window (dialog 30), the box saying who
can or can't disprove (dialog 31, with that suspect's portrait), a
human's choice of card (dialog 32), and the videos; the card shown, large
(dialog 33, `UI:Algcard`, one frame a card), at (398, 4). The board from
above (`UI:Aoverhead`) at (208, 5), its squares 13 pixels, column 9 and
row 9 at (8, 15) in it. The solution (dialog 34) fills the screen.

Whatever stands in front of a suspect on screen is drawn see-through, as
the original draws the fireplace over Miss Scarlet at the start (L 3:00)
and the walls around Mrs. Peacock (L 3:30); with "Translucency" off, it
isn't.

**Lighting Effects**, measured on the same view with it on and off (VM
0:21 and 0:28, below): each floor tile and each object takes one
brightness (the ratio is flat across each), full within 30 units of a
suspect, falling to 22% over the next 230, the distance taken on screen
(x − y, (x + y) / 2), which fits better than in the world. Reluva lights
around every suspect.

**Timings** from the VM recording: a suspect walks a square in about
0.6 s; each "can / cannot disprove" box shows about 3 s.

### Not yet as the original

- Whether the lamps light their walls too, and whether all suspects or
  only some carry light, isn't settled: the measured view had one
  suspect in it.
- Which marks the original's Auto Notes puts on a card shown is a
  guess (crossed twice, from the 720p recording's notepad); own cards
  ticked twice is as recorded.
- Where suspects stand in a room, and which cards a cards window fills
  first, are Reluva's choice.
- The clickable objects ("please choose an active object") come later
  (step 7).

## The data viewer

**F9** switches between the game and a viewer of its data (and back;
**Esc** also goes back). **Tab** switches between the viewer's pages.

- **The mansion**, first: **arrows** or a mouse drag scroll (**Shift**
  faster), **Home** goes back to the middle, **M** shows the whole house
  (click to go to a place there), **W** makes the walls nearest the camera
  see-through, solid or hidden, **O** shows or hides the black outside,
  **N** the move numbers' templates the level keeps, **F** every object's box,
  **Space** pauses the animations. The info line names what's under the
  mouse pointer, with its layer, place and box.

- **Videos**, playing with their sound: **Left/Right** the previous or
  next one, **Space** or **Enter** plays it again or stops it, **Z** the
  size.
- **Animations**, playing: **Left/Right** the previous or next one,
  **Up/Down** the previous or next file, **Space** pauses, **,/.** step a
  frame, **Z** zooms, **A** shows the anchor point (a character's feet),
  **L** one layer at a time.
- **Images**: **Left/Right** step (**PageUp/PageDown** by 10,
  **Home/End**), **Up/Down** the file, **Z** zooms, **B** changes the
  background.
- **Sounds**: the game's WAV files and the ripped music, if any.
  **Left/Right** step, **Space** or **Enter** plays or stops, **A** plays
  each as it's selected.
- **Text**: **Left/Right** the file, **Up/Down**, **PageUp/PageDown** and
  **Home/End** scroll.

`reluva --detect <folder>` lists what's found in a folder.

## Music

Source: the CD's cue sheet and its tracks, and `Clue.exe` 2.2 read where
it plays them.

The CD (volume label `Clue V2.0.01.27`) has a data track and four audio
tracks, 44.1 kHz stereo, which aren't copied to the hard disk:

| Track | Length |
|---|---|
| 2 | 3:55.00 |
| 3 | 3:11.84 |
| 4 | 2:38.51 |
| 5 | 3:48.13 |

`Clue.exe` plays them through Miles' "redbook" functions. The function
that picks the next one when a track ends (at `0x42F130`) keeps a number
from 2 to 6: 2 goes to 3, 3 to 4, 4 back to 2, 5 plays again, and 6 is
silence. So tracks 2-4 play in turn and 5 loops alone. Where each is
started (`0x404E53` starts 2, `0x411CFF` starts 5) is to be matched with
the screens by watching the game. With no CD audio the original plays no
music and carries on.

Reluva can't read the tracks from the files on the CD, so the player
rips them once, as ScummVM asks for some games, to `track02.wav` ...
`track05.wav` (or `.mp3`) in the game's folder. Tracks that aren't there
are left silent, as the original does without its CD. `reluva-tool
cue-rip` makes them from a BIN/CUE image of the CD.

## What recordings of the original show

Source: two recordings of the original on YouTube, watched frame by frame
(October 2026): a longplay captured at the game's own 640×480
(`MaiM8RGl2_c`, 52 min, one game) and a 720p one (`hwod9LA_9HU`, 56 min,
two games). Times below are theirs ("L" and "C"). Matching the game's
images in their frames also corrected the level's units (see
[formats/clue/level.md](../formats/clue/level.md)).

**Start.** The intro video (L 0:30), then the main menu: Start Game, Load
Game, Preferences, How to Play, Credits, Exit Game, over the mansion on
its hill (L 1:00). Start Game asks "Locally (on this machine)" or
"Networked (with other machines)" (C 26:30). Then the six suspects: "Please
select a human or computer player..."; clicking one asks "Miss Scarlet is
played by... Human / Computer / None" and for a human "Please enter a name
for this player" (C 0:30-1:30). At least three must play. The deal video
follows (C 2:00).

**The view.** The mansion at its own size, the camera on the suspect whose
turn it is, 640 × about 410 above a bar. The walls nearest the camera are
see-through, about half (L 3:00, 5:00). The scene is dark, lit in pools
around the lamps and the suspects (L 3:30, 33:00): the "Dynamic Lighting"
preference. The hall's floor has a large gold "Clue" inlaid (L 12:30).
A move being planned shows its squares numbered 1, 2, 3 ... on the floor
(L 36:30, C 5:30): the `tilenum` objects. Instead of the house, the board
can be shown as a flat map, the rooms drawn from above around the hall,
"To Kitchen" and "To Conservatory" at the secret passages (L 14:00); the
second recording plays most of its game on it (C 33:00 on).

**The bar** (bottom, 640 × about 70): the notepad and the cards on the
left, the six suspects' portraits (the one playing in a lit circle), the
Clue logo; under them a line of text, the ticker's sentences ("Miss
Scarlet, it is your turn.", "Mrs. White suggests Mr. Green with the rope in
the ballroom."). When it's a human's turn, icons for the die and the other
actions show at the bar's right end.

**Panels.** The left column (about 200 wide) holds the cards in hand (two
by three, over the player's portrait; L 8:00) or the notepad (every card
in three lists with each player's marks in their colour, X marks and
ticks; L 13:00, C 15:00). The right column holds the choices: "Please
choose a Weapon..." (L 7:30), "Please choose a Suspect..." (L 44:30), "You
have suggested... [suspect] in the... [room] with the... [weapon]" with
Suggest and Cancel, "Click on an element to change" (L 33:00), and the
suggestion's video, a 200 × 400 clip (L 16:30, the `CM_*.avi` size). A card
shown to disprove is drawn large with OK (L 24:00, 41:30); a human
disproving picks one of their matching cards and clicks Done (L 45:30,
50:00). A
box at the top right says who can or can't disprove, with their portrait
(L 4:00).

**The end.** A right accusation shows its three cards with "Professor Plum
has accused correctly" and Continue (C 45:30), then the player set-up
again. The prison clip (a suspect in a cell) plays near the end of the
second game (C 55:30); what leads to it is still to be seen.

### The VM recording

A recording of the original 2.2 in a virtual machine, made for Reluva
(1280×720, 27:42, October 2026; "VM" with its times): walking (0:40-
1:00), each preference changed with the view before and after (0:20-
3:00), every notepad mark and note (4:10-5:05), a lone human's wrong
accusation and the end (7:30-7:53), a whole game won by a computer
player (8:20-23:48), and a game of humans where one accuses wrongly and
chooses how to go on disproving (24:00-27:40).

## Reference screenshots still wanted

1. The same view with Walls, Translucency and Dynamic Lighting each
   turned off in the preferences, to see what each does.
2. The Preferences and Load Game screens.
3. A view with Lighting Effects on, near a lamp and away from the
   suspects, to see whether lamps light their walls.

## Reference recordings

The original running in a virtual machine is the source for timings,
which music plays where, and the computer players' behaviour.
