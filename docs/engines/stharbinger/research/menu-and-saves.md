# DEEP.EXE game menu (Game_Setup) and save/load

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

All addresses are linear addresses (code at 0x10000, data at 0x50000; a disassembly operand `0x2xxxx` is data `0x7xxxx`).
Confidence: **H** = read directly from code, **M** = inferred with a small gap, **L** = guess.
Dumps written for this note (same directory): `ms_save.asm` (0x1EA5D..0x20D54: Save_Game, Load_Saved_Game, Restore_*),
`ms_menu.asm` (0x20D54..0x22424: Game_Setup and helpers), `ms_misc.asm` (0x22A1C..), `ms_map.asm` (deck map 0x24299).
`s.py <hexaddr>...` prints the C string at a linear address.

C runtime helpers used below (Watcom register convention eax, edx, ebx, ecx, then stack) — H:
0x33A37 fopen(name, mode), 0x3375E fclose, 0x33C97 fwrite(buf, size, n, fp), 0x33ACD fread(buf, size, n, fp),
0x3490B fputc(c, fp), 0x3488A fputs(s, fp), 0x340B6 fgetc(fp), 0x336EF strcpy, 0x33727 strcat, 0x3370E strlen,
0x33E80 memset(p, c, n), 0x34013 memcpy(dst, src, n), 0x33E98 malloc, 0x33F7B free, 0x345F1 system(cmd),
0x3469A/_0x346B7 _dos_findfirst/_dos_findnext, 0x349B0/0x349D8 _dos_getdrive/_dos_getdiskfree, 0x3475E fseek,
0x3465A strcmp-like (M).
Engine helpers: 0x18FD3(show) mouse hide(0)/show(1); 0x1902F(n) set cursor image n; 0x2E24F get key (0 = none);
0x22A1C(ms) wait up to ms or until a key, returns key; 0x20BDB(ms) busy wait; 0x2D49B(srcx, srcy, pitch, buf; dstx,
dsty, w, h) blit rect to screen; 0x2D61A(buf, 0x4B000) full-screen blit; 0x2D66F(pal, first, n) set DAC;
0x19720(pal) pick UI colours from a palette (0x71EB3 = nearest black, 0x71EB4 = nearest grey(40,40,40),
0x71EB5 = nearest white(63,63,63); also sets font fg 0x5520A = grey, shadow 0x5520B = black) — H;
0x19DD1(fontctx, y, x, str; buf) draw text (fontctx 0x55209 = {font, fg, shadow}; font 0 = art\short.fnt) — H;
0x2C523(cmp, dst, size) .CMP decompress; 0x195A1(out, dir, name, ext) path builder; 0x2AAD4 RLE frame decode.

Buffers: 0x71020 = 640x480 overlay/menu canvas (colour 1 = "transparent" in game), 0x71028 = work buffer (composed
game screen), 0x71038 = clean background.

---------------------------------------------------------------------------------------------------

## 1. Game_Setup — the game menu (0x20D54, arg al = `first`)

### 1.1 When it opens — H

| caller | situation | `first` |
|---|---|---|
| main 0x10755 | program start, **every run except the very first** (see 1.9) — before OPENING runs | 1 |
| main 0x108C9 | `run_scene` returned 1: right press while MAPON (poll_input 0x24025..0x24076, edge of mouse bit 1 vs previous 0x71DAE), right click inside a conversation (CONVERSE → 0xFFFE), DEATH (cmd 0x79), ESC in the change-CD prompt (cmd 0x8D) | 0 |

There is **no keyboard shortcut** to open the menu (poll_input only handles 'm'/'M' = deck map). — H
Before returning 1, run_scene stores the current directive pointer in 0x710AC ("resume position"). — H

### 1.2 Entry sequence — H
1. CD check: if the current disc (0x71DC4) is not in the drive (0x183F1), set 0x71DC4 = 0xFF and show the change-CD
   prompt (0x13095); ESC there → Game_Setup returns 1 (= quit the game).
2. Clear the 100 save-name pointers 0x71FF8[100]; 0x72A04 (menu palette ptr) = 0; 0x71ED1 = 0.
3. Load `art\buttons.rle` (0x1A9C7 frame count → 0x72A00, 0x1AA51 frames → list 0x729FC). Missing → error log and
   return without a menu (return value 0xFFFD).
4. 0x2224E(first): load `art\s\e\setup.cmp` (u32 size + CMP data) and `palettes\setup.pal` (0x300 bytes, 6-bit VGA)
   into a new 0x72A04. If first == 0 it first copies the game palette 0x6EF20[0x710A4] into 0x710B8 (used again on
   resume). Decompresses setup.cmp **into the canvas 0x71020**.
5. 0x19720(setup.pal) (UI colours). Scan saves (0x218C6, see 2.2). Selection `sel` = 0x710B4 (last selected,
   from dlas.ds9) clamped to count-1; -1 if there are no saves.
6. Cursor image 0. Draw the save panel with full redraw (0x2199A(name, sel, 1)): this blits the whole canvas to the
   screen, sets the palette and lights the difficulty button (6 + difficulty) and the autosave button (if on).

### 1.3 Drawing — H (positions), M (art interpretation)
* Background: setup.cmp, full 640x480, in canvas 0x71020.
* **Normal (unlit) button = the setup.cmp art itself.** "Lit/pressed" = frame *b* of buttons.rle drawn **directly
  to the screen** at the frame header's (x = hdr+5, y = hdr+7), size (w = hdr+1, h = hdr+3) — 0x21FC1(b).
  "Unlit" = copy the same rect from the canvas 0x71020 to the screen — 0x21F1B(b). So buttons.rle frame i belongs to
  button i (frames 0..14; frame position from its own header, not from the hit table). — H for code, M that the
  frame rects match the hit rects below.
* Lit state is used: momentarily while an action runs (pressed), and permanently for the chosen difficulty
  (radio buttons 6..8) and for autosave (toggle 9).
* **Palette (important):** the panel draw 0x2199A loads the *selected save's* palette into 0x72A04 and sets it
  (0x19720 + DAC). setup.pal is only in effect when there are no saves. The menu art therefore has to look right
  under any game palette (M: it probably uses only shared UI colours).
* Mouse cursor image 15 while over a button, 0 otherwise (0x20FCC/0x20FD8).

Hit rectangles (tables x 0x55898, y 0x558D4, w 0x55910, h 0x5594C; s32 each; hit = x <= px < x+w, y <= py < y+h,
where (px,py) = mouse position + half the cursor size (0x559CA/2, 0x559C8/2), i.e. the cursor centre) — H:

| # | x | y | w | h | action (jump table 0x21014; handler) |
|---|---|---|---|---|---|
| 0 | 112 | 39 | 141 | 23 | **New game** (0x21109) |
| 1 | 112 | 64 | 141 | 23 | **Save** (0x211A7) |
| 2 | 112 | 89 | 141 | 23 | **Load** (0x21144) |
| 3 | 112 | 114 | 141 | 23 | **Quit** (0x21233) |
| 4 | 112 | 139 | 141 | 23 | **Resume / return to game** (0x2125C) |
| 5 | 112 | 164 | 141 | 23 | **Delete save** (0x211F8) |
| 6 | 91 | 283 | 139 | 23 | difficulty 0 (0x21282 → 0x21E85(b-6)) |
| 7 | 91 | 308 | 139 | 23 | difficulty 1 (default) |
| 8 | 91 | 333 | 139 | 23 | difficulty 2 (hardest: arcade enemies fire faster, tick countdowns shorter) |
| 9 | 42 | 447 | 287 | 22 | **Autosave** toggle (0x2128F → 0x21ECE(!autosave)) |
| 10 | 494 | 447 | 103 | 22 | **Credits** (0x2106D) |
| 11 | 332 | 328 | 99 | 23 | previous save (sel -1) |
| 12 | 494 | 328 | 99 | 23 | next save (sel +1) |
| 13 | 332 | 353 | 99 | 23 | sel -10 |
| 14 | 494 | 353 | 99 | 23 | sel +10 |

Button labels are not in the exe (they are in the art). The meaning of 11..14 is from the code (±1, ±10);
which of 11/12 is "previous" visually is M. No volume/sound/music controls exist (all 15 buttons accounted). — H

Save panel (0x2199A(name, sel, full)) — H:
* With a selection: open `savegame\<name>`, skip u16 + 4×u32 + the game-name string, read the **description**
  string into 0x71CEC, the 640x480 **screenshot** into 0x71038 and the 0x300 **palette** into 0x72A04; set that
  palette (0x19720).
* Thumbnail: every 2nd pixel of every 2nd row (x, y even) → 320x240 at **(301, 79)** in the canvas.
* Description bar: rect x 296..625, y 49..66 filled with colour 1, description drawn at **(x 301, y 52)**, font 0
  (art\short.fnt), fg = nearest grey, shadow = nearest black.
* No selection: bar row y 52..65 x 301..600 cleared to colour 1, thumbnail area filled with colour 0.
* Blit: full=1 → whole canvas; full=0 → rect (296, 49) 325x270. Then set DAC from 0x72A04. If full: light
  difficulty button and autosave button.
* There is **no text list of saves**: one save is shown at a time (thumbnail + description) and buttons 11..14 step
  through them.

### 1.4 Input loop (0x20EDA..0x213A5) — H
* Mouse only (no keys in the menu loop; keys are only read by the save-name entry).
* While the left button (0x559C2 bit 0) is down, the button under the cursor centre is taken; an action fires once
  per press (it is not repeated until the button is released: `last` = 0xFFFF while no button is down, and an
  action equal to `last` is ignored).
* Return value starts as 0xFFFD (= keep looping). Actions:

| # | behaviour |
|---|---|
| 0 New | light 0; confirm "Start a new game?" (dialog unlights 0). Yes → Load_Saved_Game("new.can") (= savegame\new.can); on success 0x714CC (map location) = 0xFF, **return 2**. |
| 1 Save | only if `first == 0` **and** not dead (0x714F0 == 0); otherwise nothing happens. Light 1; make a new file name (0x213F4, 2.3); name entry (0x21582, 2.4) into 0x71CEC; Save_Game(path). Failure → unlight 1. Success → rescan, select the new file (0x214D0), redraw panel, unlight 1. No confirmation. |
| 2 Load | only if sel != -1. Light 2; confirm "Load this saved game?" (dialog does not unlight). Yes → Load_Saved_Game(name); success → **return 2**; failure → relight difficulty, unlight 2. No → unlight 2. |
| 3 Quit | light 3; confirm "Quit and return to DOS?". Yes → **return 1**. |
| 4 Resume | only if not dead. Light 4; cur (0x71098) = 0x710AC; **return 0xFFFF**. (At start-up 0x714F0 == 1, so Resume is inert.) |
| 5 Delete | only if count > 0. Light 5; confirm "Delete saved game?". Yes → `system("del savegame\<name>")`, count--. Then rescan, keep sel (clamped), redraw, unlight 5. |
| 6..8 | unlight old difficulty button, light new, 0x714E8 = b-6. |
| 9 | toggle 0x714F8 (autosave), light/unlight 9. |
| 10 Credits | fade the menu palette out (0x1AB8C, 10 steps); 0x22424(first, 0) credits background; if OK: hide mouse (0x559C6=0), fade in, scroll credits (0x2275D), fade out, redraw setup.cmp (0x2224E), set palette, redraw panel (full); 0x559C6 = 1. |
| 11..14 | light b; sel ±1/±10; clamp (sel < 0 → 0, or -1 if no saves; sel >= count → count-1); redraw panel (partial); unlight b. |

Strings (data 0x52AC5..0x52BEB, leading spaces are for centring in the dialog): "                Start a new game?",
"              Load this saved game?", "               Delete saved game?", "             Quit and return to DOS?",
"      Click the right mouse button for \"yes\";", "                 the left for \"no\"".

### 1.5 Confirmation dialog 0x220A4(prompt, buttonToUnlight) — H
1. Hide mouse, font fg = nearest white (0x71EB5). Wait until all mouse buttons are released.
2. Save the canvas rect x 120, y 162, 400x156; draw the box 0x1330E (fill colour 0 x 120..519, y 162..317, plus a
   grey (0x71EB4) frame — same box as the change-CD prompt); text at (x 162, y 220) prompt, (162, 244) and (162, 256)
   the two "Click the right mouse button…" lines; blit the box (0x1351F).
3. Wait for any mouse button; restore the canvas rect and blit it; unlight `buttonToUnlight` unless 0xFF; relight the
   difficulty button; wait for release.
4. Returns 1 if the button word was exactly 2 (**right = yes**), else 0 (left = no). No keyboard.

### 1.6 Exit and return to the scene — H
On exit: free 0x72A04 and the buttons, free the name list (0x22A65), fill the canvas 0x71020 with 1, 0x710B4 = sel,
write dlas.ds9 (0x21E0B). Return codes and what main does:

| Game_Setup returns | main loop (0x1075D / 0x108CE) |
|---|---|
| 1 | quit: unload, leave the scene loop, exit to DOS |
| 2 (new game / load) | `run_scene(head, mode 2)` on the scene that Load_Saved_Game loaded |
| 0xFFFF (resume) | `run_scene(head, mode 2)` on the current scene (cur = 0x710AC) |
| 0xFFFD (buttons.rle missing) | treated as "next scene" → in practice ends the game (L) |

After an in-game menu, 0x18474 also resets three timers 0x71080/0x7107C/0x71074 to 0xFFFF.
run_scene mode 2 (0x10C27) skips "cur = head; cur_bg = 0xFF" and calls **Restore_Backgrounds 0x204A9**:
reload background cur_bg (0x71DAA) .CMP into the clean buffer 0x71038, canvas 0x71020 = 1, ensure the palette slot
0x6EF20[0x710A4] exists (else allocate it and copy 0x710B8 into it), draw all anims (0x22AE3 with dl=1), set palette
0x710B8, blit the work buffer 0x71028, reload DAC, UI colours 1..5 (0x13A8D), 0x71EBB = 0, show mouse. Execution then
continues at cur: for a right click in the idle loop that is the RETURN (idle again); for CONVERSE it **re-executes
the CONVERSE** (the conversation restarts from its record state); for the CD prompt it re-asks. run_scene also sets
0x714F0 (dead) = 0 at its start. — H (M for the CONVERSE consequence)

### 1.7 After DEATH (see section 5) — H
Dead flag 0x714F0 = 1 → Save and Resume do nothing. New game, Load, Delete, Quit, Credits, difficulty, autosave and
browsing work. The same is true at start-up (0x714F0 is initialised to 1 at 0x101A6) plus `first` = 1 disables Save.

### 1.8 Text/fonts summary — H
Font context 0x55209 {font, fg, shadow}: the menu uses font 0 = `art\short.fnt` (0x215A1, 0x210BD set font 0).
Description and name entry in grey with black shadow; dialog text in white with black shadow.

### 1.9 First run / start-up (main 0x106D3..0x10885) — H
* A file named **`9`** in the current directory is a run counter (u32). If it exists: read, +1, rewrite, then
  Game_Setup(first=1).
* If it does not exist (first run): work buffer = 0, 0x710A4 = 299 with an all-zero palette in slot 0x6EF20[299],
  0x710AC = cur = head of the already loaded OPENING script, description 0x71CEC = "New game", font ctx {0, 2, 0xFF};
  **Save_Game("savegame\new.can")**; on success write `9` = 1 and start OPENING directly (no menu). On failure: set a
  zero palette whose entry 2 = raw bytes (0x1E,0x78,0x50), call 0x2E2F3 (wait for a key, M) and quit (error text not
  analysed).
* So `new.can` = state at OPENING directive 0 with all globals 0, black screen, black palette; "New game" just loads it.

---------------------------------------------------------------------------------------------------

## 2. Save/load UI

### 2.1 Files — H
| file | what |
|---|---|
| `savegame\NNNNNNNN.ds9` | manual saves (8 decimal digits) |
| `savegame\<x>save.ds9` | arcade autosaves: landsave, spacsave, holosave, adr2save, plansave, cannsave, citasave, defisave, def2save, can2save (table 0x24F27 by ACTIONSEQ type) — they appear in the menu like normal saves |
| `savegame\new.can` | new-game state (written on first run; not listed since it is not *.ds9) |
| `dlas.ds9` (cwd) | options (section 4) |
| `9` (cwd) | run counter (u32) |
| `ds9.swp` (cwd) | dialogue swap file written at every scene exit (same per-dialogue format as 3.4) |

### 2.2 Listing (0x218C6) — H
`_dos_findfirst("savegame\*.ds9", attr 0)` … findnext; each file name (no path) is strdup'd into 0x71FF8[count++].
**No sorting** (DOS directory order), **no limit check** (array has 100 entries), count at 0x729F8. Number of slots
is therefore unlimited by design (practically ≤100).

### 2.3 New save file name (0x213F4) — H
Start with `savegame\00000000.ds9` and while `fopen(name,"rb")` succeeds, increment. The increment is a buggy odometer
on the digits at string offsets 16,15 (0..9 each), then 14,13,12 which roll over at '0' (i.e. after "1"), then 11:
sequence 00000000…00000099, 00001000…00001099, 00002000… (i.e. offset 14 never stays at '1'). A reimplementation only
needs "first unused name of this form"; any 8-digit name is listed. Name lookup after saving: 0x214D0 compares the
file part (from offset 9) against the list (strcmp), returns its index or 0.

### 2.4 Save-name entry (0x21582(dest = 0x71CEC)) — H
* dest = "Type a name for your saved game!!!"; the prompt **blinks** in the description bar (cleared / drawn every
  200 ms at (301,52), rect 300x14) until a key is pressed.
* Then a line editor (buffer 0x45 bytes): accepted characters = ctype class (table 0x54A80[c+1] & 0xE0, or & 4) or
  space — letters, digits and punctuation (M on exact classes); Backspace (8) deletes; Enter (10/13), 12 and **ESC (27)
  all finish** (ESC does not cancel — the game is still saved). Limits: < 0x44 (68) chars and pixel width < 294
  (glyph widths from 0x715C7 + font*0x299 + (c-0x20)*7). The first key that stopped the blinking is processed as input.
  After every key the text is redrawn at (301,52).
* The result is the save **description** (stored in the file); the file name is the generated number.

### 2.5 Errors — H
Save_Game first checks free disk space ≥ 700000 bytes (0x1F8C0 with 0xAAE60). If not: `del <file>`, play sound
`sm05`, show "INSUFFICIENT HARD DISK SPACE TO SAVE GAME" at (301,52), return 1.

---------------------------------------------------------------------------------------------------

## 3. Save file format (Save_Game 0x1EABB; Load_Saved_Game 0x1F91F) — H unless marked

All little-endian, no header magic, no version, no padding. Sizes are exact. Writer and reader orders match except
where noted.

### 3.1 Top level

| # | size | field | address | notes |
|---|---|---|---|---|
| 1 | u16 | current disc | 0x71DC4 | 0/1. Load: prompts for that CD first (0x13095); ESC aborts the load before anything changes |
| 2 | u32 | tricorder icon flag | 0x714E4 | ACTIONSEQ 0x80/0x81 |
| 3 | u32 | deck-map location | 0x714CC | ACTIONSEQ 0xA0xx; 0xFF = no map |
| 4 | u32 | tick-sound period | 0x71144 | 0xFFFF = off (ACTIONSEQ 0xF5xx/0xF0); load also loads `tick` if needed |
| 5 | u32 | tick-sound phase | 0x71140 | +4 per call of 0x2B43D; plays tick.raw when ≥ period |
| 6 | cstr | game name | 0x71E96 | argv[1] = "ds9"; load copies it to 0x71E48 and re-reads the linker files (0x17AA0) |
| 7 | cstr | **description** | 0x71CEC | user text / arcade title / "New game" |
| 8 | 307200 | screenshot | 0x71028 work buffer | 640x480 8-bit composed game screen. Used as menu thumbnail and restored as the work buffer |
| 9 | 768 | palette | write: 0x6EF20[0x710A4]; read: 0x710B8 → copied into current palette 0x710A0 | 6-bit RGB ×256 |
| 10 | u32 | palette slot | 0x710A4 | index of the background whose palette is active (299 in new.can) |
| 11 | u16 | cur_scene | 0x71DB2 | **link index (DS9.LCC order)** |
| 12 | u32 | GOSUB depth | 0x714F4 | |
| 13 | depth × u32 | GOSUB return positions | 0x7145C[i] | directive index (3.2). **Original loader bug**: reads 2 bytes per entry, then overwrites the value with fread's result (1) and resolves it against the old scene list (0x1FBB4..0x1FBDD). Files with depth > 0 are thus desynced by the original. Read u32 per entry. |
| — | | (load only) | | linker files re-read, scene `cur_scene` loaded from CC.ALL exactly like a scene entry (directives, ANIMDEF prepass, anims, sounds, labels) — but **not** its dialogue list (0x1C81F); dialogues come from the file |
| 14 | 300 × var | dialogue records | slots 0..299 = DS9.LDM index | 3.4 |
| 15 | 3000 × u16 | globals G | 0x5EBC0 | |
| 16 | u16 | current background | 0x71DAA cur_bg | index in the scene's background list |
| 17 | 300 × var | animations | slots = scene CA list index (0x6FD30) | 3.3; Restore_Animation_Status 0x20637 |
| 18 | 300 × var | hotspots | 0x6F3D0 | u8 0xFF = none, else u8 0 + 12 raw bytes {u16 x, y, w, h; u8 enabled; u16 label number; u8 cursor} |
| 19 | u32 | **resume position** | 0x710AC | directive index; load puts it straight into cur 0x71098 (Restore_Scene_Pointers 0x2094F; 0xFFFF is an error) |
| 20 | 2 × u32 | ambient sound ids | 0x706D8[0..1] | 0xFFFF = none; load restarts each with Start_Ambient_Sound (0x209E1) |
| 21 | 300 × var | timers | 0x6F880 | u8 0xFF = none, else u8 0 + 8 raw bytes {u16 reload, u16 count, u16 label number, u8 active, u8 type} |

Load prologue (before reading item 2): busy-wait one-shot sounds (0x13B78), stop ambients, 0x71144 = 0xFFFF, wait
1 s, cursor image 17, and after the CD check free the current scene/anims/sounds/dialogues (0x1E50E, 0x1E55E,
0x1BAC8, 0x1E974, 0x1E9FF, 0x1EA5D); 0x71004 = 0x70FA8 = 0.
Load epilogue: **MAPON (0x714C4) = 1** (not saved); if disc == 1 then 0x714CC = 0xFF.

**Not saved:** difficulty 0x714E8 and autosave 0x714F8 (dlas.ds9), MAPON, arcade sprite-group table 0x70C30,
carried arcade damage 0x711C4, dead flag, timers' global tick base, music. (H by absence from Save_Game.)

### 3.2 Position encoding — H
* **Directive index** (Save_Scene_Index 0x1F820 / Fetch_Scene_Directive 0x20B2D): 0-based position of the directive in
  the current scene's directive list (head 0x7113C, next at +0x29; directive 0 = SCENE). u32; NULL → 0xFFFF;
  "not found" logs an error and writes 0xFFFF. Map to "index in the parsed CC.ALL scene list".
* **Frame index** (Save_Frame_Index 0x1F780): 0-based position of a frame node from the anim's first frame (+0x16,
  next at +8; the list is circular, so a pointer not in the list would hang — never happens). u32; NULL → 0xFFFF.
* Dialogue items: (u8 group, u8 index-in-group) pairs (3.4).
* Hotspot/timer labels are label numbers (portable). Anim ANIMEND/ANIMARRV/ANIMFRAM targets are directive indexes.
* Raw structs (anim 0x34 bytes) contain pointers; the loader ignores those fields.

### 3.3 Animation slot (300×; writer 0x1ECDA..0x1EE4B, reader 0x20637) — H
| size | field |
|---|---|
| u8 | 0xFF = slot empty (reader: skip, slot left as the scene load made it), else **1 = on-the-fly (lazy) anim** (first byte of the anim's def `**0x5BE40[i]` < '0' and bit0 set, i.e. ANIMDEF p = 0/1), 0 = preloaded. Reader loads lazy anims first (0x1C155). |
| 0x34 | raw anim struct. Reader copies only: +0 x, +2 y, +4 rate, +6 rate countdown, +0xA dx, +0xC dy, +0xE speed, +0x10/+0x12 dest, +0x14 frame count (u16 each), +8 flags, +9 priority (u8), +0x22/+0x24 last drawn x/y (u16), +0x26 (u8, 0xFF until ANIMDEF), +0x27 suspended (u8). Pointers +0x16, +0x1A, +0x1E, +0x28, +0x2C, +0x30 are ignored. |
| u32 | current frame index (+0x1A) → Set_Animation_To_Frame 0x24D33 |
| u32 | last drawn frame index (+0x1E; 0xFFFF = not drawn) → 0x20A71 |
| u32 | ANIMEND target directive index (+0x28) |
| u32 | ANIMARRV target directive index (+0x2C) |
| u8 | n = number of ANIMFRAM nodes (+0x30 list, 12-byte nodes {+0 directive ptr, +4 u32, +8 next}) |
| n × (u32 + 4) | per node: directive index of +0, then the raw 4 bytes at +4 (frame number/param) |

Quirk: a slot that is empty when saved but preloaded by the scene load stays loaded after a load (M).

### 3.4 Dialogue slot (300×; writer 0x1F0D0, reader 0x1FEAB; same format in ds9.swp) — H
Record fields as in dialogue.md (record at 0x6E5C0[slot]).

| size | field |
|---|---|
| u8 | 0xFF = slot not loaded (nothing follows), 0 = present |
| cstr | dialogue name (+0x2A45). Reader loads the dialogue by name into this slot (Load_Dialogues 0x1C873) and then overwrites state |
| u16 | +0x08 current group |
| u8 ×5 | +0x0A mode, +0x0B RUDE group, +0x0C was-rude, +0x0D BACK group, +0x0E EXIT flag |
| 50 | +0x0F DONE[50] |
| 50 | +0x41 EMPTY goto[50] |
| 50 | +0x73 EMPTY reply[50] |
| 50 × var | NPC tree groups g=0..49 (+0x4BF[g]): u8 0xFF if the group is empty, else u8 n = number of top-level items (chain via +8), then n × (u8 on +0x10, u8 said +0x11). Sub-items (+0xC) are not walked. |
| 50 × var | choice tree groups (+0x587[g]), same format |
| 50 × var | DONEX[g] (+0xA5): u8 0xFF if no node, else u8 target group, u8 group, u8 index of the excluded choice item in the choice tree (0,0 + error if not found) |
| 50 × var | EMPTYX goto (+0x16D), same |
| 50 × var | EMPTYX reply (+0x235), same |
| 50 × 2 | current NPC item (+0x3C5[g]): u8 group (0xFF if NULL; search stops at group 48, so group 49 always writes 0xFF), u8 index (= +0x48D[g]) |
| 50 | +0x48D[50] item indexes |
| 50 × var | HOOK chains (+0x64F[g]): u8 count, then count × (u8 state node+0, u8 hooked group node+9) |

Reader notes: current item = head[group] advanced `index` times (NULL if group 0xFF). Hook nodes are rebuilt
(node+5 = choice head of the group). **Original bug:** Load_DoneXblock 0x20249 writes the target into the node
pointed to by the (freshly NULL) slot and never stores the excluded item — DONEX/EMPTYX nodes are effectively lost
on load and on every scene swap (they are recreated when the dialogue runs cmd 0x45/0x46 again). A reimplementation
should restore them properly. (H for code, M for impact.)

### 3.5 Sizes
Fixed part ≈ 2+16+names+307200+768+4+2+4 + 6000 + 2 + 4 + 8 bytes; total typically 320–340 KB.

### 3.6 Reimplementation mapping
* Directive pointer → index in the scene's parsed directive list (0 = SCENE). Labels → number.
* Anim slot = index in the scene's CA (animation) list; frame index = 0-based frame number.
* Dialogue slot = DS9.LDM index; item positions as (group, index).
* Background index = scene BG list index; palette slot = same index (or 299 = "none").
* To write original-compatible files: write 3.1 exactly, raw structs with pointer fields zeroed is fine.
* The screenshot is needed for the menu thumbnail; when loading, the original shows it as the work buffer and then
  redraws the background and anims on top (Restore_Backgrounds), so a reimplementation can ignore it for gameplay.

---------------------------------------------------------------------------------------------------

## 4. dlas.ds9 and setup.cfg

### dlas.ds9 (cwd; read 0x21D3F at start-up after graphics init, written 0x21E0B on every menu exit) — H
| off | size | field | default if file missing | validation |
|---|---|---|---|---|
| 0 | u32 | difficulty 0x714E8 (0..2) | 1 | outside 0..2 → 1 |
| 4 | u32 | autosave 0x714F8 | 1 | nonzero → 1 |
| 8 | u32 | last selected save index 0x710B4 | 0 | clamped when the menu opens |
12 bytes, little-endian. No volume settings anywhere in the game. 0x710B4 is also set by the arcade autosave to the
index of the file it just wrote.

### setup.cfg (main 0x102BE → 0x2CA27) — H/M
252 (0xFC) bytes read raw into 0x72C8C and handed to the graphics-driver loader 0x2C7B1 ("Can't find the graphics
driver specified in the .cfg", "Graphics Driver Error %#x"); missing → "Startup failed. Run ds9setup, and try again."
Written by the DOS installer (ds9setup). Not touched by the menu. Contents beyond the driver selection not analysed
(L: may include the sound driver). A reimplementation can ignore it.

---------------------------------------------------------------------------------------------------

## 5. Death and game over — H
* Scripts do `FADE #5` (cmd 0xA5 = fade to black) and then cmd **0x79** (labels aredead:, blewup:, dead2:).
* 0x79 (0x12DC4): dead flag 0x714F0 = 1; wait for all one-shot ASNEEDED sounds to finish; return 1.
* run_scene returns 1 → main calls Game_Setup(0) directly. There is **no separate game-over screen or text** in the
  exe: the player sees the setup menu (with the last selected save's thumbnail/palette).
* While dead: **Save and Resume are disabled** (clicks do nothing, no visual). New game, Load, Delete, Quit, Credits,
  difficulty, autosave, browsing all work. Quit → DOS. Loading/new game → run_scene mode 2 (dead flag cleared at
  run_scene start).
* Arcade deaths go through the script the same way (the arcade returns a value; the script decides).

---------------------------------------------------------------------------------------------------

## 6. Deck map ('M'/'m' with MAPON, 0x24299) — H
* Display only (no travel). Does nothing if 0x714CC == 0xFF.
* Location L = 0x714CC (set by ACTIONSEQ 0xA0xx). Map = 0 if L < 20, 1 if L < 65, 2 if L < 100, 3 if L < 150
  (else 0). Marker position (x, y) = u16 0x54BF4[L], u16 0x54D20[L].

| map | file | width × height (0x22AD3/0x22ADB) | view w × h |
|---|---|---|---|
| 0 | maps\ops.dat | 301 × 309 | 240 × 320 |
| 1 | maps\lower.dat | 393 × 767 | 240 × 320 |
| 2 | maps\upper.dat | 469 × 930 | 240 × 320 |
| 3 | maps\habitat.dat | 1101 × 883 | 320 × 240 |

* .dat = raw 8-bit pixels, row-major, width as above, no header (H by the read: fseek(width*top), read width*viewH).
* View: left = clamp(x - w/2, 0, mapW - w), top = clamp(y - h/2, 0, mapH - h) (view size clamped to the map size).
* Pixels of colour 1 are replaced with the UI grey 0x71EB4. Marker: an "X" 12 px across centred on (x, y): two
  diagonals, each 4 px wide, colours alternate 2,3 per column (palette indices 2/3, the reserved UI colours — L:
  colour-cycled, so it blinks). A 6-px grey band is drawn over the top rows of the view (and one left of the view,
  outside the blitted area — bug).
* Blitted to the screen at **(0, 480 - h)** (bottom-left), colour 1 transparent; uses the scene's palette.
* Waits for **ESC only** (other keys/mouse ignored), then redraws the screen from the work buffer 0x71028 and shows
  the mouse.

Location table (index: x, y; zero entries unused):
ops 0..10: (36,150) (58,175) (40,218) (72,217) (120,234) (180,236) (227,217) (244,158) (207,110) (145,94) (81,107);
lower 20..40: (277,72) (227,123) (198,150) (176,180) (158,210) (138,246) (110,316) (101,395) (111,472) (141,534)
(144,571) (175,601) (208,623) (266,648) (272,683) (90,618) (304,479) (254,406) (223,322) (300,278) (239,265);
upper 65..84: (284,148) (229,189) (194,222) (159,265) (125,333) (108,381) (91,429) (95,505) (107,550) (121,597)
(137,640) (160,680) (189,715) (224,747) (264,777) (328,571) (257,514) (258,403) (325,369) (375,296);
habitat 100..142: (129,755) (123,695) (118,633) (113,570) (104,509) (101,453) (157,447) (213,441) (272,438) (330,439)
(388,444) (452,452) (444,512) (436,565) (427,619) (497,630) (582,651) (572,696) (562,741) (550,790) (488,777)
(448,735) (592,605) (603,560) (654,573) (746,596) (835,629) (857,574) (877,522) (900,465) (923,408) (947,350)
(969,291) (998,228) (941,204) (884,182) (831,155) (769,131) (211,401) (265,358) (305,359) (346,359) (377,286).

---------------------------------------------------------------------------------------------------

## 7. Arcade autosave (0x26BED(type)) — H
0x710AC = cur (the **ACTIONSEQ directive itself**, so loading the autosave replays the arcade sequence); if autosave
0x714F8 == 1: path = "savegame\" + name[type] + ".ds9", description 0x71CEC = title[type] (tables 0x24F27/0x24FA7, see
arcade.md), Save_Game(path), rescan list, 0x710B4 = its index. Called for ACTIONSEQ types 4, 5, 6, 0x19, 0x1B.
