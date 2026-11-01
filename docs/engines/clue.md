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
   and the mansion with the game's bar and the Esc menu. The in-game panels
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
   needn't load. The original's `Prefs.ini` is read the first time
   (below).
8. **Network play** (done, to be tried between real machines). Games
   on the local network and over the internet by address, in the
   original's network screens. Reluva's own protocol, not DirectPlay's:
   Reluva and the original can't play each other (below).

## The computer players

As `Clue.exe` 2.2 plays them, read from its code (November 2026); before
that, Reluva's were its own, fitted to the recordings below. A computer
player (class `AI`) keeps what it knows as the original's notebook does
(0x42CC20 on), cards as bits: what each player is known to hold and to
lack, and for each player a list of "holds one of these three". It
takes in, at every level:

- its own hand: held by it, and nothing else;
- every suggestion made (the three cards, 0x42CFA0);
- who couldn't answer a suggestion: lacks all three (0x418935);
- the card shown to it (0x42CE90);
- nobody answering its own suggestion: what of the three isn't in its
  hand is the answer (0x42D1B0), as Professor Plum's accusation in the
  VM recording shows (below).

and, by level (0x421C10, 0x421C80: a novice never, a normal player half
the time, by `rand() * 100 / 32767 > 50`, an expert always):

- someone answering another's suggestion: holds one of the three
  (0x418D00);
- a wrong accusation: the accuser holds none of the three (0x41B26E),
  which needn't be true.

A "holds one of these" loses what its holder lacks and what others hold;
down to one card, that card is held (0x42CCE0). The answer is found only
by elimination: the one card of a kind not known to be held (0x42D0B0;
code there meant to use cards nobody holds tests constants instead and
never does). With all three found it accuses, at once (0x42CE90,
0x422220): only then, so never wrongly but for the wrong-accusation rule
above.

**Its turn** (0x42C740, 0x422570). In a room it may suggest in, it
suggests. Else it rolls and heads for a room: one neither known to be
held nor the answer, by chance (any room in its first three moves; not
the one it is in, 0x42D900); if it is in that room, the nearest other
(GetClosestRoom, 0x426710: through the passage if the room has one).
It goes the shortest way there, as far as the die goes, through the
passage when that leads there, and suggests on arriving. Without the die
it keeps three moves to suggest with.

**What it names** (0x42D8A0): a suspect and a weapon not known to be
held, or the answer once found; above a novice, not one a pending "holds
one of these" is about, if it can; by chance among them. That can be
itself, when it doesn't hold its own card. With nothing left of a kind,
one of its own (0x42D800).

**What it shows** (0x42D680): by chance, but above a novice a card the
suggester has been shown before if it has one, else one shown to anyone.

**Against the recordings** (Reluva's figures from `reluva-tool
clue-board <clue.lvl> 300 <level> <players>`, games of that level alone):

| Level | Original | Reluva |
|---|---|---|
| Novice (3 players) | no accusation in 14 and 12 rounds (two games); named itself 26% | 41.5 rounds a game, 7% won within 12; names itself 18% |
| Normal (6 players) | no accusation in about 10 rounds; named itself 10% | 20.3 rounds, 18% won within 12; 20% |
| Expert (5 and a human) | won in about 10 rounds; never named itself | 5 players 19.9 rounds, 6 players 15.1, 32% won within 12; 20-21% |

The novices' and normal players' recorded games fit. The recorded
expert game was quicker than most, and its experts never named
themselves where by the code they may (when they lack their own card,
about one suggestion in five here): one game, which the code doesn't
explain. In mixed games of two of each level (`clue-board ... 600`),
experts won 402, normal players 159, novices 39.

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



## Fonts

The fonts' glyphs are drawn by their cells (an EIMG 0x100 glyph's cell
width, a 0x200 glyph's image width), as the menus and dialogs show them:
the Preferences screen's labels measure 103, 111, 91 and 92 pixels in
the longplay at 640×480 (L 1:02), Reluva's 105, 115, 92 and 94. The
ticker's sentence is drawn packed, each glyph's ink a pixel after the
last's and a space its cell and a pixel: "Miss Scarlet, it is your turn."
is 169 pixels in the longplay (L 3:00) and the VM recording, 189 by
cells, 172 packed.

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

**Weather.** Found in the sound of five recordings of the original
(the longplay, "The Classic", Playthroughs 01 and 02, the Steam Deck
one), by cross-correlating each of the game's WAVs with their sound,
whitened (the music under it hides a plain correlation): in the house,
thunder only, `BigThunder5` two times in three and `MidThunder3` the
third; gaps of 12 s at least, median 38-50 s, mean 44-57 s, up to 2-3
minutes (57, 23, 14 and 56 thunders in four of them; the Steam Deck
recording's game sound is too low to tell). The other thunders and the
wind's gusts weren't heard once. Reluva plays thunder 12 s plus an
exponential 38 s on average apart (at most 3 minutes), and blows the
drapes (type 12 objects, their state 23, or 24 one time in three) with
it, silently: when the original blows them isn't known. Over the menus,
`mnThndr1`/`mnThndr2` every 12-30 s, as the recordings' first minutes
have it (10-35 s).

**Pace.** A computer player's turn without a suggestion takes 5-9 s in
the recordings, from one "..., it is your turn." to the next, as it
does with `Clue.exe`'s timings (below). With a suggestion (its video, the boxes saying who can and
can't disprove), 20-22 s; Reluva's, 20-22 s.

**Sounds of things.** The library's fire (`LBfirepl_n.wav`) and the
conservatory's fountain (`fntn_n.wav`) loop while they're near the
view's middle, louder the nearer (full within 150 pixels, none beyond
600).

## Preferences

Reluva keeps its own (`preferences.ini` in its folder for the game).
The first time, with none yet, it takes the original's `Prefs.ini` from
the game's folder: 16 bytes, the seven switches in the Preferences
screen's order (a byte each, 1 on), a byte unused, then the music's and
the master volume as 32-bit numbers from 0 to 10 (`Clue.exe` 2.2 makes
them its mixer's by 127 / 10, at 0x415E63; its preferences' defaults at
0x42FA30: everything on but Auto Notes, both volumes 5). Reluva's
defaults are the same.

## Saved games

From the Esc menu, **Save** (when it's a human's turn and nothing is
going on) opens the original's Save window: eight slots; click one,
type its name, OK (or Enter). **Load Game** on the main menu lists them
(as the original's, from screenshots: each name in the small font,
centred in its slot; an empty slot lights under the pointer but does
nothing);
choosing one goes into the mansion and on from where it was saved,
without the deal.

They are kept in Reluva's folder for the game, `saves/slot1.sav` ...
`slot8.sav`: text, a header line `reluva-clue-save 1`, the name, then
the game: the players, the rules' state (cards, places, the turn, the
die's random state), each computer player's notebook and random state,
each human's notepad, and where the suspects stand. A save is written
beside the old one and then put in its place.

## Network play

Start Game, **Networked**, then:

- **LAN TCP/IP**: **Host New Game** (a game name, your name, how many
  humans at most), or **Join Game**, which lists the games found on the
  local network (Refresh asks again).
- **Internet by address** (the original's "Internet Game Zone" button;
  the Zone closed in 2006): **Host New Game** as above, and tell the
  others your address; **Join Game** asks for the host's address,
  `a.b.c.d`, `a.b.c.d:port` or a name. The host's router must forward
  UDP port 23398 to the host's machine. That window is the Host Game
  window (the original has none of its own), relabelled, without the
  number of players: its row of boxes for them is in its picture
  (`menu:Ihostgmbg`), so Reluva covers it with the same piece of the Exit
  window's picture (`menu:Iexitbgnd`), the plain circle there, as both
  pictures are the same around it.

Then the original's players' picker, with its chat line (type, Enter or
the telephone sends it). Each player clicks a suspect to take it (again
to give it back). The host clicks any other suspect to make it a computer
player at a level, or none, and its **Options** to switch Classic / No
Die Roll; **Start Game** once there are three suspects and every machine
that joined has one. The game then deals the same cards on every
machine.

In the game each machine shows its own player's cards and notepad, and
only its own player's turns take clicks. A player who leaves (or whose
machine stops answering for 15 s) is played on by a Normal computer
player; if the host leaves, the others see "The Host Has Disconnected"
and the game ends. Saving isn't offered in network games.

**Unlike the original:** its readme says the host's "Suggest Movies" and
"Walk Animations" settings apply to every player in a network game. In
Reluva each player's own preferences apply on their own machine: whether
one watches the videos or the walks changes nothing for the others.

**How it works.** UDP, port 23398. Datagrams begin `CLU` and a version.
Finding games: a broadcast "ask", each host answering with its game's
name, its host's name and its places. Joining: "connect" with the
player's name, answered "accept" (with the machine's number) or "refuse"
(full, or begun). After that, messages to and from the host go over a
channel that numbers them, resends them every 250 ms until acknowledged,
and delivers them in order; an acknowledgement goes at least once a
second, and 15 s without one ends the connection. The host keeps the
lobby and sends it whole on each change. The game: the start carries the
random seed; then each player's action (roll, walk with its squares,
passage, suggest, accuse, end the turn, a card shown, a player taken
over) goes to the host, which sends every action, in one order, to
everyone, itself included. Every machine plays the actions out from the
same deal and seed, so the die and the computer players come out the
same everywhere. The host checks each action is from the machine whose
player it is. `src/engines/clue/netplay.h`.

Tested: unit tests over Reluva's in-memory network (finding, joining,
the lobby, chat, starting, the order of actions, a lossy network, a
player gone, the host gone); whole games of three machines in one
process with the real game, each at its own frame rate, on a clean and a
lossy (one datagram in four lost) network, with the die and without, a
machine dropping half way: every machine ends in the same state; and two
copies of Reluva on one machine over its real network, through the
screens, into the same game. Not yet tried between two real machines.

**Against the original.** The original speaks DirectPlay 5/6 (the
`DPLAYX` session, its own messages inside), which nothing but Windows
still has; to play against it, Reluva would have to speak DirectPlay's
protocol and the original's messages, which would need recording
network play of the original. Not planned unless it's wanted.

## Playing

The game opens with its intro (a key or a click skips it), then its
menus, as the original. In the game, the arrow keys move the view
(**Shift**: faster) and **Esc** opens the game's menu: Save, Preferences,
How To Play, Exit To Main (which asks "Are you sure you want to exit the
current game?" first, the original's dialog 44), Continue. The game
doesn't stop for it, as the original's doesn't (its `EscapeDialog` sets
no pause; it waits in a loop that runs the game's own, 0x441590, and
only disables Save in network games, 0x440AD0): in the recordings
computer players play on behind it ("The Classic" 46:48-47:03, a whole
turn; Playthrough 01 4:45-4:50 and 17:25-17:32), and in network games so
does everyone else. The preferences
are saved in Reluva's folder for the game (`preferences.ini`). As the VM
recording shows them, and `Clue.exe` does (below): "Translucency" on
draws what stands in front of the suspect whose turn it is see-through,
off leaves it out (the lounge's near wall gone between 0:57 and 1:09,
the same view); "Mansion Walls" off leaves out every wall in the house
and the lamps on them (its Wall class draws nothing then, at 0x42B5E6;
its furniture of the secondary type 1, the lamps' tag, neither, at
0x42A9AF), floors, furniture and pictures left (2:53 and 3:02).

### In the game

As the original, from its recordings and its How to Play texts:

- **The die** (the drawer's first icon, at the bar's right end, on a
  human's turn): rolls; the icon shows the number. Then click the
  squares of the move one at a time, starting next to the suspect. Each
  is numbered on the floor as it's clicked (the squares it could go on
  to aren't marked, as in the original). Clicking one of the move's squares again goes
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
  at it shows the note). The notepad starts empty: in the recordings
  players colour their own cards in by hand. "Auto Notes" (off at first)
  does one thing, as `Clue.exe` reads it (0x437C9D, after a card is
  shown to a player, calling 0x43A070): the card's row in the colour of
  who showed it, and a tick in its first mark.
- **The board** button shows the board from above, with the suspects'
  pawns; moves can be planned on it too.
- **Suggestions**, as `Clue.exe` runs them: the suspect named is brought
  to the room unless already there, to a place found from where the
  suggester stands (0x41BEA0); the players are asked in turn from the
  next seat on, those without cards passed over, until one can answer
  or it comes back round (0x41873E). A human out of the game after a
  wrong accusation becomes one of its "LostHumanDoDisprove" or
  "LostHumanNoDisprove" players: disproving by hand, or left to the
  computer, as they choose.
- When a human must disprove, their matching cards are shown, as
  `Clue.exe`'s DisproveDlg does it (0x441C60, its clicks at 0x442250):
  one card in the middle on the disprover's background (`...Dis2Bg`),
  with Done; two or three in the window's other places on the other
  background (`...Dis3Bg`), and a click on one shows it at once, no
  Done. The window's "..., Please click on the card" lines it never
  shows. A card shown to a human is shown large, with OK.
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
human's choice of card (dialog 32), and the videos, in their frame
(`UI:Asuggframe`, 208 × 408, black edged in white) at (427, 4) against
the right-hand frame, the clip in its middle; the card shown, large
(dialog 33, `UI:Algcard`, one frame a card), at (398, 4). The board from
above (`UI:Aoverhead`) at (208, 5), its squares 13 pixels, column 9 and
row 9 at (8, 15) in it. The solution (dialog 34) fills the screen.

**What stands in front**, as `Clue.exe` 2.2 does it (read from its code;
the recordings, OriginalClue 0:02-0:36 and L 3:00-3:30, show it). The
house is drawn solid, near walls and all. Each frame the suspect whose
turn it is (its flag at +0xD08, set with the turn by 0x425160) and each
number of a planned move (class Number, 0x429CD0) look at what is near
(0x428220, from slot 0x74 with 13 and 13): every object whose box on the
floor, less 8 units on its far sides, overlaps the 208 units (13 × 16)
beyond the actor's anchor along both x and y (0x428190). "Translucency"
on draws those see-through (their draw flags 0x30001, about half); off,
it leaves them out (flags 0). Only these kinds of object do it, each by
its class (the primary type picks the class, 0x4190B0; slots 0x68 and
0x6C): walls (0), secret passages (8), drapes (12); furniture (3)
unless it has a timing at 2000 (0x42A96C); the hot spots (4, 5) only
when the actor's square is a corridor (0x42A0A6). Floors, suspects,
numbers and the rest never. A suspect in a room does it to the walls
only (the flag 0x428080 gives, by its square's zone, 0x42A210). The
pieces are whole objects: walls come a square long, so the faded part
looks rectangular.

At the start (0:00-0:07) the lounge's armchair to the right of the
fireplace (`Olgosch_cd`) looks solid, though by the code it is in front
of Miss Scarlet. Drawn as the code does see-through (half its colours,
half the lit rug's, below), Reluva's looks the same: both are faded.

The notepad's picture (`UI:A<sca>notepad`) is ruled: each kind's rows 15
pixels apart below its line (22, 131 and 240), the columns of the colour,
the two marks and the triangle 15 wide from 131. The names, colours,
marks and triangles go in its cells.

**Lighting Effects**, as `Clue.exe` 2.2 does it (read from its code and
`ImageRLE16.dll`'s). The lights are the actors that show what stands in
front of them (above): the suspect whose turn it is and the numbers of a
planned move; nothing else gives light (not the wall lamps, nor the
fires). Each frame every object starts unlit, then each light, from the
middle of its box (a suspect's 8 units in, a number's square's middle),
reaches what is anchored in the 128-unit cells within 240 units of it
and one cell further, but for what it shows through; each object keeps
the nearest light that reached it (0x428220, 0x4287C0). The shade at a
point (0x4289C0): d = max(dx, dy) + min(dx, dy) / 2 from the light;
full within 80, then a step darker for each 16 on: 7/8, 13/16, 3/4,
11/16, 5/8, 9/16, 1/2, 7/16, 3/8, 5/16, and 1/4 from 240 on; an object
no light reached is 3/16 (the default 0x110000, set at 0x4176CB). An
object is shaded at the middle of its box; a lit wall a shade for each
16-unit slice (each its own layer, 0x42B5D0); a lit floor of 4 × 4
tiles a shade for each tile (0x42AF70; a lit floor of another size isn't
drawn). The shades are `ImageRLE16.dll`'s pixel functions (from its
`GetBlittersTable`), taking each colour's 5 bits down by shifts and
subtractions (level 6, 7/8: c − c/8 ...); see-through is its function
3, half the picture's colours and half what is behind.

Before this was read, the lighting was measured on recordings (VM 0:21
and 0:28, VM2 0:42-1:12): full near a suspect, falling to about 21%,
and a pool of light following the squares of a planned move as they are
clicked. The code accounts for both: 21% is the far shades' 1/4 and
3/16, and the planned squares are lights of their own. Set beside
OriginalClue at 0:02, Reluva's frame now matches it to within a few
levels of 255 across the view.

**Timings**, as `Clue.exe` has them: a suspect walks 8 units a step,
a step each 66 ms, a square in 0.53 s (0x4253C3; the VM recording, about
0.6 s); each "can / cannot disprove" box shows 3000 ms (0x437503);
between turns the camera goes to the next suspect, 2000 ms, before "...,
it is your turn" (0x4181F8). A computer player waits before each step
until 2 has passed on a clock of whole seconds (1-2 s; 0x423775): before
suggesting at the start of its turn, twice before rolling (its turn's
start, then the roll), before moving; not before suggesting on
arriving. (After 45 s stuck in a step, it is moved on, 0x4218D0.) The
ticker scrolls in a control of the Eagle library (`EagleM.dll`); its
timing is the recordings'.

### Not yet as the original

- Suspects walk straight from a door to their place in a room, where
  `Clue.exe` finds a path over its walk grid (0x41A0C0); the place is
  its (see [formats/clue/board.md](../formats/clue/board.md)).

The cards window fills as the original's: in card order from the bottom
right, right then left, row by row upwards (Playthroughs 01 and 02,
both hands whole).

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

### The VM recordings

A second, "VM2" (1280×720, 16:30, October 2026): a game with Lighting
Effects on, its first minutes walking along the lit walls of the lounge
and the hall.


A recording of the original 2.2 in a virtual machine, made for Reluva
(1280×720, 27:42, October 2026; "VM" with its times): walking (0:40-
1:00), each preference changed with the view before and after (0:20-
3:00), every notepad mark and note (4:10-5:05), a lone human's wrong
accusation and the end (7:30-7:53), a whole game won by a computer
player (8:20-23:48), and a game of humans where one accuses wrongly and
chooses how to go on disproving (24:00-27:40).

## Reference recordings

The original running in a virtual machine is the source for timings,
which music plays where, and the computer players' behaviour.
