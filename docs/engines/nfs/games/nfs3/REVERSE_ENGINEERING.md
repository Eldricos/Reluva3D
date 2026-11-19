# Need for Speed III: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../../../CONTRIBUTING.md) for what may and
  may not be used.

## Sources

### The copy studied

Studied October 2026, from a US "Need for Speed III Classics" PC CD and
an install made from it.

- **Executable.** `nfs3.exe` is 1,589,248 bytes, built 3 September 1998
  (PE time stamp 904846030), version resource `3, 0`. The install's copy
  and the CD's are identical. It was built with Watcom C/C++ (its runtime's
  copyright string, sections `AUTO`/`DGROUP`). It has 46,398 relocations.
- **Install** (239 MB). Holds `GameData` (tracks, cars, sounds, speech,
  HUD) and `FeData` (front end), the executable and its DLLs.
- **CD** (468 MB without the DirectX, 3dfx and ad folders). It also holds:
  - the music, `gamedata/audio/pc/*.mus|.map|.lin`;
  - the movies, `fedata/movies/*.mad`;
  - Spanish speech, and more car speech and description banks.

  `install.win` lists `D:\GameData\Audio\pc\` as the music path, so the
  game reads music (and movies) from the CD.
- **CD check.** The executable calls `GetVolumeInformationA`. The CD's
  label is `NFS3`.

Detection signatures (MD5 of the first 5000 bytes, size):

| File | MD5 (first 5000 bytes) | Size |
|---|---|---|
| `nfs3.exe` | `5cbf7d80f6b993926f7b41a0aa3ffc99` | 1589248 |
| `GameData/Tracks/trk000/tr00.frd` | `42ac1185877f2c7fbaa0de8c7b0d6351` | 1422739 |
| `FeData/text/text.eng` | `9295f24e9ca2d3b193fc39ec2429d7cd` | 35402 |
| CD `gamedata/audio/pc/alpirock.mus` | `612967aa448ea3538b53e63e2061b3dc` | 14376248 |

All nine tracks' `.frd` files are the same on the CD and in the install.

### What else was used

Allowed under CONTRIBUTING.md:

- **The retail data and executable** (this copy). Reverse engineering of
  the executable is for interoperability: the THRASH, iSND and Win32
  interfaces.
- **Public format write-ups.** The NFS modding community's documents on
  FRD/COL/FCE (Denis Auroux's NFS3 track and car format notes, as used
  by T3ED and FCEFinish), the MultimediaWiki pages on EA formats
  (RefPack, SHPI, SCHl/ASF, MAD), and fan wikis.
- **Open-source readers, as references only.** OpenNFS (MIT) and
  FFmpeg's EA demuxers and decoders (LGPL 2.1+) may be read to check our
  understanding of a format. No code is copied from them: Reluva's code is
  written from the format descriptions and the data (decided October 2026).

**Tools for reading the executable** (November 2026): `tools/nfs-re/`
holds query scripts over your own copy of `nfs3.exe` (angr and capstone):
`xref`, `field car 0x5b0` (every access to a struct's field), `decomp`
with the docs' names applied, and the hand-made name map `curated.py`.
They keep no game data; their caches go to `build/nfsre`. See its
README.md.

## Methods

- **The files** were read by comparing many of each kind, then checked
  by reading every one of them to its end (each format page says how
  many).
- **The executable** was read in its disassembly, with the query scripts
  below, for the THRASH, iSND and Win32 interfaces and for what each
  format's loader does; facts give their addresses in `nfs3.exe`.
- **The game running:** the retail game hosted in Reluva
  (`reluva-tool nfs host`), with its system calls logged (`-log`), memory
  watched (`-watch`, `-watchread`), dumped and poked at chosen moments
  (`-dumpmem`, `-poke`), its frames and sound recorded (`-frames`, `-wav`)
  and on an instruction clock for repeatable runs (`-deterministic`).
- **Recordings** of the game on the user's own copy, in a virtual machine
  through Direct3D (November 2026), compared with Reluva's frames and
  sound; each page dates what it compared.

## The tools

`reluva-tool help nfs` lists the engine's commands
(`src/engines/nfs/tools/`):

| Command | What it does |
|---|---|
| `viv-list`, `viv-extract` | A `.viv` archive's files |
| `fsh-info`, `fsh-dump` | A picture bank's pictures |
| `text-dump`, `ffn-render` | The front end's strings, and its fonts |
| `fce-info`, `fce-obj` | A car model, and as a Wavefront OBJ |
| `frd-check`, `frd-obj` | Tracks' geometry, and as a Wavefront OBJ |
| `bnk-info`, `bnk-wav` | Sound banks, and their sounds as WAV |
| `mus-info`, `mus-wav` | Music streams and their maps, and sections as WAV |
| `mad-info`, `mad-frames`, `mad-wav` | Movies: their frames and sound |
| `survey` | What an install or CD holds, each kind of file read and counted |
| `host` | The retail game run without a window, with its logs, watches, pokes, frames and sound |
| `huff-check`, `native-check`, `mmx-check` | The game's own routines against Reluva's, on its files or random inputs |
| `page` | One of the engine's pages drawn after given keys |

Research scripts that aren't part of Reluva are in `tools/nfs-re/` (queries
over your own `nfs3.exe`; [its README](../../../../../tools/nfs-re/README.md)).

## Working files not included

- The disassembly databases and caches `tools/nfs-re/` makes (in
  `build/nfsre`): made again from your own `nfs3.exe` by its scripts.
- Frames, sound and logs of runs, and the recordings of the game: made
  again with `reluva-tool nfs host` and from your own copy.
