# Human gameplay hand cursor

With Classic installed artwork enabled, human play now uses the installed
raised/pressed hand from `Language/English.dll`. This works in Original and
Master, including background-computer matches, independently of the Computer
Cursor option. No new files or re-import are needed. Disable Classic installed
artwork to retain the system pointer for human play. Minimal language bundles
without either cursor group also retain it; partial or malformed enabled
bundles report an error.

## Observed behavior and native policy

The supplied executable initializes the raised handle from group 5201 and the
pressed handle from group 5200 at `0x412827..0x412867`. Its human left-button
handler selects the pressed handle at `0x4148aa`, before testing for a cube.
Release at `0x414970` selects raised and clears the held state. The move handler
at `0x414ad0` retains the selected handle; its later left-button check restores
raised if the button is no longer held. An external instrumented harness checks
state-to-handle selection and press/release with invented handles. These are
behavioral observations, not a translation of the original routines.

The independent native input loop tracks the pointer and left-button state
before processing game actions, including word submission and validation.
Dragging retains pressed; releasing anywhere in the view restores raised.
Right-button actions do not press the hand. Artwork compositing, including
AND/XOR masks and hotspots, uses the [existing cursor reader](presentation-fidelity.md).
The desktop pointer position is never changed.

The hand is drawn only during active human play with a known in-frame pointer.
Ready, lid animations, handoffs, results, pause, notices and score dialogs retain
the system pointer. These screen boundaries are a native usability policy;
exact original dialog cursor behavior remains unverified. Pointer state resets
on focus changes and window exit. SDL now reports window mouse-leave through
an appended platform-independent event, so leaving without losing keyboard
focus restores the system pointer and cancels a held drag. The event does not
pause or finish a round. A subsequent in-frame mouse event restores the hand.
Keyboard-only launch retains the system pointer until such an event arrives.

## Roll-sequence correction

Earlier documentation incorrectly listed initial dice tumbling as missing.
The Classic roll loop at `0x42af17..0x42b030` advances the supplied `All.avi`
and blits the video surface, without a dice render/composite call. In an
external reference run, an invented 100 ms clock, empty event queue and video
advance service produce 30 advances and 30 full-video blits through frame 30;
no geometry service is needed. Decoded previews of both supplied clips show
the covered tray being shaken. The native player already displays this part
of the AVI. No separate physics simulation should be added on that evidence.
The [settled-dice overlays](lid-animation.md) supply the later reveal/conceal.

## Validation and remaining comparisons

Synthetic human-game tests compare every rendered frame with the unchanged
scene plus an independently placed invented cursor. Both variants, installed
artwork enabled/disabled and Computer Cursor enabled/disabled are exercised.
They check press, held drag, release, right click, window exit/re-entry, focus,
pause/resume, results, New Board and restart; accepted words retain their score.
Missing groups fall back, while partial bundles fail only when enabled.
Existing visible/background computer cursor checks remain in place.

All source code is independently written. Retail cursor pixels, decoded
previews, instrumented harnesses and raw analysis remain outside every Git
worktree. Whole-screen Windows/VM comparisons, original mesh lighting, and
precise dialog and pause/audio appearance remain separate work.

The full normal suite passes 63 checks, including all 40 Boggle checks with
the supplied install; ten optional id Tech 3 data checks are skipped. The SDL
backend smoke check translates window mouse-leave and verifies pointer
hide/restore through a dummy video device. Both supplied variants also run
the native human cursor flow through that backend; raised/pressed/released
previews were inspected outside Git. All 737 installed files retain their
manifest hashes. The owner subsequently confirmed the human cursor pass works.

The synthetic Boggle suites and supplied cursor-resource checker also pass
ASan/UBSan, with leak detection disabled for the execution tracer. The cursor
checker retains checksum `286d1fe04293a3f50eadc1a22262ba65`; no decoded
resource records or retail data changed in this pass.
