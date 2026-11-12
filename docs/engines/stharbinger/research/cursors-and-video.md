# DEEP.EXE: mouse cursors and Smacker video use

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Confidence: H = read directly from code, M = strong inference, L = guess.
Addresses are linear addresses (code object at 0x10000, data at 0x50000).
Helpers used: hd.py, fn.py (enclosing function), cvtab.py (dumps dwords with fixups).
Dumps: cv_init.asm (0x18C40..0x19480), cv_gfx.asm (mouse library 0x2CF00..0x2DA00),
cv_aseq.asm (ACTIONSEQ 0x25027..0x25E46), cv_smk.asm (0x27C40..), cv_arc.asm (arcade 0x261C1..),
cv_cred.asm and cv_scroll.asm (credits).

---------------------------------------------------------------------------------------------------

## Part 1: mouse cursors

### 1. art\mouse.ani file format (loader 0x1925F, load loop 0x19306..0x19475) — H

* The file is opened directly as `art\mouse.ani`, relative to the current directory (0x18D4E → 0x511B0).
  The path is not built with the CD path builder 0x195A1, so it comes from the hard-disk install
  directory.
* All values are little-endian.

```
u16 count                      ; must be 26 (0x1A). Otherwise "number of mouse cursors is incorrect"
                               ; is logged (non-fatal). A count > 26 is clamped to 26; a smaller
                               ; count is used as is.
repeat count times:
  u8  unused0                  ; low byte of word 0; never read (M)
  u8  transparent              ; high byte of word 0 (`movsx word; sar 8` → 0x559CC). Pixels equal to
                               ; this value are not drawn.
  u16 height                   ; +2 → 0x559C8
  u16 width                    ; +4 → 0x559CA
  u16 hot_y                    ; +6 → 0x71D8C (used only by the conversation-menu hit test)
  u16 hot_x                    ; +8 → 0x71D8E
  u16 unused10                 ; +10, never read (M)
  u8  pixels[height*width]     ; raw 8-bit palette indexes, row-major, top row first, no RLE, no padding
```

* Record pointers are stored in 0x58530[i] (malloc 12) and pixel pointers in 0x58598[i]
  (malloc w*h). The save-under buffer 0x559D1 is sized to the largest w*h.
* Width is +4 and height is +2. This is H, from the clamp and the drawing loops:
  * The outer loop runs over 0x559C8 rows.
  * The inner loop runs over 0x559CA columns.
  * The x range is 640-[+4]-1.
* **The cursors are not animated.** There is exactly one frame per cursor, with no timing data.
  The only "animation" is palette cycling of entries 1..5 (see section 2). — H
* After loading:
  * cursor 0 is active;
  * the position is (0,0);
  * the mouse range is x 0..640-w0-1 and y 0..480-h0-1 (0x2D2AA);
  * the cursor is hidden (0x559C6 = 0).

### 2. How the cursor is drawn

#### Mouse driver and position — H
* The mouse library is at 0x2CF5D..0x2D3FD, with its state at data 0x559B0..0x559E2:

  | address | meaning |
  |---|---|
  | 0x559B0 / 0x559B2 | x / y |
  | 0x559B4..BA | max x, max y, min x, min y |
  | 0x559BE / 0x559C0 | last drawn x / y |
  | 0x559C2 | button word |
  | 0x559C6 | visible flag |
  | 0x559C8 / 0x559CA | height / width |
  | 0x559CC | transparent colour |
  | 0x559CD | pixel pointer |
  | 0x559D1 | save-under buffer |

* An INT 33h fn 0Ch event handler (0x2D132, mask 0x1F) reads raw mickeys (fn 0Bh) and adds them
  1:1 to x/y. There is no acceleration of its own. The result is clamped to the range.
* While the cursor is visible, the handler waits for vertical retrace (0x2D2DD), restores the old
  save-under (0x2D062), then saves under and draws at the new position (0x2D0F7). The drawing goes
  directly into VGA memory, which is banked SVGA 640x480x8 (bank switch 0x2D44D).

#### Where the sprite is drawn — H
* **The sprite's top-left is at the mouse position (x,y).** The hotspot fields are not used for
  drawing.
* The scene hit test (0x18DAB) and ACTIONSEQ 0xC0..0xC2 use (x + w/2, y + h/2), the cursor centre.
* The conversation menu uses (x + hot_x, y + hot_y) (0x17020/0x17147).

#### Transparency — H
* The pixels are palette indexes written as is. Pixels equal to the per-cursor `transparent` byte
  are skipped (0x2CFA7 in the VGA path; 0x2CD86 with key = [0x559CC] in the buffer path).
* No colour is remapped.

#### Compositing during normal frames — H
* The renderer (0x23B32) clears the visible flag so that the interrupt handler does not draw.
* It then saves the work buffer 0x71028 under the cursor (0x2CD1E) and draws the cursor
  transparently into 0x71028 (0x2CD86). The cursor rectangle is added to the dirty list, so the
  cursor ends up blitted with the frame.
* The arcade engine does the same into 0x71020 (0x29641).
* **For a reimplementation: draw the cursor last, on top of everything, at the mouse position.**

#### Palette entries 1..5 — H
* BACKGRND with `palette` copies the RGB of entries 1..5 into 0x71EA3 (0x13A90).
* The 250 Hz AIL timer callback (0x2B409 → 0x2B48B) rotates these 5 colours while the flag 0x71ED1
  is 1:
  * the period is **300 ms** (counter 0x71404 += 4 per tick, threshold 0x12C);
  * the step is new[1] = old[5], new[k] = old[k-1];
  * it writes DAC entries 1..5 (0x2D66F(…, 1, 5)).
* 0x71ED1 is set to:
  * 1 when run_scene starts (0x10C4F) and after videos, arcade sequences and the menu;
  * 0 at start-up, in Game_Setup (0x20DBE), during ACTIONSEQ TRANS videos (0x25724) and arcade
    sequences (0x2546E).
* **It is a colour-cycling of palette 1..5.** That the cursor art uses these indexes for blinking
  lights is a guess (L). The cycling applies to anything using indexes 1..5.

#### Show / hide — H
`0x18FD3(show)`:
* show = 1 saves under and draws (0x2D2C6, 0x2D211);
* show = 0 restores the background (0x2D219).

It hides the cursor during these:
* BACKGRND (hide, then show at the end);
* the Game_Setup menu screens (many calls 0x21625..0x22204);
* deck map 0x242E3/0x24692;
* save/load 0x1EFF6/0x1F083;
* ACTIONSEQ:
  * types 0/0x1C/0x1D/1 hide it (type 1 shows it again only if the result is not 0);
  * TRANS hides it, then shows it again after;
  * death video: hide, play, show (0x2561B..0x2562A).

Several blits (0x2D562 callers) and the credits clear 0x559C6 temporarily.

#### Busy / loading cursor — H
* There is **no automatic "busy" cursor for scene loads.**
* Cursor **17** (0x11) is set while a saved game loads (0x1F9BA) and while an arcade sequence loads
  its data (0x2624F, 0x25645 after the sequence when the scene is reloaded).
* Cursor 16 "LOADING" is never set by code. It is only reachable through a HOTSPOT/HOTTYPE cursor
  byte.

### 3. Which cursor id is shown when — H

| situation | cursor | where |
|---|---|---|
| idle in a scene, over an enabled hotspot | hotspot cursor byte (+0xB) | 0x18F36 → 0x1902F |
| idle, over nothing | 0 NORMAL | 0x18FA8 |
| while an ANIMSUSPEND wait is active (0x71EB9) | 0 (forced every frame; the hit test returns 0) | 0x23E1F, 0x18DB9 |
| conversation: hovering a reply-menu entry | 15 (OFF) | 0x171F3 |
| conversation, subtitle bar drawn (0x2C0C9) | 15 | 0x2C2EC |
| conversation, otherwise | normal hotspot test (hotspots can end the conversation) | dialogue_notes |
| Game_Setup menu: over a button | 15; otherwise 0 | 0x20FCC/0x20FD8; 0x20EBB sets 0 on entry |
| saved game loading | 17 | 0x1F9BA |
| arcade sequence loading → playing | 17 → 14 GUNSIGHT | 0x2624F, 0x2631E |
| after an arcade sequence (scene reload) | 17 until the next hit test | 0x25645 |
| plain / TRANS / death videos | hidden | |
| special mode 0x71EC3 → hotspot_index+14 (≤25) | **dead code**: 0x71EC3 is only ever cleared (0x100B4) | 0x18F6B |

* 0x1902F does nothing if the id equals the current one (0x71EC5).
* On a change it hides the cursor and swaps the w/h/transparent/hotspot/pixels. It then re-clamps
  the range to 640-w-1 / 480-h-1, clamps the current position, and shows the cursor.
* Ids 18..25 are only reachable through hotspot cursor bytes. Their art is unknown (L).
* The exe's own name table (0x5512E) covers 0..16.

---------------------------------------------------------------------------------------------------

## Part 2: Smacker videos

### Common machinery — H
**Path builder 0x195A1(out, dir, name, ext):**
* It gives `<cd>:\ds9\<dir>\<name><ext>`, where the drive letter is in 0x71ECF.
* For dir "phonemes" it gives `..\`-style local paths (not video related).

**Get_Movie_File_Name 0x25EE6(type, pathset):**
* It uses three tables in the code segment (copied to the stack):
  * byte table 0x24E2F[32];
  * word table 0x24E4F[32];
  * name pointer table 0x24E8F[32].
* The directory table is 0x24F0F: **0 = "intro", 1 = "movies", 2 = "trans"**.
* The video path is `\ds9\<dir[pathset]>\<name[type]>.smk`.
* A type ≥ 32 or a pathset ≥ 3 logs "Requested illegal movie" and fails.

**The player** (Smacker SDK is statically linked):

| address | function |
|---|---|
| 0x36810 | SmackOpen(name, flags, extrabuf). extrabuf -1, retried with 0x5400 on failure |
| 0x35850 | SmackToScreen-like: smk, left, top, BytesPerLine 0x71408, window table 0x70E90, bank fn 0x72D88 |
| 0x356B0 | SmackToBuffer(smk, left, top, pitch 640, height 480, buf 0x71028, 0) |
| 0x34E30 | SmackWait |
| 0x360A0 | SmackDoFrame |
| 0x35DD0 | SmackNextFrame |
| 0x35E80 / 0x36490 | summary / SmackClose |

* **Start_Hires_Smacker_Video_To_Screen 0x27C40**(name, top=edx, left=ebx):
  * Open flags: **0 if there is no digital audio driver** (0x71424 = AIL HDIGDRIVER, 0 if absent),
    **else 0x2000 (SMACKTRACK1 = first audio track)**.
  * The video is then **drawn directly to the SVGA screen at (left, top), 1:1**.
  * A missing file logs "Can't find movie:" and returns 1.
* **Start_Hires_Smacker_Video_To_Buffer 0x27D54**:
  * It is used by the arcade engine, and decodes into the 640x480 work buffer at (left, top).
  * Audio flags are 0x2000 only for type 0x1A (cita), else 0 (no Smacker audio).
* Per frame (0x27E68):
  * if smk->NewPalette (smk+0x68): send smk->Palette (smk+0x6C, 256×RGB, written raw to the VGA DAC,
    so 6-bit values) with 0x2D66F;
  * busy-wait SmackWait (native Smacker frame rate);
  * SmackDoFrame;
  * SmackNextFrame;
  * frame counter 0x71410++.
* Done when 0x71410 == smk->Frames (smk+0xC) (0x29A15).
* Close: 0x29A7B.
* "Hires" only means the 640x480 SVGA mode. No other video mode or path exists. **The game does not
  scale.** Any Y-doubling/interlace comes from the .SMK's own header flags, handled by the SDK (L).

**plain_video 0x26104(name, left=edx, top=ebx, keep=cl)** — H:
* It zeroes both 640x480 buffers 0x71020/0x71028.
* **keep = 0**: it blits the zero buffer to the screen (black screen) first.
* **keep = 1**: the old screen stays visible around the video.
* Loop:

  ```
  while not done: frame; if key == ESC(0x1B): stop
  ```

  The key comes from 0x2E24F, the ASCII of the last key pressed.
* **Mouse clicks do not skip.**
* Return value: **0 = played to the end, 1 = skipped with ESC, 999 = file not found.**
* Nothing is restored afterwards:
  * the last video frame stays on screen;
  * **the palette stays the video's palette**;
  * the work buffers are black.
* The script redraws with `BACKGRND b, palette` (that is the pattern in the data).

### 4. Start-up sequence (main 0x10038) — H

1. 0x1925F: mouse driver check ("You do not have a mouse driver installed…") and **art\mouse.ani**
   load. This happens very early, still in text mode.
2. Version banner / argument check ("Please type: ds9"), `setup.cfg`, graphics driver.
3. CD check, with text-mode prompts (CD #1, Enter / ESC).
4. 0x18C58: **text-mode copyright/legal notice, then "<Press any key>"**, waiting for a key.
5. Graphics init 0x2CBD2. Read `dlas.ds9` (0x21D3F). Allocate buffers (0x19172). Read the linker
   files.
6. Scene loop with link index 0 = **OPENING**. There is no menu first.

The OPENING script (CC.ALL scene #23 in file order, offset 0x2DED8) does this:

```
IFE @8, #1, skip      ; global flag: intro already seen → skip the movies
ACTIONSEQ #28, v      ; DS9\INTRO\vnm.smk       at (96,72),  black screen
ACTIONSEQ #0,  v      ; DS9\INTRO\newlogmx.smk  at (0,0),    black screen
ACTIONSEQ #29, v      ; DS9\INTRO\vcp.smk       at (160,120),black screen
ACTIONSEQ #20, v      ; DS9\TRANS\dronrun.smk   at (108,40), black screen (TRANS path)
ACTIONSEQ #1,  v      ; DS9\MOVIES\ds9mix.smk   at (120,90), black screen
LET …; SOUND …; TIMER …; BACKGRND @0, palette; CONVERSE …   (the opening scene proper)
```

* Each video can be skipped individually with ESC.
* The intro does not run again when OPENING is re-entered with @8 = 1.
* The names suggest vnm = Viacom New Media logo, newlogmx = logo, vcp = second logo, ds9mix = title
  (L).
* If the videos are centred, the positions imply these sizes: vnm 448x336, vcp 320x240,
  ds9mix 400x300, newlogmx 640x480 (L).

### 5. ACTIONSEQ → video (dispatcher 0x25027) — H

**id → type:** an id below 256 is the type; otherwise type = id >> 8 (0x7142C). The result
variable defaults to 999.

**Name table 0x24E8F** (index = type). The byte and word tables 0x24E2F / 0x24E4F are parameters
of the arcade engine (see the end of this section).

| type | name | path | class | position (left,top) | screen |
|---|---|---|---|---|---|
| 0x00 | newlogmx | INTRO | plain | (0,0) | cleared |
| 0x01 | ds9mix | MOVIES | plain (special: shows the mouse and sets 0x71ED1 = 1 only if the result ≠ 0) | (120,90) | cleared |
| 0x02 | land | MOVIES | arcade | (120,12) 400x300 | — |
| 0x03 | spac | MOVIES | arcade | (120,12) 400x300 | |
| 0x04 | holo | MOVIES | arcade | (120,12) 400x300 | |
| 0x05 | adr2 | MOVIES | arcade | (80,62) 480x250 | |
| 0x06 | plan | MOVIES | arcade (also clears 0x713E4) | (80,62) 480x250 | |
| 0x07 | preplan | TRANS | plain | (108,40) | cleared |
| 0x08 | citadel | TRANS | plain | (108,40) | cleared |
| 0x09 | scyworm | TRANS | plain | (108,40) | cleared |
| 0x0A | squad | TRANS | plain | (120,12) | **kept** |
| 0x0B | canworm | TRANS | plain | (108,40) | cleared |
| 0x0C | canblow | TRANS | plain | (108,40) | cleared |
| 0x0D | inspace | TRANS | plain | (108,40) | cleared |
| 0x0E | canyon | TRANS | plain | (108,40) | cleared |
| 0x0F | defdead | — | **not accepted by ACTIONSEQ** (falls to the default: 999, 0x7142C = 0xFFFF). It is a death video, see below | | |
| 0x10 | attxeno | TRANS | plain | (108,40) | cleared |
| 0x11 | hornet | TRANS | plain | (108,40) | cleared |
| 0x12 | scystat | TRANS | plain | (80,60) | **kept** |
| 0x13 | citd | TRANS | plain | (80,62) | **kept** |
| 0x14 | dronrun | TRANS | plain | (108,40) | cleared |
| 0x15 | dronlog | TRANS | plain | (80,60) | **kept** |
| 0x16 | canwormB | TRANS | plain | (108,40) | cleared |
| 0x17 | epilogue | TRANS | plain, **then credits** | (108,40) | cleared |
| 0x18 | can2die | TRANS | plain | (108,40) | cleared |
| 0x19 | cann | MOVIES | arcade | (80,62) 480x250 | |
| 0x1A | cita | MOVIES | arcade (with Smacker audio track) | (120,12) 400x300 | |
| 0x1B | defi | MOVIES | arcade | (80,106) 480x250 | |
| 0x1C | vnm | INTRO | plain | (96,72) | cleared |
| 0x1D | vcp | INTRO | plain | (160,120) | cleared |
| 0x1E | def2 | MOVIES | arcade | (80,106) 480x250 | |
| 0x1F | can2 | MOVIES | arcade | (80,62) 480x250 | |

The position and "kept/cleared" columns are H (constants at 0x251F5.., 0x25308, 0x25739..0x257C6).
The video sizes are not in the exe; the 400x300 / 480x250 arcade rectangles come from 0x254AD..
(0x713E8 = width, 0x71400 = height of the video window).

**Plain types 0, 0x1C, 0x1D (INTRO) and 1 (MOVIES)** — H:
* hide the mouse, then plain_video(keep = 0);
* result = 0 / 1 (ESC) / 999.

**TRANS types 7..0x0E, 0x10..0x18 (pathset 2)** — H:
1. Swap the dialogue data out to ds9.swp (0x20C0D).
2. Hide the mouse; 0x71ED1 = 0 (stops the colour cycling).
3. ambient_restart 0x12F4A (stops/restarts the ambient sound).
4. plain_video.
5. Type 0x17 (epilogue) only: 0x22424(1,1), the credits (section 7).
6. Otherwise restart the two scene ambient sounds 0x706D8[0..1] (Start_Ambient_Sound).
7. Swap the dialogue back in (0x20CB3).
8. Show the mouse; 0x71ED1 = 1.
9. Result = 0 / 1 / 999.

Scripts use TRANS videos as cut-scenes and ignore the result (e.g. DEFIANT `ACTIONSEQ #24, #23`
then DEATH; HOLOSUIT chain 22, 25/13, 31/13, 12, 9, 7, 6/14, 19, 8).

**Arcade types 2..6, 0x19..0x1B, 0x1E, 0x1F (pathset 1 = MOVIES)** — H for the structure, M for
the details:
1. Unload the scene: free the background, anims and sounds (0x1D37B, 0x1C49D).
2. Load the arcade resources whose names come from Get_Movie_File_Name:

   | resource | path |
   |---|---|
   | sprites | `\ds9\sprites\<name>\*.rle` |
   | explosions | `\ds9\explode\expl*.rle` |
   | panel | `\ds9\panel\<name>stat.rle`, `\ds9\panel\rungun.rle`, `runsh.rle` |
   | music pattern | `\ds9\sound\<name>.mus` (0x1D5F9) |
   | voice | `\ds9\voice\<name>.vce` (0x1D9F8) |

3. Run 0x261C1, which plays the .SMK **into the work buffer at the rectangle above** (SmackToBuffer),
   with sprites, gunsight cursor 14, HUD and so on composited on top.
4. ESC is honoured only when the debug flag 0x71510 is set, and it is never set. **So arcade
   sequences cannot be skipped in retail.**
5. Results seen: 32768 = success, 0xFFFE (65534) / 0xFFFD (65533) = failure/death.
6. On 0xFFFE/0xFFFD: 0x714C8 = 1 and a **death video** plays (0x26846) at (0,0), keep = 1, ESC
   skippable:

   | types | death video |
   |---|---|
   | 2, 3, 4, 0x1A | TRANS\deathmix.smk |
   | 5 | TRANS\adr2dead.smk |
   | 6, 0x19, 0x1F | TRANS\death480.smk |
   | 0x1B, 0x1E | TRANS\defdead.smk |

7. Afterwards:
   1. cursor 17;
   2. reload the scene's anims and sounds (0x1BF43, 0x1D1EB);
   3. reallocate the clean-background buffer;
   4. reload the dialogue from swap;
   5. restart the ambient sounds.

   The script then issues BACKGRND itself.
8. The table at 0x24F2F (landsave, spacsave, holosave, adr2save, plansave, cannsave, citasave,
   defisave, def2save, can2save) and the titles at 0x24FAF ("Holosuite Sim: …", "Defiant vs.
   Nemesis" …) belong to the arcade engine: save names and sequence titles. Not analysed.

**Arcade parameter tables** — M/L:
* byte 0x24E2F[type]: values 100 or 200 (types 2..4 = 200). It is compared with the video frame
  counter (0x26435), so it is probably the frame at which gameplay starts (L).
* word 0x24E4F[type], in type order:

  ```
  69, 1135, 1500, 1501, 2200, 1350, 1225, 240, 220, 357, 130, 156, 48, 100…(14–24), 919, 361, 579, 100, 100, 596, 481
  ```

  These look like frame counts / end frames (L).

Other ACTIONSEQ types (no video) are as in interpreter_notes:
* 0x80/0x81 tricorder icon;
* 0xA0xx → 0x714CC;
* 0xC0/0xC1/0xC2 draw effects at the cursor centre or the SETRECT hotspot;
* 0xC3 no-op;
* 0xF0 / 0xF5xx countdown.

### 6. Playback details (summary) — H unless marked

* **Position:** the video is drawn at its native size at the fixed top-left given in the table.
  There is no scaling and no automatic centring.
* **Background:** the screen is first cleared to black when keep = 0. When keep = 1 (types 0x0A,
  0x12, 0x13, 0x15, death videos), the current picture stays around the video.
* **Palette:** the Smacker palette is written straight to the DAC whenever the video's palette
  changes. It is **not restored** after; the script's next `BACKGRND x, palette` restores it.
  * For keep = 1 videos, the kept background is shown with the video palette, so presumably the
    video shares the scene palette (L).
* **Colour cycling:** off (0x71ED1 = 0) during TRANS and arcade.
* **Audio:**
  * **Plain videos play the Smacker file's own (first) audio track**, opened with 0x2000 when a
    digital driver exists (Smacker audio through MSS, 0x37D80 = Smacker sound init with the AIL
    driver).
  * Arcade videos are silent (except cita/0x1A) and use `\ds9\sound\<name>.mus` pattern music plus
    `\ds9\voice\<name>.vce` and raw effects (shake.raw, quantum.raw, phasor.raw, bang0x…).
  * The scene ambient sound is stopped and restarted around TRANS videos.
* **Timing:** SmackWait busy-waits, so playback runs at the file's frame rate.
* **Skip:** ESC only, and only for plain and death videos. Clicks are ignored. Arcade sequences
  cannot be skipped.
* **Mouse:** hidden during plain, TRANS and death videos.
* **After a video:** nothing is redrawn by ACTIONSEQ; the last frame stays. Scripts follow with
  BACKGRND/NEWSCENE/CONVERSE/DEATH.

### 7. Credits and other full-screen sequences — H

**Display_Credits_Background 0x224BF(usepal, dir, name):**
* It loads `\ds9\<dir>\<name>.cmp`. The format is u32 size + compressed 640x480 image, the same
  .CMP codec as backgrounds (0x2C523).
* It loads `\ds9\palettes\<name>.pal` (768 bytes) into 0x72A04.
* It blits the image to the screen **without setting the palette**. The caller fades in.
* Error strings: "Display_Credits_Background: can't find …".

**Credits 0x22424(a, withBlank):**
* It saves the work buffer to 0x71038.
* If withBlank:
  1. show `art\b\l\blankcrd.cmp` (+ palettes\blankcrd.pal);
  2. fade out (0x1AB8C, 10 steps × 50 ms).
* It then shows `art\c\r\credits.cmp` (+ credits.pal) and returns 1 on success.

The callers:
* epilogue (ACTIONSEQ 0x17) calls 0x22424(1,1);
* the Game_Setup "credits" button calls it with withBlank = 0 (0x21084).

Both then do this:
1. Hide the mouse.
2. Fade in to 0x72A04 (0x1ACA6, 10 steps).
3. Set the full palette.
4. **Scroll the credits** (0x2275D).
5. Fade out.
6. Show the mouse.

**Scroller 0x2275D:**
* There are 269 (0x10D) text lines from the pointer table 0x55464. A leading `*` marks a heading,
  drawn in colour 0xFD; normal lines use colour 7. Font struct 0x55209 (font 1, shadow 0xFE).
* Each line is centred in the text window x = 258..638 (x = (640-w)/2 + 128).
* The window is (258,24) 380x432. 19 lines are visible, the line pitch is 24 px, and the scroll
  step is 4 px.
* Each step restores the clean background and redraws the lines; the window is copied to the
  screen with 0x2D49B. There is no explicit delay (L: paced by the blit/retrace).
* ESC stops it, and so does running past the last line.

**Game_Setup** (0x20D54, menu), not videos:
* background `art\s\e\setup.cmp` + palettes\setup.pal;
* buttons `art\buttons.rle`.

The CMP directory naming is `art\<1st letter>\<2nd letter>\` (M).

**Others:**
* The deck map 0x24299 (`maps\*.dat`).
* The 35-pixel subtitle bar at the top (0x2C0C9, during speech).
* Arcade HUD.
* None of these use Smacker.
