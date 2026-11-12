# LEGO Alpha Team: saving

> **Status:** done · **Checked against:** a `Game.txt` written and read back by the game's own code running in Reluva · **Confidence:** confirmed unless marked

## Overview

The game keeps one file, `Game.txt` in its install folder: how far each
Adventure has got, and the settings.

## How it works

- Done: saving.
  - Found with `at-run -files`: the game writes one file, `Game.txt` in
    its install folder (the readme has players edit it). It's LoadComp's
    `DDInfo` (`GameState`) saved as text: a header of comments, then a
    key and a value a line. The interface behaviour (PE0300) reads and
    sets `P<slot>Space`, the mission the Adventure in that slot has reached
    (the `SPACES_EN.TXT` ids, `M1020`), and `P<slot>Time`, when it was last
    played (`time()`), saving as it sets them; the Adventure cards show
    both. The settings go there too (the readme's `Width`, `Height`, `BPP`).
  - It's written on leaving through Exit Game (from the main menu, or
    from a mission's menu), and as an Adventure moves on. Quitting
    Reluva stops the game where it is, so it saves nothing then: closing
    the window or pressing Ctrl+F5 (back to the launcher) while the game
    runs (or while its data viewer is open over it) asks first, "Keep
    playing" or "Quit" ("Return to launcher") (`System::confirmQuit`, a
    message box in the SDL backend).
  - Two things in our run-time had to be fixed for it. The `ofstream`
    destructor that LoadComp inlines in `DDInfo::Save` reads `filebuf`'s
    close flag at +0x48 and deletes a string pointer at +0x44, which ours
    hadn't kept there (it deleted garbage and crashed). And the file is
    opened in text mode, so on Windows it has CR-LF line ends: our
    MSVCRT had made every file binary, so a saved `P0Space` kept its CR
    and wasn't recognised. Text mode now reads CR-LF as LF and writes LF
    as CR-LF.
  - Checked: a `Game.txt` with `P0Space  M1020` (CR-LF) shows "On the
    Right Path" on the first Adventure card, and choosing it starts that
    mission.

## Sources and checks

- **Where the facts come from:** `at-run -files`, and LoadComp's `DDInfo`
  (above).
- **How it was checked:** a saved `Game.txt` loaded by the game (above).
