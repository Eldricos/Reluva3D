# Star Trek: Generations — space combat (sgens.exe), implementation report

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Confidence on every claim: **H** read from code or verified against the real files, **M** inferred from code shape
or data, **L** a guess. Addresses are sgens.exe VAs (image base 0x400000).

Working material (working files, none of it kept):

| what | path (S = the working folder) |
|---|---|
| COMBAT.DAT unpacked | S/combat/ |
| Ghidra 11.1.2 decompile of the whole exe (5851 functions, one file per function) | S/tools/fn/00xxxxxx.c, all in S/tools/out/all.c |
| Python decoders written for this report (3DV/MTL loader + software renderer, RAS, LBM, SPR, PAL) | S/re/cb/model.py, S/re/cb/fmt.py, S/re/cb/fn.py (annotated disassembler) |
| speech table dump (id, channel, file, subtitle) | S/re/cb/speech.txt |
| proof renders | S/cpng/models.png (6 ships 3DV + 6 low-LOD 3DR, textured), S/cpng/ortho.png (axis check), S/cpng/debris.png (PC1-6 debris, shields, phaser/photon/billboard models), S/cpng/spr.png + S/cpng/spr_pal.png (SPR animations decoded, raw and with STANDARD.PAL), S/cpng/combat_ras_own.png (console), S/cpng/hud_mock.png (console + panels at their code positions), S/cpng/bstars.png, S/cpng/COMBAT1.png, COMBAT3.png (briefings) |

**Correction to frontend.md (H):** the combat result codes in `0x565A90` are *not* "0 lost, 1 won, 2 destroyed". GM state 2
(0x418F7A, case module type 6) does: **0 → `Jump EnterpriseDestroyed`; 1 → `Jump <Lost>` (GM+0x70); 2 or 3 → `Jump <Won>`
(GM+0x6C) and marks fleet n beaten (`0x56E918[n*0x78] = 1`)**. See §5.

---------------------------------------------------------------------------------------------------

## 0. Architecture in one page

* Module class: ctor **0x47B375** (vtable 0x5362C0). arg n (1..25) → `+0x54 = n-1`, global `0x565964 = n` (the mission
  number), +0x58/+0x5C = 640/480. vt10 = **0x47B4D6** briefing, vt3C = **0x47B688** create the combat window, vt14 =
  **0x47B83E** tick, vt18 = 0x47C6D0 finish. (H)
* The combat engine is a separate library ("sc", source paths `C:\stgens\gens\sc\space\*.cpp`: space.cpp, geoload.cpp,
  weapons.cpp, camera.cpp, fx.cpp, stars.cpp, anim.cpp, tactics.cpp) on top of a 3D library ("DRE", 0x4DFxxx–0x4E9xxx). (H)
* **The combat screen is 16-bit high colour**, 640x480, pitch 1280: `0x54A0D4=640, 0x54A0D8=0x500, 0x54A0DC=2` (bytes per
  pixel) set in 0x47B688; back buffer 0x96500 bytes at `*0x56597C`. Pixel packing uses DirectDraw's masks
  (0x47CAB5 `rgb→pixel`, shifts at 0x565978/0x565954/0x565934). Our port should render combat in true colour (H).
* Time base: `0x565940` = game clock in **ticks of 1/291 s** (timeGetTime ms / 3.4364, 0x47B86B). Every constant below that
  is a time is in ticks: 291 = 1 s, 582 = 2 s, 1455 = 5 s, 4365 = 15 s, 5820 = 20 s. Frame dt `0x57012C` is clamped to
  1..291 ticks; `0x570130 = dt/291` = seconds (0x494D18). (H)
* Per frame (combat window tick 0x47B83E → 0x496214 → **0x494D18**): UI timers (table 0x54A180), clear viewport, stars
  (0x4ADC18), DRE frame (0x4E2D70), camera (0x4A0699), every ship pass 0 (AI/helm/flight) then pass 1 (effects, render
  hooks) via 0x4A2AD2, explosions (0x4ACC4A), end test, torpedoes (0x49A1C0), console redraw (0x4AAEC7, 0x4AA350,
  0x49FE37, 0x49E232, 0x49F302, 0x4A97B7 per ship), message line, then keyboard (0x4A6976). (H)

---------------------------------------------------------------------------------------------------

## 1. COMBAT.DAT formats

COMBAT.DAT is a ZIP (997 entries, flat). The game mounts it as `%s\DATA\COMBAT\COMBAT.DAT` or a `%s\COMBAT\` directory
(strings 0x5492D0–0x5492F8). The DRE loaders prefix `OBJECT\` (models, materials) and `TEXTURE\` (textures) but fall back
to the bare name (0x4E4C79), so in the flat archive the bare name is what matters. (H)

### 1.1 Index of file types and what uses them

| ext | count | format | used by | conf. |
|---|---|---|---|---|
| .3DV | 83 | DRE object v1.10 (§1.2) | ship hi-detail LOD, effects (PHASER, PHOTON, SHIELD*, SHLD8*, ?BOARD billboards) | H |
| .3DR | 6 | same format, 1 part, no UVs | ship low-detail LOD (flag 0x8000) | H |
| .PC1–6 | 7×6 | same format | debris chunks when a ship explodes; GENERIC.PC* for ships without their own | H |
| .MTL / .MTR / .DMG | 61/6/7 | DRE material list v1.10 (§1.3) | materials for 3DV / 3DR / damaged textures | H |
| .IMG | 200 | **GIF87a** (any size, own 256-colour palette) | model textures | H |
| .LBM | 4 | IFF **PBM** (chunky), ByteRun1 or raw | effect textures: BOOM, PHOTON, PHASER1, SHIELD | H |
| .RAS | 92 | **Sun rasterfile**, 8-bit, big-endian, own colormap (§1.5) | the console/HUD | H |
| .SPR | 27 | "SPRT" compressed animation (§1.6) | explosions, torpedoes, shield hits, phaser beams | H |
| STANDARD.PAL | 1 | 768 bytes, 6-bit VGA RGB | palette for SPR frames and the starfield colours | H |
| .MET | 2 | glyph metrics (§1.7) | FONTLIT (A-Z_0-9.RAS), FONTSCAN (SCANNER.RAS) | H |
| SYSTEMS.DAT | 1 | text, ship classes (§1.8) | all ship stats | H |
| SC*.MIS | 29 | text mission files (§1.9) | SpaceCombat n → SCn.MIS | H |
| *.BIN | 11 | compiled "TACTICS" AI scripts (§3.7) | per ship type `<model>.bin` | H (role), L (bytecode) |
| COMBATn.BMP, PROCEED.BMP, CURSOR2.BMP | 25+2 | Windows BMP, 8-bit | briefing (§5) and the mouse cursor | H |
| *.WAV + *.TXT | 215/174 | PCM 22050 Hz 16-bit (speech mono, SCLOOPn stereo); TXT = subtitle line | speech, effects, music | H |
| BSTARS.MRG, ASTROMAP.DB, LEVEL0-4.DAT, ACBMAPBN, APCLAHAP | | (see §1.10) | **not referenced by the exe** | H (no name in exe) |
| BX/CX..HX.3DV/.MTL/.BIN, *.IMG they use | | a 6-faced box textured with developer photos | easter egg, BX.BIN tactic "Welcome to the next level." | M |

### 1.2 Models: .3DV / .3DR / .PCn (loader 0x4E4C30) — H, verified on all 131 files

```
file header (30 bytes)
  u32  size            ; = file length - 30; the following `size` bytes are one relocatable block
  u16  version         ; 0x6E = 110 = "v1.10"; anything else is rejected ("Incompatible object format")
  u16  maxVerts        ; largest part's vertex count (<= 5000)
  u16  maxFaces        ; largest part's face count (<= 5000)
  char name[20]        ; source name, garbage after NUL
block (offsets below are relative to the block start = file offset 30; the loader adds the block address)
  +00 u32  0 (runtime)
  +04 i32  numParts
  +08 u32  partsOffset            ; array of numParts * 44-byte part records
  +0C u32  0
  +10 f32  size[3]                ; bounding-box extent x, y, z
  +1C f32  centre[3]              ; bounding-box centre
  +28 u32  partFlagsOffset        ; numParts bytes (all 0 in the shipped files)
part record (44 bytes)
  +00 i32 numVerts  (negative = 4-vertex billboard quad: ?BOARD.3DV, PHOTON.3DV)
  +04 i32 numFaces
  +08 i32 2         (always 2; L: "triangles")
  +0C u32 vertsOff   -> numVerts * f32[3]
  +10 u32 uvOff      -> numVerts * {i32 u, i32 v}  16.16 fixed, 0..0x10000 across the texture; 0 in .3DR (untextured)
  +14 u32 0, +18 u32 0
  +1C u32 facesOff   -> numFaces * {u16 material, u16 a, u16 b, u16 c}   triangles, material = index into the MTL list
  +20 u32 vnormOff   -> numVerts * f32[3] unit vertex normals
  +24 u32 fnormOff   -> numFaces * f32[3] unit face normals, n = normalize((b-a) x (c-a))
  +28 u32 planeOff   -> numFaces * f32 d, with n·p + d = 0
```
Axes: **x = span, y = up, z = forward** (Bird of Prey head at +z, wings rise to +y in cruise; Enterprise saucer at +z);
left-handed, D3D-style (H for axes from the shapes, M for handedness). Units: Bird of Prey 102 wide, Enterprise 465×641,
Warbird 806×1095, starbase ~13000. Parts are just groups (21 in KLINGON.3DV); KLINGON2.3DV has 75 parts = the BoP with
several wing positions (M: wing animation, not used by any mission, see §3.1).

Each ship type gets three LODs (0x4A40F2): 3DV until the distance in table 0x54E5D8[type*8] (chodak/romulan/garidian
13000, enterprise 10000, klingon BoP 1800 (sic), probe 250, ferengi 5500, starbase/vorcha 220000), then 3DR (only when the
type flag 0x8000 is set) until 0x54E5DC[type*8] (128000 / 250000), then a billboard (`bboard.3dv`+`.mtl` built by
0x4A3210) up to 0x3E800 = 256000. Ships whose flags lack 0x8000 have two LODs (3DV, then billboard). (H)

### 1.3 Materials: .MTL / .MTR / .DMG (loader 0x4E5520) — H

```
u32 size (= count*36), u32 count, u16 version 110, char name[20]
count * 36-byte records:
  +00 u32 colour      ; RGB555 (r<<10|g<<5|b): flat colour; used when untextured and for the low LOD
  +04 u32 param       ; 0, 4096 (SHIELD.LBM materials), 11000 (shield spheres) — M: translucency/intensity
  +08 u32 flags       ; 1 textured opaque (ships), 2 textured effect (LBM billboards/beams, black = transparent, M),
                      ; 3 flat (all .MTR), 0 flat translucent (SHLD8x spheres), 0xA01 the easter-egg boxes
  +0C u32 runtime texture pointer (0 in file)
  +10 char texture[20]; "" = no texture; otherwise loaded by 0x4E4120 (.IMG = GIF, .LBM, .PCX)
```
* `<ship>.MTL` goes with `<ship>.3DV`, `<ship>.MTR` with `<ship>.3DR` (one flat material), `<ship>.DMG` has the same
  layout as the MTL with `D`-prefixed damaged textures (DPRYENGS.IMG …): fx.cpp 0x4ABBE4/0x4AC140 swaps textures to the
  damaged set as hull damage grows (flag 0x800 on the type) (H for load, M for when).
* Shields: `SHIELD_<c>.3DV/.MTL` (c = '1'..'9','a'..'f' = ship slot, so every ship gets its own copy because the loader
  caches by name) is a flat ellipsoid with SHIELD.LBM; `SHLD8<k>_<c>.3DV` (k = 4 for ships with 4 shields, else 0) is a
  26-vertex sphere whose faces use 4 materials = the 4 shield facings; `SHLD8<n>_0.MTL` (n = number of shields 1..4)
  colours them (magenta 0x7C1F, blue 0x001F, green 0x03E0, red 0x7C00, all param 11000) (H for names, M for meaning).

### 1.4 Textures

* **.IMG are plain GIF87a** files with their own palettes (3 distinct palettes over 200 files); PIL/any GIF decoder reads
  all 200. Sizes are arbitrary (e.g. 70x44, 728x542); UVs are normalised so no power-of-two is needed (H).
* The renderer works in 16-bit colour, so each texture is converted to direct colour at load time; there is no
  palette remapping to a combat palette (M: follows from the 16-bpp target and per-file GIF palettes).
* .LBM: IFF FORM/PBM with BMHD + CMAP + BODY; BOOM 128x128 (ByteRun1), PHOTON 8x8, SHIELD 32x32, PHASER1 8x2 (raw).
  `fmt.lbm()` decodes them (H).

### 1.5 Console bitmaps: .RAS (loader 0x49C23E) — H

Sun raster: big-endian header `59 A6 6A 95, width, height, depth=8, length, type=1, maptype=1, maplength`, then the
colormap as **R plane, G plane, B plane** (maplength/3 entries, 8-bit values), then rows padded to an even byte count.
All 92 files are type 1 (uncompressed) — `fmt.ras()` decodes them. Each has its own colormap; the loader converts to the
screen's 16-bit format (via 0x47CAB5). **RGB (255,0,163) is the transparent key** (it fills the viewscreen hole in
COMBAT.RAS and the background of every overlay) (H from the images, M for the mechanism). The full list with sizes and
roles is in §4.

### 1.6 Animations: .SPR (anim.cpp 0x4AF7B0 load, 0x4AF953 parse, 0x4AFC27/0x498050 decompress) — H, all 27 decode

```
'TRPS' u32 size u32 0x100           ; FOURCC 'SPRT' stored little-endian, as all chunk ids
'TSIL' u32 size u32 1 u32 16        ; 'LIST' header, then a sequence of chunks; every chunk = id, size(incl. 8-byte hdr)
  'EMIT' 12  u32 666                ; 'TIME' frame duration = 666 (ms per frame, M) — precedes frames 2..n
  'PMOC' size u32 w u32 h  data     ; 'COMP' frame, max 23 frames ("Too many frames in anim")
frame data: u8 bg, u8 0, u8 mode, u8 0, then
  mode 1: MSB-first bit stream, predictive: prev starts at -1
     0xxx            4 bits: prev += (x&4) ? -1-(x&3) : (x&7)+1; emit prev
     10ss ccc        7 bits: d = s(2 bits, 2→-2, 3→-3); prev += d+1; emit prev c+1 times
     110v vvvv vvc  11 bits: prev = 0x80+v(7); emit 1 or 2 times
     1110 ccccc      9 bits: emit bg c+1 times
     11110 vvvvvvv cccc 16 bits: prev = 0x80+v; emit c+1 times
     111110 vvvvvvvv cccccccc 22 bits: prev = v; emit c+1 times
     111111          7 bits (6+1): emit a whole row (w pixels) of bg
  mode 2: bit stream: 00 nnnnnnnn → bg×(n+1); 01 vvvvvvvv → v; 10 vvvvvvvv ccc → v×(c+1); 11 vvvvvvvv nnnnnnnn → v×(n+1)
  mode 3: i32 at +4 = byte distance back to an earlier frame's data → reuse that frame
  mode 4: raw w*h bytes, pixels equal to bg are left unchanged (transparent)
```
The pixel values are **indices into STANDARD.PAL** (6-bit VGA, scale ×255/63); 0 = black, drawn transparent/additive
(S/cpng/spr_pal.png shows fiery explosions and the green Klingon shield flash with that palette). Sets (prefix =
per-type letter from 0x54E690[type*8], used only when the bit in 0x54E694 allows it, else `__`):

| prefix | type | shield hit | phaser beam | torpedo |
|---|---|---|---|---|
| E | Enterprise | E_SHIELD 128² ×7 | E_PHASER 100x5 ×11 | E_PHOTON 80² ×5 |
| K | Klingon BoP / Vor'cha | K_SHIELD 128² ×7 | (default __PHASER) | K_PHOTON 40² ×5 |
| R | Romulan (+Garidian) | R_SHIELD 128² ×6 | R_PHASER | R_PHOTON 64² ×8 |
| C | Chodak | C_SHIELD | C_PHASER | C_PHOTON 64² ×8 |
| P | probe | P_SHIELD | P_PHASER | — |
| __ | default | __SHIELD, __PHASER 120x8, __PHOTON, __BOOM | | |

Explosions: 1_BOOM..5_BOOM 256² ×12, 3DBOOM/__BOOM 320x200 ×10. FINAL.SPR (562x334, 1 frame) is a picture of a
console (L: unused). The same textures are played on billboards as "ANIMTEXTURE"s (0x4AF42A create, 0x4AF598 attach to a
material, 0x4AF621 next frame).

### 1.7 Fonts: .MET — H

96 records of 8 bytes, **big-endian** u16 `x, y, w, h`, for ASCII 32..127, giving each glyph's cell in a RAS strip:
FONTLIT.MET → A-Z_0-9.RAS (160x10, capitals and digits only, h 10), FONTSCAN.MET → SCANNER.RAS (1000x18, full ASCII,
proportional, h ≤ 16). Missing glyphs point at a 1-px blank cell. Text is drawn by 0x47C85F(x, y, font, colour?, str)
(font 2 = FONTSCAN for messages, 0 = FONTLIT for small readouts) (M for the font numbering).

### 1.8 SYSTEMS.DAT — the ship classes (parser 0x49C770) — H

Text with `#` comments, one block per class, keyed by a 3-letter token (table 0x54DCF8): `cho`→type 0 Chodak, `war`→1
Romulan Warbird, `ent`→2 Enterprise, `bop`→3 Klingon Bird of Prey, `pro`→4 probe, `vor`→9 Vor'cha. Each block (stored in
a 0x194-byte class header + 27×0x40 system records):

* Header line `name maxspeed agility hitpts closeRadius destruct% numPhasers numPhotons numShields torpedoes` →
  +04 max speed, +08 agility, +0C hit points, **+14 close radius, +10 destruct%** (note the swap), +18 phaser banks,
  +28 torpedo tubes, +68 shields, +2C torpedo stock.
* 27 system lines `MAX CHARGE DISCHARGE REPAIR a b` (record +00,+04,+08,+0C,+10,+14), in the order SYS_SHIP,
  SHIELD_1..4, WEAPONS, LIFE_SUPPORT, ENGINES, POWER, COMPUTER, SENSORS, DCONTROL, CLOAK (0..12), PHASER0..9 (13..22),
  PHOTON0..3 (23..26). MAX = -1: system absent. For systems 1..12 `a` is a **cumulative threshold for random internal
  hits** (0x4A5340: r = rand(0..99); the highest system 12→5 whose `a` ≤ r is hit) and `b` is the **% chance that a shot
  aimed at that system really hits it** (0x4B0103). For weapons the 5th field is a **firing-arc digit string**: octants
  0..7 in which the bank/tube can fire (bit mask at +18). For PHOTON tubes `MAX` is the number of torpedoes per salvo.
* One "Shield #k" table per shield: 13 values = % chance of a hit leaking into the hull at shield energy
  120,110,…,0 (indexed energy/10; class header +0x6C + shield*0x34).
* `Accuracy Range Penetration` for phasers (+1C, +20, +24), `Accuracy Range Damage Concussive` for photons
  (+30..+3C), and `Cone degrees per torpedo fired` for 1..10 torpedoes (+40..+64).

Values for the shipped classes are in the file (Enterprise: speed 900, agility 90, 9 phaser banks, 2 tubes of 10, 4
shields 120/90/90/70, phaser range 30000, torpedo range 40000 damage 45). Unused by the code as far as traced: hit pts,
destruct%, the two accuracy columns, penetration and concussive (M: no reader found among the traced combat paths).

### 1.9 Missions: SCn.MIS (parser 0x4A36A4, file opened by 0x47C7BA as `sc%d.mis` with n = `0x565964`) — H

* `SpaceCombat n` → **SCn.MIS**, n = 1..25 (the developer module ids 2101..2125 map to the same 1..25). SC26–SC29 are
  never loaded (copies of a test mission).
* Line rules: lines starting with `#`, `$`, CR or LF are ignored — so **every `:` header except `:wave` and the whole
  `$ triggered …` sound section are ignored** (the `:ai_file`, `:plane_file`, `:event_file`, `:sound_file`,
  `:model_file`, `:bintarg`, `:mission` keys are leftovers of a generic mission editor).
* `:wave N` sets the wave of the following ship lines.
* Ship line: `sscanf("%d %s %d %d %d %d %d %d %s %c %d %d")` = `id type x y z yaw pitch velocity AI obj? wave_count
  target_pref`. **Used: type, x, y, z, yaw, pitch, AI. Ignored: id, velocity, obj?, wave_count, target_pref.**
* type (0x4A4DA2, case-insensitive): `chodak`→0 (flags 0xFE00), `romulan`→1 (0xFE00), `player`→2 Enterprise (0xDE00),
  `sm_klingon`→3 **Bird of Prey** (KLINGON.3DV), `klingon`→9 **Vor'cha** (VORCHA.3DV, 0x6E00), `probe`→4, `ferengi`→5,
  `starbase`→6, `garidian`→7, `shuttle`→8. Type flags: 0x200 has phasers, 0x400 has torpedoes, 0x800 damage textures,
  0x4000 loads `<model>.bin` tactics, 0x8000 has a .3DR LOD, 0x8000000 friendly. Exactly one `player` line is
  required ("Screen has no enterprise alias").
* Coordinates: x, y = the horizontal plane (y = "north"), z = altitude. Engine world vector = **(x, z, y)** (stored at
  ship+0xF0/F4/F8; DRE y is up). yaw/pitch in degrees → angle units ×16384/360; yaw 0 faces +y (enemies at +y use
  yaw ≈ 180 to face the Enterprise at the origin).
* AI column: `soran` → flag 0x40000 (Soran's own ship), `quarash` → 0x60000 (the Q'urash), `soran9` → 0x50000,
  `standard-N` → ship+0x514 = N if 5 ≤ N ≤ 12 = the internal system it aims at (5 weapons, 6 life support, 7 engines,
  8 power, 9 computer, 10 sensors, 11 damage control, 12 cloak), else -1 (random); plain `standard` = -1.
* Waves: ships with wave 1 are active; others are created hidden (flag 0x20000000, 0x4AD58C(0x4000)). When the target
  cycler (0x4A2FA9) finds no live enemy, it activates every hidden ship of the **lowest remaining wave number**, adding
  the Enterprise's current position to their coordinates (waves are placed relative to the player). (H)
* Ship names on the HUD: class name (0x54E660: DREADNOUGHT, WARBIRD, NCC-1701D, BIRD OF PREY, PROBE, …, VORCHA) with
  " %d" appended when the type occurs more than once (H).

### 1.10 Files the exe never names (H: no string in the exe; M: so they are unused)

LEVEL0-4.DAT (35 u32 each, a difficulty-like ladder), BSTARS.MRG (u16 count=7, 8 u32 offsets, 7 images of
u16 w=100,u16 h=100 + 8-bit pixels in STANDARD.PAL: star/nebula tiles, S/cpng/bstars.png), ASTROMAP.DB (513 offsets +
records containing 1990s heap pointers), ACBMAPBN, APCLAHAP (63488 bytes, compressed-looking). The starfield is
procedural (§2.4). They can be ignored.

---------------------------------------------------------------------------------------------------

## 2. The renderer

### 2.1 Screen layout and buffers (H)
* Console: COMBAT.RAS 640x480 drawn once; the 3D view is rendered into a 532x231 (0x214x0xE7) buffer blitted at
  **(52,10)** (viewport 0x54F650.. = l 52, t 10, r 584, b 241) — the magenta hole of the console (0x4A872B). The
  viewscreen frame strips VIEW1..4.RAS are drawn over it at (52,10), (528,10), (108,10), (108,231) during the open/close
  animation (state 0x570128) (H).
* Clicking the Viewscreen hotspot (0,0)-(639,249) toggles an enlarged view (0x49F302 changes the viewport down to
  y 461) (M).

### 2.2 Camera (camera.cpp 0x4A00C0–0x4A21F6) (H unless noted)
A stack of 30 camera requests (24 bytes: function, entity, mode, priority, expiry); each frame the unexpired one with the
highest priority runs (0x4A0577); default expiry 5820 ticks (20 s). Mode table 0x54E3E8 (19 modes). The two the player
uses:
* **Forward view (mode 18, 0x4A0D13)**: a chase camera behind the Enterprise along its own axes, at distance
  `radius*2 + zoom*600`, oriented like the ship.
* **Enemy view (mode 12, 0x4A1E85)**: camera on the Enterprise→target line, behind the Enterprise by
  `radius + zoom*600`, looking at the target.
The Viewscreen Zoom slider (hotspot 618,127–633,180) sets `0x54A0B0` (0..n). Other modes are cinematic (explosions,
fly-bys) (M). Projection: `0x4E7EB0(0,0,1,1, .5,.5, 0xAAB)` = full viewport, centred, **FOV 0xAAB = 60°** (M: horizontal
or vertical not confirmed).

### 2.3 Lighting, shading, sorting (M)
DRE ambient 0.55 (`0x4E9090(0x3F0CCCCD)`), one directional light (0, -1, -1) normalised (0x4E90A0). Face normals and
plane equations are stored per face, vertex normals per vertex; the obvious reading is flat or Gouraud Lambert on the
16-bit texels. Back-face culling by the plane equation (L). Ordering: DRE draws objects far-to-near (painter's) — the
port can use a depth buffer.

### 2.4 Starfield (stars.cpp, 0x4ADA40 init, 0x4ADC6A draw) (H)
2200 stars at random (x, y) in a 1920x1920 wrapping plane, each with a type 0..12, plus 7 "streak" stars. Every frame the
plane is shifted by the camera's yaw/pitch change ×0.11717 px per angle unit (1920 px per turn) and rotated by roll; zoom
scales by 1+zoom/30. Star shapes 0x4AE2BF: single pixel (palette index 0x87 or 0x84 at random), plus/cross/bar shapes
of 2-7 pixels with indices 0x82/0x84/0x87/0xA9 of STANDARD.PAL. The 7 streak stars fly outward at a rate proportional
to speed/maxspeed×0.075 and respawn when off-screen or near the centre (a cheap "speed" cue).

### 2.5 Effects (H for the pieces, M for exact blending)
* **Phaser beam**: PHASER.3DV (a 9-vertex, 256-long beam along +z) with PHASER1.LBM, stretched each frame from the firing
  ship (for the player: from the camera position) to the target (0x49B45B), plus the per-type `?_PHASER.SPR` animation;
  damage is applied when the animation passes frame 3 (§3.4).
* **Photon torpedo**: PHOTON.3DV billboard + `?_PHOTON.SPR` animation (0x4996F4), one billboard per torpedo.
* **Shield hit**: the hit ship's shield model flashes with `?_SHIELD.SPR` at the impact point (0x497B99) or the whole
  facing glows (0x497B00 when the facing is already down) — sound `Shields%d.wav` / `KShield1.wav` (0x497B99).
* **Explosion** (0x4AC2D3): 6 debris models `<type>.PC1..6` (or `GENERIC.PC1..6` scaled by radius/Enterprise radius)
  with random spin (±0x564 units) and velocity (ship velocity × k + rand(±250)), each with a BOOM billboard
  (`?_BOOM`/BOOM.LBM); sounds Explo1..4 at +2, +3, +4, +5 s (speech ids 0x6C..0x6F). When the Enterprise blows up the
  screen does the white flash/fade of state 0x570128 (0x47BC14 palette ramp).
* **Target box**: four lines around the target's projected bounding box in the ship's colour (+0xE6C) (0x4A2AD2 pass 1).
* **Cloak**: ships with a CLOAK system fade (0x4AD474/0x4AD604); a cloaked target cannot be locked
  ("Target cloaked - no weapons lock", torpedoes fly unguided) (M for visuals).

---------------------------------------------------------------------------------------------------

## 3. Simulation

### 3.1 Ship object (0xED0 bytes, built by 0x4A40F2) — key fields (H)
| off | meaning |
|---|---|
| +18 (byte) | type 0..10 |
| +28 | dead/removed |
| +2C | flags: 0x20 waypoint steering, 0x80 exploded, 0x10000/0x20000/0x40000 Soran ship kinds, 0x20000000 not yet arrived (wave) |
| +30 / +34..3C | radius / extents |
| +D0 | wave number; +D4 0x4000 = hidden/gone |
| +F0,F4,F8 | position (world x, up, north) ; +FC..104 velocity |
| +114,118,11C | yaw, pitch, roll (14-bit, 0x4000 = 360°) |
| +120.. | helm input block: +124 yaw rate, +128 pitch rate, +12C roll rate (each −100..100), +130 speed setting (−100..100), +134 stop |
| +154 / +158 | target speed / current speed (units per second) |
| +17C..188 | waypoint and its enable flag |
| +50C | current target ship |
| +514 | targeted system (AI) |
| +528 | class header (SYSTEMS.DAT) ; +52C.. 27 system records of 0x40 |
| +C20.. | phaser bank firing states (0x38 each) |
| +BEC | torpedo stock |

System record (0x40): +00 MAX, +04 charge rate, +08 discharge, +0C repair, +10 cumulative hit threshold, +14 aimed-hit %,
+18 arc mask, **+1C current energy/charge**, **+20 damage taken**, +2C..+34 repair timers, +3C flags (1 dirty, 2 under
repair, 8 destroyed). A system is destroyed when damage ≥ MAX (0x4B07C7); usable energy is capped at MAX − damage.

### 3.2 Controls and their defaults (H)
Key → action map = SGENS.CFG starship table (cfg+0x238, indexed by VK). If SGENS.CFG is missing or invalid the defaults
are written by **0x488A23**. Action codes (Controls-panel order → code, table 0x548B50) and defaults:

| Controls panel | code | default keys | effect (0x4A6976) |
|---|---|---|---|
| Fire Phasers | 9 | Shift | = click the Fire Phasers button (0x496422 → 0x49ABBC); repeat lockout 291 ticks |
| Fire Photons | 10 | Enter | = click Fire Photon Torpedoes (0x4999AB); lockout 291 ticks |
| Increase Speed | 6 | Num + | speed setting += held_s×100 (clamp ±100) |
| Decrease Speed | 7 | Num − | speed setting −= … (negative = reverse) |
| Turn Left | 4 | Left, Num 4 | yaw rate −= held×100 |
| Turn Right | 5 | Right, Num 6 | yaw rate += |
| Roll Left | 2 | Ctrl | roll rate − |
| Roll Right | 3 | Num 0 | roll rate + |
| Climb | 0 | **Down**, Num 2 | pitch rate + (flight-stick convention) |
| Dive | 1 | **Up**, Num 8 | pitch rate − |
| Close | 0x10 | 1 | helm mode 0 |
| Evade | 0x11 | 2 | helm mode 1 |
| Maintain Dist. | 0x12 | 3 | helm mode 2 (7 = "latch current range" first) |
| Full Stop | 0x13 | 4, Num 5 | helm mode 3 |
| Stand By | 0x14 | 5 | helm mode 4 (manual) |
| Warp Out | 0x15 | 6 | helm mode 5 |
| Maintain Stat. | 0x16 | 7 | helm mode 6 (keeps the current mode, just announces) |

* Held time: (now − press time)/873 (= 3 s full scale), clamped 0.01..1; rates decay linearly to 0 over 582 ticks (2 s)
  after release. Code 8 (all stop: zero all rates and speech 10) exists but has no default key.
* **Any manual flight key while the helm is not in Stand By switches to Stand By** (0x49557B: speech 0xB3 "Stand by for
  command"). Helm commands also come from clicking the CONN menu (right panel) (0x4AECC6).
* Each helm mode plays Picard: Close 0xAF, Evade 0xB0, Maintain 0xB1, All stop 0xB2, Stand by 0xB3, Warp 0xB4,
  Maintain status 0xB5 (table 0x54E290).
* Esc: options menu (0x47AF5B → module vt20); F2/F3 (0x71/0x72) call an empty function. Joystick: X/Y axes add to yaw
  and pitch, buttons 1/2 fire phasers/photons (0x47BABE).
* Mouse: the console hotspot table **0x54A180** (18 × 52 bytes: kind, l,t,r,b, onClick, image#, image x,y, timer, cb2,
  tooltip) — listed in §4.2. Mouse-over shows the tooltip text on the CONN line (0x4958C2: "Main Shield",
  "<system> Repair", "Available Energy").

### 3.3 Flight model (0x4A53A1 helm, 0x4A5E05 integrate) (H unless noted)
* dt = min(now − last, 582)/291.3 s.
* Rotation rates: manual input r ∈ [−100,100] → `table54EF60[|r|/100×83]` (1, 2, 3 … 223, 228; 84 entries) with r's
  sign, in angle units **per frame** (no dt; M: tuned for the game's ~15-20 fps — scale by dt×15 in the port).
  Rates are multiplied by agility `class.agility × table54F150[(|speed|/maxspeed×23)>>1] / 100`, clamped ≤ 1
  (54F150 falls from 1.0 at rest to 0.21 at full speed), smoothed by a 4-sample moving average (0x4A7543), applied in
  the ship's own frame (0x4A765E composes matrices and re-extracts yaw/pitch/roll).
* Speed: if the setting s ≠ 0: target = sign(s) × maxspeed × engines(+1C)/engines(MAX) × `table54F0B0[|s|/100×39]`/1000
  (31.6 … 1024.7, an ease-out curve). The current speed follows by a 5-tap filter `(v1+v2+v3+2·target)/5` clamped to
  ±maxspeed (0x4A7479). Position += forward(yaw,pitch) × speed × dt.
* AI ships run the same integrator with rates from steering (0x4A5BAB toward a point at a given speed, 0x4A66C5 =
  heading/pitch to a point).
* Collision avoidance: when within (radius sum) of the target, non-player ships pick an escape waypoint ±1100 around the
  Enterprise and print "Collision Imminent" (0x4A53A1, 0x4A7880) (M).

Helm modes for the Enterprise (`0x54A0AC`, 0x4A53A1):
| mode | behaviour |
|---|---|
| 0 Close | steer at the target (lead point 0x4A66C5) at its range, `0x4A5BAB(target, closeRadius)` |
| 1 Evade | steer away from the centroid of all other ships, speed setting 100 |
| 2 Maintain distance | steer to the target, speed regulated to keep `0x57022C` (the range when the mode was chosen) |
| 3 Full stop | rates and speed 0 (speech 10) |
| 4 Stand by | manual control |
| 5 Warp out | if engines ≥ 21% and no speech playing → **result 3 (Lost)**; else Geordi "Warp drive disabled" (0x9E) and back to Stand By |
| 6 Maintain status | announcement only |

### 3.4 Weapons (weapons.cpp) (H unless noted)
**Phasers** (0x49ABBC, player via the button, AI every 2037 ticks ≈ 7 s when in range):
* Needs a live, uncloaked target, ship not hidden, phasers online, no bank already firing, range ≤ class phaser range.
* Octant of the target in ship space (0x4AACCD): bit 4 = starboard side (x>0), bit 2 = aft (z<0), bit 1 = dorsal (y>0)
  (M on the sign conventions). Every charged bank whose arc mask contains that octant fires all its stored energy (+1C),
  which resets to 0.
* Hit or miss per bank (0x49BE85): `score = max(computer%, 0.5) × ((range − dist + targetRadius)×0.75/range + 0.25)`;
  **miss if score < 0.11×(1 + rand(0..1))**; a miss aims at a random point ±30° off the target and does no damage.
* Damage happens when the beam animation reaches frame 4: shield facing = `facingTable[numShields][octant of the
  shooter seen from the target]` (0x54E278: 4 shields → 1,1,2,2,3,3,4,4; 3 → 2,2,3,3,2,2,4,4; 2 → 3,3,4,4,3,3,4,4; 1 → 4),
  then **0x4B0103(target, facing, energy, aimedSystem)** (§3.5). Player's aimed system = the Enemy Systems panel selection
  `0x54A0A8`, AI's = ship+0x514.
* Sound `<x>_PHASER.WAV` at a volume falling to 0 at 8500 units; speech "target out of range" 0x112 for the player.

**Photon torpedoes** (0x4999AB launch, 0x49A1C0 flight):
* Tube = fore or aft by arc (0x49AA6A). A tube fires **all torpedoes loaded in it** (+1C) as a fan: spread 230 units
  (≈5°) per torpedo, staggered 40..70 ticks apart. Empty tube → Worf "Photon tubes are empty" (0x111).
* Launch speed max(2000, |ship speed| ± 2000) (aft tube subtracts); accelerates +15/frame to 900 then +30/frame to 1750
  after arming; life 20 s (5820 ticks). Pool of 100, at most 99 in flight.
* Homing (player torpedoes always, AI too unless the target is cloaked): turns toward the target's lead point; within
  1600 units it snaps, else turn rate limited (0x49A829). If the target is outside the per-torpedo **cone**
  (SYSTEMS.DAT "Cone degrees per torpedo", ×45.5 units/degree) the torpedo picks a new target (0x49A930) or goes dumb.
* Hit when distance < step + target radius: shield facing as for phasers, damage = class photon "Damage_amount"
  (Enterprise 45, Chodak 33, Vor'cha 35 …) via 0x4B0056 → 0x4B0103.
* Loading: tubes are refilled from the stock (+BEC) at `8 − lifeSupport×7/lifeSupportMax` per update (Riker's line about
  torpedoes and life support) (0x4A97B7) (M on the unit).

**Difficulty** (0x4B08C3/0x4B0056): damage taken by the Enterprise × 1.2 when `0x5700B4` = 0 and × 0.85 when = 2. The
writer of 0x5700B4 was not found (it may stay 0) (M/L). Cheat "now thats class!" (options) sets `0x5700E8` → the
Enterprise takes no damage (0x484A8D, 0x4B0103) (H).

### 3.5 Shields, systems, damage (0x4B0103, 0x4A97B7) (H)
* Damage d to facing f (system 1..4): Chodak's single shield halves d. If shield energy < d the excess leaks inside;
  else with probability `bleed[f][energy/10]` % two thirds leak and one third hits the shield. The shield's damage +=
  its part; at MAX it is destroyed; energy is capped at MAX − damage.
* The leak goes to the aimed system if `rand(100) ≤ aimed%` (record +14) and it is not destroyed, else to a random system
  (cumulative thresholds). Each system absorbs up to its remaining capacity; the rest carries on (×0.9 when it hits a
  system with nothing left, ×0.8 when it hits a destroyed one).
* Speech per system on the Enterprise: half/destroyed lines (tables 0x550128 / 0x5500F8: shields Riker 0xBA..0xC1,
  weapons Worf 0xD0/0xD2, life support Beverly 0x84, engines Geordi 0x9D/0x9F, power 0xA8/0xAA, computer Data 0x8E/0x91,
  sensors 0x88/0x8B, damage control Riker 0xC3/0xC7); on enemies Worf 0x103..0x10B "their weapons are offline…" and the
  shield-down lines 0xFD..0x102 depending on type.
* **Destruction**: after each hit count destroyed systems among 5..12: chance to explode = 10/20/40/80/85 % for 4/5/6/7/8
  destroyed (0x55009C). Enterprise: speech 200 or 0xAC (warp core) then explode → result 2. Enemy: explode (0x4AC2D3),
  Worf "target destroyed" 0x10D; an enemy with ≥2 destroyed systems retreats (0x4AFF2A). A Soran ship (0x40000) whose
  engines or power are destroyed is "destroyed" at once.
* Energy (0x4A97B7, per frame): total energy = POWER energy × SHIP MAX / POWER MAX; distributed to systems 1..12
  (not POWER) by their charge rates (0x4A9620); if demand exceeds supply all are drained equally. Phaser banks charge
  from WEAPONS energy/6 split over the banks that need it. Damage control (system 11) repairs systems flagged for repair
  (0x4AA1C9). The Enterprise/Enemy Systems panels let the player set allocations (M on the UI details).
* **Life support** at 0: Beverly 0x83 at once; after 8730 ticks (30 s) the Enterprise is lost: Beverly 0x85, Picard 0xB8,
  Data's log 0x95, lose music, **result 2**; an enemy is removed ("no life signs").

### 3.6 Enemy AI (H for the built-in part, M/L for scripts)
Built-in (0x4A7C91, every frame, plus the movement in 0x4A53A1):
* Picks the Enterprise as target. Every 2037 ticks: fire phasers if in range; fire torpedoes; compute the angle to the
  target. If ≥ ¾ phaser range away and the target is visible, set a waypoint on the target (speed 999, setting 100) —
  i.e. close in. Soran's ship (`soran`, flag 0x40000) keeps away (waypoint 2×phaser range from the Enterprise, 0x4AFF2A)
  until it is the last enemy.
* Ships with a `<model>.bin` tactic (type flag 0x4000: all mission types including the player) run the tactics VM
  every 18 ticks (0x4B0FDA).

### 3.7 TACTICS scripts (*.BIN, tactics.cpp 0x4B0B22 attach, 0x4B0C1B load, 0x4B0FDA run) (M/L)
Header "TACTICS", u32 version 3, then named pages (MAIN, KERNEL, ATTACK_SET, DEFENSIVE, AGGRESSIVE, SETUP_LOOP,
SMALL/MEDIUM/LARGE_LOOP, PICK_A_LOOP, RUN_AWAY, CLOSE, EVADE, …) of bytecode with embedded debug strings ("Executing
hard break", "Immelmann twist"). Keywords (0x5502A8..): PUSH POP PRINT FIRE FOLLOW FLUSH SET GOTO GOSUB WHEN CLEAR MESSAGE
ACQUIRE CLOAK RETURN WARP OPPOSE BUTTON ONCE HEADING CLIMB BANK SPEED POS PHASER PHOTON SYSTEM STORY PROXIMITY FOE FRIEND
TARGET BEARING FIRED DELTA SKILL PAGE MENU ENGAGE IDLE WAY; event conditions WHEN FIRED / STORY / POSITION / WAY /
ORIENTATION / SPEED / PROXIMITY / DELTA. ENTERPRS.BIN contains pages CLOSE/EVADE/SHADOW/REVERSE/PROTORP/… =
the helm autopilot manoeuvres. The bytecode was not decoded (L). **Recommendation: do not interpret the BINs at first;
implement the built-in AI above plus a native "attack run / break / loop" state machine; decode the VM later only if
behaviour differences matter.**

### 3.8 How combat ends — `0x570134` and the module result (H)
| 0x570134 | set by | module +0x10 → 0x565A90 | STATES.INI branch |
|---|---|---|---|
| 1 | all enemies of all waves gone (0x4A2FA9: Picard "Cancel red alert" 0xB7, SCWIN.WAV) or a Soran ship beaten (0x4AC2D3: escapes cloaked/at warp — Worf 0xCD/0xCE — or the Q'urash disabled — Riker 0xB9, Picard 0xB6) | 2 | **Won** (+ fleet n beaten) |
| 2 | Enterprise exploded (0x4ACC4A) or life support failure (0x4A97B7); SCLOSE.WAV | 0 | **EnterpriseDestroyed** |
| 3 | Warp out (helm mode 5) | 1 | **Lost** |
After the result is set the game keeps running until the end music and speech finish, at most 4365 ticks (15 s)
(0x494D18), then the tick (0x47B83E) maps the code, finishes the module and the GM jumps.

---------------------------------------------------------------------------------------------------

## 4. The combat screen UI

### 4.1 Pictures (all RAS, loaded by 0x49D6E6; image numbers = index in that table) (H)
COMBAT.RAS 640x480 console. Buttons: BPHASER0/1/2 (106x22: dim/normal/lit "FIRE PHASERS" bar, #2/#1/#0),
BPHOTON0/1/2 (#7/#6/#5), BPHASERN/F (14x21 on/off-line switches, #3/#4 and #8/#9), BENEMYN/F (#10/11 view toggle),
BSCANN/F (#12/13 target toggle), BSCANLN/LF, BSCANRN/RF (#14-17 shield/system view toggles), BSCANRBR (#18), SVIEWN/F
(#19/20 viewscreen zoom slider), SRADARN/F (#21/22 radar zoom), RADARM, RADARM0/1 (#23-25), POWER/ENERGY (bar pieces),
PHASYEL/RED/BLUE (12x1 phaser charge bar colours), TUBERED/BLUE (39x31 tube states), PHASERPW/PHOTONPW (118x31 power
meters), TORPEDO (5x12 torpedo count pip), CONNMNU0/1 (167x150 helm menu normal/highlight), CONNBUTT (176x114), A-Z_0-9
and 0-9BIG/0-9LITTL digit strips, SCANNER (font), STATIC1-3 (169x209 sensor-static frames shown when sensors are out),
VIEW1-4 (viewscreen frame), SEXP1/2 (9x9 explosion blips). Per enemy class (prefix SBOP, SCHO, SENT, SPRO, SVOR, SWAR =
bop, cho, ent, pro, vor, war — 0x49C0E2 `s%s_%s.ras`): `_0/_1/_2` 169x209 ship schematic in three damage states,
`_STR` 169x209 system-region map (indices = system numbers, 256-colour map), `MAP`/`MAPH` 11x11 radar blip normal/highlight.

### 4.2 Hotspots (table 0x54A180: rect, image drawn at (x,y)) (H)
| # | rect l,t,r,b | image @ | what |
|---|---|---|---|
| 0 | 472,252–628,461 | (466,255) | Enterprise Systems panel (right); helm menu CONNMNU0 drawn at (472,310), selected row = mode×20+9 (0x49FCBC) |
| 1 | 1,252–174,461 | (0,252) | Enemy Systems panel (left): target schematic, click a system to aim (sets 0x54A0A8) |
| 2 | 262,440–438,460 | – | Helm Commands (CONN: line) |
| 3 | 203,402–320,432 | #34 | Phaser Status |
| 4 | 177,317–189,342 | #10 @174,315 | Target Toggle (next target, 0x4A2FA9) |
| 5 | 177,344–189,371 | #12 @174,344 | Enemy view / Forward view (speech 0x129/0x12A) |
| 6 | 202,380–306,399 | #0 @202,379 | Fire Phasers |
| 7 | 320,380–424,399 | #5 @320,379 | Fire Photon Torpedoes |
| 8 | 307,380–320,399 | #3 | Phasers on/off line (0x12D/0x12E) |
| 9 | 425,380–438,399 | #8 | Torpedoes on/off line (0x12F/0x130) |
| 10/11 | 325,405–356,429 / 402,405–434,429 | – | Forward / Aft tube (tube pictures at (321,402)/(399,402), torpedo count "%3d" at (369,408)) |
| 12 | 191,250–448,377 | – | Radar (centre (321,323); range text "RANGE: %6.0f" at (209,365)) |
| 13 | 450,317–465,371 | #21 | Radar zoom slider |
| 14 | 618,127–633,180 | #19 | Viewscreen zoom slider |
| 15/16 | 174,435–191,461 / 449,435–467,461 | #14 / #16 | Shield view / System view toggles (0x12B/0x12C) |
| 17 | 0,0–639,249 | – | Viewscreen |
S/cpng/hud_mock.png shows COMBAT.RAS with SENT_0, SCHO_0, CONNMNU0 and the buttons at these positions.

### 4.3 Message line, subtitles, speech (H)
* Text line at (4,463) in FONTSCAN: tooltips (shown 582 ticks) and **subtitles**: when a speech WAV plays, `<name>.TXT`
  is loaded (0x4A957D) and shown if subtitles are on (cfg); lines wider than 630 px scroll left 2 px every 10 ticks
  (0x494D18 states 3/5).
* Speech table **0x54A980**: 305 entries × 20 bytes {channel, file, alt file, next-allowed time, re-trigger delay};
  ids 0x83..0x130 are the WAVs in COMBAT.DAT (Beverly, Data, Geordi, Picard, Riker, Troi, Worf, computer); the many
  `.VAC`/`.MAC` entries name files that do not exist (silent). Full list with subtitles: S/re/cb/speech.txt.
  0x496AF3(id, force) plays one if its channel is free (else drops or, with force, queues it via 0x496E16, 6.7 s
  expiry); the re-trigger delay prevents repeats.
* Music (0x497906): start **SCLOOP1-4.WAV** (random), win **SCWIN.WAV**, lose **SCLOSE.WAV**; other cues
  (scgood/scbad/sckling/scchod/scrom/sc#9) name files that do not exist. Played on the music volume.
* Other speech triggers: target selection Worf "Targeting <class> …" 0xEA..0xFC/0x110 (by class and index), Computer
  "target selected/weapons locked" 0x113/0x114, view/system/shield-display toggles 0x129..0x12C, Troi 0xCB/0xCC when the
  enemy is losing/winning (M), system-repaired lines (Geordi 0x97..0xA6, Worf 0xD3..0xE9).

---------------------------------------------------------------------------------------------------

## 5. Briefing (0x47B4D6) and result handling (H)

* Table **0x548618**, 25 × 24 bytes `{char *bmp, i32 x, i32 y, i32 r=-1, i32 b=-1, i32 unknown(2..5)}`: COMBATn.BMP and
  the PROCEED button position, e.g. 1 → (535,429), 3 → (372,118), 9 → (527,430), 25 → (373,125). r/b of −1 become
  x+81 / y+18. The last field is not read by this function (L: unused or a rating).
* It loads the BMP's palette (0x45F380), allocates the 16-bit buffers, copies the starship key map, reads the volume/
  subtitle settings and runs `CMissionBriefing(0x46176F)` modally through module vt34 = 0x460E3B (music CLOOP1-4 looping
  from MUSIC.1/2, CTAG1-2 on Proceed, PROCEED.WAV — as in frontend.md §4.4). PROCEED.BMP is 83x38 = 2 frames of 19 rows
  (M). The briefing pictures carry the text themselves (S/cpng/COMBAT1.png).
* vt3C (0x47B688): "Loading combat scenario", init (0x49461C → 0x4A872B: viewport, ambient light, ship classes from
  SYSTEMS.DAT, RAS, `SCn.MIS`, camera, effects), creates the combat window (0x47B318), starts music cue 8 on the first
  tick.
* Result: see §3.8. The tick writes module+0x10 = {1→2, 2→0, 3→1} (0x47B98E) and finishes (vt18); the GM (0x418F7A)
  jumps **0 → EnterpriseDestroyed, 1 → Lost, 2/3 → Won** (and for n = 13..24, an "Alert / Fleet n" message box before).

---------------------------------------------------------------------------------------------------

## 6. Implementation plan for engines/stg

### 6.1 New files and classes
* `combatdata.{h,cpp}` — loaders, pure data, unit-testable:
  `Model3D` (§1.2: parts, verts, uv16.16, tris with material, face normals/planes; `load(Common::SeekableReadStream&)`),
  `MaterialList` (§1.3), `RasImage` (Sun raster → RGBA + key colour (255,0,163)), `SprAnim` (§1.6 decoder → 8-bit frames
  + STANDARD.PAL), `LbmImage` (PBM), `MetFont` (§1.7, draws from a RasImage), GIF decoding for .IMG (add a small LZW GIF
  decoder to common/image or engines/stg), `ShipClass` + `SystemsFile` (§1.8), `CombatMission` (§1.9).
* `combatrender.{h,cpp}` — `CombatRenderer`: true-colour (ARGB8888) 532x231 target, perspective camera with 60° FOV,
  z-buffered triangle rasteriser with perspective-correct UVs and flat Lambert (ambient 0.55 + light (0,−1,−1));
  billboards and additive SPR frames; starfield (§2.4). The away-mission renderer (render3d) is a level/column renderer
  and cannot be reused, but its CLUT8 → screen path is irrelevant here since combat is true colour.
* `combatship.{h,cpp}` — `Ship` (state of §3.1, 27 `ShipSystem`s), flight integrator (§3.3), helm modes, energy/repair
  tick, damage model (`applyHit(facing, energy, aimed)`), destruction.
* `combatweapons.{h,cpp}` — phaser beams (bank charge, arcs/octants, hit roll, beam effect), torpedo pool (§3.4).
* `combatai.{h,cpp}` — built-in AI (§3.6) + a native manoeuvre set standing in for TACTICS.
* `combatui.{h,cpp}` — console (§4): RAS pictures, hotspot table as data, panels, radar, message/subtitle line,
  speech queue keyed by the table of §4.3 (copy the 305 ids/files/channels/delays as data).
* `combat.{h,cpp}` — `SpaceCombat` module: briefing (COMBATn.BMP + Proceed + CLOOP/CTAG), load mission, main loop at a
  fixed 30 Hz step (convert per-frame rotation tables with dt×15, M), input map from GameOptions (starship VK table
  with the defaults of 0x488A23), end detection and result codes → `Campaign` (Won/Lost/EnterpriseDestroyed).

### 6.2 Order of work
1. Loaders + tests on made-up data: Model3D, MaterialList, RAS, MET, SPR, LBM, GIF, SYSTEMS.DAT, MIS (tests must use
   synthetic files, not the game's).
2. Model viewer debug screen: textured Enterprise and the LOD switch (compare with S/cpng/models.png).
3. Scene: starfield + ships placed from SCn.MIS, chase camera (forward/enemy view), the console frame and viewport.
4. Flight: player controls and helm modes, enemy built-in AI movement.
5. Weapons and damage: phasers (charge, arcs, hit roll, beam), torpedoes (salvo, homing), shields/systems, explosions
   with debris, waves, end conditions and results, music and speech with subtitles.
6. Console panels: systems schematics with damage states, enemy system targeting, radar, power/phaser/tube meters,
   tooltips.
7. Polish: damaged textures (.DMG), cloak, cinematic cameras, viewscreen zoom/enlarge, difficulty multiplier.

### 6.3 What can be simplified at first
* Lighting: flat shading per face is enough. Painter's order is not needed (z-buffer).
* AI: built-in rules (close in, fire every 7 s, Soran keeps away) without the TACTICS VM.
* Energy allocation UI: fixed allocations; repairs on a timer.
* Cinematic camera modes: only forward and enemy view.
* Speech: play only ids with real WAVs (0x83..0x130, explosions, RCLOAK); ignore .VAC/.MAC entries.
Essential for the campaign: the three outcomes and their branches, waves, Soran-ship special endings (`soran`,
`quarash`), warp out → Lost, and the briefing.

### 6.4 Open questions
* Exact shading model and transparency of flag-0/2 materials (param 4096/11000), and the 3D FOV axis (M).
* Frame rate the per-frame rotation tables were tuned for (M/L: 15–20 fps).
* Where the difficulty variable 0x5700B4 is set (maybe never) (L).
* The TACTICS bytecode format and what each page does (L); whether KLINGON2.3DV wing frames are ever used (L).
* The 6th briefing field (2..5) (L); the role of class "hit pts", "destruct%", accuracy and penetration (M: not used by
  the traced code).
* The energy/allocation UI on the systems panels and the radar drawing details (traced only in outline: 0x4AA350,
  0x4AAEC7, 0x49E232, 0x49F302) (M).
* Octant sign conventions (which side is port/starboard) (M).
