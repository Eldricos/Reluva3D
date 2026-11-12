# Harbinger script reference

> **Status:** done · **Code:** `src/engines/stharbinger/game/`, `src/engines/stharbinger/formats/script.cpp` · **Checked against:** the code only · **Confidence:** from the code unless marked

## Overview

The scene and dialogue scripts that run the game, and what DEEP.EXE does as it
runs them: scenes, conversations, videos and action sequences, the game menu,
the credits and deck map, and the volume.

## How it works

Derived from DEEP.EXE (interpreter, loaders) and the linker cross-references
(`CCS.ALL`). The listings from `reluva-tool stharbinger script` use this notation:

| Notation | Meaning |
|---|---|
| `name` | entry in one of the scene's name lists (animation, background, sound, dialogue, timer, hotspot) or a keyword |
| `$name` | variable (`CV` list); each scene maps its variables into the global array |
| `>name` | label; `name:` on its own line defines it |
| `#n` | constant (type 40) |
| `@n` | type-02 value that could not be named |
| `bN` `cN` `x/y/z lo.hi` `dN` | byte, byte, byte pairs (dialogue choices etc.), dword |

### Files

| File | Per scene slot (32, slot 23 empty) | Format |
|---|---|---|
| `CC.ALL`/`.LST` | scene script | directive list |
| `CA` `CB` `CS` `CD` `CT` `CH` `CV` `.ALL`/`.LST` | name lists: animations, backgrounds, sounds, dialogues, timers, hotspots, variables | `name\0 source\0 flag` |
| `CCS.ALL`/`.LST` | linker cross-reference | `symbol\0 command slot` |
| `CCZ.ALL`/`.LST` | number of cross-reference entries | u16 |
| `CCB.ALL`/`.LST` | 72 dialogue scripts | see [../formats/scripts.md](../formats/scripts.md) |
| `CCQ` `CCF` `CCR` | dialogue text, clip names, menu labels | text |

Name lists index in order: `BACKGRND tlback` is entry 0 of the scene's `CB` list.
Labels are numbered in definition order. Resource files:
backgrounds `ART\X\Y\NAME.CMP` (first two letters), animations
`ANIMS\XXXX\NAME.RLE` (first four letters), sounds `SOUND\NAME.RAW`. The
source files were `NAME.CCG` (scenes) and `NAME.CCD` (dialogues).

### Main commands

| Command | Arguments |
|---|---|
| `SCENE` | scene symbol (load-time marker) |
| `NEWSCENE` | scene name: switch scene |
| `BACKGRND` | background, `palette` (load its .PAL) |
| `SOUND` | sound, `ambient` (0: loops until the scene changes, at most 2) \| `asneeded` (1: plays once); no keyword: plays once |
| `ANIMDEF` | animation, `clear` \| `onthefly` \| `flyclear` |
| `ANIM` | animation, keyword (replaces all flags), rate, x, y; restarts at the first frame |
| `ANIMSEQ` | animation, up to 7 keywords, each setting or clearing one flag (see below) |
| `ANIMPOS` | animation, x, y |
| `ANIMRATE` | animation, rate |
| `ANIMSETFRAME` | animation, frame |
| `HOTSPOT` | name, x, y, w, h, handler label, cursor (`zoomin` `touch` `door` `talk` `turn180` `forward` `back` `left` `right` `off` …) |
| `HOTON` / `HOTOFF` | up to 8 hotspots |
| `HOTTYPE` | hotspot, cursor/movement type (`forward` `turn180` `normal` `back` `bearleft` `bearrite` `left` `right`) |
| `SETRECT` | hotspot, x, y, w, h (constants or variables) |
| `CONVERSE` | dialogue, speaker hotspot |
| `TIMER` | timer, ticks, label, `scene` \| `global` |
| `TIMERON` / `TIMEROFF` | timer |
| `TIMERSET` / `TIMERGET` | timer, value / variable |
| `LET` / `ADD` / `SUB` / `MULTIPLY` / `DIVIDE` | value, variable (`var = value`, `var += value`, ...) |
| `RANDOM` | variable, n (`var = rand() % n`) |
| `IFE` `IFNE` `IFGT` `IFGE` `IFLT` `IFLE` | variable, value, label |
| `IF…SUBR` | variable, value, label (GOSUB instead of jump) |
| `JUMP` / `GOSUB` | label |
| `SWITCH` | variable, label0 … label6 |
| `RETURN` / `RETURNVAL` | — / value |
| `ACTIONSEQ` | sequence id (e.g. #41044), result variable |
| `FADEOUT` (A5) / `FADEIN` (A6) | steps of 50 ms (to black / from black) |
| `CDCHANGE` (8D) | disc 0 / 1: asks for that CD if another one is in the drive |
| `DEATH` (79) | sets the dead flag and opens the game menu |
| `MAPON` / `MAPOFF` | — |

Dialogue commands (`GOTO`, `SPEAK`, `ON`/`OFF`, `IFON` …) take byte-pair
arguments that name choices: the high byte of a type-08 pair is the choice
number, and their variable arguments resolve through the scene that hosts
the dialogue (`CD` list).

Full per-slot statistics, with the most common constants and keywords, are in
`summary.txt` under "argument reference".

### Run-time behaviour

From DEEP.EXE's scene interpreter (addresses are linear addresses in the LE
image). This is what `src/engines/stharbinger/game/` implements.

#### Execution

* Entering a scene loads its script afresh and runs it from the top. Only the
  variables carry over; hotspots, timers, animations and labels start empty.
* A script runs until `RETURN` (or `RETURNVAL`, the same outside dialogues).
  With an empty GOSUB stack the script then idles: the player has control, and a
  hotspot click, a timer or an `ANIMEND` label moves it on. With a non-empty
  stack `RETURN` pops (24 entries; a push onto a full stack becomes a jump).
* `NEWSCENE` ends the script at once. The next scene loads once all one-shot
  sounds have finished. Running off the end of a script restarts it from the top.
  An undefined label does the same.
* `LABEL n` marks the directive after it; label numbers are not 0-based per scene.
* Errors are never fatal: the game logs them to `<scene>.gme` and goes on.

#### Values

* One global array of unsigned 16-bit variables, shared by all scenes by name
  (the linker matched each scene's `CV` names, ignoring case, against `DS9.LVM`).
  All start at 0.
* A type-02 argument in a value slot is a variable; any other type is its own
  number. `IF*` comparisons are unsigned.
* `LET a, v`, `ADD a, v`, `SUB a, v`: the value first, the variable second. `SUB`
  leaves the variable unchanged if the result would be negative. `MULTIPLY` and
  `DIVIDE` (unused) take the variable first.
* `RANDOM v, n` uses the Watcom `rand()`: `s = s * 1103515245 + 12345`, result
  `(s >> 16) & 0x7FFF`, modulo n.
* `SWITCH v, L0..L6` jumps to label v if v < 7 and that slot has a label, and
  otherwise falls through.

#### Hotspots

* `HOTSPOT` defines a hotspot **disabled**; `HOTON` enables it. Redefinitions are
  ignored. `SETRECT` may use variables, `HOTSPOT` only constants.
* Hit test: the lowest enabled hotspot index wins; rectangles include their right
  and bottom edges (`x <= px <= x + w`). The point tested is the centre of the
  cursor image (art\mouse.ani, 26 cursors indexed by the cursor id).
* Only left-button presses count, and only while the script idles.

#### Timers

* Script timers tick every 100 ms, only while the script idles or waits in
  `ANIMSUSPEND` (and, for `global` timers, during conversations). They are
  freed on every scene change.
* `TIMER t, n, L, kind` sets the count to n and starts it. When the count
  reaches 0 the script jumps to L **once**; `TIMER`, `TIMERSET` or `TIMERRELOAD`
  (82) re-arm it. Per tick, counting stops at the first timer that fires.
* `TIMERON`/`TIMEROFF` only start or stop counting; `TIMERGET` reads the count.

#### Animations

The renderer runs every 20 ms on a work buffer that is kept from step to step;
`BACKGRND` refills it from the background. Each step it first erases, putting
back the background under the frame last drawn, every animation that is off and
every `clear` (or `flyclear`) animation that is not stopped. Then it draws every
animation that is not off, by priority (0 to 4, 4 on top; within a priority the
lowest index is on top), and advances each one. A frame stays up for rate + 1
steps.

Nothing else is erased, so an `onthefly` animation that moves or changes shape
leaves its old pixels behind, and a `oneframe` animation stays on screen after it
turns itself off. These leftovers are part of the original's look. While a talker's
line plays, the lip-sync loop's redraws also erase stopped `clear` animations, so
a mouth's new shape replaces the old one instead of being drawn over it.
`ANIMDUMP` frees an animation without erasing it, so its last frame also stays
until something draws over it.

Phaser and enemy beams (`ACTIONSEQ` 0xC0..0xC2) are drawn on the screen at once
and share one three-render timer, counted down by every render including
`BACKGRND`'s: the renderer redraws them into the work buffer on the first (an
enemy beam with new jitter), leaves them on the second and wipes their boxes,
3 pixels wider on each side, with the background on the third.

Flags, one bit per keyword pair (keyword k touches bit k / 2; odd k sets it):

| Bit | Clear | Set |
|---|---|---|
| 0 | `loop` (0) | `cycle` (1): turn round at the ends |
| 1 | `forward` (2) | `backward` (3) |
| 2 | `start` (4) | `stop` (5): no frame advance |
| 3 | `nonstop` (6) | `toend` (7): stop on the last frame |
| 4 | `move` (8) | `nomove` (9) |
| 5 | `animon` (10) | `animoff` (11): not drawn |
| 6 | `allframe` (12) | `oneframe` (13): draw one frame, then off |

* `ANIM` replaces all flags with its keyword's bit (so `start` means looping,
  forward and visible), sets the rate and position if given, and restarts.
  `ANIMSEQ` and `ANIMRATE` change single bits without restarting.
* `ANIMDEF` modes: `onthefly` (0) and `flyclear` (1) are loaded on first use
  (priority 3, rate 0); `clear` (2) and animations without `ANIMDEF` are loaded
  with the scene, and `ANIMDEF` gives them rate 10, off, priority 0.
* At the end of a pass an animation releases a waiting script, jumps the script
  to its `ANIMEND` label if set, and then stops (`toend`), turns round (`cycle`)
  or wraps (`loop`).
* `ANIMSUSPEND` is the only command that waits, and only if the animation will
  end by itself: `oneframe`, turned off but still on screen, or `toend` and not
  stopped. Hence the `ANIMSEQ x, toend, animon` / `ANIMSUSPEND x` pattern.
* `ANIMSOFF` turns everything off, unloads on-the-fly animations, and stops screen
  updates until the next `BACKGRND`.
* `ANIMCOMPOSIT a` copies what is on screen inside the rectangle of the
  animation's first frame (the background with the animations as last drawn)
  into the background (0x1429D). `ANIMPRISTINE a` draws the animation's current
  frame into the background (0x1432F).
* `ANIMPOS` sets an offset added to each frame's own position. `ANIMSPD`,
  `ANIMDEST`, `ANIMARRV` and `ANIMFRAM` are stored but never used.

#### Other

* `BACKGRND b, palette` shows the background at once; `palette` (0) also sets its
  `.PAL`. Without it the palette is unchanged.
* `CONVERSE d, h` runs the conversation and blocks; the script then continues.
  Clicking an enabled hotspot other than h ends it and jumps to that hotspot.
* `ACTIONSEQ id, v` runs a special function or action sequence and stores the
  result in v (999 by default; arcade sequences return values such as 32768 or
  65534). ids from 256 up are a function (id >> 8) with a parameter (id & 0xFF):
  80/81 tricorder icon on/off, F0/F5 countdown stop/start.
* `MAPON`/`MAPOFF` allow or block the deck map (M key) and the game menu (right
  click).

### Conversations

From DEEP.EXE's dialogue code (CONVERSE 0x1189F, loop 0x14518, turn 0x169DD);
implemented in `src/engines/stharbinger/game/game_dialogue.cpp`.

#### State

Each dialogue keeps, for the whole game (and in saved games): the current group
and mode (NPC line or menu next), the BACK and RUDE start groups, the "rude" and
EXIT flags, the DONE/EMPTY tables, the hooks, each NPC group's current item, and
every item's *on* (initially 1) and *said* (initially 0) flags. Per conversation:
the last choice picked in each group, the PROTAGONIST lines, TON/TOFF changes and
the talkers.

Argument notation: `x g.i` is item i of choice group g, `y g.i` / `z g.i` item i
of NPC group g (positions within the group); `b` a group number; `d` a line number.
Group numbers count empty groups too.

#### Flow

```
start: cur = 0, NPC turn next; after an earlier conversation cur = BACK group
       (if rude: RUDE group); run list A, list B; clear EXIT
loop until EXIT:
    if DONE[cur] is set and group cur is exhausted: cur = DONE[cur]   (also DONEX)
    NPC turn:  undo TON/TOFF; go to the first non-empty NPC group >= cur (none: end);
               say its current item's line; for each sub-item run its list, then
               say its line; run the item's list; then the menu of the same group
    menu turn: EMPTY/EMPTYX redirects (goto: to an NPC group; reply: to another menu);
               choices = items on and not said, plus hooked groups (a THOOK once);
               none: end; one: taken without a menu; else the player picks;
               the player's line is said, the item is marked said, its list runs,
               then the next NPC group (cur + 1) unless a GOTO/REPLY ran
```

A group is *exhausted* when no choice of it (or of a group hooked to it) is on
and not said; DONEX/EMPTYX ignore one given item. `GOTO`/`REPLY` set the next
group and stop the rest of the turn's lists. `EXIT` ends the conversation after
the current turn.

#### Commands

| Cmd | Name | Effect |
|---|---|---|
| 32 / 33 | `GOTO` / `REPLY` b | next: NPC group / menu of group b |
| 34 / 35 | `HOOK` / `THOOK` g, t | menu t also offers group g (THOOK: once) |
| 36 / 37 | `ON` / `OFF` item | permanent |
| 4B / 4A | `TON` / `TOFF` item | until the next NPC turn |
| 38 | `SET` y g.i | NPC group g says item i next |
| 39 | `EXIT` | end after this turn |
| 3A | `TALKHEAD` dlg, -, name, code, mouth, head | resets the talkers and registers the first (the code is a symbol of the scene's `CCS` list; a mouth or head outside the scene's animations means none) |
| 3B / 40 | `RUDE` / `BACK` b | start group next time after an aborted / normal end |
| 3C | `DONE` g, t | when menu g is exhausted, go to t |
| 3D | `ADDLINE` d | adds a line for `PROTAGONIST` (25 at most) |
| 3E | `MOOD` d | says the line (always line 0, an empty clip: a 250 ms pause) |
| 3F / 4F | `UNSPEAK` / `SPEAK` item | clears / sets *said* (SPEAK says nothing) |
| 41 | `EMPTY` g, t, k | when menu g is exhausted: k = 2 to NPC group t, else to menu t |
| 45 / 46 | `DONEX` / `EMPTYX` g, t, item(, k) | the same, ignoring one item |
| 42 43 44 48 49 4C 4D | `IFON` `IFOFF` `IFSET` `IFREPLY` `IFGOTO` `IFTOFF` `IFTON` $v, #n, … | if v = n |
| 47 | `PROTAGONIST` v | says the v-th `ADDLINE` line |
| 4E | `COUNTCHOICES` g, $v | v = number of available choices of menu g |
| 50 | `ANIMPHONEME` mouth, head | new animations for the first talker |
| 51 | `TALKER` mouth, head, code | adds or updates a talker |
| 7C 92 93 94 | `SAY` -, -, d | says the line |

Scene commands that change state also work in dialogue lists (backgrounds,
animations, sounds, variables, `HOTON`/`HOTOFF`, timers, `MAPON`/`MAPOFF`,
fades); conditionals, jumps and the other scene commands do nothing there.

#### Speech and lip-sync

* Line n of dialogue NAME is entry n of `DIALOGUE/NAME/NAME.ALL`, 8-bit unsigned
  mono at 22050 Hz. Its lip-sync is entry n of `PHONEMES/NAME/NAME.ALL`. The
  game skips a conversation if either archive is missing.
* The clip name (CCF) gives the speaker: `<conversation 3><speaker 3><index 2>`.
  If the speaker is a registered talker, its mouth animation follows the
  phonemes (13-frame mouths use the code as the frame; others the table
  `2 0 2 1 4 3 3 4 0 3 4 3 3`; code 5 rests) and the other animations run three
  times as fast. A talker's line without lip-sync data is not played.
* Every line is followed by a 250 ms pause. Speech cannot be skipped and there
  are no subtitles.

#### The menu

* Each choice shows its CCR label (24 characters at most, 20 entries at most) in
  `art\short.fnt` with a drop shadow, in the palette colours nearest to grey 40
  (63 under the mouse, shadow 0) on the 0..63 scale, skipping entries 1..5.
* It is placed right of the CONVERSE hotspot (left if the dialogue's widest label
  does not fit), one entry per font line, moved up to fit on screen. It stays up
  while the player's line is said.
* Left-click picks an entry. Clicking another enabled hotspot ends the
  conversation rudely and uses that hotspot; choices picked in other groups during
  the conversation become available again. Right-click (with MAPON) opens the game
  menu.
* While the menu waits, only `global` timers run. Their handler ends with
  `RETURNVAL`: `#0` keeps waiting, `x g.i` / `y g.i` continue with menu / NPC
  group g, `#1` ends the conversation, `#2` ends it rudely.

### Videos and action sequences

`ACTIONSEQ` types 0x00..0x1F are videos (DEEP.EXE table 0x24E8F). Plain videos
clear the screen to black (*keep*: play over the current picture), play the
video's first audio track and can be skipped with Esc; the result is 0 (played),
1 (skipped) or 999 (missing file). The last frame and the video's palette stay
until the script's next `BACKGRND b, palette`. TRANS videos stop the scene's
ambient sounds and start them again afterwards.

Arcade types are action sequences (see "Action sequences" below); they cannot
be skipped and return 32768 when won, 0 when the player survives to the end,
0xFFFE/0xFFFD when lost. A loss shows the cockpit's TV static
(`PANEL\<NAME>STAT.RLE`, a frame every 255 ms) while the death video opens,
then the video (the engine shows the static for 765 ms).

| Type | Video | Kind | At |
|---|---|---|---|
| 00 | INTRO\NEWLOGMX | plain | 0, 0 |
| 01 | MOVIES\DS9MIX | plain | 120, 90 |
| 02 03 04 | MOVIES\LAND, SPAC, HOLO | arcade | 120, 12 |
| 05 06 | MOVIES\ADR2, PLAN | arcade | 80, 62 |
| 07 08 09 | TRANS\PREPLAN, CITADEL, SCYWORM | plain | 108, 40 |
| 0A | TRANS\SQUAD | plain, keep | 120, 12 |
| 0B 0C 0D 0E | TRANS\CANWORM, CANBLOW, INSPACE, CANYON | plain | 108, 40 |
| 0F | (DEFDEAD) | rejected: returns 999 | |
| 10 11 | TRANS\ATTXENO, HORNET | plain | 108, 40 |
| 12 | TRANS\SCYSTAT | plain, keep | 80, 60 |
| 13 | TRANS\CITD | plain, keep | 80, 62 |
| 14 | TRANS\DRONRUN | plain | 108, 40 |
| 15 | TRANS\DRONLOG | plain, keep | 80, 60 |
| 16 | TRANS\CANWORMB | plain | 108, 40 |
| 17 | TRANS\EPILOGUE | plain, then the credits | 108, 40 |
| 18 | TRANS\CAN2DIE | plain | 108, 40 |
| 19 | MOVIES\CANN | arcade | 80, 62 |
| 1A | MOVIES\CITA | arcade | 120, 12 |
| 1B | MOVIES\DEFI | arcade | 80, 106 |
| 1C | INTRO\VNM | plain | 96, 72 |
| 1D | INTRO\VCP | plain | 160, 120 |
| 1E | MOVIES\DEF2 | arcade | 80, 106 |
| 1F | MOVIES\CAN2 | arcade | 80, 62 |

The game starts in OPENING, whose script plays the intro (types 1C, 00, 1D, 14,
01) unless it has been seen. The other types (dispatcher 0x25027):

| Type | Effect | Result |
|---|---|---|
| 80 / 81 | tricorder icon over the backgrounds on / off | 999 |
| A0xx | location xx for the deck map | 999 |
| C0, C1 | the player's phaser: a beam from (320, 479) to the cursor's centre, shown for 3 renders; the script decides what it hit (C0 after a hit, C1 after a miss) | 999 |
| C2 | an enemy shot from the SETRECT hotspot (x + w/2, y + h - 5) to (10 + random(620), 460), a widening beam | 65534 (player hit) if the point is in 213..426, else 999 |
| C3 | nothing | 999 |
| F0 | stops the countdown's tick | 999 |
| F1xx..F5xx | stage 1..5 of a countdown at location xx: d = table 0x54F14[xx][stage] + 1 + 2 * (2 - difficulty) seconds; `tick.raw` plays every 64·d ms (everywhere, menus too) | 10·d (for the script's own timer) |
| others | nothing | 999 |

PROMBAR and HABITAT drive the countdown through variables: on each room change
`ACTIONSEQ $Mdist, $Mdist` and `TIMERSET reddead, $Mdist`; the timer running out
is the death. The tick's period and phase are saved with the game.

### Action sequences

From DEEP.EXE's arcade engine (Run_Arcade 0x261C1); implemented in
`src/engines/stharbinger/game/arcade.cpp`. Formats in [../formats/action-sequences.md](../formats/action-sequences.md).

* The video plays in a rectangle of the cockpit picture (400x300 at (120, 12)
  for LAND, SPAC, HOLO, CITA; 480x250 at (80, 62) for ADR2, PLAN, CANN, CAN2 and
  at (80, 106) for DEFI, DEF2). Everything advances once per video frame.
* Targets: sprite frame k is shown on video frame start + k, at its frame's x/y
  in the rectangle; the target is gone when its frames end. Enemies with a gun
  fire every `max(1, period + 1 - difficulty)` frames once their delay has passed;
  shots hit at once (damage, a beam, a horizontal screen shake, colour 12
  flashing).
* The player aims with the gunsight's centre. Left button: phaser (two beams from
  the lower corners; one hit on the target under the sight, the last one in load
  order). Right button: a torpedo volley (4 at most in flight, not in ADR2) that
  hits where the player aimed on its fifth frame: it destroys anything but a
  boss, which takes 2 hits. Hits are tested against the frame's rectangle.
* Groups (id >= 100) share their hits; destroying one member of a group above
  100 removes the rest. Destroying a boss (id >= 200) wins 11 frames later.
* Damage runs 0..100 (death at 100) and recovers one point every 13 frames (10
  on the easiest difficulty). CITA, DEF2 and CAN2 start with a third of the
  previous sequence's final damage; DEF2 continues DEFI's groups.
* PLAN: the door must be destroyed (torpedoes 6, phaser 2 of 20); it then
  explodes and enemy fire stops. Door intact at the end: 0xFFFD.
* Difficulty (0..2, default 1) only changes the fire period and the recovery.

### The game menu

DEEP.EXE's Game_Setup (0x20D54): `art\s\e\setup.cmp` with lit buttons from
`art\buttons.rle`. It opens at start-up (the original skips it on its very first run), on a right click
while MAPON (in a scene, or in a conversation's reply menu: the conversation ends
and Resume runs CONVERSE again) and on DEATH. Buttons: New game, Save, Load,
Quit, Resume, Delete, difficulty (easy, normal, hard), autosave, credits, and
browsing the saves (one at a time, with a picture and its description). Save and
Resume do nothing after death or at start-up. Confirmations: right button yes,
left button no. With autosave on, the game saves before HOLO, ADR2, PLAN, CANN
and DEFI; loading such a save starts the sequence again.

### Credits and deck map

* **Credits** (0x22424, scroller 0x2275D): after the epilogue (ACTIONSEQ 0x17,
  first `art\b\l\blankcrd.cmp` fading out) or from the menu. `credits.cmp` fades
  in (10 steps of 50 ms), then 269 lines from DEEP.EXE (offset table at 0x55464
  in its data object) scroll up 4 px at a time through the window (258, 24)
  380x432, 24 px apart, centred at x = (640 - w) / 2 + 128, in `art\han.fnt`:
  headings (marked `*`) colour 0xFD, others 7, shadow 0xFE. Esc ends them. The
  original scrolls as fast as it draws; the engine takes 4 px per 40 ms.
* **Deck map** (0x24299): M, while MAPON and the script idles, if ACTIONSEQ
  0xA0xx has set the location xx. `maps\ops.dat` (locations below 20, 301x309),
  `lower.dat` (below 65, 393x767), `upper.dat` (below 100, 469x930) and
  `habitat.dat` (below 150, 1101x883) are raw 8-bit images. A 240x320 view
  (habitat 320x240) centred on the location's marker is shown at the bottom
  left, colour 1 in grey, with a grey band at the top and an X in colours 2 and
  3 (which cycle). Esc closes it. The marker positions are DEEP.EXE tables
  0x54BF4 and 0x54D20 (in `src/engines/stharbinger/game/game_extras.cpp`).

### Volume

DEEP.EXE sets a sample's volume (AIL, 0..127) in only two places: arcade music
plays at 64 (0x26784) and conversation speech at 75 (0x2B24A). Everything else
(scene sounds and ambients, effects, voice cues, video soundtracks, the
countdown's tick) plays at the library default of 127. There is no master or
user volume setting.

## Sources and checks

- **Where the facts come from:** DEEP.EXE's code, at the addresses given
  above, and the linker cross-references (`CCS.ALL`).
- **How it was checked:** the unit tests (`stharbinger.unit.game`,
  [TESTING.md](../TESTING.md)); not yet against the original running
  ([UNCONFIRMED.md](../UNCONFIRMED.md)).
