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
The minimum-length notice now uses the same template reader; see below. Pause
and other original dialogs remain separate work.

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
score. Multiplayer results keep their own existing Close position.

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
