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
Pause/minimum notices and other original dialogs remain separate work.

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
