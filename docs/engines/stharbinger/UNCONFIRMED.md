# Star Trek: Deep Space Nine - Harbinger: unconfirmed items

What the engine does is based on reading DEEP.EXE. Most of it was read
directly from the code; the points below were inferred or guessed and have
not been confirmed against the original running, or in the real game data.

## Read from the code, not seen running

### Behaviour seen in the scripts

* **Quark's talker code.** The TALKHEAD lines of QUARK010 and XOQ010 (and
  QUARK011) give a speaker code that the script listings show as `ras`, while
  Quark's clips use `qrk`. If that were what the game stores, Quark's lines
  in those conversations would play without lip-sync. They do lip-sync in this
  engine, so the listings probably show the wrong name for that symbol, but
  this has not been checked against the original.
* **Leftover pictures across scene changes.** The work buffer is only refilled
  by `BACKGRND`, so a scene that starts without one keeps showing the previous
  picture (and any animation leftovers) until it does. This follows the code;
  no real scene that does this has been checked.
* **Leftover mouths at the end of a line.** After the last phoneme the mouth
  is set to its rest frame without a lip-sync redraw, so for a stopped `clear`
  mouth the rest frame is drawn over the last shape until something erases it.
  This follows the code; whether it is visible in any scene is unknown.
* **Skipping the static before a death video.** Esc during the 765 ms of TV
  static before an action sequence's death video ends it there, and the last
  static frame stays as the scene's picture until the game menu that follows.
  Whether the original lets the static be skipped at all is not known; the
  frame is never seen in play, because death always opens the menu.
* **Beams across a scene change.** The beam timer and list are reset on load
  and at start-up, as in the original, but not on a scene change. A beam that
  is still up when a script changes scene would be wiped by its box in the new
  scene. No script is known to do this.

## Seen running, not traced in the code

None yet.

## Inferred

### Inferred from the code (small gaps)

* The ANIMDEF mode names: values 0 / 1 / 2 are taken to be `onthefly`,
  `flyclear` and `clear`. The bits (load on the fly, clear) are certain; the
  pairing with the names comes from cross-references.
* `ANIMPRISTINE` draws the current frame into the clean background.
* The dialogue-side meaning of `RETURNVAL` values other than `#0` (end the
  conversation as if Esc had been pressed, using the value as the choice).
* Which of the menu's save-browsing buttons 11 / 12 is "previous" on screen.
* A save slot that is empty when saved but preloaded by the scene stays loaded
  after a load.
* Parts of `ACTIONSEQ` (the script command that starts videos, action
  sequences and other special functions): the meaning of some types and of the arcade parameter
  tables (frame counts, start frames).

## Guessed

### Guesses

* Speech plays at volume 75/127 and the action sequences' music at 64/127
  (from the arguments of an audio library call, 0x30155).
* Palette entries 1..5 are the colours cycled by the 250 Hz timer.
* The art of cursor ids 18..25, which are only reachable through hotspot
  cursor bytes.
* The deck map's "X" marker blinks because its colours (2 and 3) are among the
  cycled entries.
* The action sequences' gunsight is drawn everywhere on screen, not only over
  the video (the mouse handler probably draws it outside the video too).
* The behaviour when `art\buttons.rle` is missing (treated as "next scene").

## Settled

None yet.
