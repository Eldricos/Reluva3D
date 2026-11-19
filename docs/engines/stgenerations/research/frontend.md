# sgens.exe front end: startup, menus, campaign flow

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Confidence: **H** read directly from code or data, **M** inferred from code shape and data, **L** a guess.
Addresses are sgens.exe virtual addresses. "cfg" is the options/config object (`*0x565AB0`, built by 0x4870A0).
"app" is the CWinApp at 0x565998. "game manager" (GM) is the States.ini interpreter at `*0x565AA8` (= app+0x110).

New data since the last pass: the user's drive folder now also has **STELLAR.DAT** (it holds
`DESIGN/STATES.INI` and `DESIGN/UNIVERSE.INI`), **COMBAT.DAT**, **MUSIC.1/.2**, and **MOV_01.AVI / MOV_02.AVI**. Those
two .INI files drive the whole campaign, so most of section 4 is read from data, not guessed.

---------------------------------------------------------------------------------------------------

## 1. Top level: the app's idle loop and the "request" global

### 1.1 Two drivers (H)
`CSgensApp::OnIdle` = **0x48AD25** (vtable 0x536D78, slot +0x68).

* **app+0x114 != 0 (always, in the shipped game)**: the game manager runs everything.
  The first idle creates it (`new` 0xA000 bytes, ctor 0x41411E), loads `Design\States.ini` (0x4165A0),
  resets (0x4199C3) and jumps to state `START` (0x41739C). After that, every idle calls GM.tick(delta) = **0x419250**.
* app+0x114 == 0: a developer path. app+0x11C holds a "module id" that a factory (0x48B30E) turns into a
  module (ids 1..15 away mission n, 201..217 movie n-200, 252 bridge, 265 credits, 2101..2125 combat, 40016 =
  switch to the GM, 40019 = intro). Command-line letters (0x48A086, tables 0x549950 / 0x549990 / 0x5499F8) pick one.
  Not needed for the port.

The ctor 0x489C2B sets +0x114 = 1, +0x11C = 40016, so the GM path is the real one.

### 1.2 The request global `0x56FA1C` (H)
Anything (options screen, script commands) asks the GM for a big transition by writing 0x56FA1C; GM.tick
(0x419250) reads it, moves its own state (GM+0xA8) and clears it (0x4192F3):

| request | written by | GM state | what happens |
|---|---|---|---|
| 1 load | options Load panel accept 0x482794 | 4 → 0x419330 | end module (0x418F7A), read GENSAVE slot cfg+0x380 into app+0x128 (0x4881D3), restore (0x419703) |
| 2 new game | options New panel accept 0x484D4E | 5 → 0x41930E | end module, reset universe (0x4199C3 → reload Universe.INI 0x4199F8), `Jump NewGame` |
| 3 quit | GM after credits, or startup failure (0x487326/0x487347) | 3 → 0x4194ED | end module, PostQuitMessage(0) |
| 4 end game | options Quit accept 0x48499B, script `EndGame` (0x416CF8) | 9 → 0x4193A5 | delete module; play **credits** (module 0x46147D, id 265); when it finishes set request 3 → exit |
| 5 options | script `Options` (0x416CD0) | 8 → 0x4194C2 | run the options screen modally (0x48ABA7) |

So **Quit = credits, then exit** (H), and a script `EndGame` does the same.

### 1.3 GM states (GM+0xA8) (H)
1 run the script (0x414570) · 2 end the current module and act on its result (0x418F7A) · 3 quit · 4 load ·
5 new game · 6 create the module asked for (0x418A9F) · 7 tick the module (0x418EA6) · 8 options · 9 credits then quit.

### 1.4 Modules (H)
Every full-screen activity is a module (base ctor 0x4603ED, base vtable 0x535260; +0x2C owner, +0x50 done flag,
+0x14[5] outcome codes, vt0 = kind, vt10 start, vt14 tick, vt18 finish (sets +0x50), vt20 open options, vt34 run a modal
window with briefing music). The GM keeps the current one in `0x565A80` and its type in GM+0x5C:

| GM+0x5C | vt0 kind | class / ctor / vtable | what | started by |
|---|---|---|---|---|
| 1 | 2 | away controller 0x442C9A / 0x532578 (arg = location 1..15, GM+0x60) | away mission | `ActionGame n Won Lost` (0x41786D) |
| 2 | 5 | credits module 0x46147D / 0x5352E8 → credits window 0x42CCA0 | credits | GM state 9 |
| 4 | 6 | intro module 0x4615C5 / 0x535328 → CIntroScreen 0x48D7B0 | title picture | `Intro` (0x417376) |
| 5 | 4 | movie module 0x461322 / 0x5352A8 → CMovie 0x40F880 (id = 200 + n) | movie n | `Movie n` (0x4179C4) |
| 6 | 3 | combat 0x47B375 / 0x5362C0 (arg n) | space combat n | `SpaceCombat n Won Lost` (0x417A18) |
| 7 | 1 | bridge 0x47557D / 0x535F08 (0x598 bytes, Universe.INI, GUI\GUI-Back.bmp) | the strategic map | default, after every other module |
| 8 | 7 | text screen 0x42F369 / 0x531208 → window 0x42E117 / 0x531128 | `TEXT%d.TXT` page | `ScrollText n` (0x4179EE) |

Starting a module (0x419562): end the old one, set GM+0x5C/+0x60, remember the win/lose state names (GM+0x6C, +0x70),
state 6. When a module's +0x50 goes up, GM state 2 (0x418F7A):
* away (1): `0x565A90` = 1 (accomplished) → `Jump <won>`, record `0x56F70C[loc] = 2`; 0 → `Jump <lost>`, record 1 (H).
  Also 0x56F83C = "any outcome code set" (H, use unknown).
* combat (6): 0x565A90 0 → lost, 1 → won (marks fleet), 2 → `Jump EnterpriseDestroyed`, 3 → lost (H).
* everything else: back to state 1, the script carries on with its next line (H).
Then the bridge module is recreated (state 6 with type 7) whenever the script waits (M; 0x4185FA forces type 7 when
there is no module).

### 1.5 Opening the options screen from inside a module (H)
Module base vt20 = **0x460DE7**: pause (vt8), run the options screen modally (`0x48ABA7`), restart the module's clock;
if cfg+0x360 ("leave this module") was set by the menu, finish it (vt18). The away controller wraps it (0x44341A):
mutes the level's sounds (level vt47C(1)) first and unmutes after. The key that calls vt20 (Esc, presumably) was not
traced (M).

---------------------------------------------------------------------------------------------------

## 2. Startup (H unless marked)

1. InitInstance 0x48A086: command line; registry `HKLM\SOFTWARE\MicroProse\Star Trek Generations\1.0` values
   `CDPath`, `Path`, `InstallSet` (0x487390); `InstallSet` "MED" means a medium install and a free-space check (0x487C5C);
   cfg+0x38C = install dir, cfg+0x490 = CD path, cfg+0x79D = OPTIONS dir prefix used in every `%s<name>.bmp`.
2. `0x46261C` mounts `%s\DATA\OPTIONS\OPTIONS.DAT` and checks the CD; if it's missing, shows the insert-CD screen
   (§6).
3. Reads `%s\SGENS.CFG` (0x487DE1; §5.1). DirectDraw 640x480.
4. First idle: GM loads STATES.INI, `Jump START`.

`STATES.INI [Start]` is:
```
[Start]
Intro
Jump NewGame
```
So the game shows **INTRO1.BMP**, then goes straight into a new game: `[NewGame]` plays `Movie 1`, `Movie 18`, sets up
the map and starts the bridge. **There is no title menu at startup** (H). The CAPTAINS.BMP menu appears only when the
player opens it (Esc, module vt20) or a script says `Options` (`[EnterpriseDestroyed]`, `[EndThroughDestroyedSuns]`,
`[EnterpriseWonPicardDead]`: game-over screens that leave the player in the menu with nothing to return to).

### 2.1 Intro screen (CIntroScreen, 0x48D7B0, vtable 0x536F68) (H)
* Picture `%sINTRO1.BMP` (OPTIONS), its palette (0x45F380), display 640x480x8.
* Ends after **5000 ms** (+0x478 = 0x1388; tick 0x48DD74) or when +0x70 is set (input; the handler was not traced, M:
  any key or click). On the way out it can fade the palette (+0x74, vtE0/vtE4 save/animate the palette; M).
* No sound of its own.

---------------------------------------------------------------------------------------------------

## 3. The options screen (CAPTAINS.BMP)

Object 0x26D0 bytes, ctor 0x47D82D, built by **0x47DCBB**, run by 0x48ABA7: a modal PeekMessage loop calling
**0x481221** each pass until it returns non-zero (+0xF34 "close"). Panel state +0xF60 picks the handler:

| +0xF60 | handler | panel |
|---|---|---|
| 0 | 0x4814FA | main (buttons only) |
| 1 | 0x482225 | Load |
| 2 | 0x482841 | Save |
| 3 | 0x482EAC | Settings |
| 4 | 0x48478F | Quit (confirm) |
| 5 | 0x484A8D | New (confirm) |
| 6 | 0x483843 | Controls |

### 3.1 Widgets (H for rectangles; right/bottom are exclusive-ish, the bitmaps are 1 px wider)
All bitmaps come from OPTIONS.DAT. Every button bitmap is a strip of **3 frames counted from the bottom**:
frame 0 = disabled (dim), 1 = normal, 2 = pressed/lit. vtA8(n) picks the frame, vt54(b) shows/hides, vt94() = "was
clicked" (H, from the call pattern; frame order confirmed against the pictures).

Screen coordinates (640x480), on CAPTAINS.BMP:

| widget | member | rect (l,t,r,b) | bitmap | notes |
|---|---|---|---|---|
| Load | +0xA4 | 107,32,226,58 | LOAD.BMP 120x81 | |
| Save | +0xA0 | 107,62,226,88 | SAVE.BMP | disabled when no game is running (+0x158C) |
| New | +0xAC | 107,92,226,118 | NEW.BMP | |
| Quit | +0x94 | 107,122,226,148 | QUIT.BMP | |
| Settings | +0xA8 | 107,152,226,178 | SETTINGS.BMP | |
| Controls | +0xB4 | 107,182,226,208 | CONTROLS.BMP | |
| Return to game | +0xB0 | 378,389,541,421 | RETURN.BMP 164x99 (3 x 33) | |
| Accept/Cancel bar | +0x90 | 378,389,560,422 | (container) | replaces Return while a panel needs confirming |
| Accept | +0x9C | bar + 0,0,78,32 | ACCEPT.BMP 82x66 (2 frames: 1 normal, 0 dim?) | M on frame count |
| Cancel | +0x98 | bar + 81,0,160,32 | CANCEL.BMP | |
| Panel area | +0x68 | 227,29,546,388 (320x360) | OPTNEW/OPTQUIT.BMP | shows the confirm picture |
| Version text | +0x2BC | 500,373,540,388 | Options.fnt, `v%1.2f` of 1.0 → "v1.00" | text id 0x309 |

Sub-panels are 320x360 at **(227,29)**; their children are relative to that corner.

**Load panel** (+0x74, OPTSAVE.BMP, mode 3) / **Save panel** (+0x78, OPTSAVE.BMP, mode 4):
* Title `SAVELOAD.BMP` 310x100 at 3,3,312,27 (+0x6C / +0x70): 4 frames of 25 rows, from the bottom:
  0 "LOAD GAME", 1 "SAVE GAME", 2 "SAVE GAME AT START OF CURRENT MISSION", 3 "... ENCOUNTER" (H for the pictures,
  M for which mode uses which: Save shows 2 in an away mission, 3 in combat, M).
* 9 slot names: x 31..228, y = 34 + 34*i, 24 high (i = 0..8), colour RGB(0x47,0x47,0x73), text ids 100+i. Text
  objects (0x427FA4) on Load; edit fields (0x42A3FB, max 0x30 chars) on Save.
* 10 right-hand texts: x 232..309, same rows (i = 0..9), colour RGB(0x4F,0x4F,0x87), ids 200+i: the slot's
  **stardate** (M on the format, filled by 0x485081).
* Load only: the autosave row button `AUTOSAVE.BMP` at 6,340,230,365 (+0x128, "AUTOMATIC SAVED GAME"), slot 9.
* Selection highlight: `NAMEBAR.BMP` 312x33 (a 1-frame bar; the `Bar` class 0x47D380), moved to the row (0x47D7FD).
* Click a slot: "Empty" (or stardate 48623.4 = never saved) → sound X_1002, nothing selected; else select it, X_1001.
* Accept on Load: cfg+0x380 = slot, request 1 (load), close. Accept on Save: save (0x485574 → 0x488440), close (M on
  the exact path).
* Cheats (H): selecting a slot named **"paradigm shift"** in row 6 sets cfg+0 = 1; **"now thats class!"** in row 4
  sets cfg+4 = 1; both play X_1000. (cfg+0 is checked by Quit, cfg+4 by New; their effect was not traced, L: level
  select / skip.)

**Settings panel** (+0x7C, OPTSET.BMP). Working copy of the options struct at +0xD10 (0x220 bytes, = cfg+0x138), so
+0xF10 = music, +0xF14 = sound, +0xF18 = difficulty, +0xF1C = window size, +0xF20 = subtitles:

| widget | member | rect (panel) | bitmap | effect |
|---|---|---|---|---|
| Effects volume slider | +0xDC | 106,126,317,143 | drawn (0x426C2B; colours 0x734747 / 0x4FD3FF / 0x2F7F97; 30 steps) | +0xF14 → cfg+0x33C, sound dirty cfg+0x378, preview X_ sound |
| Music volume slider | +0xD8 | 106,157,317,174 | drawn | +0xF10 → cfg+0x338, music dirty cfg+0x374, preview M_ sound at music volume |
| Difficulty label | +0xB8 | 3,182,150,210 | DIFFICUL.BMP | |
| Difficulty value | +0xBC | 151,182,233,210 | DIF-OPS.BMP 83x87, 3 frames of 29 | cycles 0→1→2→0, **only when the running module's kind is 1 (bridge)** (+0x1068) |
| Window size label | +0xC0 | 3,212,150,240 | WINDOW.BMP | |
| Window size value | +0xC4 | 151,212,233,240 | WIN-OPS.BMP 2 frames | toggles cfg+0x344, dirty cfg+0x36C |
| Subtitles label | +0xC8 | 3,242,150,270 | SUBTITL.BMP | |
| Subtitles value | +0xCC | 151,242,233,270 | SUB-OPS.BMP 2 frames | toggles cfg+0x348 (0 = on), dirty cfg+0x370 |

Frames from the bottom: DIF-OPS 0 Captain, 1 Lieutenant, 2 Ensign (default 1); WIN-OPS 0 Large, 1 Small (default 1);
SUB-OPS 0 On, 1 Off (default 0 = On, which matches the subtitle code) (M: the order is from the pictures).
Clicking either the label or the value toggles (H). Accept copies the working struct back (0x488770) and writes
SGENS.CFG; Cancel restores it (M).

**Controls panel**: two pages in the same 320x360 area:
* Away page (+0x80, AWAY.BMP) with tab `STARBTTN.BMP` at 170,0,318,31 (+0x8C) to switch, `LKSPRING.BMP` label at
  171,300,284,332 (+0xD0) and `LKSONOFF.BMP` 30x66 value at 286,300,315,332 (+0xD4; frames 0 On, 1 Off).
* Starship page (+0x84, STARSHIP.BMP) with tab `AWAYBTTN.BMP` at 3,0,151,31 (+0x88).
* Key cells (H): action i, column x0 = 84 (i < 10) or 252 (i >= 10), row y = 37 + 30*(i % 10); two cells per action at
  x0 and x0 + 34, each 32 x 26; text ids 500+n, colour RGB(0x4F,0x4F,0x87). Away has **14** actions (28 cells, +0x17C),
  starship **17** (34 cells, +0x21C). Selected cell highlight `KEYHIGH.BMP` 39x33. A click captures the mouse
  (SetCapture) and the next key goes in the cell (0x483843; the details, e.g. clearing duplicates, were not traced, M).
* Away actions, in order (from AWAY.BMP): Attack, Forward, Left, Right, Backward, Slide Left, Slide Right, Jump, Crouch,
  Run, Look Up, Look Down, Look Center, Mouse Look. Starship: Fire Phasers, Fire Photons, Increase Speed, Decrease
  Speed, Turn Left, Turn Right, Roll Left, Roll Right, Climb, Dive, Close, Evade, Maintain Dist., Full Stop, Stand By,
  Warp Out, Maintain Stat. (H)
* Storage: cfg+0x138 (away) and cfg+0x238 (starship), 256 bytes each, initialised to 0xFF (H); most likely indexed by
  virtual key → action (M). Default bindings were not found (L: probably applied when a byte is 0xFF).

**New** / **Quit**: the panel area shows OPTNEW.BMP ("NEW GAME?") / OPTQUIT.BMP ("QUIT GAME?"), the Accept/Cancel bar
replaces Return, the six main buttons go dim. Accept: New → request 2 (+ leave module), Quit → request 4. Both play
X_1003 then M_2001. Cancel: X_1003, M_2000, back to the main panel (H).

### 3.2 Main-panel behaviour (0x4814FA) (H)
Each main button: frame 2 + click sound, then shows its panel (vt54), dims the other buttons, swaps Return for the
Accept/Cancel bar (except Controls, which keeps Return? M), sets +0xF60. Return (+0xB0): X_1003, writes SGENS.CFG
(0x487FF8), M_2002, closes (+0xF34 = 1). If the menu changed anything (cfg+0x368), the running module's vt2C is told
(0x481357).

### 3.3 Sounds (0x485753 → 0x485A4A) (H)
`0x485753(id, wait)` redraws, then plays `%sX_%4d.WAV` for ids 1000..1003 (effects volume) or `%sM_%4d.WAV` for
2000..2002 (music volume when a slider preview asks, else effects), and with wait = 1 blocks until it ends.
* X_1000: menu opened (end of 0x47DCBB), cheat accepted.
* X_1001: button pressed / panel opened / slot picked.
* X_1002: empty slot picked.
* X_1003: Accept or Cancel pressed.
* M_2000: cancelled. M_2001: confirmed (new / quit). M_2002: return to game.
(M_ files are short voice clips; which words they say was not checked.)

No music plays in the options screen itself (H: nothing in 0x47DCBB/0x481221 opens a loop).

### 3.4 Cursors (H)
`OPTCURS.BMP` arrow; `OPTWAIT1..3.BMP` a 3-frame busy cursor while saving or loading. Both are drawn as sprites when
`0x56FA2C` is set (the DirectDraw full-screen path); otherwise Windows cursors 0x8D..0x8F / 0x67 from resources.

---------------------------------------------------------------------------------------------------

## 4. The campaign: STATES.INI and the game manager

The GM is a script interpreter over `DESIGN/STATES.INI` (in STELLAR.DAT; 178 `[State]` sections, 2158 lines).
Each line is one command; `;` starts a comment; `Jump X` goes to section X. Parser 0x4147A0..0x4164F3 (H); each
command is an object whose vt14 runs it.

Commands that leave the bridge (the front end has to provide these):

| command | effect |
|---|---|
| `Intro` | INTRO1.BMP, 5 s |
| `Movie n` | `%s\movie\mov_%02d.avi` (n = 1..18; 4, 11, 12, 17 unused) |
| `ActionGame n Won Lost` | away mission at location n (1..15, as our location table), then `Jump Won`/`Jump Lost` on accomplished |
| `SpaceCombat n Won Lost` | combat n (1..25; 13..24 are the roaming fleets "FightFleetN"), result 2 → `EnterpriseDestroyed` |
| `ScrollText n` | text screen TEXTn.TXT |
| `Options` | open the CAPTAINS menu |
| `EndGame` | credits, then exit |
| `AutoSave` | save to slot 9 as "!AUTOSAVED!" (0x4886E8) |

Everything else (`ShowStellar`, `Select`, `ZoomIn`, `Move`, `WarpFleetIn`, `QueAudio`, `QueMusic`, `Wait`, `SDTimer`,
`SetFlag`, `IfFlag`, `OnTransport` "when the player beams down, go to state X", `Scanner`, `JumpRandom`, `Call`/`Pop`/
`Return`, ...) belongs to the bridge (strategic map) and needs UNIVERSE.INI and STELLAR.DAT's GUI. That is a project of
its own (the bridge module 0x47557D).

### 4.1 The story, as the script runs it (H, from STATES.INI)
`NewGame`: Movie 1, Movie 18, bridge at Amargosa → `ActionGame 1` (Amargosa1). Won/lost both continue (lost plays
Movie 2 and destroys the star). The path branches by `JumpRandom` and timers: Halee (3), Arvada (2), Antilios (6),
Bersus (7), Galorndon Core (9, 15), Epsion (10), Qurash (5), Amargosa 2 (14), Veridian (12), Enterprise (13), with
space combats in between. Endings:
* `[EnterpriseWon]`: if a clue flag, `ScrollText 2` (TEXT2: Picard lost) then `Options`; else `Movie 15`, `EndGame`.
* `[EnterpriseLost]`, `[VeridianLost]`: Movie 14 / 13 → `[EndThroughDestroyedSuns]` → `Options`.
* `[EnterpriseDestroyed]` (combat result 2): `ScrollText 1` (TEXT1: Enterprise lost) → `Options`.
* `[SpaceCombat9Won]`: Movie 16, `EndGame`.

### 4.2 Text screens (`ScrollText n`, window 0x42E549) (H/M)
`TEXTn.TXT` (OPTIONS): line 1 background (`Creditbk.bmp`), line 2 music (`GOver1.wav` / `GOver2.wav`, from the music
directory), line 3 font (`credits.fnt`), the rest is the text (leading spaces centre it by hand). Shown at 640x480x8
with the bitmap's palette; the music plays at music volume and its end callback (0x42F31F) closes the screen (M).
The text is drawn in a box 0,0..640,480 (the scrolling rate was not traced, M: it scrolls up while the music plays).

### 4.3 Credits (module 0x46147D → 0x42CCA0) (H for data, M for timing)
`creditbk.bmp` + `credits.fnt` (OPTIONS). The text is **inside the exe**: a table at 0x5401CC of records
`{char *heading, char *name, int style}` (0 = blank line, 1 = more names follow, 2 = last name), from "Producer /
SIMON FFINCH" to the copyright lines. Music `Credits.WAV`, then `MUSI9022`, `MUSI5003`, `MUSI9024`, `MUSI9020`
(0x540198), changing at the line counts stored just before the table (0x834, 0x898, 0x384, 0x8FC; M).

### 4.4 Mission briefing (CMissionBriefing, 0x46176F, vtable 0x535368) (H)
Before each away mission (0x442EB1) and combat (0x47B4D6) the module runs vt34 = **0x460E3B** with a briefing window:
`<location>/briefing.bmp` (and `briefing.txt` if present: only HALEE1 has one) with a **PROCEED.BMP** button and
PROCEED.WAV (COMMON.DAT). While it is up, music `%s<A|C>LOOP<1-4>.WAV` loops (A for away = kind 2, C for combat = 3,
random 1..4, music volume) and on Proceed `%s<A|C>TAG<1-2>.WAV` plays to the end. (This corrects music-speech.md, which
called this the in-game menu.)

---------------------------------------------------------------------------------------------------

## 5. Files

### 5.1 SGENS.CFG (install dir) (H)
```
u32 8                 ; size of the version header
u32 0x2B, f32 1.0     ; version header (43, 1.0)
u32 0x220             ; size of the options struct
byte[0x220]           ; cfg+0x138..+0x357
```
Options struct (offsets from its start, = cfg − 0x138):
| off | cfg | what | default |
|---|---|---|---|
| 0x000 | 0x138 | away key map [256] | 0xFF |
| 0x100 | 0x238 | starship key map [256] | 0xFF |
| 0x200 | 0x338 | music volume 0..29 | 20 |
| 0x204 | 0x33C | effects volume 0..29 | 20 |
| 0x208 | 0x340 | difficulty 0..2 | 1 |
| 0x20C | 0x344 | window size (1 = small?) | 1 |
| 0x210 | 0x348 | subtitles (0 = on) | 0 |
| 0x214 | 0x34C | look spring? (M) | 0 |
| 0x218 | 0x350 | double, gamma? (L) | 1.0 |

### 5.2 Saved games: `<install>\SAVEGAME\GENSAVE.DA<n>`, n = 0..9 (9 = autosave) (H)
```
u32 0x108
SAVE_HEADER (0x108): char name[0x104]; f32 stardate (app+0x1C0 as float)
u32 8; u32 0x2B; f32 1.0                ; version
u32 0x9F48; byte[0x9F48]                ; GAME_STATUS = app+0x128.. (GM state, flags, ...)
game-manager data (0x414706)            ; the GM's own save
```
Writer 0x488440, reader 0x4881D3 (seeks past the header to 0x10C), slot list 0x484E47 (missing → "Empty", stardate
48623.4). The away mission's own state is not in this file: a save restores the GM, which re-enters the state it was
in (M). Our engine's saves stay our own format (they can't be compatible without the bridge).

---------------------------------------------------------------------------------------------------

## 6. Insert-CD screen (0x46261C) (H)
Shown if OPTIONS.DAT or the CD files are missing. `insrtcd1.bmp` / `insrtcd2.bmp` (disc 1 / 2) full screen,
Accept 337,281,416,313, Cancel 419,281,498,313 (ACCEPT/CANCEL.BMP), `cd-light.bmp` 16x54 (3 frames) at
167,197,182,223 blinking while it checks. For the port: not needed (all files are read from the configured paths);
at most show it when a movie is missing (L).

## 7. Movies (H)
`%s\movie\mov_%02d.avi`, %s = CD path (cfg+0x490), n = module id − 200 (0x40F90C). The original switches to
640x480x16 and plays through its own AVI/VfW code (0x4C969A open, 0x4CA0B8 "still playing", 0x40FACD stop); if
DirectDraw isn't up it marks the module done at once. MOV_01/02 (checked): RIFF AVI, **Cinepak 640x320 24-bit, 24 fps
(41666 µs), PCM 22050 Hz stereo 16-bit**. The engine already decodes these (engines/stg/movie.{h,cpp}); draw it at
y = 80 (centred in 480). Skipping: our current key/click skip is fine (the original's handler was not traced, M).

---------------------------------------------------------------------------------------------------

## 8. Implementation plan for engines/stg

The original has no title menu, but a port without the bridge needs a home screen anyway. The plan keeps the
original's pieces and order, and puts a thin "campaign runner" in place of the bridge until the bridge exists.

### 8.1 New files
* **`frontend.{h,cpp}` — `class OptionsScreen`**: the CAPTAINS.BMP menu, one instance, modal.
  - Loading: like HudScreen::load — find `<NAME>.BMP` in OPTIONS.DAT through ResourceManager, decode with
    Image::BitmapDecoder, keep the palette of CAPTAINS.BMP (all OPTIONS bitmaps share it; check, M), frames from the
    bottom, transparency key = first pixel where a bitmap needs it (OPTCURS/OPTWAIT only).
  - `struct Button { Common::Rect rect; const Image *image; int frames; int frame; bool visible; }` with the table of
    §3.1 as data (rect, file, frame count, parent panel offset).
  - Panels enum = the +0xF60 values; `update()` mirrors 0x481221 (one handler per panel); mouse down sets frame 2,
    mouse up inside = click.
  - Text: Font (OPTIONS.FNT) for the version string, slot names, stardates and key names; colours from §3.1.
  - Sliders: draw 30 ticks in the given colours (VOL-ON/VOL-OFF.BMP are 5x17 and fit 30 steps of 7 px in the 211-px
    track; use them if the drawn version looks wrong).
  - Sounds: `playUi(int id, bool wait)` → X_/M_ WAV from OPTIONS.DAT via Audio::Mixer (Wave decoder), effects volume.
  - Result: `enum Result { kReturn, kNewGame, kLoad(slot), kQuit }` handed to the engine.
* **`options.{h,cpp}` — `struct GameOptions`** = the 0x220-byte struct (§5.1) with sync() through
  Common::Serializer, read/written as `SGENS.CFG`-compatible bytes in the save directory (or ConfMan keys; the binary
  file lets a user's real SGENS.CFG be read, M). Hook music/effects volume to the mixer's music/SFX types,
  subtitles to `_subtitle`, window size to the 3D view size, lookspring to the walker.
* **`intro.{h,cpp}`** (small): INTRO1.BMP for 5 s or until input; TEXTn.TXT screens (§4.2: background, music file
  from MUSIC.1/.2 or the music dir, Credits.fnt, text scrolling until the music ends); credits (§4.3, text table
  copied as data into the source, it is plain credits text).
* **`campaign.{h,cpp}` — `class Campaign`**: a minimal STATES.INI interpreter. Parse sections and lines; implement
  `Intro`, `Movie`, `ActionGame`, `ScrollText`, `Options`, `EndGame`, `AutoSave`, `Jump`, `JumpRandom`, `Call`/`Pop`/
  `Return`, `SetFlag`/`IfFlag`/`IfNotFlag`, `SetActionGamePlayed`/`IfActionGamePlayed/Won/Lost`, `IfClue` (= a flag
  the away missions set; L which one), and treat the bridge-only commands as no-ops (log them once).
  `SpaceCombat` is not implemented: take its Won branch (or offer a choice) until combat exists. `OnTransport X`:
  jump to X at once (there is no bridge to beam down from). This gives the real mission order, movies and endings
  without the strategic map.

### 8.2 Engine flow (stg.cpp)
1. `run()`: if OPTIONS.DAT and STATES.INI are present → `Campaign::start("Start")`: Intro, then NewGame. Keep the
   current starfield/mission list as the fallback (no STATES.INI) and as a debug screen (a key).
   Better for players: show the CAPTAINS menu first with Return disabled (the original's `Options` state with no game),
   since there is no bridge to land on; New starts `NewGame`, Load lists the slots. (A deliberate change; say so in
   the README.)
2. Esc in an away mission → `OptionsScreen` modal over a frozen frame (mute level sounds like 0x44341A). Results:
   Return → resume; New → leave mission, `Campaign::jump("NewGame")`; Load → load slot; Quit → credits → quit.
3. `ActionGame n`: `playMission(location n)`; on accomplished → Won state, else Lost (we already have the accomplished
   flag, mission.getOutcomes()). Before it: the briefing screen (§4.4) with ALOOP/ATAG music and the Proceed button
   (we already show BRIEFING.BMP as a transition).
4. `Movie n`: existing `playMovie("MOVIE/MOV_%02d.AVI")`, centred vertically.
5. Saves: keep our format; add a campaign block (current state name, line, flags, played/won tables) before the
   mission block, bump the version. Slot names and stardate for the Load/Save panels; slot 9 = autosave written by
   `AutoSave`.

### 8.3 Order of work
1. GameOptions + SGENS.CFG read/write, hook volumes and subtitles. (Small, testable.)
2. OptionsScreen: main panel + Return/Quit/New with confirm, sounds. Then Settings, Load/Save, Controls.
3. Campaign interpreter with the commands above + intro + text screens + credits.
4. Briefing screen with PROCEED and A/C loop music.
5. Tests: parse STATES.INI-like text (a made-up sample, not the game's file), button hit-testing, SGENS.CFG round trip,
   frame-from-bottom selection.

### 8.4 Open questions (all L/M)
* The default key bindings and the exact key-map layout (index by VK or by action).
* What M_2000..2002 say; whether the ACCEPT/CANCEL strips have 2 or 3 frames (66 rows = 2 x 33).
* Which flag `IfClue` tests; the credit roll speed; the intro's fade.
* The meaning of cfg+0/+4 cheats.
