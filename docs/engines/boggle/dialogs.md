# Installed dialog layouts and dictionary review

Classic dictionary review now reads the installed Unknown Word dialog's control
placement when Classic installed artwork is enabled. Its buttons follow the
resource IDs: No to All, Yes to All, No, Yes from left to right in the supplied
layout. Each button retains the native keyboard shortcut printed on it:
1 Yes, 2 No, 3 Yes to All, 4 No to All. Mouse drawing and picking share the
same rectangles. Pending words, provisional points, per-player all-choice scope,
user-dictionary persistence and the displayed-prompt guard are unchanged.

Missing dialog resources retain the previous native panel. Turning off installed
artwork also retains that panel. A present malformed selected dialog reports an
error; unrelated dialogs do not gate gameplay. No additional imports or re-import
are needed: the existing English DLL supplies the resource and is read as data.

## Template reader

The independently written reader supports standard `DLGTEMPLATE` and version-1
extended templates, including signed coordinates, DWORD item alignment, style
and help fields, 16/32-bit control IDs, ordinal/string fields, font metadata and
opaque creation data. UTF-16 is validated, including surrogate pairs. It retains
these fields without creating a Windows window or loading executable code.

Input is capped at 1 MiB, 1,024 controls and 4,096 UTF-16 units per string.
Truncation, unsupported extended versions, malformed text, oversized creation
data and unexpected non-padding trailing bytes fail with an empty result.
Standard creation-data counts include their size word; extended counts describe
the following bytes. Up to three zero trailing padding bytes are accepted.
The public Microsoft [DLGTEMPLATE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-dlgtemplate),
[DLGITEMTEMPLATE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-dlgitemtemplate),
[DLGTEMPLATEEX](https://learn.microsoft.com/en-us/windows/win32/dlgbox/dlgtemplateex)
and [DLGITEMTEMPLATEEX](https://learn.microsoft.com/en-us/windows/win32/dlgbox/dlgitemtemplateex)
format descriptions define these layouts; no external implementation code was used.

## Review integration and limits

Runtime selection is exact RT_DIALOG 16003 / language 0x0409. Actions bind by
control ID (Yes 6, No 7, Yes All 4, No All 3), not enumeration order or copied
button coordinates. The message area uses control 15801. The consumer validates
required classes, unique IDs, bounds and non-overlapping buttons before using
positions. Names and raw dialog text are not embedded in source or fixtures.

Native presentation uses two pixels per template coordinate unit and centers
the client rectangle in the 800x600 view. Colors, font, explanatory wording,
keyboard hints and panel frame remain independently authored native UI. Decoded
font metadata is retained for later work; it is not used as Windows font metrics.
This stage establishes installed relative placement and consistent mouse targets,
not exact Windows dialog-unit conversion, original skinning or modal styling.
The minimum-length notice now uses the same template reader; see below. The
[pause command](pause-presentation.md#pause-control-and-settled-view) is a menu
toggle rather than a dedicated dialog. The [game-selection mode row](frontend.md#installed-mode-button-placement) now
uses the same reader. Remaining frontend dialogs are separate work.

## Minimum-word-length notice

With installed artwork enabled, the too-short-word notice now selects exact
RT_DIALOG 16006 / language 0x0409. It uses message area 1350 and OK control 1349.
The original single-button mode hides control 1348; native display follows that
choice without drawing an inactive second button. Missing templates and disabled
artwork retain the previous notice and acknowledgment rectangle. A malformed
present selected layout fails explicitly.

The minimum check at `0x414d50` formats the current minimum and calls message
service `0x414370` with the single-button kind. Its constructor at `0x444ba0`
selects template 16006. Initialization at `0x44535a..0x445405` hides the left
control and labels the right control for that kind. An external isolated probe
with invented string/window services verifies all three button-mode branches;
three checks pass. Original bytes and the probe remain outside Git.

The consumer validates unique required controls, their classes and bounds, and
separation of message and acknowledgment areas. Placement keeps the existing
native two-pixel unit policy. Text wraps within the installed message area with
the native bitmap font and independently authored title/wording. This does not
claim original Windows metrics, colors or skinning.

Only presentation and mouse targets change. The notice still holds the gameplay
clock, preserves the current Chess player and consumes its acknowledgment batch.
OK, Enter, keypad Enter, Space and Escape retain their existing behavior. Wrong
clicks cannot dismiss it; focus regain must display the notice before a queued
acknowledgment is accepted. Music and lid behavior are unchanged.

## High-score table

The Classic high-score view now uses RT_DIALOG 16007 / language 0x0409 when
installed artwork is enabled. The ten rows bind by IDs 15400..15409 (name),
15410..15419 (score) and 15420..15429 (the existing native version column).
Close binds to button ID 1. The original loader requests template 16007 at
`0x43e068`; row-control lookups include `0x445a35` and `0x445acf`.

The wider template fits uniformly inside a 768x552 area of the 800x600 view,
using at most the existing two-pixel unit scale. Drawing and mouse targets use
the same rounded coordinate transform. Required IDs/classes, bounds, column and
row separation, button clearance and minimum readable cell sizes are checked.
Missing templates or disabled artwork retain the previous table. A malformed
present selected template fails explicitly.

The ranking model, native save format, repeated names, mixed Original/Master
scores, zero scores and one-record-per-round rule are unchanged. Only placement
changes. The third column continues the existing native Original/Master field;
matching the original skill-display semantics is not claimed. Other game-mode
controls in the resource are not exposed. Native colors, text and bitmap fonts
remain in use; this is not an original-skinned Windows dialog.

Enter, Escape, H and mouse Close still dismiss the table and consume the input
batch. Native Retry / R remains available for save errors, with its mouse target
fitted beside Close. Opening or closing the table cannot replay or record another
score. Multiplayer results use their own independent layout and Close target.

## Multiplayer match results

With installed artwork enabled, the match summary uses exact RT_DIALOG 16008 /
language 0x0409. Names bind to static controls 15400..15403, scores to
15410..15413, message area to 1352, OK to button 1 and High Scores to button 2.
The constructor selects this template at `0x439fd4`, the Classic final-results
caller constructs it at `0x41b3f6`, and initialization binds those controls at
`0x43a20f..0x43a27c`. The superficially similar 16010 template is a separate
network-results dialog and is not selected for local Classic results. These
addresses refer to the installed executable SHA-256 recorded in
[inspection.md](../../plans/boggle-1997/inspection.md).

The client area is centered at two native pixels per template unit. Required
controls must have the expected class and unique ID, lie inside the client
area, and leave readable, separated rows, message and action areas. Missing
resources or disabled artwork retain the prior native summary; a malformed
present selected layout reports an error. No retail text or coordinates are
embedded in the implementation or its invented fixtures.

Clicking a displayed name or score, or pressing 1–4, opens that player's word
review. OK, Enter, keypad Enter, Escape or V closes the summary. High Scores / H
opens the existing rankings and returns to the summary on dismissal; opening
rankings directly from word review still returns to word review. Each transition
consumes the remaining input batch, so closing a panel cannot accidentally
replay a round. The fallback summary also accepts H. These native review keys
and nested-panel navigation are explicit implementation choices; full original
navigation and Windows modal behavior are not claimed.

Final totals, shared-word cancellation, human-only high-score recording and
save formats are unchanged. Native fonts, wording, frame and colors remain;
this stage follows installed relative placement, not original dialog skinning.

## Multiplayer turn handoff

With installed artwork enabled, the next-player screen uses exact RT_DIALOG
15900 / language 0x0409. Its message area binds to static control 15901 and its
Start action to button 1. The original constructor selects template 15900 at
`0x449815`; the Classic player-switch caller constructs it at `0x419dba`, and
control binding occurs at `0x449a5a..0x449a76`. The executable and English DLL
hashes are recorded in [frontend research](frontend.md#evidence-and-missing-file-correction).
Only read-only resource/caller observations inform this independently written UI.

The client area is centered at two native pixels per template unit, matching the
other small native dialog consumers. Required IDs/classes, uniqueness, bounds,
message capacity and separation from Start and the native footer are checked.
Missing templates or disabled installed artwork retain the previous handoff and
mouse target. A present malformed selected layout fails explicitly. No retail
text, screenshots or coordinates are embedded in source or fixtures.

Start drawing and mouse picking share the installed button rectangle. Enter and
keypad Enter retain their existing acknowledgment behavior. The screen must have
been displayed before it accepts acknowledgment; the accepted input batch is
consumed so queued typing cannot enter a word for the next player. Focus regain
also consumes its event batch. Computer handoff automation is unchanged.

The full board/word-list cover remains a native privacy policy. Traditional still
shows the next player and same-board/full-round explanation. Chess still shows
any finished-player notices, the next player and that player's paused budget.
Native fonts, colors, wording, consolidated retirement notices and footer remain
explicit presentation choices; this is installed relative placement, not a full
recreation of the original modal skin or timing. Turn order, player clocks,
background computers, scores and persistence are unchanged.

## Checking and validation

```
reluva-tool boggle dialog-check /external/Boggle
```

The command decodes every RT_DIALOG leaf and reports counts plus a canonical
metadata checksum, without exporting text, layouts or images. The supplied
install and CD report 99 dialogs (89 standard, 10 extended), 729 controls and
`cda929500e9798bd9d05cb0badf54567`. The new optional dialog check and fresh-import
check record only hashes. Selected import files and all prior gameplay/resource
digests remain unchanged.

Invented fixtures test both formats, fonts, signed/full-width IDs, Unicode,
alignment, creation data, all truncations, oversized counts, exact languages,
missing layouts, duplicate/overlapping controls and the legacy fallback. Scripted
Original/Master rounds click each of the four installed-layout choices and verify
the resulting scores, including the guard against choices in the finishing event
batch. External SDL runs inspect supplied-resource review and click No in both
variants. All fixtures are original synthetic data; retail resources, raw
research, probes and screenshots remain outside Git.

Notice fixtures additionally cover standard/extended templates, the primary OK
position, wrapping, missing/exact-language fallback, invalid required controls
and overlapping message/button areas. Original/Master scripted rounds exercise
Traditional and Chess clocks across long focus gaps, mouse/keyboard acknowledgments
and event-batch consumption, then submit a word successfully for the same score.

External SDL software-backend checks also display the supplied minimum notice
and acknowledge its installed OK button in both variants, restoring the human
cursor afterward. No screenshots or decoded templates are stored in Git.

High-score fixtures additionally cover both template encodings, fitted geometry,
all ten rows, clipped names, malformed/overlapping controls, mouse and keyboard
close, replay guards, successful retry recovery and multiplayer results. External
SDL runs display the supplied table and close it by mouse and keyboard in both
variants. Stored rankings and resource/import checksums remain unchanged.

Match-summary fixtures cover both template encodings, exact-language/missing
fallback, malformed classes/IDs, overlapping rows/buttons, out-of-bounds controls
and picking only occupied rows. Four-human Original/Master rounds check shared
words scoring zero, mouse and keyboard player review, nested and standalone
rankings, repeated summary opening, obsolete-target rejection and queued replay
protection. The existing high-score fixture still verifies independent fallback
summary Close. External SDL software-backend runs display all four supplied-layout
rows and exercise rankings, fourth-player review, reopening and mouse Close in
both variants. Retail captures and the preview harness remain outside Git.

For the match-summary stage, all 43 Boggle CTest checks pass with the supplied
install; the full suite passes 66 checks and skips 10 optional idtech3 data
checks. Dialog, competition, Traditional, Chess and engine suites pass ASan/UBSan
with LeakSanitizer disabled for the execution tracer. All 737 installed files
retain their manifest hashes. Windows owner testing of this stage is pending.

Handoff fixtures cover standard/extended templates, exact-language and missing
fallback, malformed classes/IDs, overlap, footer clearance and privacy masking.
Original/Master human matches in Traditional and Chess check the installed Start
target, obsolete-target rejection, long waits, focus loss/regain, pre-display and
acknowledgment-batch guards, continued word entry and shared-word totals. External
SDL software-backend runs inspect the supplied handoff, acknowledge Start by
mouse and finish two-human matches in both versions/turn modes. Raw research,
retail captures and the preview harness remain outside Git.

For the handoff stage, all 43 Boggle CTest checks pass with the supplied install;
the full suite passes 66 checks and skips 10 optional idtech3 data checks. Dialog,
Traditional and Chess suites pass ASan/UBSan with LeakSanitizer disabled for the
execution tracer. All 737 installed files retain their manifest hashes. Windows
owner testing of this handoff-layout stage is pending.
