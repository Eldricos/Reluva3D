# Away-mission screen: layout and HUD (sgens.exe)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Confidence: H = read from code, M = code plus inference, L = guess.
"game" = CGame (vtable 0x531768, *0x545EA8 / *0x5454D0). Rectangles are (left, top, right, bottom)
and inclusive, as the code stores them. "abs" = 640x480 screen coordinates.

All HUD widgets are built in the game init 0x4336F0, between 0x433D17 and 0x434C95.
The borders are built in game.vt4A8 0x4378F1 and the subtitle line in game.vt4A4 0x43775E.

## 0. The widget system (H)

* **Containers** (class 0x422D0F, vtable 0x530508) are MFC windows. +0xA0 holds the container's
  screen rect. A container passes mouse messages to its children in screen coordinates
  (0x423BCB and the functions after it):

  | Windows message | container | child |
  |---|---|---|
  | WM_LBUTTONDOWN | vtF0 | vt74 |
  | WM_LBUTTONUP | vtF4 | vt78 |
  | WM_RBUTTONDOWN | vtF8 | vt7C |
  | WM_RBUTTONUP | vtFC | vt80 |
  | WM_MOUSEMOVE | vt100 | vt70 |
  | WM_TIMER | — | vt84 |

  vtEC(rect, 1) invalidates a rect.
* **Panels** (base ctors 0x42455F / 0x4246A1):
  * +4 is the container and +8 the parent.
  * +0x18 is the rect relative to the parent and +0x28 the absolute rect (relative rect plus the
    parent's origin).
  * Panels nest. vt6C draws the panel and vt54(on) shows or hides it.
* **Image panel** (0x425579, vtable 0x5307F0):
  * vtA4(id, file, 3) = 0x4256F9 loads an 8-bit BMP into the DIB at +0x54.
  * The BMP is a vertical strip of frames. Each frame is as tall as the panel, set by vt64(h).
  * vtA8(n) = 0x42598B selects frame n and repaints.
* **File lookup.** Bitmaps open through the resource file system (0x401A48 → 0x48D015).
  * A plain name (e.g. "actionx.bmp", "map\dot-tril.bmp", "Fonts\Action.fnt") is a global file,
    i.e. in COMMON.DAT (M). The repo also loads FONTS/ACTION.FNT and WAIT1.BMP from COMMON.DAT.
  * Mission files are built as `game+0x5C → +0x44` (the mission folder, e.g. "verid1\") plus the
    name.
  * Script resources (game.bin section 1) go through 0x44959D(i): a leading '*' gives the name
    without the '*' (global); otherwise the mission folder plus the name.

## 1. Screen layout (H)

```
y=0   +---------------------------------------------------------------+
      |  3-D view  (640x252, or 504x252 at x=68 with border bitmaps)  |
y=251 +---------------------------------------------------------------+
y=252 | left panel 0..205 (l-m-m-o.bmp)  | action panel 206..639 (actionx.bmp)
      |  tabs | map / body+health / object  | weapon display | inventory 3x4 |
      |       |                             | AUX viewer     |               |
y=461 |  subtitle line x 10..639, y 461..477 (over both panels)              |
y=479 +---------------------------------------------------------------+
```

### 1.1 3-D view (game+0x274, vtable 0x531610)

* The size comes from a table at 0x545310, indexed by the config value *0x565AB0+0x344
  (read by 0x488B47; view ctor 0x42F8B4 → 0x430686):
  * mode 0: W=640, H=252;
  * mode 1: W=504, H=252.
* Words view+0x78 = W and +0x7A = H; dword +0x7C = W/2.
* View rect (+0x50..+0x5C) = (x0, 0, x0+W-1, H-1), with x0 = (640-W)/2:
  * mode 0: **(0,0)-(639,251)**;
  * mode 1: **(68,0)-(571,251)**.
* The renderer (0x4B58A0(W, W/2)) draws into a **W x W/2** surface: 640x320 or 504x252. Only rows
  [vt118, vt118+H) are shown, with vt118 = (W/2 − H)/2 = **34 in mode 0** and 0 in mode 1
  (the present call is at 0x430DF7).
  * Focal length = W/2, so the horizontal FOV is 90°.
  * In mode 0, rows 34..285 of the 640x320 render are what you see.
  * Pick coordinates and every overlay add vt118 to y.
* Borders (mode 1 only, game.vt4A8 0x4378F1, only if the 0x488B47 value is non-zero):
  * container game+0x27C (id 0x64) at **(0,0)-(67,251)** shows **borderl.bmp**;
  * container game+0x280 (id 0x65) at **(572,0)-(639,251)** shows **borderr.bmp**.

### 1.2 Bottom console

The console is container game+0x278 (class 0x422D0F, id 2) at **(0,252)-(639,479)**, 640x228.
Inside it is group panel G (0x42530C, id 3) at the same rect, with no bitmap.

| field | class | id | abs rect | size | bitmap | notes |
|---|---|---|---|---|---|---|
| (ebp-4) action panel | 0x4254E3 | 4 | (206,252)-(639,479) | 434x228 | **actionx.bmp** (global) | background of the right side; the inventory has no own background (the grid art is in actionx) |
| +0x288 left panel | 0x4434B1 (vt 0x5325C0) | 5 | (0,253)-(205,458) | 206x206 | **l-m-m-o.bmp**, 4 frames of 206 rows | frame = mode: 0 off, 1 object, 2 map, 3 life |
| +0x28C | 0x426ABD | 6 | (43,415)-(71,441) | 29x27 | levels.bmp | map button (deck level) |
| +0x290 | 0x426ABD | 7 | (74,415)-(102,441) | 29x27 | zoomz.bmp | map zoom |
| +0x294 | 0x426ABD | 8 | (117,415)-(141,441) | 25x27 | s-life.bmp | scan filter: life |
| +0x298 | 0x426ABD | 9 | (141,415)-(165,441) | 25x27 | s-energ.bmp | scan filter: energy |
| +0x29C | 0x426ABD | 10 | (165,415)-(189,441) | 25x27 | s-lith.bmp | scan filter: trilithium (M) |
| +0x2A0 automap | 0x443739 (vt 0x532690) | 11 | (39,267)-(192,414) | 154x148 | markers map\dot-tril, dot-enrg, agpface2, dot-romu, dot-sorn, dot-injr, dot-klng, dot-chod, dot-fish, dot-fuzy, dot-pred, overload.bmp | map mode |
| +0x2A4 body | 0x425579 | 12 | (40,269)-(117,445) | 78x177 | <mission>\BODY.BMP or a script image | life mode |
| +0x2A8 health | 0x4459AD (vt 0x532728) | 13 | (118,270)-(189,442) | 72x173 | 3health.bmp, unithlt0.bmp | life mode |
| +0x2AC | (set by game.vt3CC) | | | | | optional extra life-mode widget, probably the shield (L) |
| +0x2B0 object | 0x425579 | 14 | (39,267)-(192,445) | 154x179 | <mission>\**OBJECT.BMP** | object mode |
| +0x2BC | 0x425EBF (vt 0x5308C0) | 0x15 | (207,253)-(219,268) | 13x16 | **wepcycle.bmp** | "next weapon" button |
| (weapon display) | e.g. 0x44B721 | 0x11 | (222,252)-(431,296) | 210x45 | per weapon, e.g. **weapphas.bmp** | created by SET_ACTIVE_ITEM (§5) |
| +0x2C0 AUX viewer | 0x448A58 (vt 0x532A48, *0x545EA0) | 0x10 | (223,311)-(433,458) | 211x148 | <mission>\**AUXDEF.BMP** (child image panel +0x64) | close-up windows (natives 6C/6E/9D/9E/9F) |
| +0x2B8 | 0x426122 (vt 0x530990) | 0x12 | (207,425)-(219,459) | 13x35 | **auxcycle.bmp** | cycles AUX windows; enabled when 2 or more exist |
| +0x2B4 inventory | 0x445F28 (vt 0x532858) | 15 | (435,256)-(639,457) | 205x202 | <mission>\**INVENT.BMP** (icon strip), grayout.bmp | §2 |
| +0x2C8 subtitles | 0x42B23E (vt 0x530F28) | 0x14 | (10,461)-(639,477) | 630x17 | Fonts\Action.fnt | §6; only if the subtitle option (0x488B15) is on |

Notes on the table:
* Child rects relative to the parent, as stored:
  * left panel children: levels (43,162,71,188), automap (39,14,192,161), body (40,16,117,192),
    health (118,17,189,189), object (39,14,192,192);
  * inventory (229,4,433,205) in the action panel; AUX (17,59,227,206); auxcycle (1,173,13,207);
    wepcycle (1,1,13,16);
  * weapon display (16,0,225,44); subtitles (10,209,639,225) in G.
* Nothing draws (0,252)-(205,252) or (0,459)-(205,479) apart from the subtitle text. That leaves
  the container background (L: black).

### 1.3 Left-panel modes (H)

The left-panel mode is selected by l-m-m-o frame 0..3 (0x288.vtA8). vtAC returns the current frame.

| mode | game method | key | tab hot rect (PtInRect, right/bottom exclusive) | shown children |
|---|---|---|---|---|
| 3 LIFE | vt364(sound) 0x43F679 | B | (6,261)-(28,312) | body +0x2A4, health +0x2A8, +0x2AC |
| 2 MAP | vt368 0x43F815 | H | (6,331)-(28,382) | automap +0x2A0 and the 5 buttons |
| 1 OBJECT | vt36C 0x43F9B1 | A | (6,401)-(28,452) | object +0x2B0 |
| 0 off | vt370 0x43FB4D | – | – | none |

* Tab rects are in left panel +0x16C/+0x17C/+0x18C (ctor 0x4434B1). The click handler is
  vt74 0x443617.
* Tabs and keys work only while the mode is not 0.
* A switch plays sample game+0x2EC (0x1000032) when arg = 1.
* Init calls vt364(0), so the panel **starts in LIFE**.
* GAME_MODE_CMD (A5): cmd 3 → vt364(0) (life), cmd 2 → vt370 (off).
* Map buttons: keys M, F, Z, L, E call vt374..vt384, which press levels, zoomz, s-life, s-energ
  and s-lith (button.vt98).

## 2. Inventory (game+0x2B4, CPanelInventory 0x445F28) (H)

### 2.1 Slots and drawing

* **12 slots**, no scrolling.
  * Slot positions come from the table at 0x545E40, relative to the inventory origin (435,256):
    `(1,1) (70,1) (139,1) / (1,52) (70,52) (139,52) / (1,103) (70,103) (139,103) / (1,154) (70,154) (139,154)`.
  * So the icon top-left corners are at x = **436, 505, 574** and y = **257, 308, 359, 410**.
    Slot i = row·3 + col, row-major.
  * Rect arrays built by init 0x4460B9:

    | field | rect | use |
    |---|---|---|
    | +0x208[i] | (x, y)-(x+63, y+45), 64x46 | icon |
    | +0x2C8[i] | (x-1, y-1)-(x+64, y+46) | repaint rect |
    | +0x388[i] | (x-2, y-2)-(x+67, y+49) | mouse hit rect |
* State:
  * +0x170[i] = icon index, or −1 when the slot is empty;
  * +0x1D0[i] = the item object;
  * +0x1A0[i] = slot disabled flag;
  * +0x448 = inventory enabled;
  * +0x200 = "cursor" slot (the last added, or set by keys; no visible mark was found in the draw);
  * **+0x204 = active (equipped) slot**;
  * +0x44C = slot being dragged.
* **Icon source.** One strip, <mission>\INVENT.BMP, with **46-row frames** (vtA4 override 0x446353
  sets the frame height to 0x2E). The frame number = the item controller's +4, returned by
  ctl.vt18. That is the `b` argument of native 0x82 ATTACH_ITEM_BEHAVIOUR(c, b, kind, id) (M for
  charge kinds, H for kind 1).

  verid1 items:

  | object | slot | icon | kind | PANEL_SHOW_IMAGE resource |
  |---|---|---|---|---|
  | 0x2004 | 0 | 1 | 1 | – |
  | 0x2001 | 1 | 0 | 1 | 3 TRICRDR |
  | 0x2002 | 2 | 3 | 2, the phaser | 2 PHASER |
  | 0x2003 | 3 | 2 | – | 1 HYPSPRAY |
  | 0x2010 | 4 | 7 | – | 28 PHARBATT |
  | 0x2009 | – | 4 | – | 5 AUXCLOAK |
  | 0x201D..0x2022 | – | 6 | – | – |
  | 0x212C.. | – | 5 | 6 | – |

  The .BMP names in game.bin are **not** inventory icons. They are AUX close-ups, body images,
  overlays and panel buttons.
* **Draw** vt6C 0x446414. For every occupied slot that is not being dragged:
  1. blit INVENT frame +0x170[i] into the icon rect;
  2. if !+0x448 or +0x1A0[i], overlay **grayout.bmp** (global) on it;
  3. else, if i == +0x204, draw a 1-px **rectangle in palette index 0x22** at
     (x+1, y+1)-(x+62, y+44) (4 lines, surface vt7C).
  This is the "active item" highlight.

### 2.2 Adding and removing

* INVENTORY_ADD (native 0x84) → game.vt27C → vt278 → **0x446DD0(slot, obj)**.
  * `slot = −1` takes the first free slot of **1..11**. Slot 0 is only filled when asked for
    explicitly.
  * Returns −1 if the slot range is full. The item is then simply not added.
  * An occupant of the chosen slot is removed (0x4472CC, event 44) and re-added to a free slot.
  * The new slot becomes +0x200. If no slot is active and ctl.vt8() is true (equippable: the charge
    weapon kinds; kind 1 returns 0), it also becomes +0x204.
  * Sends **event 43** to the item.
* Remove: 0x4472CC / 0x447013, **event 44**.

## 3. Inventory interaction (H)

### 3.1 Mouse

* **Left button down** in a slot's hit rect (slot enabled and occupied) → vt74 0x44687C:
  * sets +0x468 = slot and +0x46C = 1;
  * captures the mouse (0x4239D3);
  * game+0x58 = 0 (3-D input paused).
* **Left click** (released inside the panel without moving) → vt78 0x4469C2 → **0x44789E(slot)**:
  * if the item is equippable and not already active:
    * the old active item is "clicked" again first when its ctl.vt64() is true, which toggles it
      off;
    * +0x204 = slot, and both rects repaint;
  * then **event 45** goes to the item. Its script usually runs SET_ACTIVE_ITEM 0x8C.
* **Left drag** (mouse move of 4 px or more on either axis while held, slot ≠ 0) →
  vt70 0x446CFC → 0x44732C:
  * starts the drag (the dragged icon is INVENT frame ctl.vt18; the slot is hidden through +0x44C);
  * the drop handler is 0x44763A.
* **Drop inside the inventory** on a slot 1..11 (hit rect): the item moves there. A miss means the
  first free slot; an occupant is displaced.
* **Drop outside the inventory:**
  * the item is removed from the inventory and its AUX window restored (0x448C90);
  * obj.vt134(0) runs and the item gets **event 63 (0x3F)**, i.e. "dropped".
* Dragging an AUX close-up into the inventory adds it (caller 0x44857F). That is a pick-up, and it
  sends event 43.
* **Right click** (press and release within the same slot's hit rect) → vt7C/vt80
  0x446A89/0x446BBB → **event 62 (0x3E)** to the item (examine).
* wepcycle button (207,253): per frame (0x43AC95) it calls **0x447AE5**, which picks the next
  equippable, enabled slot after +0x204 (wrapping 0..11) and clicks it with 0x44789E.

### 3.2 Keyboard

Keymap: byte table 0x545500, keydown game.vt118 0x43E8FB, `action-1` indexes the jump table at
0x43F1E0. The defaults below can be overwritten from the config.

| key (vk) | action | effect |
|---|---|---|
| `[` (DB) | 49 | active item ctl.vt54: power level −1 |
| `]` (DD) | 50 | ctl.vt50 = 0x44A159: level +1 up to max +0x10, then the weapon display vtE4 moves the arrow |
| Space (20), Num0 (60) | 34 | 0x447A11(0): cursor +0x200 = slot 0 |
| actions 35..45 | – | cursor to slot 1..11; no default keys |
| action 32 | – | use the cursor slot (0x44789E(+0x200)); no default key |
| action 31 | – | remove the cursor slot (0x4472CC); no default key |
| Backspace (08) | 33 | game.vt398 → AUX 0x44933E (forwards to the visible AUX window, vtFC) |
| T (54) | 30 | game.vt39C → AUX 0x4493AC (forwards, vt100) |
| B / H / A | 22 / 23 / 24 | life / map / object mode |
| M F Z L E | 25..29 | map buttons |
| F2 / F3 | – | brightness/gamma down/up (config +0x350, view.vt138 rebuilds the palette) |
| Tab, End | 48 | no-op in keydown |
| Ctrl / LMB | – | fire or punch (player-attack.md) |
| RMB in the view | 52 | game.vt120(1) |

### 3.3 Cursors

Cursors are chosen per frame by view.vtD8 0x430756 and view.vtFC 0x4310B8.
* **target**: Windows cursor resource 0x8C, or the software **target.bmp** with hotspot (16,16)
  when *0x56FA2C. It is used when a held weapon is shown and the crosshair is above the gun line
  (item usable).
* **select/arrow**: resource 0x67, or software **select.bmp** with hotspot (2,2). It is used
  otherwise and outside the view.
* Functions: 0x4323D0 (target), 0x432489 (select).

## 4. Health and body (H)

* **Health bar** (game+0x2A8, class 0x4459AD, draw 0x445AFE). Value +0x9C:
  * **3health.bmp**, **3 frames of 50 rows**, drawn at **(118,393)-(189,442)** (widget top + 123):
    * frame 2 if health > 15;
    * frame 1 if health > 5;
    * else frame 0 (M: green / yellow / red).
  * Then **unithlt0.bmp** "units" at x = **119**: unit k (k = 0..health−1) has its top at
    `y = 442 − uh − 1 − 3·k − (k ≥ 15 ? 7 : 0)`, with uh = the unit bitmap height.
  * So it is a stacked segment meter growing upward, 3 px pitch, with a 7 px gap after 15 units.
* **Update path.** ADD_HEALTH 0x89 → vt354, DAMAGE_PLAYER 0x87 → vt34C. Both end in
  **game.vt35C 0x43A6C0(h)**:
  1. h = min(h, max game+0x204);
  2. bar+0x9C = h;
  3. bar.vt88 invalidates;
  4. game+0x200 = h.
  At init, +0x9C = game+0x200 and the bar repaints.
* **Body image** (game+0x2A4, (40,269)-(117,445), 78x177): SET_BODY_IMAGE 0xC3 → 0x441639(image,
  frame).
  * image = −1 gives <mission>\BODY.BMP. Any other value is a script resource through 0x44959D.
  * The panel loads it with vtA4(image+2, file, 3), then selects `frame` (frames 177 rows).
  * Init calls vt458(−1, 0).
  * verid1: 4 KIRKY-HO, 8 KIRHURT1, 9 KIRHURT2, 10 PICHURT1, 11 PICHURT2, all frame 0. Script e004
    picks the image from GET_HEALTH.
* Body and health are only visible in LIFE mode.

## 5. Held weapon in the view and the punch overlay (H)

* **Equip.** SET_ACTIVE_ITEM 0x8C → 0x439F9E:
  * the old ctl.vt6C() detaches its display;
  * the new ctl.vt68(actionPanel(id 4), rect (16,0,225,44), id 0x11, view);
  * game.vt3B8; then broadcast 0x31.
  * With on = 0: view.vtF8(0), which hides the held sprite, and vt3BC.
* Phaser (kind 2) vt68 = 0x44A291:
  * creates the weapon display 0x44B721 at **(222,252)-(431,296)** with **weapphas.bmp**;
  * plus a child **p-arrow.bmp** (rel (20,0,49,13)) placed at x = 222 + 11·level − 4;
  * clicking the display sets level = (x − 239)/11 + 1 (0x44B896);
  * a charge gauge is drawn by vt6C 0x44BA38 (fill at x+191, y+28, 7 px; ceil(59·charge/max) steps)
    (L for the exact orientation);
  * then **view.vtF4(0x430E06)("nuphaser.ovr", poses 0x5428D8, muzzle 0x542A30, 0x542A40)** and
    view.vtF8(1).
* Other kinds:

  | kind | overlay | display class | display bitmaps |
  |---|---|---|---|
  | 3 | nudisrup.ovr | 0x44C681 | rompisto / rpis*.bmp |
  | 4 | nudrifle.ovr | 0x44CD9C | romrifle / rrif*.bmp |
  | 6 | nuklidis.ovr | 0x44BBEA | kldiscon / kdis*.bmp |
  | 11 | nuplant.ovr | 0x44D638 | plntback, dart |
  | 12 | nuplant2.ovr | 0x44D945 | plntbac2, dart2 |

  romheavy and choarea/chosubdu/chokill belong to the remaining kinds (M).
* **The held sprite** (view+0x80): OVR frames are **88 rows** (0x4029A5(0x58)); there are 12
  frames.
  * Frames **6..11 are the idle poses** and frames **0..5 the same poses while firing**
    (frame = +0xC8 − 6 while flash counter view+0xAC > 0; +0xAC counts down by the frame ms).
  * Pose (dx,dy) hotspot table, 6 pairs (phaser): (116,34) (116,3) (75,0) (35,2) (35,33) (74,29)
    for poses 6..11. The pose names are: 6 right-low, 7 right-high, 8 centre-high, 9 left-high,
    10 left-low, 11 centre-low.
  * Per frame, vtFC(cx, cy) with the crosshair:
    * anchor `X = W/2 + 140·(cx − W/2)/(W/2)`;
    * `Y = A + (B − A)·cy/B`, with A = H − (88 − max(dy1,dy2,dy3)) and
      B = H − (88 − max(dy0,dy4,dy5))/2;
    * line T = H − max(dy0,dy4,dy5) − 5.
    * Phaser values: A = 167, B = 225, T = 213.
  * Pose choice, with the thirds of the view at W/3 and 2W/3:
    * **cy ≥ T**: the item is not usable (vt3BC, arrow cursor). Pose 10 / 11 / 6.
    * **cy < T**: the item is usable (vt3B8, target cursor). High poses 9 / 8 / 7 if
      `Y − dy(low pose) + 88 < H`, else the low ones.
  * Drawn (0x430D19) at `(X − dx, Y − dy + vt118)` in the render surface, so on screen the gun
    follows the cursor around the bottom of the view. With no item it is not drawn (view+0xA4 = 0).
* **Punch overlay** (view+0x948, SET_PUNCH 0xC4, e.g. KIRKHIT.OVR 504x252 frames):
  * drawn by surface vt94 0x409DDC at y = vt118 (the top of the visible viewport);
  * full height H = 252;
  * **horizontally centred**: x = (W − ovrW)/2, which is 68 in 640 mode and 0 in 504 mode.
  * The frame rate and the hit frame are in player-attack.md.
* **Full-view overlay** NATIVE_B8 (2 args: image on top, then param) → game.vt44C 0x4415A7 →
  view.vt104:
  * sets an extra centred OVR at view+0x480 (resource image, or −1 to remove);
  * drawn each frame before the punch;
  * not used in verid1.
* VIEW_OVERLAY 0xB0: a scrolling scan-line sprite *0x545308, drawn at y = vt118 + (scroll − 63)/64.

## 6. Text (H for placement, M for timing)

* Subtitles and messages go to the text panel game+0x2C8:
  * class 0x42B23E;
  * font **Fonts\Action.fnt** (COMMON.DAT FONTS/ACTION.FNT), held in a font object at +0x90;
  * rect **(10,461)-(639,477)**, one line 17 px tall, on top of the console art;
  * the restore background comes from panel 0x63, or 4 = actionx;
  * registered with the speech manager 0x41EB41 (game+0x80);
  * ticked every frame (vtAC(dt), 0x43ACF4);
  * defaults +0x54 = 1000 and +0x58 = 2000 ms (L: minimum and per-line display time);
  * drawn with GDI text in vt6C 0x42BD53.
* The panel exists only when the subtitle option 0x488B15 is on.

## 7. Other HUD elements

* There is no compass and no always-on mini-map. The automap is the left panel's MAP mode
  (154x148 at (39,267)). Its band setup is in natives-c.md (SET_AUTOMAP_LEVELS).
* **BLUETIME / GRENTIME / REDTIME.BMP** are not HUD bars. Actors put them as buttons (indices
  100..102) on their AUX close-up windows with AUX_ADD_BUTTON 0x9D. They show inside the AUX viewer
  (223,311)-(433,458), at the button's (x,y) relative to that panel.
* COMBADGE.BMP is resource 0. It is not found as a HUD element (L: an AUX or panel image).
* The weapon power arrow and charge gauge (§5) and the grayed-out inventory items are the only
  other dynamic HUD elements.

## 8. Files the away-mission screen loads

Global (COMMON.DAT, M), in order of loading:
* At init: actionx.bmp, l-m-m-o.bmp, levels.bmp, zoomz.bmp, s-life.bmp, s-energ.bmp, s-lith.bmp,
  map\dot-tril, dot-enrg, agpface2, dot-romu, dot-sorn, dot-injr, dot-klng, dot-chod, dot-fish,
  dot-fuzy, dot-pred, overload.bmp (automap), 3health.bmp, unithlt0.bmp, grayout.bmp (inventory),
  auxcycle.bmp, wepcycle.bmp, Fonts\Action.fnt (subtitles).
* In 504 mode: borderl.bmp, borderr.bmp.
* Software cursors only: select.bmp, target.bmp.
* wait1/2/3.bmp: busy cursor while loading, in game.vt488 0x436FFB.
* On equip: <weapon>.ovr plus the display bitmaps (weapphas.bmp, p-arrow.bmp, …).
* At the start or end of a mission: briefing.bmp, proceed.bmp.

Mission archive (`<mission>\`):
* game.bin;
* OBJECT.BMP (object mode);
* INVENT.BMP (inventory icons, 64x46 frames);
* BODY.BMP (default body image);
* AUXDEF.BMP (AUX viewer background);
* script resources on demand: body images, punch .OVR, AUX close-ups, panel buttons.
