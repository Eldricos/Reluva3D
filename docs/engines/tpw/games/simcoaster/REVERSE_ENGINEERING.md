# SimCoaster: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- **The game's program isn't read.** It's encrypted
  ([executable.md](formats/executable.md)), and a copy with its
  protection removed isn't used.
- See [CONTRIBUTING.md](../../../../../CONTRIBUTING.md) for what may and
  may not be used.

## Sources

- **The game's files**, from the US release, installed and on its CD.
- **Recordings of the original**, run from its CD on Windows (a real
  machine, or a virtual machine with the CD passed through), and public
  gameplay videos: for what the files don't say.
- **Published documentation:** Microsoft's PE/COFF specification (the
  executable's headers), the DirectX documentation, and public notes on
  Theme Park World's formats (the OpenTPW project's) where a page says
  so, every field checked against the data.

### What the US install holds

Surveyed from the install (the folder setup made: 1,323 files, 409 MB);
everything was read with a few lines of throwaway Python.

| Where | What |
|---|---|
| `Game.exe` | The program, under SafeDisc 2 ([executable.md](formats/executable.md)) |
| `QMixer.dll`, `usp10.dll`, `wea*.dll` | QSound's mixer, Windows' Unicode script library, the online services |
| `TPinc30.avi` | The opening movie: Cinepak, 320x240 |
| `Data/*.wad`, `Data/levels/<world>/**/*.wad` | `DWFB` archives: models (`.md2`, `.hmp`), pictures (`.fsh`, `.tga`), scripts (`.rse`), settings (`.sam`) |
| `Data/*.sam`, `Data/missions/*.sam` | Settings as text, `Key.Field value` lines; their comments name "Theme Park World" |
| `Data/Language/American/` | String tables (`BFST`, `.str`) and fonts (`F4FB`, `.bf4`) |
| `Data/global/sound/`, `Data/global/Speech/` | Sound banks (`.sdt`) with their `.map`s; `speechHD.SDT` is 37 MB of speech |
| `Data/levels/` | `wIntro`, `arabian`, `science` and `water`: terrain, objects (`scape.omp`), rides, shops, sideshows, scenery, skies and sounds |
| `Data/movies/*.tgq` | The logos: EA's `SCHl` streams of TGQ video |
| `Data/my coasters/*.cos`, `Data/PreBuilt/prebuilt.TPWS` | The coasters that come with the game, and a park |
| `Data/Init/<width>/` | The start-up screens, as `.png`, for each screen width |

The CD holds `Game.exe` (the same file), InstallShield 6's cabinet
(`data1.hdr`, `data1.cab`, `data2.cab`) and the folders setup copies as
they are: `Speech/American`, `CoasterData/American`,
`PostCardData/American` ([cd.md](formats/cd.md)).

## Methods

- **Telling the releases apart:** each file's `reluva --hash` signature.
  The settings files say "Theme Park World" and the CD's `AUTORUN.INF`
  "Theme Park Inc"; "SimCoaster" is in `Setup.ini` and in the string
  tables, in the game's own character set ([str.md](formats/str.md)). The
  US release is told by its `American` string tables.
- **Where setup puts the CD's files:** each installed file's MD5 matched
  to the cabinet entry or the CD file with the same MD5, which gave each
  group's and folder's place in the install; then the CD imported with
  `reluva-tool import` and compared with the install byte for byte
  ([cd.md](formats/cd.md#sources-and-checks)).

## The tools

`reluva-tool help tpw` lists the engine's commands
(`src/engines/tpw/tools/`):

| Command | What it does |
|---|---|
| `reluva-tool tpw files` | Lists every file in a game's folder, with its size |
| `reluva-tool tpw wad-list` | Lists an archive's files: sizes, how each is stored, the record's first word |
| `reluva-tool tpw wad-extract` | Writes an archive's files, or one of them, under a folder |
| `reluva-tool tpw sam-check` | Reads every settings file of a game, or lists one's settings |
| `reluva-tool tpw str-check` | Reads every string table in the language's character set, or lists one's strings |
| `reluva-tool tpw data-check` | Reads every file of a game's `Data` folder, archives and loose files, with a checksum of them all |

## Working files not included

None yet.
