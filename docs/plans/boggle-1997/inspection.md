# Boggle: CD and install inspection

Stage 1 of [the implementation plan](../boggle-1997.md). This is static
inspection of the owner's supplied files, not a claim that the replacement
engine can play them. All findings were made on `engine/boggle-1997`.
Current detection and initial reader support are documented in the
[engine README](../../engines/boggle/README.md); statements below describe the
stage 1 inspection before implementation.

## Sources and method

The connected Google Drive tool successfully listed the owner's
[source folder](https://drive.google.com/drive/folders/1PQlLY6Lnxcpv5dPefUHmk4ZupoaWwSjS).
The two roots are [Boggle (install)](https://drive.google.com/drive/folders/1kOod-bXd53NyIlWd5Cti3aPD-vs4CXkr)
and [BoggleCD](https://drive.google.com/drive/folders/1s15VxR2SI1YG46HVaXKZZWzAZHKQWU8d).
Every discovered subfolder was listed recursively. No individual listing
reached the requested 1,000-item bound (the largest had 572 children).

The source files and raw analysis are outside every Git worktree, under
`/workspace/work/boggle-1997/`; the transfer tool also materializes files in
`/workspace/attachments/`. Originals retained for analysis are read-only.
No original EXE, DLL, installer or script was executed. No leaked source or
external implementation source was consulted. Tools used were Reluva's own
`--hash` and `pe-info`, GNU `objdump`, `file`, Python standard-library byte
inspection, and FFmpeg/ffprobe as research tools. Ghidra and Wine are not
installed in this environment; neither was used.

The committed manifests contain paths, sizes, whole-file SHA-256 values and
Drive IDs only. They contain no retail payloads, word lists, strings extracted
from the game, images, recordings or disassembly. Detailed resource inventories,
decoded string tables, disassembly, logs and probe outputs remain external.
See [unconfirmed.md](unconfirmed.md) for the precise limits of this review.

Manifests: [install-manifest.tsv](install-manifest.tsv) and
[cd-manifest.tsv](cd-manifest.tsv). Whole-file hashes and transfer sizes were
verified locally for 2,955 files. The remaining row explicitly records the
transfer limit instead of presenting an invented or partial hash.

## Inventory

| Tree | Files | Bytes |
|---|---:|---:|
| Install | 737 | 77,674,514 |
| Entire CD file copy | 2,219 | 204,481,387 |
| CD's expanded `Boggle/` subtree | 720 | 74,172,905 |
| Both supplied trees | 2,956 | 282,155,901 |

Every install file has a byte-identical CD counterpart, verified by size and
whole-file SHA-256: 720 at the same relative path under `cd/Boggle/`, twelve
`Xtras` files under `cd/Xtras/`, four DLLs under `cd/System/`, and `boggle.id`
at the CD root. There are no changed, unmatched or install-only files in
that mapping. This is equality of the supplied copies, not proof of other
pressings or of the original installer behavior. Neither `data.z` extraction
nor installing DirectX is necessary to obtain this supplied install's files.

Both expanded trees include `Boggle.LOG`; it is runtime diagnostic text,
not an authoritative installation manifest. Its existence does not prove
the supplied directories are a pristine runtime state. No log contents are
reproduced in Git.

The CD includes expanded game resources, localized setup components, an
autorun application, DirectX redistributables/drivers and AOL promotional
files. These are file copies, not an image with a readable volume label,
sector layout or track table. Drive modification times cluster in December
2000; they do not establish the date or identity of the original CD pressing.

`cd/data.z` is 41,554,858 bytes. Its raw file was available through Drive, but
the executor rejected materialization because the transfer limit is 32 MiB.
Its manifest SHA-256 is explicitly unavailable. Do not infer its contents
from the smaller archive or claim its extraction has been validated.

## Executable and dependencies

`Boggle.exe` is a PE32 i386 Windows GUI executable with six sections,
linker version 5.0, image base `0x400000`, entry RVA `0x68e70` and a PE
timestamp of 23 October 1997, 11:01:16 UTC. Its whole-file SHA-256 is
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
The timestamp agrees with the requested period, but is not a verified
retail edition/version identifier.

Its 594 imports come from 14 DLLs: `WINMM`, `WSOCK32`, `DPLAYX`, `d3drm`,
`MSVFW32`, `REDLLW32`, `MFC42`, `MSVCRT`, `KERNEL32`, `USER32`, `GDI32`,
`DDRAW`, `DSOUND` and `AVIFIL32`. In particular:

- `Direct3DRMCreate` and `D3DRMCreateColorRGB` establish Direct3D Retained
  Mode use; DirectDraw and DirectSound creation imports are also present.
- AVI stream reads and Video for Windows/MCI calls establish a video path.
- Winsock and DirectPlay imports establish networking dependencies, not
  proof of a documented protocol or successful multiplayer operation.
- Five imports from `REDLLW32` are by ordinal (2, 3, 5, 9 and 11).
  That DLL imports six ordinals from `PDDLLW32`. Both identify INSO in
  their binary strings; they are proprietary components to investigate,
  not libraries to link into the replacement engine.

The supplied `ReadMe.txt` independently identifies Visual C++ 5.0/MFC,
DirectX 3.0, Direct3D, DirectSound, DirectPlay and Third-i Net.
Evidence: EXE COFF/optional/import headers and the readme's technology
section. `SourceDirectory` appears at EXE file offset `0x86f40` and
`Thirdi Productions` at `0x86f50`. These are leads for configuration/CD
lookup research; they do not establish a complete registry-key contract.

`Setup.exe` is an NE Windows 3.10 GUI executable, distinct from the PE32
game. `min.z` is identified by `file` as InstallShield Z archive data,
with signature at offset zero. The installer has not been unpacked or run.
The CD's `Autorun.dxr`, `Autorun.cxt` and Xtras belong to a separate
Director-style autorun path; they do not establish a Director gameplay engine.

## Resource survey

Counts below describe the install; the CD's expanded game subtree has the
same resource category counts.

| Resource | Observed format and count | Consequence |
|---|---|---|
| `Bitmaps/`, `Textures/` | 572 and 28 BMP files; 16 one-bit, 319 eight-bit and 265 twenty-four-bit, all BI_RGB | Existing image reader successfully loads all supplied BMPs |
| `Palettes/` | 14 JASC-PAL 0100 files, each with 256 RGB entries | Original text palette reader needed |
| `Scripts/` | Four text DirectX `.x` meshes, header `xof 0302txt 0064` | Geometry readers needed; these files are not gameplay scripts |
| `Anims/` | 25 RIFF AVI files: 7 Cinepak, 18 Microsoft Video 1 | AVI demuxing and two video decoders needed; Bink does not cover these |
| `Waves/` | 35 RIFF WAVE files: 28 unsigned 8-bit PCM, 7 signed 16-bit PCM | Existing WAV support covers the observed encoding types |
| `Fonts/` | Three TrueType fonts (`KPSI`, `NELSON`, `TARA`) | Decide on original font rendering; the shared debug bitmap font is not visually equivalent |
| `Language/` | Five PE resource DLLs: Dutch, English, French, German, Spanish | Read PE resources as data; do not load/execute DLLs |
| `Dicts/` | 14 `.dic`, 12 `.dst`, one `.DAT` | Separate word membership, user words, dice data and definitions |

Every `.x` file contains one Mesh block; no Frame or Animation block was
found. This supports a mesh-data interpretation, not an assertion that no
game logic could exist elsewhere. Each palette's first three lines and
259-line total were checked. Each font has a TrueType scaler and `glyf`,
`loca`, `cmap`, `head`, `hhea` and `hmtx` tables, among others.

All 25 install AVI files and all 35 install WAV files were probed and decoded
to a null sink with local FFmpeg without reported decode errors or warnings.
Video dimensions range from small UI animations to 640x480 clips. Two AVI
files also contain PCM audio. WAVs are mono: twenty 8-bit/22,050 Hz,
eight 8-bit/11,025 Hz and seven 16-bit/22,050 Hz. These tests validate the
supplied media, not a Reluva implementation or license to copy decoder code.

A separate external C++ survey linked the existing `reluva_common` library
and called `image::decode` and `audio::decodeWav` on every transferred BMP
and WAV from both trees: 1,200 images and 70 sounds, zero failures. It wrote
only diagnostic counts, not decoded assets. This establishes current reader
compatibility for these files; it does not test animation, playback or mixing.
The bitmap dimensions span widths 3 through 837 and heights 5 through 3,750,
so do not treat every bitmap as an ordinary screen-sized picture.

### Language resources and feature scope

`Language/English.dll` has `.rsrc` and `.reloc` sections, no imports and
204 resource leaves. Its resources include 99 dialogs and 396 nonempty
string-table entries. The resource tree, rather than ad-hoc scanning, was
used to locate these strings. The DLL's SHA-256 is
`9922ca1656fa8a9f376b79cdc3c1a4a71bd0d75f01156e07e4c3a126304628fe`.

All five language DLLs have no imports. Their resource-leaf counts are
Dutch 203, English 204, French 198, German 198 and Spanish 198; nonempty
string counts are 395 except English's 396. Do not assume identical UI
layouts/resource coverage solely because language names correspond.

String IDs 11001 through 11005 name five modes: Classic, BreakAway, Battle,
Space and In Your Face. IDs 14020 through 14025 identify six dictionary
choices: U.S. English, U.K. English, French, German, Spanish and Dutch.
IDs 14040 through 14045 name their retail dictionary paths. This is static
evidence of UI choices, not verification of their rules or behavior.

Player, pause, user-word addition, high-score and networking resources are
present. The EXE also contains mode-specific class/error identifiers and
dictionary initialization/definition-display identifiers. Examples for later
cross-reference work are EXE file offsets `0x88bf4` (Classic), `0x8ce68`
(the internal Tetris-named class), `0x8768c` (definition initialization) and
`0x893d4` (definition shutdown). Internal class names are not a public mode
specification. No game rules, scoring constants, random algorithm, timing,
save format or network packet layout has been confirmed by this inspection.

### Dictionaries and dice

All twelve `.dst` files are comma-separated text: six `*Org.dst` files have
16 rows, six `*Mas.dst` files have 25, and every row has six fields. Their
EXE/resource references and layout make them strong dice-set candidates;
face order, random selection and multi-letter handling still need tracing.
No retail face lists are reproduced in the repository.

Six `*User.dic` files are empty. The eight nonempty `.dic` files include
the language dictionaries, `British.dic`, `UK.dic` and `AI.dic`. They contain
binary structures and cannot be treated as newline-delimited word lists.
Their encoding and search algorithm remain unconfirmed. The English UI
selects `UK.dic` for U.K. English; a file named `British.dic` is not enough
to justify substituting it. In this supplied copy, `British.dic` and
`English.dic` are byte-identical (verified by direct byte comparison and SHA-256).

`R016E613.DAT` is 13,263,360 bytes and begins with an INSO copyright marker.
English string ID 14080 references this path. Together with definition-related
EXE strings and INSO imports, it is a strong lead for the definition subsystem;
its record format and the precise DLL interfaces remain unresolved. Keep
definition lookup separate from word-membership work until that is established.

## Detection candidates

These values were independently checked with `reluva --hash` and Python.
They are MD5 of the first 5,000 bytes plus the whole-file size, not the
whole-file SHA-256 used by the manifests. No detection table is implemented yet.

| Relative path | Prefix MD5 | Bytes |
|---|---|---:|
| `Boggle.exe` | `72f212ea3c87e3c47d480164b8a0f981` | 656,384 |
| `Language/English.dll` | `b5d3759c5103ff9c2ddeb8debc3dce2a` | 124,416 |
| `Dicts/English.dic` | `fd7191cd64475101ad717be88ef9985a` | 718,084 |

Use a verified EXE fingerprint with required data/resource checks. Do not
infer a language-exclusive edition from the presence of English assets;
this copy includes multiple UI languages and dictionary choices. Do not
use `Boggle.LOG`, user dictionaries, modification times or a small marker
file as the sole release identifier.

## Runtime boundary and next work

Proceed with an original native C++ engine reading the user's resources.
The evidence supports native game logic, external assets and PE UI resources;
no gameplay bytecode format was identified. Do not select the existing x86
DLL host as a shortcut to running the game EXE, and do not reproduce Windows,
MFC or DirectPlay wholesale. Use Reluva's `System`, 2D/3D graphics, events,
timing, filesystem and audio facilities.

The conservative initial import candidate is the expanded game's `Bitmaps`,
`Textures`, `Palettes`, `Scripts`, `Anims`, `Waves`, `Fonts`, `Language` and `Dicts`
directories, preserving relative paths. This is a starting set for dependency
tracing, not a proven minimum or a runnable import plan. The EXE may still be
used as a detection fingerprint and research reference. Its five embedded
resource leaves are four icons (type 3) and one icon group (type 14), all
tagged language 1033; no gameplay or UI layout resource was found there.

`MFC42`, `MSVCRT`, `REDLLW32`, `PDDLLW32`, DirectX redistributables, localized
installers, AOL and autorun/Xtras are outside the intended native runtime.
Removing the INSO DLL dependency requires original dictionary/definition
readers; it does not mean their features are already replaced. Keep mutable
user dictionaries and settings under the target's save path, not in a
read-only install. Exact persisted paths and formats still need research.

The expanded `cd/Boggle/` tree permits an initial CD import without running
the installer or depending on extraction of `data.z`; validate the same
runtime resources after import. This folder-copy route must be documented
separately from support for arbitrary compressed-only releases.

Next: generate the `boggle` scaffold, add measured detection and resource
validation, then implement the dictionary reader and Classic mode as the
first playable vertical slice. Add a bounded PE resource reader, JASC palette
reader and needed asset viewers before implementing the menus. Plan AVI
decoders, font rendering and `.x` geometry support explicitly. Preserve the
remaining modes, localization, definitions, AI, persistence and networking
as visible compatibility requirements rather than silently narrowing the game.

Because these research docs live under `docs/plans/`, they do not collide
with the engine generator's requirement that `docs/engines/boggle/` be absent.
Move/link them into the engine's documentation when its scaffold is generated.

## Reproducing the analysis

With the owner-supplied files outside the repository:

```sh
reluva --hash /external/install/Boggle.exe /external/install/Language/English.dll /external/install/Dicts/English.dic
reluva-tool pe-info /external/install/Boggle.exe
objdump -p /external/install/Boggle.exe
ffprobe -v error -show_streams -show_format -of json /external/install/Anims/INTRO.AVI
ffmpeg -v error -nostdin -i /external/install/Anims/INTRO.AVI -f null -
```

Whole-file SHA-256 is `hashlib.sha256(path.read_bytes()).hexdigest()`.
For PE evidence, read the COFF/optional headers and section table; map RVAs
through the section's virtual address and raw offset. Walk the resource
directory rather than treating offsets as addresses. A string-table resource
block contains sixteen length-prefixed UTF-16LE strings; ID is
`16 * (block_id - 1) + slot`. All EXE offsets cited above are file offsets;
the entry point is explicitly an RVA. Keep extracted text/disassembly external.
