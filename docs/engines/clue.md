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
3. **Video.** An AVI reader and a Cinepak decoder, with the sound
   through the mixer. Play the intro, the deal and the suggestion clips.
4. **The interface.** The `RESI` layouts: windows, buttons, lists, text
   boxes, the bitmap fonts and cursors. The main menu, game setup,
   preferences, How to Play, credits, the notepad, the cards and the
   ticker.
5. **The game.** The board: squares, doors, secret passages, from the
   level. Turns, the die or credits, moving (with walk cycles),
   suggestions (moving the suspect and weapon into the room), disproving,
   accusations and the end. Hot-seat play with hidden cards. The spoken
   lines and the ticker's sentences.
6. **Computer players.** They note what they learn and deduce from it as
   the originals do; their choices compared with the original's over
   recorded games.
7. **The rest.** The clickable objects' animations, thunder and other
   ambient sounds, music (see below), saved games and `Prefs.ini`. Saved
   games are Reluva's own: the original's needn't load.
8. **Network play** (later). The original's local-network games, and
   games over the internet by address in place of the Gaming Zone, which
   is gone. Whether to speak DirectPlay's protocol, so as to play against
   the original, is to be decided then.

## The data viewer

Until the game runs, the engine shows its data. **Tab** switches between
the pages; **Esc** goes back to the launcher.

- **The mansion**, first: **arrows** or a mouse drag scroll (**Shift**
  faster), **Home** goes back to the middle, **M** shows the whole house
  (click to go to a place there), **W** makes the walls nearest the camera
  see-through, solid or hidden, **O** shows or hides the black outside,
  **N** the move numbers' templates the level keeps, **F** every object's box,
  **Space** pauses the animations. The info line names what's under the
  mouse pointer, with its layer, place and box.

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

## Reference screenshots still wanted

1. The same view with Walls, Translucency and Dynamic Lighting each
   turned off in the preferences, to see what each does.
2. The Preferences and Load Game screens.
3. A wrong accusation, and the end of a game won by a computer player.

## Reference recordings

The original running in a virtual machine is the source for timings,
which music plays where, and the computer players' behaviour.
