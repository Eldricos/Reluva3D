# Boggle (1997)

The `boggle` engine recognizes the supplied multilingual Windows file set
and offers early single-player **Classic Original and Master** in U.S. English.
Original uses 4x4 dice with a three-letter minimum; Master uses 5x5 with a
four-letter minimum. Both have dictionary validation, scoring and results. Other modes, original menus, audio, AI and networking are unfinished.
See [scope and rule evidence](classic.md) and the
[original/native comparison procedure](comparison.md).

Press Enter to start, type a word and press Enter to submit. Alternatively,
click or drag through touching cubes, then press Enter. Backspace undoes input;
right click clears it. F1 ends a round, Enter replays the same board, F2 creates
a new board between rounds, and Escape returns to the launcher. The initial
board uses a fixed native seed for reproducible checks; its shuffle differs
from the original game.

In the launcher's game options, choose Original or Master and a round duration:
1–5 minutes in 30-second steps, or Not Timed. Three minutes is the default.
End an untimed round with F1 or End Round. See
[settings and reference evidence](options.md).

Unknown words with a valid path stay in red and carry provisional points. At
round end, review them with Yes / No / Yes to all / No to all (buttons or keys
1 / 2 / 3 / 4). Approved words persist in the launcher's per-game save folder.
Use Page Up / Page Down to browse a long word list. See the
[user-dictionary behavior and evidence](user-dictionary.md).

## Detection

Game ID: `boggle1997`. Add the installed folder containing `Boggle.exe`, or
its equivalent expanded `Boggle/` directory on the supplied CD. The CD root
and compressed-only installers are not recognized as runnable directories;
Import Game is not implemented yet.

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
- BMP and PCM WAV through the existing shared readers.

Not decoded: INSO `.DAT` definitions,
AVI/Cinepak/Microsoft Video 1 media and TrueType fonts. The current checker
also does not interpret PE dialog layouts or validate every asset the original
program may request. Some bitmap paths
in the EXE are absent from the supplied copies; see the
[remaining research questions](../../plans/boggle-1997/unconfirmed.md).

## Tests

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
supported resource survey, dictionary decoding and deterministic Original/Master rounds. Without a game path they skip. When running them,
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
