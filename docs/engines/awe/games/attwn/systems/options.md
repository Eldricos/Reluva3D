# And Then There Were None: options

> **Status:** done · **Code:** `src/engines/awe/game/scene_options.cpp` (shared), `src/engines/awe/games.h` (`OptionsLayout`), `src/engines/awe/attwn/attwn.cpp` (this game's) · **Checked against:** the code (`ac.exe`); the screen looked at in the Scenes page · **Confidence:** from the code

## Overview

The settings and the options screen are Orient Express's
([../../motoe/systems/options.md](../../motoe/systems/options.md)): the
same nine settings, kept under
`HKCU\Software\DreamCatcher\Agatha Christie\Agatha Christie - And Then
There Were None\Settings` with the same names (`Music`, `Sound`, `Gamma`,
`Captions`, `Shadows`, `Water`, `Fog`, `Rain`, `AntiAlias`), and the same
screen object made by the scene's `PUZZ options`
(`interface\scenes\options.ac1`). What differs is where it draws.

## How it works

The screen object (vtable `0x474a4c`) against Orient Express's (`0x482c50`),
compared with the field offsets set aside:

- **The clicks** (`0x4243c0`, Orient Express's `0x42c220`): the same.
- **The sliders** (`0x4244f0`, `0x42c350`): the same but for the numbers:
  a thumb at x 515 + 18 × its value (Orient Express 694 + 23), and a
  dragged one going on a step once the cursor is 9 past it (11).
- **The labels** (`0x424290`, `0x42c090`): `options1` to `options9` from
  `interface.txt` (Music Volume, Effects Volume, Brightness, Captions,
  Shadows, Animated Water, Fog, Rain and Lightning, Anti-Aliasing), in
  font 7 (Arial Narrow 10), white, at fixed places rather than by the
  scene's pictures: the sliders' centred in 200 pixels from x 512, at y
  45, 110 and 175 (35 above their rows, 80, 145, 210); the boxes' ending
  at x 645, at y 258 to 508, 50 apart, the tick drawn at x 645 on the row
  (y 255 to 505) when the setting is on.
- **OK** (`0x424550`): writes all nine settings from the screen's copies
  (`0x424572` to `0x4245db`), applies them, and resets the device if
  anti-aliasing changed (`0x4245f2`); Orient Express's writes four, the
  rest having changed at once. The clicks are the same code, so here too
  the toggles change at once.

## In Reluva

`OptionsLayout` in the per-game table: the slider's numbers, and the
labels' font, colour and places; Orient Express's are the defaults
(labels placed by the scene's pictures).

## Sources and checks

- **Where the facts come from:** `ac.exe`'s options object, its vtable
  and constants (`0x474a78`: 18's reciprocal, 515.0, 7.0), each function
  diffed against `motoe.exe`'s.
- **How it's checked:** the screen looked at in the Scenes page from the
  main menu's Options (`awe page-shot <game> scenes out.tga start:new
  click:590,380 ms:2500`).
