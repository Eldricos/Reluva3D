# Star Trek: Deep Space Nine - Harbinger: videos (`.SMK`)

> **Status:** decoded · **Code:** `src/engines/stharbinger/game/game_video.cpp`, `src/video/smacker.cpp` · **Checked on:** the intro, cut-scene and action-sequence videos played in STDS9H (October 2026); the decoder against FFmpeg on made-up files (the unit tests) · **Confidence:** confirmed unless marked

## Overview

Smacker (SMK2/SMK4) files in `DS9\INTRO`, `DS9\MOVIES` and `DS9\TRANS`, shown
1:1 at fixed positions on the 640x480 screen and played by `ACTIONSEQ`; see
[../systems/script-reference.md](../systems/script-reference.md).

## Layout

Smacker, read by Reluva's shared decoder: see
[smacker.md](../../../common/video/smacker.md).

## In Reluva

`Game::playVideoFile` (`src/engines/stharbinger/game/game_video.cpp`) plays
them with `src/video/smacker.h`. The data viewer can play one video on its
own (`start_mode=video` in reluva.ini, the video named in `start_scene`).

## Sources and checks

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).
