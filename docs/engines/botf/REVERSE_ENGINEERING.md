# Star Trek: Birth of the Federation: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../CONTRIBUTING.md) for what may and may not
  be used.

## Sources

- **The game's files:** an English retail install and its CD (and the
  CD's German `stbof.res`), October-November 2026. Samples of them (sizes,
  hashes, first and last bytes, chosen files whole) were made on the
  owner's machine with the scripts in `tools/botf-re/`, and only those
  samples were studied.
- **The executables:** `trek.exe` and `Trek.icd`, their headers, imports,
  resources and relocations read with a PE parser. `Trek.icd`'s code is
  encrypted by SafeDisc, and nothing was decrypted or run
  ([formats/executable.md](formats/executable.md)).
- **The manual:** the First Edition (May 1999), from its scan on the
  Internet Archive (item
  `Star_Trek_THE_NEXT_GENERATION_BIRTH_OF_THE_FEDERATION`): what the
  screens' controls do and their defaults, what the races are good at,
  the levels of civilization, which ships cloak.
- **Recordings of the game:** a Let's Play series playing the Federation
  ("Let's Play Star Trek Birth of the Federation, UFP", 38 parts),
  October 2026: the ships' and buildings' numbers on the game's screens,
  and how the game setup and empire selection screens look.
- **The modding community's work:** the Ultimate Editor (Java, GPL-2.0,
  by DCER and others; <https://gitlab.com/stbotf/ultimate-editor>, read at
  commit `7ee45c4b`), read for its file layouts and field names only, no
  code copied; and the Armada Fleet Command forums
  (<https://www.armadafleetcommand.com/onscreen/botf/>), for files the
  editor doesn't read (`habit.bin`, `ppgrowth.bin`, `combat.bin`). Their
  knowledge comes partly from the community reading the game's
  executable; every field taken from them was checked against the data
  ([formats/tables.md](formats/tables.md)).
- **Not used:** the unofficial "Joker patch" and any other unprotected
  `trek.exe` ([PLAN.md](PLAN.md#decisions)).

## Methods

- **Layouts from the files:** record sizes from where the names repeat;
  description links as the one field that's a string's start in every
  record; counts and offsets checked to end exactly at each file's end;
  every retail file of a kind read by its reader (the 377 layouts, the 21
  fonts, the 22 text tables, the 35 data tables, the 31 `alt.res`
  pictures).
- **Numbers from a recording:** each part of the Let's Play downloaded,
  the screen's stats box cropped once a second, near-duplicate crops
  dropped, the rest read with Tesseract OCR, then matched to the tables'
  records by name and numbers, taking the most common reading of each and
  checking against the stills where the OCR and the data disagreed.
- **Screens against a recording:** Reluva's rendering of a screen laid
  over a still of the recording (scaled to 800x600), text pixel for text
  pixel; text colours measured from the brightest pixels of the text.
- **By ear and by eye:** the ADPCM sounds' rate by listening at 11,025,
  22,050 and 44,100 Hz; `alt.res`'s pictures, the fonts and the layouts in
  the viewer, on the game's own files.

## The tools

`reluva-tool help botf` lists the engine's commands
(`src/engines/botf/tools/`):

| Command | What it does |
|---|---|
| `files <game folder>` | Every file in the game's folder, with its size |
| `alt-list <alt.res> [folder]` | `alt.res`'s pictures, each decompressed to check it, and extracted into the folder |
| `snd-list <file.snd> [-m] [-r rate] [folder]` | A `.snd` archive's sounds, each decoded, and written as WAV files into the folder; `-m` for `music.snd`'s layout, `-r` another rate for the ADPCM sounds |

The scripts in `tools/botf-re/` (Python 3, standard library only) are run
by the game's owner on their own copy, to make the samples studied:

| Script | What it makes |
|---|---|
| `botf_sample.py` | A report on the install's and CD's files: sizes, hashes, their first and last bytes, the archives' listings |
| `botf_res_sample.py` | Files of chosen kinds from `stbof.res`: their first bytes, and chosen ones whole (`--ext`, `--whole`, `--all-whole`) |
| `botf_probe.py` | Checks of the layouts against whole files: `alt.res`'s directory and every file decompressed, the `.snd` archives' entries |
| `botf_fonts.py` | The `.fnt` fonts, their headers and glyph sheets |

## Working files not included

The samples the scripts made, the recording's crops and their OCR
readings, and the modding community's editor's source aren't in the
repository. The samples and crops are remade by running the scripts on
your own copy, and from the recording; the editor's source is at the
address above.
