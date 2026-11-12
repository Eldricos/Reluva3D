# DEEP.EXE arcade ("action sequence") engine

Confidence: H = read directly from code, M = strong inference, L = guess.
Addresses are linear (code object at 0x10000, data at 0x50000). In hd.py output a data operand prints as
`0x21xxx`; the fixup note (`->0007xxxx`) gives the real address, which is the one used here.
Dumps written for this note: ar_load.asm (0x19ED8..0x1AA51), ar_mus.asm (0x1D5F9..0x1DB00),
ar_main.asm / ar_c.asm (0x25E46..0x2A200; ar_c.asm is the compact version).

## Function map

| addr | name (mine) | notes |
|---|---|---|
| 0x25027 | ACTIONSEQ | arcade branch 0x25351..0x256A6 |
| 0x25E46 | Clear_Arcade_Stats(type) | zeroes the 9-dword record 0x70230+type*0x24 |
| 0x25EE6 | Get_Movie_File_Name | builds every arcade path (see 1.2) |
| 0x261C1 | Run_Arcade | load, main loop, unload; returns the result |
| 0x26700 | Music_Play_Segment(loop) | |
| 0x267B8 | Music_EOS_Callback | moves to the next .MUS segment |
| 0x26846 | Play_Death_Video | with "death static" (STAT.RLE) while it loads |
| 0x269BE | Music_Stop_All | sets 0x71454 = 1 |
| 0x26A77 | Death_Static_Timer | 200 Hz timer, period 50 |
| 0x26B6A / 0x26BED | Arcade autosave | savegame\<x>save.ds9 |
| 0x26C46 / 0x26D4A | Save / restore sprite-group state | carries defi into def2 |
| 0x26DF9 | Torpedo_Setup_Stage | |
| 0x27136 | Torpedo_Direction_Frame(dx,dy) | returns 0..9 |
| 0x27215 | Draw_Phaser_Beam | |
| 0x273E4 | Draw_Enemy_Beam | |
| 0x27D54 | Start_Hires_Smacker_Video_To_Buffer | |
| 0x27E68 | Video_Frame_Plain | used by death videos |
| 0x27EE5 | Arcade_Video_Frame | |
| 0x27F94 | Copy_Video_With_Shake | |
| 0x2807F | Arcade_Logic | timer 1, once per video frame |
| 0x29484 | Arcade_Draw_HUD | timer 2, once per video frame |
| 0x29641 | Arcade_Compose_And_Blit | |
| 0x297FF | Destroy_Sprite | |
| 0x2997B | Sprite_Screen_Pos | |
| 0x299D7 | Flash_Palette_12 | |
| 0x29A15 | Video_Done | |
| 0x29A7B | Video_Close | |
| 0x29AC7 | RLE_Draw_Clipped_To_Video_Rect | 9 clip variants 0x29C91..0x2A801, plus 0x2A98F unclipped |
| 0x19F6C | Load_Death_Static | file not found: "death static file not found" |
| 0x19FFC | Load_Shield_Indicator | |
| 0x1A096 | Load_Gun_Indicator | |
| 0x1A130 | Load_Windshield_Overlay | |
| 0x1A1BA | Load_Sprites | |
| 0x1A3C2 | Load_PDoor | |
| 0x1A4FC | Load_Explosions | |
| 0x1A7FC | Load_Torpedos | |
| 0x1A9C7 | Read_RLE_Header | returns the frame count |
| 0x1AA51 | Load_RLE_Frames | |
| 0x1AB2F | Free_Frame_List | |
| 0x1D40F / 0x1D4CD | Music tables alloc / free | |
| 0x1D5F9 | Load_Music_Pattern_File | |
| 0x1D753 | Load_Music_Samples | |
| 0x1D975 / 0x1D9F8 / 0x1DB78 / 0x1D8B6 | Voice cue alloc / load .VCE / load samples / free | |
| 0x1D084 | Load_One_Sound(name, &ptr, bits) | `\ds9\sound\<name>.raw`, 22050 Hz; bits = 8 or 16 |
| 0x143A0 | Start_Sound(data, debugname) | one-shot; returns the handle |
| 0x2B776 | Timer_Create(period, fn) | 200 Hz software timer list |
| 0x2B58E | Timer dispatcher | AIL timer at 200 Hz (0x2B3CB) |
| 0x2B7D3 / 0x2B83C | Load / free the 8 effect sounds | |

## Timer model — H

* There are two AIL timers.
  * 0x2B409 runs at 250 Hz. Each tick adds 4 to the ms counters 0x7143C (reset every video frame),
    0x71094 and 0x7108C (reset when the arcade loop starts).
  * 0x2B58E runs at 200 Hz. It walks the timer list 0x54E94. For each node with enabled
    (+0x18) ≠ 0:
    * if counter (+0x14) ≥ period (+0x10), it resets the counter and calls fn (+8);
    * otherwise it increments the counter.
* The arcade creates two nodes with period 0, so each fires on the next 5 ms tick after it is
  enabled:
  * T1 = 0x2807F (logic), handle 0x71430;
  * T2 = 0x29484 (HUD, cursor and blit), handle 0x71434.
* The handshake, once per video frame:
  1. 0x27EE5 decodes a frame and then waits until T2 is idle.
  2. It copies the video into the compose buffer and enables T1.
  3. T1 ends by disabling itself and enabling T2 (0x29453..0x29476).
  4. T2 ends by disabling itself (0x297E3).
* **So the logic and HUD run exactly once per video frame.** In a reimplementation they become
  plain sequential calls.

---------------------------------------------------------------------------------------------------

## 1. Overall flow

### 1.1 ACTIONSEQ arcade branch (0x25351..0x256A6) — H

1. Clear globals: 0x713F0 = 0 (door destroyed). For type 6, 0x713E4 = 0 (door damage).
2. **Autosave.** Types 4, 5, 6, 0x19 and 0x1B (not 2, 3, 0x1A, 0x1E, 0x1F) call 0x26BED(type) and
   then 0x22A65. If option 0x714F8 == 1 (a dlas.ds9 field, default 1), this saves to
   `savegame\<x>save.ds9`. The names come from table 0x24F27 and the titles from 0x24FA7, both
   indexed by type (decoded in section 8).
3. Get_Movie_File_Name(type, pathset 1). If it fails, the result stays 999.
4. 0x20C0D (swap the dialogue out); free buffer 0x71038; 0x1D37B and 0x1C49D (unload the scene's
   anims and sounds).
5. Load the overlay files:

   | function | file | stored in | when |
   |---|---|---|---|
   | 0x19F6C | `<name>stat.rle` | 0x7102C, count 0x71048 | always |
   | 0x1A130 | windshield overlay | 0x71040 | type ≠ 5 |
   | 0x19FFC | shield indicator | 0x71044, count 0x71024 | type ∉ {5, 0x1B, 0x1E} |
   | 0x1A096 | gun indicator | 0x7103C, count 0x71034 | type ∉ {5, 0x1B, 0x1E} |

6. Clear_Arcade_Stats(type). Set 0x71ED1 = 0 (palette cycling off). 0x71438 = type.
7. Run_Arcade 0x261C1 with the parameters below. The 10th stack argument (keep) is 1, so the
   screen is not cleared.

   | types | rectangle (x, y, w, h) |
   |---|---|
   | 2, 3, 4, 0x1A | (120, 12, 400, 300) |
   | 0x1B, 0x1E | (80, 106, 480, 250) |
   | 5, 6, 0x19, 0x1F | (80, 62, 480, 250) |

8. Music_Stop_All. Free the overlays as they were loaded.
9. **If the result is 0xFFFE or 0xFFFD:** set 0x714C8 = 1, hide the mouse, Play_Death_Video
   (0x26846), show the mouse.
10. Free the death static. 0x71ED1 = 1. Cursor 17. Reload the scene's anims and sounds; allocate
    0x71038 (0x4B000 bytes); swap the dialogue in; restart the 2 ambient sounds.
11. The result goes to the script variable: `G[v] = result`.

### 1.2 Paths from Get_Movie_File_Name 0x25EE6 — H

`name` = table 0x24E8F[type]. `run` = table 0x24F1B[1]; that table holds 0 = "def", 1 = "run",
2 = "ops", and the arcade always passes 1.

| path | use |
|---|---|
| `<cd>:\ds9\movies\<name>.smk` | the video |
| `<cd>:\ds9\sprites\<name>\*.rle` | wildcard for findfirst/findnext |
| `<cd>:\ds9\sprites\<name>\` | prefix for each sprite file |
| `<cd>:\ds9\explode\expl*.rle` | wildcard |
| `<cd>:\ds9\explode\` | prefix |
| `<cd>:\ds9\panel\<name>stat.rle` | **"death static"**: the error text at 0x5129D says so, and only the death-video timer draws it |
| `<cd>:\ds9\panel\rungun.rle` | **gun status indicator**: loaded but **never drawn** (no read of 0x7103C besides the free) |
| `<cd>:\ds9\panel\runsh.rle` | **shield status indicator** (error text 0x512BB) |
| `art\<name>run.rle` | **"windshield overlay"** (error text 0x51308) |
| `<name>` (copied twice) | .MUS base name and .VCE name |

* The overlay path is relative, so it is read from the **hard-disk install directory**, like
  art\mouse.ani. Examples: art\landrun.rle, art\holorun.rle.
* It is not in the CD listings. Whether these files exist in the user's install is **unknown**.
* The scripts show backgrounds with the same names (`BACKGRND holorun / citarun / cannrun /
  planrun / defirun, palette`) just before ACTIONSEQ. So the overlay is presumably the parts of
  that cockpit picture that lie over the video (M).

### 1.3 Run_Arcade 0x261C1(smk, spriteWildcard, spriteDir, explWildcard, explDir, musName, vceName, startFrame, endFrame, w, h, x, y, keep)

The steps below are H.

1. 0x71444 = 0 (win countdown). Local voice-damage flags[20] = 0. Result = 0.
2. If keep == 0, clear the screen. Copy the whole work buffer 0x71028 to the compose buffer 0x71020.
   * So the scene picture shown with BACKGRND stays visible around the video.
3. Cursor 17. Then load, in this order:
   1. Load_Explosions;
   2. Load_Torpedos;
   3. Load_Sprites → count 0x7141C.
4. Type-specific set-up:
   * type 0x1E: restore the sprite groups (0x26D4A, section 7);
   * types 0x1E, 0x1F, 0x1A: **initial damage = previous arcade's final damage / 3**
     (0x711C4 / 3 → 0x7024C);
   * type 6: Load_PDoor.
5. Load the 8 effect sounds (0x2B7D3, table 0x2AC57):
   * BANG01..05 → 0x706B8[0..4];
   * QUANT → 0x706CC;
   * PHASOR → 0x706D0;
   * SHAKE → 0x706D4.

   All are 16-bit, from \ds9\sound\.
6. Music: 0x710BC = 0xFFFF, then alloc. If Load_Music_Pattern_File(name) succeeds, load the
   samples. Then voice: alloc, Load .VCE, and load the cue samples if the .VCE exists.
7. Cursor 14 (GUNSIGHT). Torpedo slots 4 × state 0xFF. 0x7143C = 0.
8. Start_Hires_Smacker_Video_To_Buffer(smk, x, y). Smacker audio track 1 is used only for 0x1A;
   the video is decoded into 0x71028 at (x, y), pitch 640. **On failure the result is 999**
   (0x3E7) and the sequence is skipped.
9. Store the rectangle:

   | global | value |
   |---|---|
   | 0x713F8 | left = x |
   | 0x713F4 | top = y |
   | 0x713E8 | W |
   | 0x71400 | H |
   | 0x711C8 | right = x + W |
   | 0x711C0 | bottom = y + H |
   | **0x710C4** | **launchY = 3·H/4** (integer, sar 2 of 3H): 225 for H = 300, 187 for H = 250 |

   Also 0x71ECB = 0 and 0x71ECD = 0 (shake), then create timers T1 and T2. Music segment index
   0x711BC = 0; Music_Play_Segment(loop = 1). 0x7108C = 0.
10. **Main loop** (0x2641C):

    ```
    while (!Video_Done() && result == 0):         // Video_Done: frame 0x71410 == smk->Frames
        Arcade_Video_Frame()                       // decode, compose, logic, HUD, SmackWait, frame++
        if frame == startFrame:                    // table 0x24E2F: 200 for types 2..4, else 100
            tagFrame = endFrame - (elapsed_ms * MUSVALUE)/10000 + endFrame/40     // 0x710C0
        if frame == tagFrame:                      // play the music "tag" once (see 5.1)
            Music_Stop_All(); seg = tagIndex(0x710BC); Music_Play_Segment(loop = 0)
        voice cues (5.2)
        damage = stats[type].dmg (0x7024C)
        if damage >= 100: result = 0xFFFE
        else if damage/5 != prev/5 and damage > prev and flags[19 - damage/5] == 0:
            flags[19 - damage/5] = 1
        prev = damage
        if winCountdown (0x71444): if --winCountdown == 1: result = 0x8000
    ```

    * endFrame = word table 0x24E4F[type]. elapsed_ms = 0x7108C.
    * ESC (only with the debug flag 0x71510, which is never set) gives result 1 and 0x714C8 = 1.
11. After the loop:
    * type 6 with result still 0: result = 0x8000 if the door was destroyed (0x713F0 == 1),
      else 0xFFFD.
    * Then: Music_Stop_All; free T1 and T2; close the video.
12. Save the sprite groups (0x26C46). 0x711C4 = final damage. Free voice, music and effects; free
    PDoor (type 6), sprites, torpedoes and explosions. Return the result.

### 1.4 Results — H

| value | meaning |
|---|---|
| 0x8000 (32768) | **won**: a sprite with id ≥ 200 (the "boss") was destroyed, and 11 frames later the loop ends; or, for type 6, the door was destroyed by the end of the video |
| 0 | **video ended, player alive, boss not killed** (all types except 6) |
| 0xFFFE (65534) | damage ≥ 100: killed. Death video follows |
| 0xFFFD (65533) | type 6 only: video ended with the door intact (crash). Death video follows |
| 999 | video missing |

* The scripts treat anything but 65534 (and 65533 for plan) as survival:
  * HOLOSUIT `IFNE $ret, #65534, >survived`;
  * DEFIANT: 32768 from defi skips def2, otherwise def2 is played;
  * plan: 32768 → surv, 65533 → crashed.

  (M; from the script listings.)

### 1.5 Death video 0x26846 — H

1. Music_Stop_All. 0x71030 = 0 (static frame index), 0x7104C = 0. Create a timer with period 50
   (fires every 51 × 5 ms = 255 ms) running 0x26A77, and enable it.
2. Pick the video:

   | types | video |
   |---|---|
   | 2, 3, 4, 0x1A | `trans\deathmix.smk` |
   | 6, 0x19, 0x1F | `trans\death480.smk` |
   | 5 | `trans\adr2dead.smk` |
   | 0x1B, 0x1E | `trans\defdead.smk` |

3. Play it to the screen at (0, 0) (0x27C40, with Smacker audio) until done, or ESC.
4. **While the video opens**, each timer tick draws the next frame of `<name>stat.rle` (cycling
   through all of them):
   * at the frame header's own x, y (absolute screen coordinates);
   * into 0x71020, then blits that rectangle to the screen.

   The first decoded video frame sets 0x7104C = 1 (0x27E9C), and the timer then turns itself off.
   **So STAT.RLE is the TV-static shown between the fatal hit and the start of the death video.**

---------------------------------------------------------------------------------------------------

## 2. Targets (sprites)

### 2.1 Loading 0x1A1BA — H

* It reads **all `*.rle` files in `sprites\<name>\`**, in findfirst/findnext order. On ISO9660 CDs
  that is sorted by name (M). At most 150.
* For each file, a 20-byte record goes to 0x70730[i], and the frame list goes to 0x709D8[i].
* **The file name is never parsed.** Everything comes from the file's 8-byte header, which
  [../formats/animations.md](../formats/animations.md) calls a magic. Scene anims have `00 00 01 01 0A FF 00 00` there, i.e. start 0, hp 1,
  dmg 1, period 10, gun 0xFF (never fires), id 0.
* The digits in names like CAN2601A / LANDL02 are only the artists' naming. In the
  3-digit-plus-letter sets the number probably is a shot or scene number. L/S/W/R are probably
  left/right/… (L). **None of this matters to the engine except the load order.**

Sprite file layout:

```
u16  start      ; +0x00  video frame at which frame 0 is shown
u8   hp         ; +0x0C  hits needed (phaser hit = 1, torpedo = 2 for id>=200, instant kill for id<200)
u8   dmg        ; +0x0E  damage per enemy shot, added to player damage (0..100 scale)
u8   period     ; +0x0F  fire period in video frames (adjusted by difficulty)
u8   gun        ; +0x10  0xFF = never fires; 2 = alternate muzzle 0/1 each shot; else fixed muzzle (+0x11, starts 0)
u16  id         ; +0x02  group id / fire delay, see below
u16  frameCount
frames: 25-byte header + data ([../formats/animations.md](../formats/animations.md)). Header fields used here:
   +0 u8 (unused)  +1 u16 w  +3 u16 h  +5 s16 x  +7 s16 y
   +9 s16 muzzleX[3]  +0xF s16 muzzleY[3]  (offsets from the frame's top-left; "12 unknown bytes")
   +0x15 u32 dataLen
```

In-memory record fields:

| offset | meaning |
|---|---|
| +4 | "draw" frame node |
| +8 | "path" frame node; both start at the head |
| +0xD | hits taken |
| +0x11 | muzzle index |
| +0x12 | state: 0 alive, 1 exploding, 2 gone |
| +0x13 | group-destroyed flag |

**id semantics** (0x287DB, 0x28E94, 0x2934B, 0x297FF):

| id | effect |
|---|---|
| < 100 | independent target; it may fire once frame > start + id |
| ≥ 100 | **group**: every sprite with the same id shares damage. A hit on one adds the same +1 (phaser) or +2 (torpedo) to all the others' +0xD. Fire delay = id − 100 |
| > 100 | when one member is destroyed, all other members get state 2 (vanish) and +0x13 = 1 |
| ≥ 200 | **boss group**: torpedoes do only 2 "hits" instead of killing instantly, and destroying it starts the win countdown (0x71444 = 12 → result 0x8000 after 11 more frames) |
| ≥ 100 | the state is saved across sequences (section 7) |

Note: for ids from 200 up the fire delay is id − 100, so at least 100 frames.

### 2.2 Scheduling and drawing (Arcade_Logic 0x2865F..0x28B1A) — H

For each sprite i in load order, while 0x709D8[i] ≠ 0 and i < count:

1. Skip it if state == 2 or start > frame. The frame used is the 0-based index of the frame just
   decoded.
2. Position: `(x, y) = (path.hdr.x + left + shakeX, path.hdr.y + top + shakeY)` (0x2997B).
   **The RLE header x/y are relative to the video rectangle's top-left.**
   * w, h are the draw frame's (+4).
   * If exploding (state 1), centre the explosion: x −= (w − path.w)/2, y −= (h − path.h)/2.
3. If alive and the gunsight point is inside x ≤ gx < x+w, y ≤ gy < y+h (0x2877B), set
   `targetUnderSight` (0x71ECE) = i. **The last sprite in order wins.**
4. Draw the draw-node frame at (x, y) into 0x71020, clipped to the video rectangle (0x29AC7).
   Transparency comes from the RLE skip codes.
5. Enemy fire (2.3).
6. **Animate:** draw = draw.next; path = path.next.
   * If either becomes null, state = 2 and stats.gone++.
   * **One sprite frame per video frame:** sprite frame k is shown on video frame start + k.
   * The sprite disappears when its frame list ends. A sprite that exits without being destroyed
     is not penalised, beyond the shots it fired.

The draw order is load order, so later files are drawn on top.

### 2.3 Enemies shooting back — H

Conditions, all required:
* state == 0;
* door not destroyed (0x713F0 == 0);
* win countdown not running (0x71444 == 0);
* start + delay < frame (delay = id, or id − 100 when id > 100);
* gun ≠ 0xFF;
* (frame − start) % P == 0, where P = max(1, period + 1 − difficulty).

Difficulty is 0x714E8, 0..2 (default 1) from dlas.ds9 and the Game_Setup buttons 6..8. So
difficulty 2 fires faster.

Then:
1. Muzzle = draw-frame header muzzleX/Y[mi], with mi = +0x11.
   * If both are 0, there is no shot.
   * The muzzle point (x + mx, y + my) must lie inside the video rectangle.
2. If no shake is running (0x71ECB == 0), play SHAKE (0x706D4).
3. **Damage at once** (there is no projectile to dodge):
   * stats.totalDmg += dmg;
   * stats.dmg (0x7024C) += dmg;
   * stats.hitsTaken++.
4. Draw the enemy beam 0x273E4 into 0x71020:
   * from the muzzle (x+mx, y+my) to (cx, bottom − 6);
   * cx = x + mx clamped to [left + W/8, right − W/8];
   * colour 1, or 7 when dmg > 10. Algorithm in section 6.
5. Shake: 0x71ECB = 4; 0x71ECD = (min(dmg,3) − 1)·2 + (0x7143C & 1). Section 3.5.
6. If gun == 2, toggle +0x11 (mi ^= 1).

### 2.4 Hits and destruction — H

**Hit test = axis-aligned bounding box of the current frame (header w/h), not a pixel mask.**

* **Phaser** (left button, 3.2):
  1. If targetUnderSight ≠ 0xFF and that sprite's state == 0: stats.phaserHits++, +0xD += 1.
  2. If +0xD ≥ hp: Destroy, and stats.kills++.
  3. Otherwise, if id ≥ 100, every other sprite with the same id gets +0xD += 1.
* **Torpedo impact** (fourth tick after firing, 3.3): the impact point is the gunsight point at
  firing time (slot +1/+3).
  1. **The first sprite in load order** with state 0 and start < frame whose rectangle contains it
     is hit. That rectangle uses the draw frame's hdr x/y + left/top, without shake, and
     x ≤ px ≤ x+w.
  2. stats.torpHits++.
  3. If id ≥ 200: +0xD += 2; if ≥ hp, Destroy and kills++; otherwise all same-id sprites get
     +0xD += 2.
  4. If id < 200: Destroy at once, and kills++.
* **Destroy_Sprite 0x297FF(s, w, h):**
  1. state = 1.
  2. half = nExpl/2. The explosions are sorted by name, so with the retail files 0..4 = EXPLLG01..05
     and 5..9 = EXPLSM01..05.
  3. r = half + random(half).
  4. If the area w·h of **frame 4** (the 5th frame) of EXPLSM[r] ≥ w·h of the target, use the
     **small** explosion EXPLSM[r]; otherwise use the **large** EXPLLG[r − half]. This is the
     "small enough?" test.
  5. Play BANG0(r − half + 1) (0x706B8[r − half]) in both cases.
  6. The draw node becomes the explosion's first frame. The path node keeps following the sprite's
     path, so the explosion moves with the target, centred on it (2.2). It ends with either list.
  7. If id > 100, apply the group effects; if id ≥ 200, set the win countdown (2.1).
  * Bug (M): in the phaser path, the w/h passed are those of the last sprite iterated in the draw
    loop, not the hit sprite's (locals [ebp-0x64]/[ebp-0x3c]). This only affects the small/large
    choice.
* Score: there is **no score shown**. The stats record is only written, never read (see 4.4).

---------------------------------------------------------------------------------------------------

## 3. Player

### 3.1 Gunsight and mouse — H

* Cursor 14 (GUNSIGHT) is set after loading.
* **The aim point is the cursor centre:** (mouseX + curW/2, mouseY + curH/2) (0x280B0).
* The mouse is read from the mouse-library globals (0x559B0/B2 position, 0x559C2 buttons) once per
  logic tick. Edge detection uses the previous button word 0x71DAE, saved at the end of each tick.
* Button bit 0 = left = **phaser**; bit 1 = right = **torpedo** (INT 33h bit meaning: M-H).
* Shots fire only when the aim point is inside the video rectangle.
* **There are no keyboard controls in retail** (ESC works only with debug flag 0x71510).

### 3.2 Phaser (left click) — H (0x29261..0x29447)

* Needs a new left press, the aim point in the rectangle, and no torpedo fired in this tick.
* stats.phaserShots++. Hit and damage as in 2.4.
* **Two beams** (0x27215, colour index 0x12 = 18):
  * from the lower-left launcher (left, top + launchY) to the aim point;
  * from (right − 1, top + launchY) to the aim point.
* Play PHASOR (0x706D0).
* No ammo and no cooldown. The rate is one shot per click, at most one per video frame.
* The beam is drawn only on the tick of the click, so it is visible for 1 video frame.

### 3.3 Torpedoes (right click) — H (0x28B30..0x2925C, 0x26DF9)

* Not available in type 5 (adr2: 0x28B23 skips the whole torpedo block).
* Needs a new right press, the aim point in the rectangle, and a free slot. There are 4 slots
  (0x711D0 + i·0x85; byte +0 = state, 0xFF = free). With no free slot, nothing happens.
* stats.torpVolleys++. Slot target = aim point. state = 0. Play QUANT (0x706CC).
* For each stage k = 0..3 and each side (L, R):

  ```
  dyL = aimY - (top + launchY)
  side L: dx = aimX - left;        side R: dx = aimX - (right - 1)
  dir   = Torpedo_Direction_Frame(dx, dyL)     // 0..9, per side
  mult  = {0, 6, 11, 15}[k]                    // sixteenths of the way (k=0 uses just the +8 rounding)
  half  = {29, 17, 9, 5}[k]
  ox = trunc((dx*mult + 8)/16), oy = trunc((dyL*mult + 8)/16)      // toward-zero /16
  L: X = ox + left
  R: X = ox + right - width(frame0 of that stage's R file)
  Y1 = oy - half + top + launchY ;  Y2 = oy + half + top + launchY
  frame = file[k] node #dir, for k < 3 (T29x, T17x, T09x); T5ALL node 0 for k = 3
  ```

* **Each launcher fires a vertical pair** at (X, Y1) and (X, Y2). The pair converges because half
  shrinks, flying from the lower corners toward the aim point. That gives 4 torpedo sprites per
  volley (H for the coordinates; "pair" is M).
* The files are T29L/T17L/T09L/T29R/T17R/T09R (array 0x70EA0 in the order of table 0x18D5A:
  t29l, t17l, t09l, t29r, t17r, t09r) and T5ALL (0x71414). **The number is the size class:**
  29, 17, 9 and 5 px are the half-gaps, and probably also the sprite sizes (M).
* **L/R = fired from the left or right launcher.** Each L/R file holds ≥ 10 frames, one per
  direction bucket (M; 0x26DF9 skips dir nodes).
* Flight, per tick:
  * states 0..3: draw the 4 sprites of the stage, with shake added, clipped. Then state++.
    * Quirk: the 4th sprite (R lower) uses the R node pointer at +0x55, not +0x75; they are the
      same pointer anyway.
  * state 4: impact (2.4), then the slot is freed.
  * **Visible for 4 frames; it hits on the 5th.**
* **Torpedo_Direction_Frame 0x27136(dx, dy):**
  * if |dx| < 58 and |dy| < 58 → 9;
  * otherwise s = −(dy·128)/|dx| (0xFFFFFF if dx = 0), and the result is 0 if s < −54, 1 if < −17,
    2 if < 17, 3 if < 54, 4 if < 101, 5 if < 175, 6 if < 350, 7 if < 1000, else 8. So 0..8 go
    from "aim below / level" to "steeply up" (dy < 0 means up).

### 3.4 Damage, regeneration, HUD — H

* **Player damage** is stats[type].dmg (0x7024C), 0..100+. Death comes at ≥ 100. There is no
  separate health; shield = 100 − damage.
* **Regeneration** (0x2861A): if damage ≠ 0 and frame % R == 0, damage −= 1.
  * R = 13, or **10 when difficulty == 0**.
* **Shield indicator** (RUNSH.RLE, n frames; T2 0x2950B):
  * Drawn for types 2, 3, 4, 6, 0x19, 0x1A, 0x1F. Not for 5, 0x1B or 0x1E.
  * Frame index = max(min(damage·n/100, n), 1) − 1, except every 10th video frame
    (frame % 10 == 9), which draws frame 0. That makes a blink.
  * Drawn unclipped at the frame header's own **absolute screen x, y**, into 0x71020, and that
    rectangle is blitted to the screen at once. It sits on the cockpit panel outside the video.
* **RUNGUN.RLE**: loaded, never displayed. RUNSH and RUNGUN are both 1143 bytes.
* **Windshield overlay** `art\<name>run.rle` (types ≠ 5): every tick **all frames of the file are
  drawn**, each at its own header x, y in absolute screen coordinates, clipped to the video
  rectangle (0x295F1). It is a static multi-piece overlay, e.g. cockpit struts over the video.
* **<name>STAT.RLE**: the death static (1.5), not part of the HUD.

### 3.5 Hit feedback — H

**Screen shake** (0x29641, 0x27F94):

* While countdown c = 0x71ECB > 0:
  * shakeX = 0x54E9C[int·5 + c] and shakeY = 0x54ED8[int·5 + c] (s16), read before the decrement;
  * then c−−.
* Tables, rows of 5 for intensity int = 0..5:

  ```
  X: [0,2,-2,2,-2] [0,0,0,0,0] [0,4,-4,4,-4] [0,0,0,0,0] [0,6,-6,6,-6] [0,0,0,0,0]
  Y: [0,0,0,0,0] [0,2,-2,2,-2] [0,0,0,0,0] [0,-4,4,4,-4] [0,0,0,0,0] [0,6,-6,6,-6]
  ```

* int = (min(dmg,3) − 1)·2 + (0x7143C & 1). 0x7143C only ever grows by 4, so bit 0 is always 0.
  **In practice the shake is horizontal only:** offsets −a, +a, −a, +a, 0 over 5 frames, with
  a = 2 / 4 / 6 for shot damage 1 / 2 / ≥ 3.
* The offset applies to:
  * the copy of the video into the compose buffer (src → dst shifted by (sx, sy); the uncovered
    strip keeps the previous frame's pixels);
  * the sprite and torpedo positions.

**Palette flash:**
* While c is counting, after each decrement **DAC entry 12** is set to (63,63,0) when c is odd and
  (58,33,5) when c is even (6-bit values, table 0x27760).
* It stays (58,33,5) until the video next changes its palette.

---------------------------------------------------------------------------------------------------

## 4. Win/lose rules per type — H

* There are **no kill quotas and no timers** other than the video length.
* **Lose:**
  * damage ≥ 100 → 0xFFFE, for every type;
  * type 6 only: video ends with the door intact → 0xFFFD.
* **Win** (0x8000):
  * destroying any sprite with id ≥ 200 ends the sequence 11 frames later;
  * type 6: door destroyed by the time the video ends. It is checked only at the end; destroying
    the door does not end early, but it stops enemy fire.
* **Otherwise 0** (survived to the end of the video).
* **Difficulty 0x714E8** (0..2, default 1, saved in dlas.ds9 as the first u32; Game_Setup buttons
  6/7/8 = difficulty + 6). It changes:
  * the enemy fire period: + (1 − difficulty);
  * shield regeneration: every 10 frames on difficulty 0, else every 13.

  No other effect was found in the arcade.
* **Carry-over:**
  * 0x1A (cita), 0x1E (def2) and 0x1F (can2) start with damage = previous arcade's final damage / 3
    (0x711C4, set at the end of every arcade, including deaths);
  * def2 also restores the sprite groups saved after defi (section 7).

### 4.4 Stats record 0x70230 + type·0x24 (cleared by 0x25E46) — H

The record is written but never read, except +0x1C.

| offset | meaning |
|---|---|
| +0 | phaser shots |
| +4 | torpedo volleys |
| +8 | phaser hits |
| +0xC | torpedo hits |
| +0x10 | sprites finished (exited or explosion ended) |
| +0x14 | kills |
| +0x18 | total damage taken |
| +0x1C | **current damage** |
| +0x20 | enemy shots received |

---------------------------------------------------------------------------------------------------

## 5. Audio

### 5.1 .MUS (Load_Music_Pattern_File 0x1D5F9, 0x1D753) — H

* A text file `\ds9\sound\<name>.mus`, read with fgets (80 chars per line).
* **Line 1:** a decimal number (atoi) → 0x71138 = MUSVALUE. It is used only in the tag-timing
  formula of 1.3.
* **Each further line:** a segment suffix. The token ends at the first char < '0', so CR/LF/space
  end it. At most 20 lines.
* The sample loaded is `\ds9\sound\<name><suffix>.raw` as **8-bit**. Examples: land + a1 →
  LANDA1.RAW; adr2 + tag → ADR2TAG.RAW.
* Equal suffixes share one sample slot (0x706E0[slot], at most 20).
* The suffix **"tag"** marks the ending segment: 0x710BC = its index.
* **Playback** (0x26700, 0x267B8):
  1. Segment 0 starts with an end-of-sample callback (0x30F26).
  2. At the end of each segment the next index plays. If the next entry is empty or is "tag",
     playback wraps to 0. So the segments before "tag" loop forever.
  3. 0x30155(h, 0x40) is called on each segment; probably volume 64/127 (L).
  4. **At video frame tagFrame** (1.3), all music stops and the tag segment plays once (no
    callback).
  5. If there is no tag, the music just stops at tagFrame (0x711BC = 0xFFFF → no play).
* tagFrame = endWord − elapsed_ms·MUSVALUE/10000 + endWord/40. elapsed_ms is the real time from the
  start of the loop to video frame startFrame (100 or 200).
  * Intent (L): end the tag near video frame endWord. endWord = 0x24E4F[type] (table in 8).
  * A reimplementation can use elapsed_ms = startFrame·1000/fps.
* Music stops at any end (0x269BE: AIL_end_sample + release for playing handles; 0x71454 = 1 stops
  the chaining).
* The disc segment names match the suffix scheme: LANDA1..3, LANDB, LANDTAG; ADR2A, B1, B2, C, TAG;
  CAN2A, B1, B2, C1, C2, TAG; CANN* the same.

### 5.2 .VCE voice cues (0x1D975, 0x1D9F8, 0x1DB78) — H

* A text file `\ds9\voice\<name>.vce`. Each cue is a **pair of lines**:
  1. **name**: the token, cut at the first char < '0'. The sound is `\ds9\sound\<name>.raw`, 8-bit.
  2. **trigger**:
     * a decimal **video frame number**; or
     * `d<n>` / `D<n>`: a **damage cue**.
* At most 25 cues (record 0x1D bytes: +0 data, +4 handle, +8 name, +0x15 frame, +0x19 dmg; both
  0xFFFF = unused).
* In the main loop after each frame, for each cue:
  * frame cue: plays when frame == cue.frame;
  * damage cue: idx = n/5; it plays once, when flags[idx] == 1 (then flags[idx] = 2).
    flags[19 − damage/5] is set when damage rises into a new 5-point band, so **`D<n>` fires when
    damage first enters [95 − 5·(n/5), 99 − 5·(n/5)]**. Example: D20 → damage 75..79, shields
    about 21–25 %.
  * The sounds SHIELD20/30/40.RAW in SOUND are likely such cues (L).
* A missing .VCE is fine (no cues).

### 5.3 Effects — H

| event | sound |
|---|---|
| phaser | PHASOR.RAW ("phasor.raw") |
| torpedo | QUANT.RAW ("quantum.raw") |
| enemy hit on player | SHAKE.RAW, only if no shake is in progress |
| target destroyed | BANG01..05 (random, paired with the explosion pick) |
| door explosions | BANG0x, x = explosion index/2 + 1 (5.4) |

All are 16-bit, loaded per sequence. The Smacker audio track plays only for cita (0x1A).

### 5.4 Plan door explosions — H

These are the 9 explosions described in 7.

---------------------------------------------------------------------------------------------------

## 6. Drawing order, buffers, timing — H

* **Buffers:**
  * 0x71028 = video/work buffer: Smacker decodes into it at (left, top), pitch 640. It holds the
    scene background around the video.
  * 0x71020 = compose buffer, 640×480.
* **Per video frame** (0x27EE5 → T1 → T2):
  1. If the video palette changed, write all 256 entries to the DAC (6-bit).
  2. SmackDoFrame into 0x71028.
  3. Wait for the previous T2.
  4. Copy the video rectangle 0x71028 → 0x71020 with the shake offset.
  5. **Logic (T1)**, drawing into 0x71020 in this order:
     * (type 6) door overlay and door explosions;
     * sprites, in load order, each followed by its enemy beam;
     * new torpedo set-up;
     * torpedo sprites (in flight);
     * phaser beams.
  6. **T2:**
     * shield indicator, drawn and blitted to the screen at once;
     * windshield overlay;
     * shake/flash update;
     * gunsight cursor into 0x71020 at the mouse top-left, with save-under;
     * (debug 0x71504: frame number at (300, 310), never on);
     * **blit only the video rectangle** 0x71020 → screen;
     * restore under the cursor.
  7. SmackWait busy loop (native video rate), SmackNextFrame, frame++.
* **The game runs at the video's own frame rate**, with one logic step per frame. The video frame
  counts and rates are not in the exe (unknown).
* The cockpit picture around the video is whatever the script's BACKGRND drew. The arcade never
  redraws it, except the shield indicator.
* **Beam algorithms:**
  * **Phaser 0x27215(x0, y0 → x1, y1, buf, colour, pitch):**
    * a Bresenham walk from the launcher to the aim point;
    * width w starts at 12 and drops by 1 every len/12 steps (len = the major-axis length; it
      drops every step if len < 12), with minimum 1;
    * at each point it draws a **vertical** run of 2·(w>>1)+1 pixels centred on the point, so it
      tapers from 13 px to 1 px;
    * it skips pixels already equal to the colour, and clips only to the 640×480 buffer.
  * **Enemy beam 0x273E4(x0, y0 → x1, y1, buf, c, pitch):**
    * a Bresenham walk;
    * at step i: width 1 and colour c; after len/3: width 3 and colour c+1; after 2·len/3: width 5
      and colour c+2;
    * x jitter = random(width) − width/2 (random = 0x1380D, Watcom LCG);
    * at each point it draws a vertical streak of 20 px downward, stopping at the video's bottom.
  * Both colours are palette indexes of the video's palette (18; 1..3 or 7..9).
* **RLE drawing:**
  * 0x29C36 sets dest = (y − 1)·640 + x (the stream starts with a new-row code);
  * 0x2A98F draws unclipped;
  * 0x29AC7 picks one of 9 clip variants against the video rectangle.

---------------------------------------------------------------------------------------------------

## 7. Per-type differences — H

| type | name | rectangle | torpedoes | shield HUD | overlay | special |
|---|---|---|---|---|---|---|
| 2 | land | 120,12 400×300 | yes | yes | yes | start 200, end 1500; no autosave; death deathmix |
| 3 | spac | same | yes | yes | yes | start 200, end 1501; no autosave |
| 4 | holo | same | yes | yes | yes | start 200, end 2200; autosave; deathmix |
| 5 | adr2 | 80,62 480×250 | **no (phaser only)** | **no** | **no** | end 1350; death adr2dead |
| 6 | plan | 80,62 | yes | yes | yes | **door objective + PDOOR.RLE**; 0xFFFD on failure; death480 |
| 0x19 | cann | 80,62 | yes | yes | yes | end 919; death480 |
| 0x1A | cita | 120,12 400×300 | yes | yes | yes | **Smacker audio on**; damage carried from previous /3; end 361; deathmix |
| 0x1B | defi | 80,106 | yes | **no** | yes | end 579; defdead; sprite groups saved for def2 |
| 0x1E | def2 | 80,106 | yes | **no** | yes | **restores defi groups**; damage /3 carry; end 596; defdead |
| 0x1F | can2 | 80,62 | yes | yes | yes | damage /3 carry; end 481; death480 |

* "start" = 0x24E2F, "end" = 0x24E4F. Both only time the music tag.
* All are shooting games. There are no puzzle or steering modes.

### Group save/restore (0x26C46, 0x26D4A)

* **Save** happens at the end of every arcade sequence. For each sprite with id ≥ 100 it writes an
  entry to 0x70C30[k] (4 bytes: u16 id, u8 state, u8 hits):
  * state = 2 if +0x13 (group destroyed), else the byte is left as it was;
  * hits = the max +0xD among same-id sprites;
  * +2 of those other same-id sprites is cleared so the group is saved once.
* **Restore** happens only for def2. For every saved entry with id ≥ 100, the def2 sprites with that
  id get +0x12 = state and +0xD = hits.
* So the Nemesis groups (shared ids between DEFI*.RLE and DEF2*.RLE) keep their damage or
  destroyed state. The table is never cleared, so stale entries can linger (M).

### Plan door (type 6; Load_PDoor 0x1A3C2, logic 0x2814A..0x28615)

* `\ds9\sprites\pdoor.rle` (error text "The file, plandoor, not found"). Its first u16 = door start
  frame (0x71DBE). It has normal frames.
* Each tick with frame > doorStart, the door frame node advances (0x711CC; it stays on the last
  frame). The door rectangle = node hdr (x + left, y + top, w, h), without shake.
* While door damage 0x713E4 < 20:
  * each torpedo in impact state (≥ 4) whose target lies in the rectangle adds **6**;
  * a new left click with the aim point in the rectangle adds **2**;
  * so it takes 10 phaser clicks, or 4 torpedoes (3 torpedoes + 1 phaser).
* When it reaches ≥ 20:
  1. 0x713F0 = 1, which also stops all enemy fire.
  2. Spawn 9 explosions. For i = 0..8:
     * expl = random(nExpl/2), i.e. **large** EXPLLG only;
     * delay = random(5) + 1;
     * position = doorX + fx[i]·w/4, doorY + fy[i]·h/4, with fx = {1,2,3,1,2,3,1,2,3} and
       fy = {1,1,1,2,2,2,3,3,3} (0x2774E / 0x27757): a 3×3 grid at quarter points.
  3. 0x713FC = 2.
* Each tick, for each explosion:
  * if delay == 1, play BANG0(expl/2 + 1);
  * if delay > 0, delay−−;
  * otherwise draw the current frame centred at (x − w/2, y − h/2) and advance (it stops at the
    end).
* When door damage ≥ 20, after a 2-tick delay **the PDOOR frames are drawn** (clipped). So PDOOR.RLE
  is the blown-open door overlay that tracks the camera (M).
* The video ending with the door destroyed → 0x8000, otherwise 0xFFFD.
* Plan also has normal sprites (PLANS*.RLE) that shoot. Killing an id ≥ 200 sprite would also win
  early (H, but whether plan has one is unknown).

---------------------------------------------------------------------------------------------------

## 8. Decoded exe tables

**Per-type parameters** (index = ACTIONSEQ type):

* **0x24E2F** (byte, startFrame):
  * 100 for all types except 2, 3, 4, which are 200.
* **0x24E4F** (u16, endFrame), types 0..31:

  ```
  69, 1135, 1500, 1501, 2200, 1350, 1225, 240, 220, 357, 130, 156, 48, 100 ×12 (types 13..24),
  919, 361, 579, 100, 100, 596, 481
  ```

  Arcade types: land 1500, spac 1501, holo 2200, adr2 1350, plan 1225, cann 919, cita 361,
  defi 579, def2 596, can2 481.

**Name and title tables:**

* **0x24F1B** (panel/overlay prefixes): "def", "run", "ops". The arcade uses index 1, "run".
* **0x24F27** (save names, index = type):

  | type | save name |
  |---|---|
  | 2 | landsave |
  | 3 | spacsave |
  | 4 | holosave |
  | 5 | adr2save |
  | 6 | plansave |
  | 0x19 | cannsave |
  | 0x1A | citasave |
  | 0x1B | defisave |
  | 0x1E | def2save |
  | 0x1F | can2save |

* **0x24FA7** (titles, index = type):

  | type | title |
  |---|---|
  | 2 | "Holosuite Sim: Intercept at Kinsara IV" |
  | 3 | "Holosuite Sim: Panhelios 18 Depradator Ambush" |
  | 4 | "Holosuite Simulation: Citadel Run" |
  | 5 | "Drones attack Deep Space Nine" |
  | 6 | "Kira and Bannik descend upon the Citadel" |
  | 0x19 | "Harbinger menaces Deep Space Nine" |
  | 0x1A | "Inside the Citadel" |
  | 0x1B | "Nemesis assails Deep Space Nine" |
  | 0x1E | "Defiant vs. Nemesis" |
  | 0x1F | "Kira and Bannik pursue Harbinger" |

**Torpedoes and effects:**

* **0x18D5A** (torpedo files → 0x70EA0[0..5]): t29l, t17l, t09l, t29r, t17r, t09r. T5ALL →
  0x71414.
* **0x2AC57** (effect sounds → 0x706B8[0..7]): bang01, bang02, bang03, bang04, bang05, quant,
  phasor, shake (16-bit).

**Door explosions and hit feedback:**

* **0x2774E / 0x27757** (door explosion grid, in quarters):
  * fx = 1,2,3,1,2,3,1,2,3;
  * fy = 1,1,1,2,2,2,3,3,3.
* **0x27760** (flash colours for DAC 12): (58,33,5), (63,63,0).
* **0x54E9C / 0x54ED8**: shake X / Y tables, given in 3.5.

**Torpedo flight constants:**

* stage multipliers 0, 6, 11, 15 (/16);
* half-gaps 29, 17, 9, 5;
* launcher y = top + 3H/4.

**Direction thresholds** (0x27136):
* the 58-px near zone → frame 9;
* slope cuts −54, −17, 17, 54, 101, 175, 350, 1000.

**Death videos (0x26846):**

| types | video |
|---|---|
| 2, 3, 4, 0x1A | deathmix |
| 6, 0x19, 0x1F | death480 |
| 5 | adr2dead |
| 0x1B, 0x1E | defdead |

All are in TRANS, played at (0, 0).

## 9. Unknown / not verified

* The contents of the .MUS, .VCE and sprite files (none were available). The header
  interpretation is from code only.
* Whether `art\<name>run.rle` files exist in the user's install.
* The Smacker frame rates of the arcade videos.
* The exact meaning of the 0x30155(h, 64) call (probably volume).
* Whether the INT 33h handler also draws the gunsight directly on screen outside the video
  rectangle. 0x1902F shows it, so probably yes (M). A reimplementation should simply draw the
  gunsight on top everywhere.
