# Midtown Madness 2: the HUD

> **Status:** done · **Code:** `src/engines/age/ui/hud_map.cpp`, `hud_pictures.cpp` · **Checked against:** recordings · **Confidence:** seen, not traced unless marked

## Overview

The heads-up display: the map, and the game's speed, gear, revs, damage and clock pictures. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## How it works

### The map

`tune/<city>.mmHudMap`: **Size** and **Pos** (shares of the screen:
0.21 by 0.25 at 0.78, 0.75, its top left corner, so it's the bottom
right corner), **ZoomIn** (starting near), **Approach Rate**, **ZoomInDist**
and **ZoomOutDist** (577 and 1,195 in San Francisco), **IconScaleMin**
and **IconScaleMax** (34 and 52.4), the same four for full screen (the
**FS** ones), and **Ocean Color**. "Approach Rate" and "Ocean Color" are
two words, read as a name and the rest. On the game's screens the map's
top left is at Pos, but it's drawn a little smaller than Size: 124 x
110 at 640 x 480 (Size gives 134 x 120), 245 x 165 at 1280 x 720; Reluva
draws it at 0.92 of Size. It turns with the player (whose arrow always
points up) and keeps the player in its middle.

`geometry/hudmap_sf.pkg` and `hudmap_london.pkg` are the map: the map
pictures (`s_SFMapNW_f`, ... the ones on the cities' bus shelters) laid
flat on the city's ground plan at height 0, so a point on the map is
that point in the city. `hudmap_tri` is the cars' arrow (28 long, its
point along -z) in ten paint jobs of colours, `hudmap_square` a dot in
nine (RED_DOT, BLUE_DOT, GREEN_DOT, GREY_DOT, YELLOW_DOT, FINISH_DOT,
GOLD_DOT, BANK_DOT, HIDEOUT_DOT). Reluva draws the map in its corner on
the ocean's colour, seen from the zoom's distance above the player with
a 30-degree field of view (a guess: 60 shows half the city), turned so
the way the player faces is up, easing between zooms at the approach
rate. The icon sizes read as lengths in metres from near to far (34 m at
577, 52 m at 1,195); the numbers fit that: the FS ones (15 and 18 at 786
and 1,581) come out a similar share of a full-screen map. The player is
yellow, the opponents red and the police blue; the race's checkpoints
are dots, the next green, the finish its own. **Z** zooms, **F** shows
it full screen.

### The pictures

The game's HUD pictures are `texture/*.tga`, made for a 640 x 480
screen: `SPEED` (a speedometer's panel; it has no alpha: what's past
its curve is pure black, its boxes' (8, 8, 8)), `DIGITAC_0`-`9` (41 x
56, and `_HALF`), `DIGITAC_GEAR_1`-`8`, `D`, `N`, `P`, `R` (20 x 23),
`TACOMETER TICKS_HALF`, `SPEED_TICKS` (129 x 43: rising bars, green to
red), `MPH`, `DAMAGE` (a green to red bar, 500 x 12) and `DAMAGE_LABLE`
("DAMAGE METER", 129 x 12), `DIGI_0`-`9` and `DIGI_COLON` (the clock;
no alpha), `opp_icon` (places 1-8 and a gold $, four to a row, each
stored upside down), and the dots.

Where they go is in the game's program, but its screens show it
(gameplay footage at 640 x 480, [footage.md](../research/footage.md)):

- The speed in `DIGITAC_*` digits, three places ending at x 144, their
  top at y 366, with no leading zeros.
- The gear at (15, 424): `D` driving an automatic, the gear's number a
  manual, `N` standing, `R` backing.
- `SPEED_TICKS` at (8, 421), as many of its bars as the revs' share of
  their most (all of them revving in neutral).
- The damage meter at (8, 467): `DAMAGE_LABLE` over a 129-pixel window
  of `DAMAGE`, unscaled. Through a race the window's right end goes
  green, yellow, orange, red and its left end from green to orange, and
  it's green again at the next race: the window slides along the bar
  with the damage, from its green end to its red.
- The clock top middle (x 247-390 at y 0) in `DIGI_*`: minutes,
  seconds and hundredths, "01:02:19"; their black is see-through.

`SPEED`, `MPH`, `TACOMETER TICKS_HALF` and the `_HALF` digits aren't on
any of the screens seen, and aren't drawn.

## In Reluva

`HudMap` (`src/engines/age/ui/hud_map.{h,cpp}`) draws the map, and
`HudPictures` (`src/engines/age/ui/hud_pictures.{h,cpp}`) the speed,
gear, revs, damage and clock. The map is drawn at 0.92 of its Size,
with a 30-degree field of view (above).

## Sources and checks

- **Where the facts come from:** `tune/<city>.mmHudMap`, `hudmap_<city>.pkg`, the HUD pictures in `mm2tex.ar`; the places measured on public gameplay videos ([research/footage.md](../research/footage.md)).
- **How it was checked:** against the recordings' screens, pixel positions at 640 x 480.
