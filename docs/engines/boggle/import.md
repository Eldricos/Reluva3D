# Importing Classic from the supplied files

**Import Game** accepts the supported installed folder, the expanded `Boggle`
folder from the CD, or the CD root containing that folder. Select a destination
in your external game library. The launcher previews the copy plan before
copying. **Add Game** can still use the original installed folder directly.
Neither operation runs the Windows installer or original executable.

The plan now copies 45 files (13,892,691 bytes for the supplied release): the current
U.S. English Classic Original/Master dependencies plus the existing detection
markers, the optional game-selection background and five mode-button clips. This supports all native Classic options, including both lid clips,
eight effects and fifteen music tracks. Source paths are matched without regard
to case and destination names are consistent across platforms. The plan is
stable and does not recursively copy unknown files.

| Purpose | Selection |
| --- | --- |
| English rules and music labels | `Dicts/English.dic`, `Language/English.dll` |
| Original/Master dice and palettes | `Dicts/EngOrg.dst`, `Dicts/EngMas.dst`, `Palettes/ClsO1Pal.pal`, `Palettes/ClsM1Pal.pal` |
| Cube lettering | `Textures/Letters.bmp`, `Textures/LettersQu.bmp` |
| Round clips | `Anims/ClsO1All.avi`, `Anims/ClsM1All.avi` |
| Game-selection background and buttons (when present) | `Bitmaps/Select1.bmp`, plus the five clips listed in [movie playback](movies.md) |
| Effects and music | The eight cues in [presentation](presentation.md) and fifteen paths established in [music](music.md) |
| Additional detection markers | `Boggle.exe`, `Palettes/Title.pal`, `Scripts/Die.x`, `Anims/INTRO.AVI`, `Fonts/TARA.TTF`, `Bitmaps/ClsM1CS.bmp` |

The additional markers preserve the existing release identification contract;
they are not all consumed by the current renderer. In particular, `Boggle.exe`
is retained for its detection fingerprint and never executed. This is a tested
file set for the current native Classic slice, not a proven minimum for the
complete original game. Other modes and languages will need a revised plan as
their dependencies are established. The imported copy is therefore not a full
backup of the original installation.

Installers, compressed installation archives, bundled system/INSO DLLs, registry
files, unrelated media, and original personal dictionaries are excluded. Native
approved words remain in the launcher's separate per-target save directory.
Do not select the repository or any other Git worktree as an import destination.
No retail files are distributed with Reluva.

The selected source must match the existing measured release signatures. Every
selected dependency must also exist as a nonempty regular file. Missing files,
case-colliding names, or symbolic links in the named resource paths reject the
plan. The launcher currently reports its generic “nothing to import” message
when no complete plan is available. Only the supplied expanded layouts are
supported; a compressed-only `data.z` source cannot be imported. Presence checks
do not guarantee that every resource decodes: malformed data still produces
reader errors when used. No original CD device is required for native WAV music.

## Evidence and implementation

Selection follows the current native readers and Classic resource requests,
not a guessed original full-game dependency list. Release identity and the two
expanded layouts were measured in the [inspection](../../plans/boggle-1997/inspection.md)
and its SHA-256 manifests. The source EXE's SHA-256 is
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
The Classic and music pages document the executable addresses establishing the
resource choices. No additional external source or implementation was used.

`MetaEngine::planImport` authenticates the source and delegates the explicit
file selection to `classicImportPlan`. The shared `runImport` copies files into
the chosen destination using its existing progress/cancellation mechanism.
It never alters the source. There is no patch-import support for this engine.

Read-only plan verification, with only counts and checksums in the output:

```sh
reluva-tool boggle import-check /external/Boggle
```

To preview filenames use `reluva-tool import /external/CD`; adding a destination
argument performs the copy just as the launcher does. Keep redirected output
and any copied files outside Git worktrees. The two supplied sources produce
45 files / 13,892,691 bytes, checksum `9fa2df147dd9cefe8039e1d912caa57c`.
Earlier 40-file imports lack only the five optional button clips and retain
checksum `83fd9b02d89500220a026c13c828d999`. The original 39-file imports also
lack the optional background and remain supported;
without it the plan retains 39 files / 12,699,713 bytes and checksum
`31ad9a484df0af2bd5d4c6e16eb4bd38` for the supplied Classic subset.
The digest covers ordered destination names (NUL terminated) and full-file MD5
values, independently of source directory spelling. It checks reproducibility,
not authenticity beyond the existing release signatures.

## Validation

Synthetic tests cover exact selection, excluded extras and personal words,
case-insensitive lookup, duplicate elimination, byte-for-byte copying, progress,
missing/empty/non-file resources, ambiguous names, symbolic links where the OS
allows them, and empty plans after failure. The source is moved away before
reading the copied dictionary, animation and music. Invented files never pass
retail detection.

The optional `boggle.gamedata.import` step copies to a fresh temporary directory
under the required external analysis directory, compares all selected file
checksums, confirms detection, and runs both Classic board checks plus animation,
effects and music checks plus optional menu-art validation against the copy. It removes that test copy after
success. Raw logs stay outside Git; committed records contain metadata only.

Both supplied expanded copies pass that optional import step with the same
record. The CD root produces the same plan as its expanded `Boggle` subfolder.
External SDL dummy video/audio runs using only the 39-file copy reached native
Original and Master play and produced nonzero audio. These checks establish
software integration. The owner subsequently confirmed that importing the
game from the CD works on their Windows setup. This confirms the import
workflow; it does not separately establish post-import play in both variants.
All 17 Boggle CTest checks pass with the supplied install. The
normal full CTest run has 30 passing checks and 18 optional data checks skipped
without configured game paths. Boggle tests and actual CD-root plan hashing also
pass ASan/UBSan, with LeakSanitizer disabled for the execution tracer. All 737
original install files still match the recorded SHA-256 manifest.

The frontend pass adds `Select1.bmp` when available; missing optional artwork
uses native menu drawing and does not prevent importing an older Classic-only
copy. The original 39-file Windows import confirmation above predates this
addition. The extended 40-file import and menu flow have native Linux checks;
the new frontend still needs the owner's Windows test. See [frontend](frontend.md).

Intro/menu movie support adds five optional button clips to new imports. The
intro was already included for detection. Re-import to obtain the full 45-file
set; earlier 39/40-file copies use static fallback for missing button clips.
The movie pass has native Linux integration checks and still needs the owner's
Windows movie/audio test. See [movies](movies.md).
