# Boggle (1997)

The `boggle` engine recognizes the supplied multilingual Windows file set
and offers early single-player **Classic Original and Master** in U.S. English.
Original uses 4x4 dice with a three-letter minimum; Master uses 5x5 with a
four-letter minimum. Both have dictionary validation, scoring and results.
Classic installed scenes, word panels, animated hourglasses, lid/roll animation, sound effects and WAV background music are available. Other modes,
the full original frontend, AI and networking are unfinished.
See [scope and rule evidence](classic.md) and the
[original/native comparison procedure](comparison.md).

The default game-selection screen opens Classic with Enter. Choose Original
or Master and a duration, advance to Players, then Play / Enter to start.
Change Name / N on Players edits the human name, remembered for this game.
See [player naming and persistence](players.md).
Escape returns to selection from version/setup or Players; it does nothing
on selection or in-game. Use Menu / F3 in-game or Launcher / L on selection. See
[frontend scope and controls](frontend.md). Disable **Game selection menu** in
launcher options to use the previous direct Ready screen.

During play, type a word and press Enter to submit. Alternatively,
click or drag through touching cubes, then press Enter. Backspace undoes input;
right click clears it. F1 ends a round, Enter replays the same board, F2 creates
a new board between rounds. After dictionary review, click a counted word or
use Up/Down to inspect its numbered cube path and points. Right-click restores
the final summary. See [completed-round review](results.md). With the menu disabled, Escape returns directly
to the launcher. The initial
board uses a fixed native seed for reproducible checks; its shuffle differs
from the original game.

**Auto-orient cubes** keeps letters upright by default. Disable it in the
launcher's game options for individual quarter-turn orientations, including Qu.
Replay retains orientations; New Board changes them. See [orientation settings](options.md#cube-orientation).

Classic's round animation and WAV effects are enabled by default. The timer
starts after the opening finishes, and closing finishes before results/review.
The launcher can disable **Classic round animations** or **Classic sound
effects** independently. **Intro and menu animations** plays the publisher/game intros with
their soundtracks and available animated mode buttons; Escape skips the intro.
See [movie playback and scope](movies.md). **Classic music** offers no music (default), a selected
installed track, or a random track chosen per launch. See [music](music.md) and [presentation behavior and evidence](presentation.md).

In the launcher's game options, choose Original or Master and a round duration:
1–5 minutes in 30-second steps, or Not Timed. Three minutes is the default.
End an untimed round with F1 or End Round. See
[settings and reference evidence](options.md).

Unknown words with a valid path stay in red and carry provisional points. At
round end, review them with Yes / No / Yes to all / No to all (buttons or keys
1 / 2 / 3 / 4). Approved words persist in the launcher's per-game save folder.
Use the word-panel arrows, Page Up / Page Down, or the wheel over the panel
to browse a long word list. See [history controls](history.md). See the
[user-dictionary behavior and evidence](user-dictionary.md).

## Detection

Game ID: `boggle1997`. Add the installed folder containing `Boggle.exe`, or
its equivalent expanded `Boggle/` directory on the supplied CD. The CD root
and compressed-only installers are not recognized as runnable directories.
**Import Game** accepts the install, the expanded CD folder, or the CD root
containing it, and copies the English Classic set (52 files with Classic and menu artwork and clips) into your external
library. See [import scope and validation](import.md).

Detection requires measured prefix-MD5/size signatures for `Boggle.exe`,
`Language/English.dll` and `Dicts/English.dic`, plus representative resources
from the other folders. Paths are case-insensitive. It does not require the
bundled Windows/MFC/INSO libraries or run any original code. Fingerprints
identify this file set; they do not establish a pressing/patch label, prove
all files are intact, or imply English-only support.

Provenance and full SHA-256 manifests are in the
[inspection report](../../plans/boggle-1997/inspection.md). Both supplied
expanded copies match the detection table. All game files must remain
outside the repository and other Git worktrees.

## Readers and tools

`Resources` (`src/engines/boggle/resources.h`) provides case-insensitive
access to palettes, dice sets, meshes, PE language resources/UTF-8 strings,
images, sounds and binary word dictionaries. `resource-check` visits every supported file in the known
resource directories and produces a checksum of decoded values, with fixed
byte order and stable path sorting. It does not write assets or print retail
text, face lists or geometry.

```sh
reluva-tool boggle resource-check /external/Boggle
```

For the supplied install and expanded CD, both runs report:

```
images 600, sounds 35, palettes 14, dice sets 12, meshes 4, languages 5
vertices 712, faces 222, dice 246, strings 1976
decoded resources: 38b73816bcda5859e6a414202dddc001
```

`reluva-tool help boggle` lists all commands. `files` lists source filenames
and sizes for local research; keep any redirected output outside Git.

Supported formats:

- [JASC palettes and dice-set text](formats/text-resources.md).
- [The observed DirectX text mesh subset](formats/mesh.md).
- [Shared PE resource and string-table reader](../../common/pe-resources.md).
- [Binary word dictionaries](formats/dictionary.md).
- [Single-stream 8/16-bit AVI / Microsoft Video 1](../../common/avi-video1.md)
  for the Classic lid and hourglass clips.
- [AVI / Cinepak with optional PCM soundtrack](../../common/avi-cinepak.md)
  for the intro and mode buttons.
- [Printable ASCII TrueType outlines](../../common/font-truetype.md), used for
  the [Classic handwriting font](fonts.md).
- BMP and PCM WAV through the existing shared readers.

Not decoded: INSO `.DAT` definitions and broader font/language forms. The resource checker
also does not interpret PE dialog layouts or validate every asset the original
program may request. Some bitmap paths
in the EXE are absent from the supplied copies; see the
[remaining research questions](../../plans/boggle-1997/unconfirmed.md).

See [Classic artwork and layout](layout.md) for scope, resource selection and fallback.

## Tests

`font-check <folder>` validates the optional handwriting font and reports
ASCII coverage/advance checksums without exporting glyphs.

`artwork-check <folder>` validates both optional Classic artwork bundles and
reports checksums of native rendered states without exporting images.

`movie-check <folder>` decodes available intro/menu clips and their PCM,
reporting only counts and checksums.

`frontend-check <folder>` validates the optional game-selection background and
reports a decoded checksum.

`import-check <source folder>` validates and fingerprints the Classic import
plan without copying files.

`music-check <folder>` validates all fifteen installed WAV loop tracks and
reports counts/checksums without exporting samples.

`presentation-check` decodes both Classic lid clips and eight sound cues,
reporting only counts and checksums.

`dictionary-check` decodes all eight binary dictionaries and reports only
counts and checksums. `dictionary-query` checks an ASCII English word.
`classic-check <folder> [original|master]` validates every dictionary word
against the deterministic initial board, checks submissions, duplicates, scoring and expiration, and
reports only counts and a checksum. `classic-replay` uses an externally
recorded board and timed submissions to help compare VM observations; its
numbered output omits faces and words. No tool exports word lists.

`reluva-tests boggle` runs synthetic detection/resource/dictionary/round tests and the
`boggle.engine` record. `common.unit.pe_resources` tests the shared binary
reader. All fixtures are assembled from original code; no retail bytes or
face/word lists are embedded. Tests cover UTF-16 surrogate pairs, language
selection, invalid offsets/counts, truncation, topology, finite numbers,
partial-result clearing and case-insensitive file access. Scripted engine tests
exercise keyboard and mouse input, timeout, results, replay and text-input
cleanup using synthetic assets; rendered states have checksum records. A decoded palette
change must change the resource checksum even when counts remain the same.

Optional local game-data tests check positive detection and the complete
supported resource survey, dictionary decoding, deterministic Original/Master rounds,
and a complete external import/copy check. Without a game path they skip. When running them,
explicitly select an analysis output directory outside all Git worktrees:

```sh
RELUVA_BOGGLE1997=/external/Boggle \
RELUVA_BOGGLE_ANALYSIS=/external/analysis/boggle \
ctest --test-dir build -L boggle --output-on-failure
```

The committed game-data records contain only checksums and counts. Raw output
is redirected outside the build tree. A relative analysis path or a path
inside a Git worktree is rejected. Record updates use the shared
`RELUVA_GAMEDATA_UPDATE=1` workflow, followed by diff review. See
[DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
