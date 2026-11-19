# Script-driven screen features: close-up panels, automap, screen effects, controls

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Source: sgens.exe (interpreter 0x44F9D4, case table 0x455A45) and verid1/game.bin (verid1b.dis).
Conventions are the same as natives-c.md. **Push order** lists arguments first-pushed to last-pushed,
so the last one is on top and is popped first. "d" means a 32-bit PUSH32 value (two slots).
Resource indexes are verid1 section-1 indexes: 0 COMBADGE, 1 HYPSPRAY, 2 PHASER, 3 TRICRDR, 4 KIRKY-HO,
5 AUXCLOAK, 6 KIRKHIT.OVR, 7 PICHOP.OVR, 8 KIRHURT1, 9 KIRHURT2, 10 PICHURT1, 11 PICHURT2, 12 RSORAN,
13 BLUETIME, 14 GRENTIME, 15 REDTIME, 16 MENUACTV, 17 SUBMENU, 18 AMTARGTS, 19 TOGTARGT, 20 CLMPBTTN,
21 LOCKTXT, 22 LAUNCH, 23 CLAMPS, 24 DISRPAUX, 25 LBATAUX, 26 KFEMDEAD, 27 KMDEAD, 28 PHARBATT,
29 KIRKDIES, 30 BLANK, 31..35 auxh01..auxh05, 36 auxhench.

---------------------------------------------------------------------------------------------------
## 1. Screen layout around the aux area (from the game window setup, 0x4336F0)

| window | parent | rect (l,t,r,b), inclusive | absolute on 640x480 |
|---|---|---|---|
| bottom console root (game+0x278, 0x422D0F) | screen | (0,252)-(639,479) | 640x228 under the 640x252 3D view |
| "actionx.bmp" console (bitmap id 4) | root | (206,0)-(639,227) | (206,252)-(639,479) |
| **aux panel manager** `*0x545EA0` = game+0x2C0 (class 0x532A48, ctor 0x448A58, id 0x10) | actionx | **(17,59)-(227,206)** | **(223,311)-(433,458), 211x148** |
| aux cycle button game+0x2B8 (auxcycle.bmp, id 0x12) | actionx | (1,173)-(13,207) | (207,425)-(219,459), 13x35 |
| weapon cycle button game+0x2BC (wepcycle.bmp) | actionx | (1,1)-(13,16) | (207,253)-(219,268) |
| left HUD panel game+0x288 (l-m-m-o.bmp) | root | (0,1)-(205,206) | (0,253)-(205,458) |
| automap game+0x2A0 (ctor 0x443739, id 0xB) | left HUD 0x288 | (39,14)-(192,161) | (39,267)-(192,414), 154x148 |
| automap buttons levels/zoomz/s-life/s-energ/s-lith (0x28C..0x29C) | left HUD | y 162..188 (see §3) | y 415..441 |

A window rect is stored at +0x18 relative to the parent. The absolute rect at +0x28 = rect + parent origin
(0x42455F). The 3D view is 640x252 (or 504x252 in narrow mode, table 0x545310).

When no panel is open, the manager window is hidden and a background window showing **auxdef.bmp** fills
the same 211x148 rectangle (mgr+0x64, created in 0x448AD5).

---------------------------------------------------------------------------------------------------
## 2. Close-up ("aux") panels

### Data model
* The manager holds a list of **panel windows**: class 0x532928, 0x1CC bytes, ctor 0x447EF5, derived from
  the generic bitmap button class 0x426122 (vtable 0x530990).
* There is **one panel per script object**. The panel's owner is at +0x1A8 (vt[0x104] returns it).
* Every panel fills the whole manager client rect, (0,0)-(210,147).
* Panel window id = `(0x10<<16) + image`.
* mgr+0x54 = panel count. mgr+0x5C = section-1 name table (12-byte entries, name pointer at +8).
  mgr+0x50 = directory prefix.
* Image file name (0x44959D): if the resource name starts with `*`, the name after it is used literally.
  Otherwise the name is prefix + name.

### Bitmaps and frames (0x426257 load, 0x4264AD/0x42598B select frame, DIB 0x52F0D8)
* `win.vt[0xA4](key=image+1, name, mode)` loads the BMP. If `key` equals the loaded key (+0x50), nothing is
  reloaded, so setting the same image again only changes the frame.
* **Frame height = the window's height** (r-t+1). frameCount = bmpHeight / frameHeight. Frame n is the
  horizontal strip of rows [n*h, (n+1)*h) counted from the top of the picture (0x4038A0/0x40390E).
  * A panel image is 211 x (148*n).
  * A 43x25 button with 12 frames is 43x300.
  * After a load, frame 0 is shown and the animation range is set to 0..count-1.
* The `mode` argument (1 for panels, 3 for buttons) only matters through bits 4 and 8 (orientation flag),
  so in practice it has no visible effect.
* A selected frame index is not clamped.
* Drawing (vt[0x6C] = 0x425A36):
  * opaque blit (surface vt[0x48]), unless the window's +0x5C flag is set;
  * if +0x5C is set, a colour-keyed blit (vt[0x5C]). The key is palette index **0xFE** by default
    (0x40396D). If +0x5C is already set when a bitmap is loaded, the key becomes the first byte of the
    pixel data (0x425A00 → DIB vt[0x60]\(-1)).

### 0x6C PANEL_SHOW_IMAGE (3: frame, image, id) → obj.vt[0x124] → 0x448C90(mgr, obj, image, frame)
* Push order: `frame, image, id` (id on top). Self or another id (game.vt[0x258]).
* Steps:
  1. Find this object's panel. If there is none, create it: new panel, rect = whole manager.
  2. Load `image` (key image+1) and select `frame`.
  3. Hide every other panel. If another panel had the mouse capture, reset its press state and release the
     capture. Show this panel.
  4. If the panel was new:
     * hide the auxdef background and show the manager;
     * `count++`;
     * when count becomes 2: cycle button: frame 1, `vt[0xCC](33, 300)` (blinks, alternating 33/300 ms
       frame times), enabled (vt[0xF4]\(1)).
* The newest SHOW is the visible one; earlier panels stay underneath.
* Calling SHOW again for the same object re-shows it and swaps the image or frame. The panel is not
  recreated, so its buttons survive.
* At creation the panel records the player position (camera x, camera y minus eye height, camera z) in
  +0x1AC..0x1B4, with walk-away radius +0x1B8 = 2.0 (0x447FEF).
* image & 0x8000: jumps straight to the "show" loop with no panel. This crashes in the original; it is
  unused.
* verid1 always uses frame 0:
  * `PUSH 0; PUSH 36; PUSH_SELF; PANEL_SHOW_IMAGE` (wall 0x43A9, auxhench);
  * `0 25 SELF` (e193, LBATAUX);
  * `0 24 SELF` (e194, DISRPAUX);
  * `0 27 SELF` (e0dd/e0de templates, KMDEAD);
  * `0 30 SELF` (e002, BLANK);
  * `0 12 SELF` (actor 0xA004, RSORAN).

### 0x6D NATIVE_6D = PANEL_ANIMATE_IMAGE (5: ms, loops, last, first, id) → obj.vt[0x128] → 0x448F67
* Push order: `ms, loops, last, first, id`. Animates the panel image itself.
* If `last == 0 || last == first`: show frame `first`, no timer.
* Otherwise:
  * frame range first..last;
  * total frames = (last-first+1)*loops (0 = forever);
  * if ms != 0, a completion callback 0x449069 sends **event 60** (no args, synchronous) to the owner when
    the count runs out;
  * timer ms per frame.
* Not used in verid1.

### 0x6E PANEL_REMOVE (1: id) → obj.vt[0x12C] → 0x449138(mgr, obj)
* Destroys the object's panel and its buttons, and decrements the count. If the removed panel was the
  visible one, the first remaining panel in the list is shown.
* When none are left: hide the manager and show auxdef.bmp. When 1 is left: the cycle button gets frame 0,
  its animation stops and it is disabled.
* obj == 0 (internal) removes all panels.
* No effect if the object has no panel.
* verid1: `PUSH_SELF; PANEL_REMOVE` in every event-35 handler (walk-away), on event 43 (picked up), on
  death (e0dd ev 41) and on timers.

### 0x6F NATIVE_6F = PANEL_FIRST_OWNER (0 → push 1)
* Pushes the id (vt[4]) of the owner of the **first panel in the manager's child list**, or 0 if no panel
  is open. The list is the creation order, as far as I can tell, so this is "is any close-up open / which".
* Not used in verid1.

### 0x9D PANEL_ADD_BUTTON (6: h, w, y, x, image, button) → obj.vt[0x148] → 0x44950D → panel.vt[0x110] = 0x4487C6
* Self only.
* Creates a bitmap button (class 0x426122, 0x1A4 bytes):
  * rect (x, y)-(x+w-1, y+h-1) relative to the panel's top-left, so screen = (223+x, 311+y);
  * id = `(panelId<<16) | 0x4000 | button`;
  * image `image` loaded with key image+1. Frame height = h. Frame 0 shown.
* +0x5C (transparency) is 0, so the button is drawn opaque.
* No dedupe: adding the same number twice gives two children; finds return the first.
* If self has no panel, nothing happens (the call is skipped).
* verid1, wall 0x43A9 (auxhench, 211x148): buttons 1..5 with images 35..31:

  | button | image | rect |
  |---|---|---|
  | 1 | 35 | 43x43 at (126,73) |
  | 2 | 34 | 43x25 at (66,49) |
  | 3 | 33 | 50x16 at (72,74) |
  | 4 | 32 | 58x24 at (69,92) |
  | 5 | 31 | 48x82 at (16,33) |

  Sample: `PUSH 43,43,73,126,35,1; PANEL_ADD_BUTTON`.
* verid1, actor 0xA004 (RSORAN):

  | button | image | rect |
  |---|---|---|
  | 100 | BLUETIME | 28x30 at (26,84) |
  | 101 | GRENTIME | 28x30 at (157,82) |
  | 102 | REDTIME | 15x15 at (165,90) |
  | 103 | MENUACTV | 98x10 at (56,38) |
  | 104 | SUBMENU | 30x10 at (25,38) |
  | 105 | SUBMENU | 30x10 at (157,39) |
  | 106 | AMTARGTS | 29x27 at (25,54) |
  | 107 | TOGTARGT | 14x14 at (165,59) |
  | 108 | CLMPBTTN | 33x26 at (108,83) |
  | 109 | LOCKTXT | 27x19 at (117,63) |
  | 110 | LAUNCH | 28x16 at (62,57) |
  | 111 | CLAMPS | 35x7 at (70,105) |

### 0x9E SET_PANEL_BUTTON (3: frame, image, button) → obj.vt[0x14C] → 0x449680
* Self only.
* Find self's panel (0x44960C), then the child with `(id & 0x3FFF) == button` (0x4488BD).
* Load `image` (key image+1; no reload if unchanged; a new image resets the frame range and frame 0).
* vt[0xA8]\(frame): **stops any running animation** (vt[0xD8]) and shows `frame`.
* The image can be changed per call (any bitmap). The frame count comes from bitmap height / button height.
* verid1:
  * `PUSH 1; PUSH 35; PUSH 1` (button 1 lit, frame 1);
  * `PUSH 0; GET_LOCAL 0; PUSH 31; PUSH 5` (button 5 shows counter value 0..4);
  * timer bars `GET_GLOBAL 1; PUSH 13; PUSH 100` (frame = global 1).

### 0x9F PANEL_ANIMATE_BUTTON (5: ms, loops, last, first, button) → obj.vt[0x150] → 0x449707
* `last == 0 || last == first`: show `first`, stop.
* Otherwise:
  * frame range first..last (vt[0xD0]);
  * count = (last-first+1)*loops (0 = forever);
  * frame timer = ms.
* Frames advance first→last and wrap; first > last runs backwards (0x4267C6/0x426862).
* When the count runs out the timer stops on the last shown frame (`last` for whole loops).
* **No event** is sent.
* verid1:
  * `100 0 11 0 2` (button 2: frames 0..11 forever, 100 ms);
  * `50 0 7 0 3`;
  * `500 0 7 0 4`;
  * 0xA004 `350 0 1 0 110` (LAUNCH blinks frames 0/1 every 350 ms).

### 0xA0 NATIVE_A0 = SET_PANEL_BUTTON_TRANSPARENT (2: on, button) → obj.vt[0x154] → 0x4497AB
* button.vt[0xB8]\(on) sets +0x5C, which turns on colour-keyed drawing (see Bitmaps).
* verid1, 0xA004: `PUSH 1; PUSH 101; NATIVE_A0` (GRENTIME bar drawn over the panel art). Here the image
  is already loaded, so the key stays index 0xFE.

### 0xA1 NATIVE_A1 = PANEL_REMOVE_BUTTON (1: button) → obj.vt[0x158] → 0x4497F1
* Deletes the button and repaints the panel. Unused in verid1.

### 0x8F NATIVE_8F = PANEL_ADD_HOTSPOT (5: h, w, y, x, hotspot) → obj.vt[0x140] → 0x449481 → panel.vt[0x10C] = 0x44871C
* An invisible clickable rect (class 0x4251EA, 0x48 bytes): rect (x,y)-(x+w-1,y+h-1), id
  `(panelId<<16)|0x4000|hotspot`.
* Clicks are reported exactly like buttons (event 47 with its number).

### 0x90 NATIVE_90 = PANEL_REMOVE_HOTSPOT (1: hotspot) → obj.vt[0x144] → 0x44985A
* Removes the hotspot. Neither 0x8F nor 0x90 is used in verid1.

### Mouse and keyboard on a panel (panel vtable 0x532928) → events to the owner (+0x1A8)
| input | handler | result |
|---|---|---|
| left button down inside the panel (nothing else captured, no right press) | vt[0x74] 0x44808E | +0x1BC = 1, capture, remember the point |
| left button up inside, panel has **no** children | vt[0x78] 0x448164 → vt[0xFC] 0x44859C | owner.vt[0x138] = **event 45** (activate, like op 0x72), synchronous |
| left button up inside, on a button or hotspot (first child whose rect contains the point) | same | **event 47**, 1 arg = button number (`id & 0x3FFF`) |
| left button up inside, panel has children but none hit | same | **event 47**, arg 0 |
| left button down then drag ≥ 4 px (`|dx| \| |dy| >= 4`) while the owner has an item controller with an icon | vt[0x70] 0x448440 | press cancelled; drag to the inventory (0x44732C(-1, owner, ...), which also removes the panel): the picked-up object |
| right button down inside | vt[0x7C] 0x4482C6 | +0x1C0 = 1 |
| right button up inside | vt[0x80] 0x448388 | **event 62**, no args. verid1 uses it to say a line: `PUSH 120; PUSH -8142 (e032); SEND_SIGNAL` |
| every frame (player update 0x43AD04 → mgr.vt[0xA0] 0x449420 → every panel's vt[0x108] 0x448628) | | if dist(current player point, point stored at panel creation) > 2.0 → **event 35**, no args. It is sent every frame until the script removes the panel. |
| key Space / Numpad0 (VK 0x20/0x60) | game.vt[0x398] → 0x44933E | visible panel's vt[0xFC] → **event 45** to its owner |
| game.vt[0x39C] → 0x4493AC | (no key mapping found) | visible panel's vt[0x100] → put the owner in the inventory (0x446DD0(-1, owner)) |
| click the aux cycle button (id 0x12), if count > 1 (0x43A77E → 0x44908D) | | hide the visible panel and show the next one in the list (wraps) |

* Buttons do **not** change frame by themselves on a click. +0x18C "auto-press" is 0 for script buttons.
  Lit or pressed frames are entirely up to the script (SET_PANEL_BUTTON).
* Mouse clicks are not queued: the events are dispatched synchronously from the mouse handler.

### Typical verid1 pattern (wall 0x43A9; templates e0dd/e0de; e193/e194; actor 0xA004)
```
on event 14 (player touches the object):  PANEL_SHOW_IMAGE(0, img, SELF); PANEL_ADD_BUTTON...; SET_PANEL_BUTTON...
on event 47 (click):                     the event arg is on the stack: `PUSH 1; IF_EQ` → button 1 handling
on event 62 (right click):               SEND_SIGNAL(line, 0xE032)
on event 35 (walked away):               PANEL_REMOVE(SELF), GOTO_STATE ...
on event 43 (picked into inventory):     PANEL_REMOVE(SELF)
```

### Countdown timer bars (BLUETIME / GRENTIME / REDTIME)
These are not a separate native. They are panel buttons 100/101/102 of actor 0xA004's RSORAN close-up.
* Logic object e006 runs a 125 ms timer:
  * global1 = (g1+1) & 7;
  * when it wraps, global2 = (g2+1) & 7;
  * when that wraps, global3++;
  * global3 == 4 → time up: signal 2 to 0xA004, signal 666 to 0xA006, sound 27, disable self.
* Every tick e006 sends SEND_SIGNAL(1, 0xA004). In its event-4 handler 0xA004 does:
  `SET_PANEL_BUTTON(GET_GLOBAL 1, 13, 100)`, `(GET_GLOBAL 2, 14, 101)`, `(GET_GLOBAL 3, 15, 102)`.
* So BLUETIME has 8 frames of 30 px (28x240), GRENTIME 8 frames of 30 px, and REDTIME at least 5 frames
  of 15 px. The globals are reset elsewhere.

---------------------------------------------------------------------------------------------------
## 3. Automap / tricorder map (game+0x2A0, class vtable 0x532690, ctor 0x443739, draw 0x444A04)

### When it is shown
* The left HUD window (0x288, "l-m-m-o.bmp") has 4 states (vt[0xAC] returns the state):

  | state | how | shows |
  |---|---|---|
  | 0 | game.vt[0x370] = GAME_MODE 2 | all sub-windows hidden |
  | 1 | vt[0x36C], key **M** | object view 0x2B0 (object.bmp) |
  | 2 | vt[0x368], key **A** | **automap** 0x2A0 and its 5 buttons |
  | 3 | vt[0x364], key **H** or GAME_MODE 3 | body image 0x2A4 + health bar 0x2A8 (+0x2AC) |

* A key press also plays sound game+0x2EC; the GAME_MODE path passes 0 (no sound).
* The 5 automap buttons (rects in the left HUD) and their keys, active only in state 2:

  | button | bitmap | rect | key |
  |---|---|---|---|
  | 0x28C | levels.bmp | (43,162)-(71,188) | F |
  | 0x290 | zoomz.bmp | (74,162)-(102,188) | Z |
  | 0x294 | s-life.bmp | (117,162)-(141,188) | L |
  | 0x298 | s-energ.bmp | (141,162)-(165,188) | E |
  | 0x29C | s-lith.bmp | (165,162)-(189,188) | T |

  The keys go through game.vt[0x374..0x384]. Those buttons toggle the level band (+0x78), the zoom, and
  the three marker filters +0x90 (life), +0x94 (energy) and +0x98 (trilithium); all three filters start
  at 1.

### What it draws (each paint, 0x444A04)
1. Fill the rect with palette index 0x30, then draw a scaled background grid/bitmap (surface vt[0x78]).
2. Centre: when "follow" (+0xA0 = 1, the default) it is the player (camera x, z, and height = camera y
   minus eye). When not following it is a panned offset.
3. Scale: `px = ((coord - centre + 0x84) * scale) >> shift`. The (scale, shift) pairs are +0x44..+0x58,
   default (3,15),(2,15),(1,15); the zoom index is +0x7C/+0x80.
4. **Walls**:
   * for every wall record (0x57A3C4, stride 0x98, count 0x57A57C) skip flag 0x20000;
   * draw the 2D line between its two vertices (+0x86/+0x88 → vertex table 0x57A584) only if
     **flag 0x800 is set** and the wall's height range [+0x2C,+0x30] overlaps the current band;
   * when following, flag 0x800 is copied to wall+0x18; when not following, +0x18 is copied back into
     flag 0x800.
5. **Markers**: map258 if the life filter is on, map274 if energy is on, map290 if trilithium is on.
   * For each id in the map: only billboards (0x2xxx, 0x4B5100) and actors (0xAxxx, 0x4B5070) have a
     position (x +0x1C, height +0x20 + (+0x2F4), y +0x24).
   * Drawn if the height is inside the current band. The marker bitmap is blitted centred at the scaled
     point, clipped to the map rect.
   * If +0x9C (GAME_MODE 4) and the life filter are on, the "overload.bmp" interference texture is tiled
     and scrolled over the map first (0x4451D9).
6. If the band index +0x78 == 1 (the player's own band), draw the player arrow agpface2.bmp:
   5-px frames, frame = ((338 - headingDeg) mod 360)/45, 6x5 at the centre.

### Level bands (heights relative to the player's height +0xAC)
* Band 0 = (+0x5C,+0x60), band 1 = (+0x64,+0x68), band 2 = (+0x6C,+0x70). The current band
  (+0x88,+0x8C) is chosen by +0x78 (0x44420F).
* 0x80000000 / 0x7FFFFFFF mean unbounded.
* Default: (-inf,-6.0), (-6.0,6.0), (6.0,+inf), band 1.

### Marker bitmaps (map\ directory; frame height in brackets)

| style / use | field | bitmap |
|---|---|---|
| 0 | +0xF0 | dot-romu (7) |
| 1 | +0x114 | dot-injr (7) |
| 2 | +0x138 | dot-sorn (7) |
| 3 | +0x1A4 | dot-klng (7) |
| 4 | +0x1C8 | dot-chod (7) |
| 5 | +0x1EC | dot-fish (7) |
| 6 | +0x210 | dot-fuzy (7) |
| 7 | +0x234 | dot-pred (7) |
| energy | +0x15C | dot-enrg (7) |
| trilithium | +0x180 | dot-tril (7) |
| player | +0xCC | agpface2 (5) |
| overlay | +0x2AC | overload (179) |

* Note: in 0x444433 style 1 maps to +0x138 (sorn) and style 2 to +0x114 (injr). The table at
  0x4445C3, in order 0..7: romu, sorn, injr, klng, chod, fish, fuzy, pred.

### Natives
| op | push order | effect |
|---|---|---|
| 0x7B MAP_MARKER | `style, id` | game.vt[0x3D4] → 0x444433: map258[id] = marker style 0..7 (romu, sorn, injr, klng, chod, fish, fuzy, pred). Any other style is ignored. Shown with the **life** filter. verid1 (wall 0x43A9, after 9 presses): `PUSH 4; PUSH -24468; MAP_MARKER` (actor 0xA06C gets dot-chod). |
| 0x7C MAP_REMOVE | `id` | game.vt[0x3D8] → RemoveKey on map258. verid1: dying actors `PUSH_SELF; MAP_REMOVE` (e0dd/e0de/e050/e05a). |
| 0x7D NATIVE_7D = MAP_ENERGY_ADD | `id` | map274[id] = dot-enrg (energy filter). |
| 0x7E NATIVE_7E = MAP_ENERGY_REMOVE | `id` | removes from map274. |
| 0x7F NATIVE_7F = MAP_TRILITHIUM_ADD | `id` | map290[id] = dot-tril (trilithium filter). |
| 0x80 NATIVE_80 = MAP_TRILITHIUM_REMOVE | `id` | removes from map290. (0x7D..0x80 are unused in verid1.) |
| 0x86 SET_ZONE_ON_MAP | `on, zone` | 0x4B5220: for every wall whose zone masks (+0x3C/+0x40) contain bit `zone` **or bit 0**, set or clear flag 0x800 ("drawn on the map"). zone 0 = all walls. verid1 floor 0xC039 (ev 14): `PUSH 0; PUSH 45; SET_ZONE_ON_MAP` for zones 45,52,19,13,53,4,6,20,18,... (hides those areas). |
| 0xCA MAP_SET_LEVELS | `d hi2, d lo2, d hi1, d lo1, d hi0, d lo0` | 0x4443D6 stores the 3 bands and re-applies the current one. e001: `D(25) D(12) D(10) D(-2) D(-7) D(-15)` → bands (-15,-7), (-2,10), (12,25); floor 0xC015 on touch: `... D(-2) D(-10) D(-15)`. |
| 0xC8 NATIVE_C8 (6 words) | `shift2, scale2, shift1, scale1, shift0, scale0` | 0x44555F: the three zoom levels (0x8000 = keep, 0x8001 = default). e001: `15 2 15 1 16 1` → (1>>16), (1>>15), (2>>15). |
| 0xC9 NATIVE_C9 (2 dwords) | `d b, d a` | 0x4442DF: simple bands (-inf,a), (a,b), (b,+inf) (0x80000001 = default -6/+6). |
| 0x40 NATIVE_40 (self actor) | `kind, id` | actor+0x58 = kind and automatically MAP_MARKER(self, style): kinds 1/2→0, 3→1, 4→2, 5→3, 6→4, 7→5, 8→6, 9→7 (0x457163). Unused in verid1. |
| GAME_MODE 4/5 | | overload interference on/off (+0x9C). |
| GAME_MODE 9/10 | | marker height offset +0x2F4 = 3.0 / 0. e001 init uses 9. |

---------------------------------------------------------------------------------------------------
## 4. Screen effects (3D view game+0x274, class vtable 0x531610)

### Palette-effect slots (view+0x4A4, 4 records x 40 bytes) — setter view.vt[0x124] = 0x431772
`set(slot, r, g, b, pct, fadeIn, hold, fadeOut, ownerObj)` stores:
* owner, r, g, b;
* `level = pct*128/100` (integer);
* `t1 = fadeIn`, `t2 = fadeIn+hold`, `t3 = fadeIn+hold+fadeOut` (ms).

How the start time t is set:
* fadeIn == 0 && fadeOut == 0: t = (hold == 0 ? -1 = **permanent** : 0), cur = 0.
* Otherwise, if the slot is idle (old level 0): t = 0, cur = 0.
* If the slot is still active (retrigger without a jump):
  * fadeIn != 0: `t = cur*fadeIn/oldLevel`;
  * else: `t = fadeIn + hold + (oldLevel-cur)*fadeOut/oldLevel`.

pct = 0 clears the slot (level 0 = idle).

### Per-frame update 0x43199C(dt_ms), slots 3,2,1,0
* For each slot with level != 0:

  | condition | result |
  |---|---|
  | t == -1 | cur = level |
  | otherwise t += dt, then: t < t1 | cur = t*level/t1 |
  | t < t2 | cur = level |
  | t < t3 | cur = (t3-t)*level/(t3-t2) |
  | else | level = 0 (slot finished); if owner != 0: owner.SendEvent(**55**, no args), synchronously in the frame update |

* If the event-55 handler restarted the same slot (level != 0 again), the slot is processed again in the
  same frame.
* Event 55 therefore arrives on the first frame where elapsed ≥ fadeIn+hold+fadeOut, which can be up to
  one frame late. It is never sent for a permanent slot (t == -1) or for a slot that was cleared with
  pct 0.
* If any slot is active, the base palette (view+0x548, 256 RGBX) is copied, then slot 3, then 2, then 1,
  then 0 is applied. For entries 1..254 (0..255 in the DirectDraw flag mode 0x5630FC):
  `c = (c*(128-cur) + col*cur) >> 7` for R, G and B, and the flags byte is set to 1.
  The result goes to view.vt[0x13C]\(pal). With no active slot: vt[0x13C]\(0) restores the base palette.
* Slot 0 is applied last, so it ends up on top.

| op | slot | push order | notes |
|---|---|---|---|
| 0xBA SCREEN_FLASH_NOTIFY (11 slots) | 0 | `d fadeOut, d hold, d fadeIn, pct, B, G, R, notifyId` | notifyId 0 = none, self, or a looked-up object → event 55 at the end. |
| 0xBB SCREEN_FLASH (10) | 1 | `d fadeOut, d hold, d fadeIn, pct, B, G, R` | no notify |
| 0xBC SCREEN_TINT (4) | 3 | `pct, B, G, R` | times 0 → permanent; `0 0 0 0` clears |
| 0xBD NATIVE_BD = SCREEN_PULSE (6) | 2 | `total_ms, pct, B, G, R, notifyId` | if total == 0 or pct == 0 the slot is cleared; else fadeIn = fadeOut = total/2, hold 0, notify → event 55. Unused in verid1. |

verid1 calls (push order):
* e002 intro: `D(4000) D(300) D(0) 75 26 159 207 SELF BA` (orange 75%: snaps on, holds 0.3 s, fades out
  over 4 s, then event 55).
* Then `D(1000) D(18000) D(4000) 50 0 0 0 SELF BA` (dim to 50% black over 4 s, hold 18 s, back over 1 s).
* Then `D(500) D(2000) D(400) 100 255 255 255 SELF BA` (white-out).
* e003:
  * `100 255 255 255 SCREEN_TINT` (permanent white);
  * `D(100) D(100) D(0) 100 255 255 255 SELF BA`;
  * an event-55 handler re-flashes with `RANDOM 255` colours (chained strobe);
  * `0 0 0 0 SCREEN_TINT` clears.
* e005 (broadcast event 9, player damaged, by damage type):
  * type 1: `D(400) D(0) D(0) 100 0 0 180 BB` (red, instant, fades out over 400 ms);
  * type 6: `... 100 30 150 10` (green);
  * type 5: `... 100 150 30 10` (blue);
  * otherwise `... 100 75 0 180`.
* e009 water: on event 56 (player medium = water) `30 200 0 0 SCREEN_TINT` (30% blue, permanent) +
  `VIEW_OVERLAY 1`; on event 57 `0 0 0 0 SCREEN_TINT` + `VIEW_OVERLAY 0`. While drowning:
  `VIEW_SHAKE 4` + `D(500) D(0) D(0) 60 255 255 255 BB`.
* e00b: `D(0) D(5000) D(1000) 80 0 0 255 SELF BA` (red 80%, fade in 1 s, hold 5 s, off at once).
* e05b: `VIEW_OVERLAY 1` + `D(400) D(200) D(0) 50 255 255 255 SELF BA`; its 600 ms timer sends
  VIEW_OVERLAY 0.

### 0x8E VIEW_SHAKE (1: mode) → game.vt[0x3F0] → view.vt[0x108] 0x4313DD; per frame view.vt[0x150] 0x43149D
* The rendered view image is shifted through surface vt[0x8C]\(xTop=+0x404, yTop=+0x408, xBot=+0x40C,
  yBot=+0x410), a per-row shift interpolated from the top row to the bottom row.

| mode | per-frame behaviour |
|---|---|
| 0 | all offsets 0 (off) |
| 1 | xTop, xBot = 4 - rand(8) (-3..+4 px), new every rendered frame |
| 2 | 8 - rand(16) (-7..+8) |
| 3 | no change |
| 4 | one decaying jolt: SET stores vel +0x418 = 9999. On the first frame vel = 20, pos = 0. Then each frame `a = -(2*pos)/3; vel = a + 4*vel/5; pos += vel; yTop = pos; yBot = -pos` (int maths, so it is frame-rate dependent). When a == 0 && vel == 0 the mode goes back to 0. |

* verid1: e009 `PUSH 4; VIEW_SHAKE` on each drowning damage tick.

### 0xB0 VIEW_OVERLAY (1: on) → game.vt[0x41C] → view+0x41C = on
* This is a **wave/ripple distortion**, not a bitmap. Each frame (0x430B03), after the 3D render, a
  "span program" at 0x542B88 runs on the view surface (interpreter 0x407632):
  * 421 rows; row k is moved horizontally by `shift[k]` px (memmove of the row by +shift);
  * the shifts form a sine-like table in -5..+6 with a period of about 168 rows;
  * the table starts at y = viewTop + (scroll-63)/64.
* scroll (+0x420) decreases by the frame ms and wraps by +0x2A00 when ≤ -0x2A00. So the wave scrolls up
  at 1 row per 64 ms and repeats every 10.75 s.
* Row width = view width (640 or 504).
* Shift table (rows 0..420):
  ```
  0,0,0,1,1,1,1,2,2,2,2,2,3,3,3,3,3,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,6x22,5x8,4x6,3x5,2x5,1x4,0x9,-1x5,-2x5,
  -3x6,-4x9,-5x22,-4x8,-3x6,-2x5,-1x5,0x9,1x4,2x5,3x5,4x6,5x9,6x22,5x8,4x6,3x5,2x5,1x5,0x9,-1x4,-2x6,-3x6,
  -4x8,-5x22,-4x8,-3x6,-2x5,-1x5,0x9,1x5,2x4,3x6,4x6,5x8,6x22,5x8,4x6,3x6,2x4,1x5,0,0
  ```
  The exact list can be dumped from 0x542B88. Format: `cmd 1 x,y` / `2 w` / `3 shift` / `7` (do row) /
  `4 dx,dy`, ending with 12.
* verid1: underwater (e009 ev 56/57), e007 init `PUSH 0`, e05b (with a white flash).

### Not screen effects
* **0xB9 NATIVE_B9** (`v, id` → obj.vt[0xCC] = 0x44EA51): adds v to the object's item-controller counter,
  clamped to [0, max]. It belongs with the inventory natives.
* **Hurt images** KIRHURT1/2 and PICHURT1/2 are body images in the HUD body window, set with 0xC3
  SET_BODY_IMAGE by e004 from health (verid1: `0 8`, `0 9`, `0 10`, `0 11`, `0 -1`).
* **KIRKHIT.OVR / PICHOP.OVR** are the SET_PUNCH first-person overlay (image 6 or 7). That is the HUD/attack
  agent's area.

---------------------------------------------------------------------------------------------------
## 5. Mode and controls natives

### 0xA5 GAME_MODE (1: cmd) → game.vt[0x40C] 0x440937 (switch on cmd-2, table 0x440A4A)
| cmd | effect |
|---|---|
| 2 | game.vt[0x370]: left HUD state 0 (body, health, automap, object views all hidden) |
| 3 | game.vt[0x364]\(0): left HUD state 3 (body image + health bar shown, automap hidden), no click sound |
| 4 / 5 | automap +0x9C = 1 / 0 (overload interference over the life markers) |
| 6 / 7 | game+0x160 = 1 / 0 (player velocity zeroed on event 0x38) |
| 8 | stop the player: +0xF8/+0xFC/+0x100 = 0, +0xE4/+0xE6 = 0x8000 |
| 9 / 10 | automap marker height offset +0x2F4 = 3.0 / 0 |
| other | ignored |

verid1: e001 init `PUSH 9; GAME_MODE`; e001 ev 46 `PUSH 8; GAME_MODE` then `PUSH 6; SET_PLAYER_CONTROLS`.

### 0xAE SET_PLAYER_CONTROLS (1: mask) → game.vt[0x414] 0x440B28
* game+0x1E8 = mask. Effective mask +0x1E4 = mask, except while airborne (+0x1A4 = 0x57DA00 bit 0)
  without low gravity (+0x148 == 0): then it is forced to **6**, re-evaluated every frame at 0x43AF36.
* Default 0x5F (0x432FD3).
* Bits, tested in the player update 0x43AD04:

| bit | meaning | confidence |
|---|---|---|
| 0x01 | walk forward/back with keys (speed +0x164) | medium |
| 0x02 | turning with keys and mouse X (heading) | medium |
| 0x04 | look up/down: PgUp/PgDn flags +0x220/+0x224 and mouse Y (pitch) | medium |
| 0x08 | jump: X / Num-. sets +0x244 → 0x4B6800(jump impulse +0x16C) | medium-high |
| 0x10 | crouch: Enter / C sets +0x240 → target eye height 2.5, ramped by 0.5 per frame | medium |
| 0x20 | position-delta handling, distance per ms (footsteps or walk animation?) | low |
| 0x40 | vertical swim/fly movement when low gravity is set | low |

* verid1 values:
  * 0x1F: normal; e001 init; e007 after the intro;
  * 0x17: e007 intro, no crouch;
  * 0x06: e001 ev 46, only turn and look;
  * 0x5F: water, adds bit 0x40.

### 0xAF NATIVE_AF = SET_AUTO_TURN (1: rate) → game.vt[0x418] 0x440B90
* game+0x1EC = rate (signed). Every frame, if rate != 0, the player heading turns by
  `MulDiv(rate, frame_ms, 1000) * 32768/180` (0x43B6F6 → 0x4B6790), i.e. **rate degrees per second**.
* Default 0 (0x432FED). Unused in verid1.

---------------------------------------------------------------------------------------------------
## Event summary for these features
| event | sender | args |
|---|---|---|
| 14 | player touches the wall/flat/billboard/actor (engine code 1/2/4/6/10, see events.md); scripts open their close-up here | none |
| 35 | panel walk-away: player > 2.0 units from the point where the panel was created; every frame | none |
| 45 | left click on a panel with no buttons, or Space/Num0 on the visible panel | none |
| 47 | left click on a panel that has buttons/hotspots | 1: button/hotspot number (& 0x3FFF), 0 = missed |
| 55 | palette-effect slot 0 (BA) or 2 (BD) finished, to its notify object | none |
| 60 | NATIVE_6D panel animation finished (when ms != 0) | none |
| 62 | right click on a panel | none |

## Corrections to earlier notes
* natives-a.md / natives-c.md:
  * "vt[0xA4]\(image+1, name, 3) loads a 3-frame DIB": wrong. The 3 is a load-mode flag with no visible
    effect, and the frame count comes from bitmap height / window height.
  * "image+1 is the alternate state": wrong. image+1 is only the reload cache key.
* natives-b.md: the 0x7B style table is romu, **sorn, injr**, klng, chod, fish, fuzy, pred
  (+0xF0, +0x138, +0x114, ...).
* natives-c.md: GAME_MODE 2 hides the left-HUD views (state 0). GAME_MODE 3 selects the body/health view
  with no sound (the sound only plays on a key press).
* VIEW_OVERLAY is a scrolling horizontal ripple of the view rows, not a blitted bitmap.
* VIEW_SHAKE shifts the image with per-row interpolation between a top offset and a bottom offset. Mode 4
  is a vertical squash/stretch jolt.
