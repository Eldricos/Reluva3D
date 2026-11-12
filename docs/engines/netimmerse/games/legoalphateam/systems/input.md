# LEGO Alpha Team: input

> **Status:** done · **Checked against:** the game's own code running in Reluva, taken by `at-run`'s clicks and pointer moves from its demo to the main menu · **Confidence:** from the code unless marked

## Overview

The mouse and the keyboard, as the game reads them.

## How it works

- Done: input. Found from LoadComp's `DDMouse` and the program's window
  procedure, the game reads:
  - **the pointer by its movement:** each frame `DDMouse::GetState` takes
    `GetCursorPos` against where it last put the pointer, then puts it
    back in the window's middle with `SetCursorPos`. Its own pointer, drawn
    by the game, is that movement added up: from 0 to 1 across the screen
    from the bottom left, 1/640 a pixel times the sensitivity;
  - **clicks and keys as window messages:** `WM_LBUTTONDOWN` and the like
    go to `DDMouse::ProcessMouseClick`, which clicks where the game's
    pointer is, not where the message says. `WM_KEYDOWN` and the like go
    to `DDKeyboard::processKey`;
  - **DirectInput not at all:** `DDMouse::InitSystem` would make a
    DirectInput mouse, but only if a flag is set, and nothing sets it.
    DirectInput is stood in for all the same
    ([x86.md](../../../../../common/x86/README.md)), for other games.

## In Reluva

- In exclusive fullscreen, changing the display mode now makes it the
  desktop's size and puts the program's window over the whole screen, as
  DirectDraw does. The pointer and the window then agree.
- `at-run -click <frame>[@x,y]` clicks when a frame is shown (where the
  game's pointer is, without a place), and
  `-move <frame>@dx,dy` moves the pointer. A click on frame 20 leaves the
  demo for "Destination: Alpha Team Headquarters" and the main menu
  (Adventure, Single Missions, Options, Exit Game). Moving the game's
  pointer onto "Exit Game" and clicking brings up "Really Exit Game?".
  Everything the game imports is now stood in for.

## Sources and checks

- **Where the facts come from:** LoadComp's `DDMouse` and the program's
  window procedure (above).
- **How it was checked:** `at-run`'s clicks and pointer moves taking the
  game from its demo to the main menu and "Really Exit Game?" (above).
