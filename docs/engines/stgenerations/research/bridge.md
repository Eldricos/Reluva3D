# sgens.exe: the bridge (strategic star map)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Confidence marks: **H** read from code or data, **M** inferred from code shape and data, **L** a guess.
All addresses are sgens.exe virtual addresses (image base 0x400000). "GM" is the game manager (the STATES.INI
interpreter, `*0x565AA8`); "bridge" is the bridge module (ctor 0x47557D, vtable 0x535F08, 0x598 bytes).
"status" is the saved GAME_STATUS block app+0x128 = **0x565AC0 .. 0x56FA08** (0x9F48 bytes).
Read `re/frontend.md` first for the module system and the GM states.

Verification pictures (working files, not kept):
`scene.png` (bridge screen: BACKGRND + EQUIP + Data + Picard + GUI-BACK), `sector_mock.png` (suns placed with the
projection of §5.2 and labelled from SYSNAMES), `fleet_frames2.png` (fleet RLEs decoded), `nexus_all_flipped.png` and
`nexus_check1.png` (Nexus RLEs over the map, with the projected suns), `sheet1.png` / `sheet2.png` (GUI buttons, suns,
planets, cursors). Helper scripts: `br/t/fd.py`, `br/t/fdc.py` (function dumpers with string/float annotation),
`br/t/xr.py` (xrefs), `br/t/rle.py` (RLE decoder).

---------------------------------------------------------------------------------------------------

## 0. The short version

* The bridge is a separate full-screen module. While it is the current module, the GM runs **one script
  command per frame** (GM state 7 → module tick 0x418EA6 → `0x414570(0)`) (H). Any bridge command executed
  when no bridge exists creates it first (`0x4185FA`, GM+0x5C = 7) (H). `ShowStellar` does only that (H).
* **Time only moves when the player acts.** The "stardate" is a double at 0x565B58. Nothing advances it on its
  own. Warping adds 1.0, simulating a star's destruction adds 0.3, each scan adds 0.1 and each zoom adds 0.1 (H).
  The added amount goes into a *target* stardate (0x565B60). The displayed stardate climbs towards the target by
  0.1 every 500 ms, with a beep for each step (H). `SDTimer d` sets a deadline of displayed stardate + d. A
  `Wait Timer` is released when the displayed stardate reaches the deadline **and** the Enterprise is idle at a star (H).
  So "SDTimer 2.0" means "two warps, or 20 scans, or a mix" (H).
* The player selects objects on the map (left click). The bottom panel has:
  * **SET COURSE**: warp to the selected star.
  * **FULL SECTOR VIEW / SYSTEM VIEW**: zoom out or in.
  * **LONG / SHORT RANGE SCAN**: short-range only when the Enterprise is in the selected object's system.
  * **SIM. DESTRUCTION / CANCEL SIMULATION**: predicts the Nexus path.
  * **TRANSPORT / TACTICAL / COMPUTER LOG**: a context button.
  * **OPTIONS**.
  * Arrows that scroll the text.
* `OnTransport S` / `OnTactical S` only *arm* the context button. It shows TRANSPORT (or TACTICAL) when three things
  hold: the selected object has Soran (object flag 1), it has had a **short-range scan**, and the Enterprise is in its
  system. Pressing the button jumps the script to S (H).
* **Red-alert** fleets attack the Enterprise as soon as it arrives in their system, which starts a combat (H).
  The TACTICAL button on any other fleet starts its combat by hand (H).
* `IfClue` is true when the last away mission set **any** SET_MISSION_OUTCOME code (native 0x97). This answers the
  open question in frontend.md (H).
* `ClueWave X` sets the clip that the COMPUTER LOG button replays (H).
* `.RLE` files use the already known **OVERLAY** format (`engines/stg/overlay.h`), including the "frames top-down,
  rows inside a frame bottom-up" rule. Verified on all fleet and Nexus files (H). The palettes come from
  `FLEET\ENTSIN.BMP` (fleets) and `NEXUS\NEXUS.BMP` (Nexus) (H).

---------------------------------------------------------------------------------------------------

## 1. Objects and memory map

### 1.1 Bridge module (0x598 bytes, ctor 0x47557D, start vt10 = 0x475C23, tick vt14 = 0x476E9E) (H)
| off | what |
|---|---|
| +0x30 | main window (0x5C bytes, vtable 0x535F48; +0x58 = "busy" flag set while warping, 0x475BC0) |
| +0x60 | controls enabled (busy cursor when 0) |
| +0x70 | audio queue (0x478EB5; §8) |
| +0x74 | handle of a looping music (Stellar.wav at start, 0x5470F8) |
| +0x78/+0x7C | idle-music timer / toggle (§8.3) |
| +0x84 | clue wave name (CString; `ClueWave`) |
| +0x88 | GM pointer |
| +0x8C | GUI-Back.bmp palette |
| +0x494 | star map view (0x558 bytes, ctor 0x46A690, vtable 0x535B28, rect 0,0..639,361) |
| +0x498 | GUI panel (0xB04 bytes, ctor 0x472BC9, vtable 0x535DA0? (M), rect 0,362..639,479) |
| +0x4A0 / +0x4A4[10] | sun count / suns (index 0..9 = Universe Sun1..Sun10) |
| +0x4D4 / +0x4D8[30] | fleet count / fleets; [0] = Enterprise (= +0x550), [1..25] = Fleet1..25 |
| +0x550 | Enterprise (0x140 bytes, ctor 0x469C2B, vtable 0x535AA0) |
| +0x554 | space station (0x110, ctor 0x46972F, vtable 0x535A30) |
| +0x558 | Nexus (0x5C8, ctor 0x468943, vtable 0x5359C0) |
| +0x564 | Fonts\Stellar.fnt |

### 1.2 Map object classes (H; vt38 returns the type)
| vtable | type | class | load (vt54) | tick (vt68) |
|---|---|---|---|---|
| 0x535708 | 0 | base object | 0x4644ED | 0x464AF2 |
| 0x535778 | – | orbiting object (base of planet/moon) | 0x464DF5 | 0x464F54 |
| 0x5357F0 | 1 | sun | 0x465A03 | 0x466A18 |
| 0x535860 | 2 | planet | 0x466CD7 | 0x466F4A |
| 0x5358D8 | 3 | moon | 0x467238 | 0x467260 |
| 0x535950 | 4 | fleet | 0x4679DE | 0x468406 |
| 0x5359C0 | 6 | Nexus | 0x468CC9 | 0x46950C |
| 0x535A30 | 5 | space station | 0x469847 | 0x469A50 |
| 0x535AA0 | 7 | Enterprise (fleet subclass) | 0x469CA0 | 0x469D86 |

Common virtuals: vt1C(flag) reads an object flag; vt20(flag, value) writes it; vt24 = the sun the object belongs to;
vt30 = scan graphic; vt34(short) = scan text; vt40 = has energy (flag 3); vt48 = has Soran (flag 1);
vt4C = has trilithium (flag 2); vt58 = scan finished; vt5C / vt60 = draw; vt64(b) = set flag 1 (Soran); vt68(dt) = tick;
vt6C(sun, sound) = warp in (fleets). Object +0xA4 = INI section name, used as the object's name in scripts (H).

### 1.3 Object flags (stored in the status block, so they are saved) (H for storage, M for meanings unless marked)
Storage:
* sun i (0..9): `0x565BE0 + i*0x960 + f*4`.
* planet p of sun i: `0x565BE0 + i*0x960 + p*0x78 + f*4`. Each sun has 20 records of 0x78 bytes; record 0 is the sun.
* fleet n (1..25): `0x56E8F8 + n*0x78 + f*4`.
The INI keys HasLife / HasEnergy / HasTrilithium are copied into the flags only while `0x56F834 < 1`, that is on the
first load of a new game (0x4644ED) (H).

| flag | meaning | evidence |
|---|---|---|
| 1 | Soran is here | `Move Soran` sets/clears it (0x41760D, vt64) (H); comments "Remove Soran" |
| 2 | trilithium | HasTrilithium (H), "Has Trilithium" comments |
| 3 | energy readings | HasEnergy (H) |
| 4 | life signs | HasLife (H) |
| 5 | scanned / name known | set by every scan, warp and simulation (0x464944, 0x4669BF) (H); script: "Has been scanned, show title" |
| 6 | cloaked / hidden fleet | `HideFleet` sets it, `Move Fleet` clears it; INI `Cloaked` (H for set/clear, M for "cloaked") |
| 7 | destruction being simulated (suns) | 0x4659E0 (H) |
| 8 | sun destroyed; for fleets: beaten in combat | 0x4659BD (H); fleets: combat won sets it, and `WarpFleetIn` then skips the fleet (0x41914B, 0x413B65) (H) |
| 14 | warping (Enterprise and fleets) | 0x469CC8 sets, arrival clears; `Wait Warp` tests it (H) |

### 1.4 Other status fields used by the bridge (H)
| address | what |
|---|---|
| 0x565ACC | 0x113 after a reset (version/marker) |
| 0x565AD0 | current state name (char[]) |
| 0x565B58 | displayed stardate (double); new game = **48623.4** (0x4199F8) |
| 0x565B60 | target stardate (double) |
| 0x565B68[n] | script flags `SetFlag/IfFlag n` (int; also written by the bridge, 0x478399) |
| 0x56E880[0] | Enterprise location: sun index 0..9, or -1 |
| 0x56E880[n] | fleet n location (1..25); -1 = nowhere. All 26 are -1 after a reset |
| 0x56F708 | context mode set by OnTransport (1) / OnTactical (2) |
| 0x56F70C[n] | away mission n result: 2 won, 1 lost |
| 0x56F7D4 | suns destroyed (counter for `IfSunsDestroyed`) |
| 0x56F7D8 / DC / E0 | Nexus state / current graphic / next graphic (§6.7) |
| 0x56F7E4 | name of the selected object (restored on bridge start; cleared after a combat) |
| 0x56F828 | long-range scan counter (5th → hint Data0114) |
| 0x56F82C | "Nexus path changed" speech already played |
| 0x56F834 | INI flags already applied (§1.3) |
| 0x56F838 | sim enabled (EnableSim) |
| 0x56F83C | clue flag (copy of GM+0x80) |
| 0x56F840 | context mode (copy) |
| 0x56F8C4 | Soran's location (object name) |
| 0x56F904 | clue wave name; new game = `Sound\Voices\Comp0210.wav` ("Short and long range scans may determine Soran's location") |

GM fields: +0x08 controls enabled, +0x10 sim enabled, +0x44 ms clock, +0x48 SecTimer deadline, +0x50 SDTimer deadline
(double), +0x58 command run when a timer fires, +0x80 clue flag, +0x84 current state, +0x8C call stack,
+0xAC kind of the current Wait, +0xB0 OnTactical state name, +0xB4 OnTransport state name, +0xB8 status snapshot taken
at each Jump (H).

---------------------------------------------------------------------------------------------------

## 2. STATES.INI commands

### 2.1 How commands run (H)
* Parser 0x4147A0: compares the first word against each keyword (strcmp, **case-insensitive**: `movie`, `jump` and
  `SdTimer` also work in the file), creates the command object, then calls vt1C(gm, rest-of-line) to parse the
  arguments. vt14(gm, skip) runs the command, vt18 returns its type id, and vt20 resolves the state names it refers to.
  The command table, with vtables and execute functions, is in `br/t/parser.txt`.
* A command's run normally does its work when `skip == 0`, then advances the state's line pointer (0x413F0D).
  The GM always passes `skip = 0` (H). The non-zero path was not seen used (M).
* **Conditions** (`If...`) hold the rest of the line as a chained command. If the condition is true they run it,
  otherwise they skip the line. Conditions nest: `IfNotFlag 11 IfNotObjectFlag Arvada 5 QueAudio ...` (H).
* **One command per frame** while the bridge is up (§0). With no module (GM state 1) the script runs until something
  starts a module (frontend.md).

### 2.2 Bridge commands
Names in the Args column are INI section names: `Amargosa`..`Veridian`, `Amargosa3`, `SpaceStation`, `Fleet3`, ...
Sun arguments use the name table 0x53F648 ("Unknown", Amargosa, Antilios, Arvada, Bersus, Epsion, GalorndonCore,
Guernica, Halee, Tarendia, Veridian), looked up with 0x416A12. A sun index is 1-based in the table and 0-based in
storage.

| command | args | GM method / bridge method | effect | conf |
|---|---|---|---|---|
| `ShowStellar` | – | 0x4183C3 | make sure the bridge module exists (create it if another module or none is running) | H |
| `Select` | object | 0x417FB7 → map 0x46EF11 | set the map's selection (+0x4D8), refresh the buttons | H |
| `ZoomIn` | sun | 0x418961 → 0x4787D8 → map 0x46F37F | if not already in system view: 2 s camera move into the sun's system, sound GUI+0xAFC | H |
| `ZoomOut` | [sun] | 0x418A24 → 0x478873 → 0x46F65B | if not in sector view and the current system ≠ arg: 2 s move back to the sector; sound GUI+0xB00. With no argument the lookup gives "Unknown" → null, which always zooms out | H |
| `Move Enterprise X` | sun | 0x417546 | status loc = X; with a bridge, place the Enterprise at X at once (0x47836F); clear flag 6 | H |
| `Move Soran X` | object | 0x41760D | old Soran object vt64(0), status Soran name = X, new object vt64(1) (flag 1) | H |
| `Move Fleet n X` | n, sun | 0x4176BC | fleet loc = X; with a bridge, place it at once (0x4783CD); clear flag 6 | H |
| `WarpFleetIn n X wav` | n, sun, wav | 0x413B65 → 0x4188BE | skipped if fleet flag 8 (beaten). Otherwise, if the fleet is not already at X: queue wav (0x477E5E), fleet vt6C(X, queued) → warp-in animation **after the audio queue empties** (§6.3) | H |
| `WarpFleetAway n wav` | n, wav | 0x41880F | if the fleet is not cloaked/hidden (flag 6 clear): queue wav, warp-out animation after the audio (0x468857). Without a bridge: loc = -1, flag 6 = 1 | H |
| `HideFleet n` | n | 0x416EB7 | bridge 0x477826 (remove from the map); loc = -1; flag 6 = 1 | H |
| `WarpAway S` | sun | 0x41865D → 0x4785CC | only if the Enterprise is at S: pick a random sun ≠ S that is not destroyed and has no Red-alert fleet, play its GotoSound, warp there (as Set Course, §6.2), controls off until arrival. Without a bridge it only changes the location | H |
| `Destroy S` | sun | 0x416C2A | suns-destroyed counter +1, create the bridge, sun 0x4666D6(1): destruct animation then the dead sun; Nexus update (§6.7) | H (M for the animation details) |
| `SetFleetFlag n f v` | ints, TRUE/FALSE | 0x418096 | fleet flag f = v (status only) | H |
| `SetObjectFlag X f v` | object, f, v | 0x4180C7 | object vt20(f, v) | H |
| `IfObjectFlag X f` / `IfNotObjectFlag X f` | | 0x41712E / 0x4171B9 | test object vt1C(f); a name starting `FLEET` reads the fleet record (only one digit is parsed: `byte[5]-'0'`, 0x416E02, so `Fleet11` reads fleet 1 — a bug, not used in conditions) | H |
| `IfEnterpriseAt S` | sun | 0x417047 | Enterprise location + 1 == index of S | H |
| `IfSunsDestroyed n` | int | 0x41732B | counter **==** n (not ≥) | H |
| `IfClue` | – | 0x416FFB | GM+0x80 != 0 (§6.6) | H |
| `IfActionGamePlayed/Won/Lost n`, `IfSpaceCombatPlayed/Won/Lost n` | int | 0x416F14 ... 0x4172DE | results array (frontend.md) | H |
| `SetText K X text...` | K ∈ Dead, Long, Short, Soran; object; rest of line | 0x41812A | writes `DeadScanText`/`LongScan`/`ShortScan`/`SoranScan` of section X in the **Universe INI object** (GM+4, which is saved, §9) and in the live object (+0x174/+0xAC/+0xA8/+0xB0). `~` in the text is a line break (M) | H |
| `QueAudio wav` | path | 0x417CA1 → 0x478098 | queue a voice/sound (type 3) | H |
| `QueMusic wav` | file | 0x417CE6 → 0x478116 → 0x4790F9 | queue music (type 2) **only if the queue is empty**, otherwise it is dropped | H |
| `Wait K` | Timer, Zoom, QueAudio, QueMusic, Warp | 0x4183E1 | blocks the script (§2.3) | H |
| `SDTimer d [command]` | double, optional command | 0x417F85 | GM+0x50 = displayed stardate + d; GM+0x58 = optional command, run when the timer fires. The optional form is not used in the shipped script | H |
| `SecTimer n [command]` | seconds | 0x41800F | GM+0x48 = GM ms clock + n*1000 (also checked by Wait Timer); not used | H |
| `DisableControls` / `EnableControls` | – | 0x41986B(0/1) | GM+8; bridge 0x476DE8: busy cursor, GUI buttons off/on, map clicks off/on | H |
| `DisableSim` / `EnableSim` | – | 0x4198B9(0/1) | GM+0x10 and 0x56F838; GUI+0x584 enables the SIM. DESTRUCTION button | H |
| `Scanner M` | Federation/Fed, Female Talk(ing)/Listen(ing), Male Talk(ing)/Listen(ing), Normal | 0x417EB1 → text panel mode 5/8/9/6/7, Normal restores (§4.4) | H |
| `ClueWave wav` | path | 0x416BD8 → 0x4782EB | bridge+0x84 = wav and status 0x56F904 = wav; if the context button is idle it becomes COMPUTER LOG (which replays wav). **Runs even when skip is set** | H |
| `ShowNexus` | – | 0x418396 → 0x478590 | Nexus: pick the path graphic (0x468F82), then draw it (state 3 the first time, else 2) (§6.7) | H |
| `OnTransport S` / `OnTactical S` | state | 0x418349 / 0x4182FC | GM+0xB4 / +0xB0 = S; context mode 1 / 2 (0x56F708, GUI+0x170). Does **not** block. The jump happens when the player presses the button (§6.5) | H |
| `FleetAlert n level` | n, Green/Yellow/Red | 0x416DC5 → 0x4781D1 | set a fleet's alert; not used in STATES.INI | M |
| `Figit Picard/Data n` | | 0x416D8C / 0x416D53 | force Picard/Data animation n; not used | H |
| `FadeOut` | – | 0x416D20 → 0x4773EC | palette fade (M); not used | M |
| `Call S` / `Pop` / `Return` | | 0x416B6B / 0x417B48 / 0x417D2B | Call pushes the current state (its line pointer is **not** advanced: Call's execute 0x4118E9 never calls 0x413F0D) and jumps to S. Pop makes the caller current again, so it resumes at the line that was current when the Call happened, and re-sends the clue wave to the bridge. Return = Pop + advance one line, which is what a script `Call` needs. STATES.INI only uses Pop, after the engine's own `Call FightFleetN` (§6.4). An empty stack gives "Pop being executed without prior Call" | H |
| `AutoSave` | – | 0x416B4A → cfg 0x4886E8 | save to slot 9 | H |

`SetFlag n v` (0x418071) simply writes 0x565B68[n] (H). Flags used by the script (M, from comments and code):
* 1..4: track Soran / trilithium / energy / life. They enable the scanner's indicator icons (§4.4).
* 5, 7, 15: branch memory ("has attempted Arvada1Or2", "has been to Halee1Or2/Antilios/SC12", second sun lost).
* 6: a sun has been lost. 8: Amargosa lost twice.
* 10: **set by the bridge** when the predicted Nexus path crosses Veridian III (0x468F82).
* 11: **set by the bridge** when a scan finds Soran (0x478177); the dialogue guarded by `IfNotFlag 11` is then
  skipped. Cleared by `SetFlag 11 FALSE`, on bridge creation and on load.
* 12: set by the bridge after the "Nexus path changed" speech (0x46950C).
* 13: "reached Veridian".

### 2.3 What `Wait` waits for (0x4183E1) (H)
GM+0xAC = kind. With **no bridge**, every Wait passes at once. With a bridge:
| arg | kind | released when |
|---|---|---|
| (anything else) | 0 | never: the script hangs |
| `QueAudio` **or** `QueMusic` | 1 | the audio queue is empty (0x4792D3). Both arguments mean the same |
| `Timer` | 2 | displayed stardate ≥ GM+0x50 **and** GM ms clock ≥ GM+0x48 **and** the Enterprise is idle at a star (state 8). Then the optional SDTimer command runs |
| `Warp` | 3 | Enterprise flag 14 (warping) is clear |
| `Zoom` | 4 | the map is not zooming (0x46D4C6) |

### 2.4 Commands that leave the bridge
`ActionGame`, `SpaceCombat`, `Movie`, `ScrollText`, `Intro`, `Options`, `EndGame` are covered in frontend.md. After
such a module ends the GM recreates the bridge the next time a bridge command runs (H).
* SpaceCombat results (0x418F7A):
  * 0 or 3: lost.
  * 1: won, and fleet flag 8 is set (H).
  * 2: `Jump EnterpriseDestroyed`.
  * For fleets 13..24 the GM also writes `Alert=Red` into the INI object's `[FleetN]`, so the fleet attacks the next
    time the Enterprise arrives (H).
* The selected object name is cleared after a combat (H).

---------------------------------------------------------------------------------------------------

## 3. Time

* **Stardate widget** (class vtable 0x535DA0, GUI+0xAC4, tick 0x47267A, draw 0x472891) (H):
  * Every 500 ms while target > displayed: displayed += 0.1, light the digits for 380 ms, play TimeBeep (GUI+0xAF8).
  * When target ≤ displayed: target = displayed.
  * Drawn with `GUI\Numerals.bmp` (66x52 = 2 frames of 26 rows; glyphs `0123456789.`, 6 px each; frame 1 = lit,
    frame 0 = normal; M for which is which) at GUI (485,35)-(534,61), i.e. screen (485,397).
  * The value is formatted with `_fcvt`-like code, 6 digits; the `%.1f` look is M.
* **What adds time** (all through 0x472A91, target += x) (H):
  | action | x |
  |---|---|
  | warp (Enterprise leaves a sun) | 1.0 |
  | arrival | the Enterprise warps in once displayed ≥ departure + 0.9 |
  | SIM. DESTRUCTION (start or cancel) | 0.3 |
  | scan result shown (long or short) | 0.1 |
  | zoom in or out (VIEW button) | 0.1 |
  Script zooms (ZoomIn/ZoomOut) cost nothing.
* So a warp takes: GotoSound (Picard: "Set course for X"), the warp-out animation (17 frames × 100 ms), then
  ≈ 4.5 s of stardate ticking, then the warp-in animation (9 × 100 ms).
* **Real time**: the bridge tick gets the frame delta in ms (`*0x546D68`); the frame is limited by Sleep(...)
  (0x47738E).

---------------------------------------------------------------------------------------------------

## 4. The screen

640x480. The original sets **640x480x16** through its own DirectDraw/D3D layer (0x45D133(640,480,16,3), 0x5630F8)
(H), and every bridge bitmap keeps its own palette (62 of the STELLAR BMPs have palettes different from GUI-BACK's)
(H). For our CLUT8 screen, either compose the bridge in RGB, or remap every bitmap to one shared palette.

### 4.1 Layout (H, `br/scene.png`)
| area | rect | contents |
|---|---|---|
| map view | 0,0 - 639,361 | `View\Backgrnd.bmp` 640x362 (stars, drawn first), then objects, then `View\Equip.bmp` 288x166 at **(155,196)** (the bridge rail) with Data and Picard on top |
| GUI panel | 0,362 - 639,479 | `GUI\GUI-Back.bmp` 640x118 at (0,362) |

Transparency: magenta, palette index **254**, in the sprites, Equip, Data and Picard (H for these files). The cursor
bitmaps are drawn the same way.

### 4.2 GUI widgets (GUI loader 0x47347C; rects relative to the panel, add 362 to y) (H)
Button class 0x46F9CA:
* `0x46FB29(file, nStates, hasDim)` loads the bitmap.
* frame = base + 2*state + highlight, where base = 1 when there is a dim frame (frame 0 = disabled/dim).
* Frames are counted **from the bottom** of the strip, 27 rows each.
* Per state: `0x46FB78(state, flashFlag, ?, helpId)`. Flashing states toggle the highlight every 125 ms (H for the
  timer, M for the meaning).

| member | id | rect (l,t,r,b) | bitmap | states (frames from bottom) | action when clicked |
|---|---|---|---|---|---|
| +0x590 | 5 | 536,0,639,26 | gui\setcours.bmp 104x54 | 1 state: 0 normal, 1 lit | SET COURSE (§6.2) |
| +0x588 | 0x20 | 536,36,639,62 | gui\TransAct.bmp 104x189 (7) | dim; s0 **TACTICAL** (1 blank, 2 text, flashes); s1 **TRANSPORT** (3,4, flashes); s2 **COMPUTER LOG** (5,6) | §6.5 |
| +0x594 | 0x10 | 536,73,639,99 | gui\options.bmp 104x54 | 1 state | bridge vt20 = options screen (frontend.md §1.5) |
| +0x5A4 | 0x21 | 292,0,411,26 | Gui\view.bmp 120x108 (4) | s0 SYSTEM VIEW (0,1), s1 FULL SECTOR VIEW (2,3) | zoom in/out (§5.4) |
| +0x59C | 0x22 | 292,36,411,62 | Gui\Simulate.bmp 120x135 (5) | dim; s0 CANCEL SIMULATION (1,2), s1 SIM. DESTRUCTION (3,4) | §6.8 |
| +0x598 | 0x23 | 292,73,411,99 | Gui\scan.bmp 120x108 (4) | s0 SHORT RANGE SCAN (0,1), s1 LONG RANGE SCAN (2,3) | §4.4 |
| +0x58C | 0x1D | 258,27,282,44 | gui\uptext.bmp 25x54 (3 of 18) | dim, normal, lit | scroll the text up |
| +0x5A0 | 0x1C | 258,46,282,63 | gui\dntext.bmp | same | scroll down |
| +0x5A8 | 0x50 | 0,6,255,101 | text/scanner panel (§4.4) | | |
| +0xAC4 | 0x1E | 485,35,534,61 | stardate (§3) | | |
| +0xAC0 | 0x1F | 0,104,639,117 | status line, Stellar.fnt, timing 500/2000 ms (vtA0) | | subtitles (§8.2) |

Automatic button states (0x472FB2, every tick):
* VIEW: s1 in system view (map modes 0, 2), s0 in sector view (modes 1, 3). Forced to s0 when the selected
  object's sun ≠ the current system (map+0x1A0).
* SCAN: s0 (SHORT) when the selected object's sun is the Enterprise's sun, otherwise s1 (LONG).
* SIMULATE (only when a sun is selected):
  * enabled and s1 when the sun is neither destroyed nor simulated;
  * s0 (CANCEL) when it is simulated;
  * disabled when it is destroyed or when the sim is disabled.
* OPTIONS: disabled while the main window is busy (warping, +0x30→+0x58).

Enable/disable (0x474175; EnableControls/DisableControls and "busy"):
* Off: VIEW, SCAN, SIM, TransAct and SET COURSE are dimmed and map clicks are ignored.
* On: SIM is enabled only if EnableSim; TransAct is set as in §6.5.

Other GUI bitmaps in STELLAR.DAT that the code does **not** load: ACTION, LETTERS, MOD, REPLAY, SPA9, STATION,
SC-ACTIV.RLE (H: no string references; SC-ACTIV.RLE does not even parse as OVERLAY).

Key handling (bridge window message map 0x5355B8, WM_KEYDOWN 0x47898F) (H):
* Space: skip the current audio item (0x479AD7).
* Up / Down: scroll the text one line. PgUp / PgDn: one page.
* F2 / F3: two config toggles (0x48904D / 0x488FEC) followed by a redraw (L: detail or volume).
* Other keys: the module base, e.g. Esc → options (M).

Map messages (0x535538):
* Left button: if the map is enabled (+0x1B4) and the scanner panel is idle (+0x54), select the object under the
  cursor (0x46D06F → bridge 0x478463 → map +0x4D8). There is no sound and no time cost (H).
* Right button: nothing (H).

Cursors (DirectDraw mode; 0x476257) (H for the files):
* `View\CurNorm.bmp`: map, nothing under the cursor.
* `View\CurSel.bmp`: over a selectable object.
* `GUI\CurGUI.bmp`: over the panel.
* `View\CurBusy1..3.bmp`: animated busy cursor, frames 4..6 every 250 ms while controls are off (bridge tick 0x476F02).

### 4.3 Selection markers (map; 0x46D608 loads) (M for their exact use)
`View\CurIdle.bmp` 128x256 (2 frames of 128: blue ring), `View\CurScan.bmp` (yellow ring), `View\Nexcurs.bmp`
(small yellow/red ring; drawn on VERIDIAN3 while flag 10 is set, 0x46BA18), `View\SysNames.bmp` 96x130 = 10 names of
13 rows (Amargosa..Veridian, top to bottom, drawn under each sun in the sector view; the frame order is M),
`View\Circle.bmp` 64x64 glow, `View\Suns.bmp` 64x384.

### 4.4 Text / scanner panel (GUI+0x5A8, class 0x470219, draw 0x47160B) (H unless marked)
* Panel origin = GUI (0,6) = screen **(0,368)**, 256x96.
* The text widget (+0x1B0, Stellar.fnt) uses panel x 100..252, full height. It is scrolled by the arrows and the keys.

Panel modes (+0x48):
| mode | contents |
|---|---|
| 0 | empty |
| 1 | scanning: `GUI\sc-activ.bmp` 95x2755 = 29 frames of 95, at panel (0,0) |
| 2-4 | scan result: the object's ScanGraphic (or DeadScanGraphic) at (0,0); indicators at y=85 (see below); the scan text on the right. At mode 2 the stardate target gets +0.1 |
| 5 | `GUI\scfeder.bmp` 120x90 (Federation emblem) at (63,0) — `Scanner Federation` |
| 6 / 7 | `GUI\scmale.bmp` 254x93 at (0,1); eyes `scmeyes.bmp` (2 frames of 15) at (111,24); mouth `scmmouth.bmp` (5 frames of 28) at (117,41) — Male Talking / Listening |
| 8 / 9 | `GUI\scfemale.bmp` at (0,1); eyes `scfeyes.bmp` (2 of 11) at (128,30); mouth `scfmouth.bmp` (5 of 23) at (121,44) — Female Talking / Listening |

The draw call adds 6 to y, so in GUI coordinates the y values of modes 5..9 are 6, 7, 30, 47, 36, 50. Talking animates
the mouth, listening only blinks (M).

Indicators in modes 2-4 (4-frame 11x11 animations):
| icon | GUI position | shown when |
|---|---|---|
| `SC-Life.bmp` | (12, 85+6) | short scan and object has Soran (flag 1) and script flag 1 |
| `SC-Energ.bmp` | (36, 91) | object has energy (flag 3) and script flag 3 |
| `SC-Trili.bmp` | (60, 91) | short scan and trilithium (flag 2) and script flag 2 |

Life signs (flag 4 / script flag 4) get no icon of their own (M).

Scan text, object vt34 (0x4643E1):
* long: `LongScan`;
* short: `SoranScan` if the object has Soran and the text is not empty, otherwise `ShortScan`;
* a destroyed sun: `DeadScanText`;
* a Red-alert fleet always gives `LongScan` (0x4678B6).
* `~` starts a new line (M).
The scan text is not shown for the comm modes 5-9.

---------------------------------------------------------------------------------------------------

## 5. The star map

### 5.1 Map view state (map = bridge+0x494) (H)
| off | what |
|---|---|
| +0x234 | mode: 0 zooming in, 1 zooming out, 2 system view, 3 sector view |
| +0x1A0 | current system (sun) |
| +0x4D8 | selected object |
| +0x1E8 | camera position (3 doubles) |
| +0x1D0 | camera target |
| +0x200 | camera velocity per ms |
| +0x230 | zoom time left (2000 ms) |
| +0x218 | sector camera = **(319500, 180500, 1000)** = (639*1000/2, 361*1000/2, MaxZ) |
| +0x4E0[] / +0x508 / +0x50C | Data animations / current / count |
| +0x51C[] / +0x544 / +0x548 | Picard animations / current / count |

### 5.2 World coordinates and projection (H)
Sun world position (0x465A03), from `Location=x,y,z`:
`X = (x + 320 − CenterX) * MaxZ`, `Y = (y + 180 − CenterY) * MaxZ`, `Z = z * 10`.

Projection (0x46CD96):
```
rel = obj − camera;   d = |rel.z|
sx = CenterX + round(rel.x / d)
sy = CenterY + round(rel.y / d)
r  = round((Size/2) * SizeMagnification / d), clamped to 1..63   (screen radius)
```
`Size` defaults to the texture width (0x464716); object +0x38 holds Size/2.

In the sector view (camera (319500,180500,1000)) a sun at depth z appears at
`sx = CX + 1000*(x − CX + 0.5)/(1000 − 10z)`, `sy = CY + 1000*(y − CY − 0.5)/(1000 − 10z)`. With z = 0 that is
(x, y), so `Location` is a screen position, and z pushes the sun away from (CenterX, CenterY) = (316,120) and makes it
bigger. A Size-64 sun is about 13-21 px across (`br/sector_mock.png`, H for the method).

* HFOV/VFOV are read into map+0x1AC/+0x1B0 through `tan()` of the value taken as radians. Those fields are **never
  used** (H), so ignore HFOV/VFOV.
* MinZ is the zoom distance; MaxZ is the world scale and the sector camera z.
* Objects are culled against the view rect (0,0)-(639,361) (map+0xE0).

### 5.3 Planets, moons, station (H unless marked)
* Planet i (1-based, 0x466C48 / 0x464F18) orbits its sun on a circle:
  * radius `Distance` if given, else 40 + 20*(i−1);
  * orbit tilt: the sun's `OrbitPitch` and `OrbitIntersect` in degrees (a planet's own keys override them);
  * random start angle 0..359°;
  * `Speed` = the period in seconds (default radius/3); angular speed = 2π/(1000*Speed) rad per ms.
* Each tick (0x464F54):
  * angle += dt*speed;
  * (r cos a, r sin a) is rotated by pitch and intersect;
  * x and y are scaled by `DistanceMagnification / s` with `s = (camera.z − MinZ)/(MaxZ − MinZ) + 0.8`;
  * the sun's position is added;
  * the moons tick. Moons orbit the planet the same way, using the angle steps 0x563720.. (M).
* Planets are drawn as **texture-mapped spheres** (TextureGraphic is a 64x64 tile, e.g. AMPV1 grey rock). The
  original goes through its 3D layer and the sun's `OffsetRed/Green/Blue` light table (0x40F3B1, only when the 3D
  layer is up) (M). For the port: a software sphere with the texture wrapped around it and Lambert light from the sun's
  direction (L for the exact look).
* The station (`[SpaceStation]`) sits at its sun + `OffsetSun=40,30` (M: world units ×1000 like fleets). `AmOV.bmp`
  is 2 frames of 128 at 2 fps; Size 126.
* Planets and moons appear only in the system view (M).

### 5.4 Zoom (H)
* ZoomIn (0x46F37F):
  * mode 0; current system = sun;
  * target camera = sun position + (0, 0, **+MinZ**). The camera ends 200 units in front of the sun, so the sun is
    drawn at radius 63 (the clamp) and the planets spread around it;
  * velocity = (target − camera)/2000; time 2000 ms.
* ZoomOut (0x46F65B): mode 1; target = sector camera; 2000 ms.
* At the end the mode becomes 2 or 3 (map tick 0x46C5F5).
* The VIEW button zooms into the selected object's system or out (0x476C68) and costs 0.1 SD.

### 5.5 Suns (H for keys and frames, M for the drawing order)
* `TextureGraphic` 64x384 = 6 frames of 64 at `TextureFPS` 10: the normal sun sprite, scaled to 2r.
* When destroyed: `DestructGraphic` 64x768 = 12 frames (white flash → expanding → dark remnant; frames from the
  bottom), then `DestroyedGraphic` 6 frames.
* Scan pictures: `ScanGraphic` 96x96 (`DeadScanGraphic` when dead).
* Simulation shows the same destruct/dead sequence while flag 7 is set (0x4669BF → 0x4668AF(4); cancel → (1))
  (M for the frames).
* Each sun bitmap has its own palette, so the suns differ in colour.

### 5.6 Fleets on the map (H)
* Up to four fleets per sun, in slots (sun +0x180[1..3]; 0x4658D3).
* Fleet world position = sun position + offset*MaxZ (0x46E96B):
  | slot | sector view | system view |
  |---|---|---|
  | 0 | (−15, −10) | (−40, −10) |
  | 1 | (−15, +10) | (−40, +25) |
  | 2 | (+25, −10) | (+40, −10) |
  | 3 | (+25, +10) | (+40, +25) |
* Sprites: `Fleet\<T>SIN.rle` 64x64 (9 frames) and `<T>SOUT.rle` (17 frames) in the sector view; `<T>LIN/<T>LOUT`
  128x128 in the system view (0x46CF58: map mode 2 → large).
* T by fleet type (0x4679DE; type index 1..5 → map+0x238 + type*0x70) (H for the files, M for the mapping):
  | T | ships |
  |---|---|
  | BOP | Klingon Bird of Prey: `Type=Klingon` (and BirdOfPrey) |
  | CHO | Chodak |
  | ENT | Galaxy = Enterprise |
  | VOR | Klingon Vor'cha: `Klingon2` |
  | WAR | Romulan Warbird: `Romulan` |
* Animation states (+0x110) are in §6.3. Idle (state 8): show the last IN frame, swapping with the one before it every
  800 ms.
* Cloaked/hidden fleets (flag 6) are not drawn (0x46B384, M).
* The fleet's scan picture: `GUI\<T><n>.BMP` 96x96 by number of ships (BOP1-5, CHO1-4, VOR1, WAR1-4), or the INI
  `ScanGraphic` (Fleet08/9/12/25).

### 5.7 Picard and Data (0x46A046 sequence player; 0x46EFB7 driver) (H)
* `[DataN]` / `[PicardN]`: `Graphic` strip, `NumFrames` (frame height = bitmap height / NumFrames: Data 179x209,
  Picard 139x230), `Location` = top-left on screen (Data 178,153; Picard 273,132 — both end at y=362, behind the panel
  edge), `SequenceK=frame,ms` (frame 1-based; frames counted from the bottom (M), verified visually for frame 1).
* A sequence plays each (frame, ms) step once and stops on its last frame.
* After a sequence ends, the driver waits a random **5000..15000 ms**, then starts a random sequence index different
  from the last one (separately for Data and for Picard).
* Drawn after Equip (0x46E76F: background, objects, Equip at (155,196), Data, Picard).

---------------------------------------------------------------------------------------------------

## 6. Simulation

### 6.1 Enterprise and fleet location
The location is a **sun index** (status 0x56E880). The Enterprise "is at" a sun when that index matches
(`IfEnterpriseAt`); there is nothing finer than a star system. Planets and the station are only targets for scans
and transport (H).

### 6.2 SET COURSE (GUI tick 0x474346, button +0x590) (H)
1. With no selection, queue Data0211 ("may I suggest selecting a destination"). If the selection's sun is the
   Enterprise's sun, queue Data0210 ("we are already here"). Otherwise:
2. Set the sun's flag 5, beep (GUI+0xACC), queue the sun's `GotoSound` (Picard: "Set course for X"; 0x475172 returns
   its length), call Enterprise 0x469CC8(sun, len), set the context button to idle, controls off (0x476DE8(0, sim)),
   window busy (0x475BC0(1)).
3. 0x469CC8: flag 14 on; destination = sun; state 4 (wait for the audio queue, then warp out) if a sound was queued,
   else state 5.
4. Warp out (states 5→6): warp-out sound by type (GUI+0xAE4..AF4 = ewarpout/kwarpout/rwarpout/cwarpout), 17 frames at
   100 ms, then leave the sun (location −1) and go to state 0.
5. Enterprise tick (0x469D86): in state 0 → state 7, arrival = stardate + 0.9, target stardate += 1.0. In state 7,
   once stardate ≥ arrival → vt6C(destination) = warp in (states 2→3: ewarpin, 9 frames at 100 ms) → state 8 (idle).
6. On arrival (3→8):
   * flag 14 off, **EnableControls** (GM 0x41986B(1): this re-enables controls even if the script disabled them),
     window not busy;
   * then for each fleet at the destination with **Alert = Red** (+0x11C == 2): queue CommBoop and an intercept line
     (random Riker or Worf by fleet type, tables 0x547110.., e.g. "The Klingons are moving to intercept"), set the
     context button to TACTICAL and flash it, and **start combat at once** (bridge 0x476BC0 → GM 0x419610(n),
     §6.4) (H).

Alert values: Green 0, Yellow 1, Red 2 (`Alert=` in INI; the comparisons are case-insensitive, M). Yellow has no
effect found (M).

### 6.3 Fleet state machine (+0x110, fleet tick 0x468406, table 0x4687B1) (H)
| state | what |
|---|---|
| 0 | idle, not animating |
| 1 | about to warp in: wait for an empty audio queue (idle-animate), then 2 |
| 2 → 3 | warp in: visible (flag 6 = 0), warp-in sound (GUI+0xAE8..), frames 0..8 of `IN` every 100 ms → 8 |
| 4 | about to warp out: wait for an empty audio queue, then 5 |
| 5 → 6 | warp out: sound, frames of `OUT` every 100 ms, then flag 6 = 1, leave the sun (location −1), state 0 |
| 7 | Enterprise in transit (§6.2) |
| 8 | at a sun, idle |

Fleets **never move on their own**; only the script (`WarpFleetIn/Away`, `Move`) and the Enterprise logic move
them (H).

### 6.4 Combat start (GM 0x419610(n)) (H)
* n = −1: `Jump <OnTactical state>` (GM+0xB0).
* 12 < n < 25 (fleets 13..24): `Call FightFleet<n>` (snapshot first, 0x41447D(1)). Those sections end with `Pop`,
  so the script resumes the interrupted state at the line it was on (usually a `Wait Timer`) (H).
* Otherwise: `Jump SpaceCombat<n>`. This restarts that state from the top, including AutoSave and WarpFleetIn —
  this is how the scripted fleets 1..12 and 25 fight.

### 6.5 The context button (TRANSPORT / TACTICAL / COMPUTER LOG) (H)
Its state is recomputed whenever controls are enabled or the selection, scan or Enterprise changes (0x474175,
0x47533B). The panel's object = the object of the last scan (+0x44); "short" = that scan was short-range (+0x50).
* If the object exists, was short-scanned, and the Enterprise is in its system:
  * object has Soran (flag 1): mode = GUI+0x170 (1 → **TRANSPORT**, 2 → **TACTICAL**; set by
    OnTransport/OnTactical, initially 0);
  * else if the object is a fleet: **TACTICAL**;
  * else idle.
* Otherwise idle.
* Idle (mode 0): **COMPUTER LOG** if a clue wave is set, else disabled.

Pressing it:
* TRANSPORT: DisableControls, `Jump <OnTransport state>` (0x476C34 → GM 0x4196D6).
* TACTICAL on a fleet with Soran: `Jump <OnTactical state>`. TACTICAL on another fleet: DisableControls, queue
  Pica0289 ("Raise shields and ready phasers"), then combat with that fleet's number (§6.4).
* COMPUTER LOG: queue the clue wave.

Notes:
* GM+0xB0/+0xB4 are **not cleared** by Jump (H). A stale OnTransport stays armed, but the scripts clear the Soran
  flags when they leave a mission, so nothing can trigger it (M).
* Without the TRANSPORT press, the mission states run their SDTimer deadlines and take the "missed" branch: the star
  is destroyed (`Movie n`, `Destroy`). That is the core loop: find Soran by scanning, get there, beam down in time.

### 6.6 Clues (H)
* `IfClue` tests GM+0x80. It is computed when an **away mission** module ends (0x418F7A): 1 if any of the module's
  five outcome slots (+0x14..+0x24) is non-zero. The slots are filled by the away mission's SET_MISSION_OUTCOME
  native 0x97 (natives-f.md; e.g. amargos1 sets 11 when global26 == 1; enterp sets 1 when global20 == 1). The guard
  GM+0x20 is always 1.
* The flag is copied to status 0x56F83C and restored on load. In our engine: `!mission.getOutcomes().empty()`.
* So `IfClue` after Amargosa1 means "the player found the clue on the station". After the Enterprise mission it
  selects the "Picard dead" ending (STATES `[EnterpriseWon]`).
* `ClueWave` is a separate thing: the computer's spoken hint for the current state, replayed by COMPUTER LOG.

### 6.7 Nexus (object 0x5359C0; picker 0x468F82; tick 0x46950C) (H)
* Graphics: `[Nexus] GraphicN=Nexus\NexusN.rle`, `LocationN` = top-left on screen. Each is one frame, the full width
  of the path; 640x(60..220), NEXUS7 336 wide.
* sunGone(i) = sun i (1-based in Universe order) is destroyed (flag 8) or being simulated (flag 7). The picker
  (0x468F82) tests these in order:
  1. Epsion(5), Tarendia(9) or Guernica(7) gone → **8**
  2. Arvada(3) or Halee(8) gone → **7**
  3. flag 13 and Veridian(10) gone → **5**, crosses Veridian III
  4. flag 8 and Veridian gone → **4**, crosses
  5. Veridian gone and at least two of {Amargosa(1), Bersus(4), Galorndon(6)} gone → **5**, crosses
  6. at least two of {1, 4, 6} gone → **3**
  7. Amargosa gone → **2**
  8. Antilios(2) gone → **6**
  9. else → **1**
* If the new path crosses Veridian III and flag 10 was not set: queue Comp0162 ("Projected Nexus path intersects
  Veridian Three") and a random `Sound\SoundFX\SSoran<1..5>.wav` as music. Flag 10 = crosses.
* If the graphic changed and the Nexus is visible, it is redrawn:
  * state 2: wipe out the old path from the left;
  * state 3: wipe in the new one at dt*640/1000 px per tick (1..32), with sound Nexus1/2.wav (+0x154/+0x158);
  * then state 1 and controls back.
  If a sun newly disappeared and the first-change speech was already played: queue Comp0197 ("Nexus path update").
* State 1, first time the graphic ≠ 1 with an empty queue: queue Data0127 ("the change in the stars' gravitational
  fields has altered the path of the Nexus") and Pica0138; flag 12 = 1.
* The picker runs on `ShowNexus`, `Destroy` and simulate/cancel.
* Nexus RLE palette: `Nexus\Nexus.bmp` (4x1).
* Verified: graphic 4, drawn rows bottom-up, crosses the projected Veridian (`br/nexus_check1.png`).

### 6.8 Simulating a destruction (H)
* SIM. DESTRUCTION (only with a selected sun, and the sun = the current system):
  * pressed in s1: set the sun's flag 5, queue the sun's `SimDestructionSound` ("Simulating destruction of X"),
    beep GUI+0xAD8, sun 0x4669BF (flag 7 on, destruct display), Nexus re-pick;
  * pressed in s0: queue Comp0159 ("Simulation cancelled"), beep GUI+0xAE0, 0x46583C (flag 7 off, normal
    display, Nexus re-pick).
  * Both add 0.3 SD.
* It is enabled only after `EnableSim`. The game uses it to let the player foresee which star Soran needs.

### 6.9 Scans (H)
* SCAN button: SHORT when the selected object's sun is the Enterprise's, else LONG (0x476AA1, arg = short).
* Every 5th long scan queues Data0114 ("a short range scan from within the same star system would provide more
  detailed information").
* Panel 0x471D52(object, short): scanning animation (mode 1, SC-ACTIV). When it finishes:
  * the panel shows the result (mode 2 adds 0.1 SD);
  * object vt58 sets flag 5;
  * if the object has Soran (flag 1): script flag 11 = 1, play the "found Soran" reaction (0x46E8F1 / 0x47522A:
    Data0217/0218/0219 "It appears we have found Soran" / "Bingo!" / "El-Aurian lifesigns detected", Enhance.wav)
    (M for which lines).
* The computer confirms with Comp0074..0079 ("short/long range scan of planet/star/station complete") (M for where).

### 6.10 Sun destroyed by script (`Destroy`)
Suns-destroyed counter +1; destruct animation (12 frames) then dead (6 frames); SimDest.wav (M); the scan texts switch
to DeadScanText and DeadScanGraphic; Nexus re-pick (H for the counter and the call chain, M for the sound).

---------------------------------------------------------------------------------------------------

## 7. File formats

### 7.1 `.RLE` = OVERLAY (H, all 20 fleet and 8 Nexus files decode with 0 bad rows; `br/t/rle.py`)
```
char[8] "OVERLAY\0"; u32 width; u32 rows; u32 rowOffset[rows] (from the end of the table)
row: int16 runs until width: n<0 skip −n transparent; n>0 n palette bytes, padded to even
```
* Frames are stacked top-down. **Rows inside a frame are stored bottom-up**, as `engines/stg/overlay.h` already does.
  Verified: ships are upright only when each frame is flipped, and Nexus graphic 4 then crosses Veridian.
* Frame height: fleet strips are square (64 or 128). `ENTLOUT/WARLOUT` have 2175 rows, so their last frame has 127
  rows: use height = width and clip.
* Frame counts: SIN/LIN 9, SOUT/LOUT 17. Nexus files are 1 frame.
* No palette in the file:
  * fleets use **`FLEET\ENTSIN.BMP`**'s palette (64x577, loaded 0x467CFF);
  * the Nexus uses **`NEXUS\NEXUS.BMP`**'s (4x1, loaded 0x468CFC).
  Rendered with GUI-BACK's palette the ships come out dark green, which is wrong.
* `GUI\SC-ACTIV.RLE` is not OVERLAY data and is unused; the BMP is used.

### 7.2 BMPs (H)
* 8-bit, each with its own palette (§4). Transparent pixels are index 254 (magenta 255,0,255) in the sprites checked
  (Equip, Data, Picard, suns, buttons).
* Frame strips are counted from the bottom of the bitmap, as elsewhere in the game.
* Frame sizes (from code): sun textures 64 (6 frames); destruct 12; dead 6; station 2 of 128; CurIdle/CurScan/
  NexCurs 2 of 128; SysNames 10 of 13; Numerals 2 of 26; sc-activ 29 of 95; SC-* 4 of 11; scmeyes 2 of 15; scmmouth
  5 of 28; scfeyes 2 of 11; scfmouth 5 of 23; buttons 27 rows.

### 7.3 UNIVERSE.INI (Design\Universe.INI, loaded by GM 0x4199F8 and the bridge 0x4778D8 / 0x46D608 / 0x474E17) (H)
The file has a second copy of most sections at the end, holding only the scan texts ("for internationalisation"). INI
lookups merge both copies (M: standard GetPrivateProfile-style lookups find the key in either copy).

| section | keys |
|---|---|
| `[Universe]` | NumSuns=10, SunN=section names (Universe order = sun index 0..9), NumFleets=25 |
| `[Display]` | MinZ=200 (zoom distance), MaxZ=1000 (world scale, sector camera z), HFOV/VFOV (unused), DistanceMagnification=400 (planet orbit scale), SizeMagnification=200, CenterX=316, CenterY=120 (projection centre) |
| `[PicardDataAnimation]` | NumDataSequences=4, NumPicardSequences=4 (number of `[DataN]` / `[PicardN]` sections) |
| `[DataN]` / `[PicardN]` | Location=x,y; Graphic; NumFrames; NumSequences; SequenceK=frame,ms |
| `[Nexus]` | NumGraphics=8, GraphicN, LocationN=x,y |
| `[Enterprise]` | Location=Amargosa (not used: the script's `Move Enterprise` decides, M), Type=Galaxy, FramesPerSecond=5 (M) |
| `[SpaceStation]` | OffsetSun=40,30; HasEnergy/HasLife; ScanGraphic/ScanFrames/ScanFPS; TextureGraphic/TextureFrames/TextureFPS; Size=126; LongScan/ShortScan/SoranScan |
| sun `[Name]` | Location=x,y,z; Size; Offset{Red,Green,Blue} (light colour, 3D layer only); OrbitPitch / OrbitIntersect (default planet orbit tilt, degrees); TextureGraphic/Frames/FPS; ScanGraphic/Frames/FPS; DestructGraphic/Frames; DestroyedGraphic/Frames; DeadScanGraphic/Frames/FPS; GotoSound; SimDestructionSound; NumPlanets; LongScan; ShortScan; DeadScanText |
| planet `[NameN]` | ScanGraphic (80x80); TextureGraphic (64x64 sphere texture); Size; Distance (orbit radius); Speed (seconds per orbit); OrbitPitch/OrbitIntersect; HasLife/HasEnergy/HasTrilithium; NumMoons; LongScan/ShortScan/SoranScan |
| moon `[NameNMoonM]` | TextureGraphic (Textures\Moon1.bmp); Size |
| `[FleetN]` | Type (Klingon, Klingon2, Romulan, Chodak; also BirdOfPrey, Galaxy); NumShips (scan picture <T><NumShips>); Cloaked (flag 6); Alert (Green/Yellow/Red, case-insensitive); HasEnergy/HasLife/HasTrilithium; ScanGraphic (optional override); LongScan/ShortScan/SoranScan |

Notes:
* `Energy=TRUE` (Guernica3) and `Life=TRUE` (Veridian4) are typos the game ignores (H: no such key strings).
* Antilios has no `Size`, so its texture width (64) is used (H).
* The INI object is written into saves, with the SetText and Alert changes (§9).

---------------------------------------------------------------------------------------------------

## 8. Sound

### 8.1 Audio queue (bridge+0x70; class 0x478EB5; items 0x478B7D, 0x120 bytes; global array 0x563278[32], count 0x563318) (H)
* Item: path, type (1 effect, 2 music, 3 voice), playing flag, timer.
* Paths:
  * QueAudio paths are relative to the data root (`Sound\Voices\X.wav` in STELLAR.DAT);
  * QueMusic names are looked up in the music directory, i.e. MUSIC.1/2 (`GameStar.wav`, `TimeOut1-4`, `Win1-4`,
    `Lose1-4`, `Loop1-4`).
* Items play **one at a time**, in order (tick 0x4791F0):
  1. the head starts (0x47976A);
  2. its subtitle is shown;
  3. when it ends, it is removed and the subtitle line cleared (0x479646).
* Queueing a voice while **music is the head** fades the music out over min(remaining, 1000 ms), then stops it
  (0x478C7C / 0x478E56).
* `QueMusic` is dropped if the queue is not empty (0x4790F9).
* Space skips the current item.
* Volumes: music items use the music volume (0x4888D5), others the effects volume (0x4888F4) (M).
* `Wait QueAudio` / `Wait QueMusic` = queue empty.

### 8.2 Subtitles (H)
* When subtitles are on (cfg 0x488B15), a starting item looks for `<path>.txt` next to the WAV and shows it in the
  status line (GUI+0xAC0: 0,104-639,117 → screen y 466..479, Stellar.fnt).
* The text stays for the item's duration. Voices: WAV data size/44 ms (22 kHz 16-bit mono). Music: the length from
  the mixer.
* "File: %s has no subtitle" appears only in a debug mode (0x54716C).

### 8.3 Fixed bridge sounds (H for names, M for the trigger where marked)
* On bridge start:
  * `Sound\SoundFX\SLoop.wav` loops while it loads (at least 1 s);
  * then `Sound\SoundFX\Stellar.wav` (0x5470F8) loops as background ambience (+0x74).
* Idle music: every **60 s without queued audio** (+0x78 is reset whenever anything is queued) the bridge
  alternately queues `Loop<1..4>.wav` (random) or skips once, i.e. one loop at most every 2 minutes.
* Button beeps: GUI+0xACC..0xB00 (beep3/beep14/beep16/beep20/boop1). Which beep is which button was not traced (M).
* Voice help per button state: help ids 0x131..0x13C ↔ Comp0184..0196/0213 ("Set course to selected system",
  "Zoom in to view selected system", ...). The trigger (hover or click-and-hold) was not traced (L).
* Warp sounds by type: ewarpin/ewarpout (Enterprise), kwarpout (Klingon), rwarpout (Romulan), cwarpout (Chodak),
  warp.wav (M).
* Scan complete: Comp0074..0079. Nexus: Nexus1/Nexus2.wav, Comp0197, Comp0162, Data0127, Pica0138, SSoran1-5.
  Intercept lines: Rike0238..0246 / Worf0124..0132.

---------------------------------------------------------------------------------------------------

## 9. Saved state (H)
* The save holds the whole status block 0x565AC0..0x56FA08 (§1.3/1.4), so the stardates, flags, locations, Nexus
  state, clue wave, Soran's location and selection are all saved. It also holds the **Universe INI object** (GM+4,
  written by 0x48E0E6), which carries the SetText and Alert changes.
* GM+0xB8 is a status snapshot taken at every Jump. It is not what the save writes (the save writes the live
  status, 0x416E9B) (H).
* Load (0x419703):
  1. restore GM+0x10 (sim) and GM+0x80 (clue);
  2. clear script flag 11;
  3. remove fleets that were in transit (flag 14 → location −1);
  4. `Jump <saved state name>`: the state restarts from its **first line**.
  The bridge then rebuilds everything from status + INI.
* For our engine: save the campaign state plus a `BridgeState` mirroring the fields above (no need to be binary
  compatible).

---------------------------------------------------------------------------------------------------

## 10. Implementation plan for engines/stg

### 10.1 New code
* **`universe.{h,cpp}` — data**:
  * `struct Universe` loads UNIVERSE.INI with both copies of sections merged;
  * `SunDef`, `PlanetDef`, `MoonDef`, `FleetDef`, `StationDef`, `AnimDef` (Data/Picard), `NexusDef`, `DisplayDef`;
  * `setText(kind, section, text)` for SetText, kept as overrides in the save.
* **`bridgestate.{h,cpp}` — the saved block**:
  * stardate and target;
  * flags (int map);
  * per-object flag arrays: suns[10][20][30], fleets[26][30];
  * locations[26]; Soran's location; clue wave; selected name; Nexus state/current/next; counters (suns destroyed,
    long scans); context mode; sim and controls enabled; clue flag; initialised marker;
  * `sync(Serializer&)`. Campaign owns it.
* **`audioqueue.{h,cpp}`**: FIFO of {path, type}; one item playing; music fade on voice; skip; `isEmpty()`; subtitle
  callback. Reuse the WAV and subtitle playback that already exists.
* **`rle` support**: reuse `Overlay`, plus `drawFrame(frame, frameHeight = width)` and a palette parameter. Load
  palettes from ENTSIN.BMP / NEXUS.BMP.
* **`starmap.{h,cpp}`**:
  * camera, projection, zoom (2 s linear);
  * draw order: background → suns/planets/moons/station → fleets → markers → Equip → Data/Picard;
  * hit-testing (projected rect of each object); selection markers; SysNames;
  * planet spheres: software texture sphere, cached per size;
  * Nexus wipe;
  * Picard/Data idle animations.
* **`bridgepanel.{h,cpp}`**: the GUI of §4.2 as data (rect, bitmap, nStates, hasDim, flash), the stardate widget, the
  text/scanner panel modes 0-9, the status/subtitle line, the arrow scrolling.
* **`bridge.{h,cpp}` — `class Bridge`** (the module):
  * owns the above;
  * Enterprise and fleet state machines (§6.2/6.3), scans, simulation, Nexus picker, context button;
  * the API used by Campaign: `select, zoomIn, zoomOut, moveEnterprise, moveFleet, moveSoran, warpFleetIn,
    warpFleetAway, hideFleet, warpAway, destroy, setObjectFlag/getObjectFlag, setFleetFlag, queueAudio, queueMusic,
    clueWave, showNexus, scanner, setControls, setSim, setContextMode, isZooming, isWarping, isEnterpriseIdle,
    audioEmpty`;
  * events out to Campaign: `transportPressed`, `tacticalPressed(fleet or -1)`, `combatForced(fleet)`, `options`.
* **`campaign.cpp`**:
  * replace the bridge no-ops with real calls;
  * implement Wait (blocking kinds §2.3), SDTimer (deadline in stardate), one command per frame while the bridge is up;
  * Call/Pop/Return as GM 0x416B6B/0x417B48/0x417D2B (Pop resumes the caller at its current line, Return at the next one);
  * IfClue = the last mission's outcomes were not empty;
  * OnTransport/OnTactical store names and arm the button;
  * combat start rules of §6.4 (FightFleetN via Call);
  * SpaceCombat results as in §2.4. Until space combat exists: take Won (or offer a choice), and set fleet flag 8 on
    won.

### 10.2 Order of work
1. **Data + state**: Universe loader (test: parse the real file in a local test only; unit tests use a made-up mini
   INI), BridgeState sync round trip.
2. **AudioQueue + subtitles**, and the script's QueAudio/QueMusic/Wait QueAudio. Already playable as a "radio play"
   with a static background.
3. **Panel + stardate + buttons** (static layout from §4.2, frames from the bottom; check against `br/scene.png`).
4. **Star map**, sector view only: suns, names, selection, projection (§5.2; check against `br/sector_mock.png`).
   Then fleets with RLE animations and the warp in/out state machine.
5. **Enterprise travel** + time model + SDTimer / Wait Timer / Wait Warp. **At this point the story is playable**:
   select a star, set course, scan, transport, timers expire.
6. **Scans and text panel** (long/short, texts, icons), the context button, OnTransport/OnTactical, ClueWave/
   COMPUTER LOG.
7. Zoom/system view, planets (simple shaded discs first, textured spheres later), station, moons.
8. Simulation and Nexus (picker table, wipe, speech).
9. Picard/Data idle animations, Scanner comm portraits, busy cursor, Space to skip, idle Loop music.

### 10.3 What can be simplified
* Render the bridge in RGB, or quantise all bitmaps to one palette; the 16-bit original has no single palette.
* Planets as flat-shaded textured discs (sphere UV + Lambert) instead of the 3D layer; the moons' orbit constants
  are approximate.
* Ignore HFOV/VFOV, `[Enterprise] FramesPerSecond`, FadeOut/Figit/FleetAlert/SecTimer (unused by the script).
* Saves: our own format, but keep "restart the saved state from its first line" and drop fleets in transit.

### 10.4 Tests (with made-up data, never the game's files)
* Projection: sun at (x,y,0) → screen (x,y); z = 40 scales by 1000/600 around (316,120); radius clamp.
* Stardate: target += 1.0 reaches displayed after 10 steps of 500 ms; the timer does not fire while warping.
* Nexus picker: a truth table over sunGone sets and flags 8/13 (§6.7).
* Context button truth table (§6.5).
* AudioQueue: music dropped when not empty; voice fades music; skip.
* Overlay: frame flip within a frame; odd last frame height (2175 rows).
* Script: Wait kinds release conditions; `IfSunsDestroyed` is equality; nested conditions; Call/Pop/Return resume lines.

---------------------------------------------------------------------------------------------------

## 11. Open questions
* Planet/moon rendering details (texture mapping and lighting in the 3D layer, 0x40F3B1/0x5630F8): M/L.
* Exact hit-test shape (0x46D06F) and which objects can be selected in each view (planets only in the system view?): M.
* Which beep goes with which button; the help-voice trigger; F2/F3 meaning: L.
* SysNames and Data/Picard frame order (bottom-up assumed): M.
* The "found Soran" reaction lines (0x46E8F1 / 0x47522A) and where Comp0074..0079 are queued: M.
* Yellow alert has no effect found; Cloaked fleets: hidden only, or also unscannable?: M.
* The `skip`/replay argument of command vt14 (non-zero never seen): M.
* Scan durations (the SC-ACTIV animation runs 29 frames at an untraced rate) and the text widget's scrolling metrics: M.
