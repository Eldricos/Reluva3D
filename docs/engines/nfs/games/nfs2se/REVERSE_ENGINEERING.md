# Need for Speed II SE: how it was worked out

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

Studied November 2026: a US "Need For Speed II SE" PC CD (English and
Spanish set-up) and an install made from it. Its executables, their
dates and the files to recognise it by are in
[research/first-survey.md](research/first-survey.md).

### What else may be used

Allowed under CONTRIBUTING.md, as for NFS III
([its sources](../nfs3/REVERSE_ENGINEERING.md#sources)):

- **The retail data and executables** (this copy). Reverse engineering
  of the executables is for interoperability: the Win32, DirectX, Glide
  and iSND interfaces, and what each format's loader does.
- **Public format write-ups:** the NFS modding community's documents on
  NFS II's `.trk` and `.geo` files, MultimediaWiki's pages on EA's
  formats (SHPI, RefPack, SCHl, the `mTCD` video), and fan wikis.
- **Open-source readers, as references only:** OpenNFS (MIT), which reads
  NFS II, and FFmpeg's EA demuxer and decoders (LGPL 2.1+), read to check
  an understanding of a format. No code is copied from them.
- **Not used:** Denis Auroux's "NFSII Unofficial Track File Formats"
  (1997), the public write-up of `.trk` and `.col`: it forbids using its
  information commercially, which doesn't sit with the GPL. The `.trk`
  and `.col` pages rest on the files alone (decided November 2026,
  [PLAN.md](PLAN.md#decisions)).

## Methods

- **The files:** each kind identified by its header and the names the
  executables use for it, then read with NFS III's readers
  (`reluva-tool nfs survey`) to see which already hold.
- **NFS II's own formats** ([formats/](formats/)): each read off the
  files and checked on every retail file by survey scripts, the car
  models' layout against OpenNFS's reader too, and the geometry looked at
  (`trk-obj`, `geo-obj`): Proving Grounds a closed circuit, the models
  cars and a T. rex. The movies' frames decoded by a prototype in Python
  until their pictures came out whole, then compared with FFmpeg's
  decoding of the same frames (its output only).
- **Recognising the release:** the files compared between the install
  and the CD (`cmp`) for ones both have the same, and hashed as
  detection hashes them (MD5 of the first 5000 bytes).
- **The executables:** their PE headers, imports, exports and strings.
- **The game running:** `nfs2sen.exe` loaded in Reluva's x86 emulator
  (`reluva-tool x86-run`), then run on the engine's host
  (`reluva-tool nfs host`), its calls logged, its frames saved, its
  routines disassembled where it went wrong (`objdump`).

## The tools

`reluva-tool help nfs` lists the engine's commands
(`src/engines/nfs/tools/`). Those used here:

| Command | What it does |
|---|---|
| `reluva-tool nfs survey <folders...>` | Reads every file of each kind it knows under the folders, and counts what reads and what fails |
| `reluva-tool nfs viv-list <file.viv>` | Lists a VIV archive |
| `reluva-tool nfs fsh-info <file>` | Lists a picture bank's pictures |
| `reluva-tool x86-run <file>` | Loads a Windows program in the x86 emulator and runs its start-up |
| `reluva-tool detect <folder>` | Which game, and which release, the launcher finds in a folder |
| `reluva-tool nfs page <folder> <out.tga> <page>` | Draws one of the engine's pages for the game in the folder, without a window (the Status page, say) |
| `reluva-tool nfs trk-obj <file.trk> <out.obj> [level]` | A track as a Wavefront OBJ, to look at elsewhere: its ground at a level of detail and its placed objects |
| `reluva-tool nfs geo-obj <file.geo or file.viv name> <out.obj> [-level n]` | A car model as a Wavefront OBJ, all its parts or one level's |
| `reluva-tool nfs host <install> [-program file] [-deterministic N] [-frames dir] [-keys ms:key,...] [-keysfile file] [-log N]` | Runs the game's program on the host without a window: its calls, files and frames (each frame's moment in `frames.txt`), keys and mouse given at moments of its clock, or as they're added to a file (`-keysfile`, to drive it by what its frames show); `-program` runs another (the CD's `NFS2SEA.EXE`, on Glide as a Voodoo Graphics); `-net`, `-serial`, `-modem` its links (two copies racing: [research/network-play.md](research/network-play.md)) |
| `reluva-tool nfs dct-frames <file.dct> <output dir> [first] [count] [every]` | A movie's frames as pictures |

## Working files not included

None yet.
