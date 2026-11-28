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

### What else was used

Allowed under CONTRIBUTING.md, as for NFS III
([its sources](../nfs3/REVERSE_ENGINEERING.md#sources)):

- **The retail data and executables** (this copy). Reverse engineering
  of the executables is for interoperability: the Win32, DirectX, Glide
  and iSND interfaces, and what each format's loader does.
- **OpenNFS** (MIT, `LibOpenNFS`'s NFS2 reader), read only, as a guide
  to the car models' layout, the only public description of them found
  ([formats/geo.md](formats/geo.md#sources-and-checks)). No code is
  copied from it. No published description of NFS II's tracks or `.col`
  files is used ([formats/trk.md](formats/trk.md#sources-and-checks),
  [formats/col.md](formats/col.md#sources-and-checks)).
- **FFmpeg** (LGPL 2.1+): only its output, its decoding of the movies'
  frames compared with Reluva's; its `mdec` decoder wasn't read
  ([mdec.md](../../../../common/video/mdec.md#sources-and-checks)).
  `ffprobe` named the movies' stream MDEC too
  ([formats/dct.md](formats/dct.md#sources-and-checks)).
- **psx-spx's description of the PlayStation's GPU** (prose, read only),
  for the `.lgt` files' scale
  ([formats/hrz.md](formats/hrz.md#sources-and-checks)).
- **The formats NFS II SE shares with NFS III** rest on the sources
  their own pages give, in `docs/engines/nfs/formats/`.
- **Denis Auroux's "NFSII Unofficial Track File Formats"** (1997), the
  public write-up of `.trk` and `.col`, checked only for which files it
  lists ([formats/hrz.md](formats/hrz.md)): it forbids using its
  information commercially, which doesn't sit with the GPL. The `.trk`
  and `.col` pages rest on the files alone (decided November 2026,
  [PLAN.md](PLAN.md#decisions)).

## Methods

- **The files:** each kind identified by its header and the names the
  executables use for it, then read with NFS III's readers
  (`reluva-tool nfs survey`) to see which already hold.
- **NFS II's own formats** ([formats/](formats/)): the layouts read off
  the files and checked on every retail file by survey scripts, the car
  models' layout against OpenNFS's reader too, and the geometry looked at
  (`trk-obj`, `geo-obj`): Proving Grounds a closed circuit, the models
  cars and a T. rex. The movies' frames decoded by a prototype in Python
  until their pictures came out whole, then compared with FFmpeg's
  decoding of the same frames (its output only).
- **How the game uses them:** most of the format pages' facts, the
  fields' meanings and what the game does with them, come from both
  programs' code (`nfs2sen.exe` and `NFS2SEA.EXE`), their loaders and
  renderers above all, disassembled and read, and from values read in
  the hosted game's memory (the software program's depth-cue tables);
  each checked over the retail files.
- **Recognising the release:** the files compared between the install
  and the CD (`cmp`) for ones both have the same, and hashed as
  detection hashes them (MD5 of the first 5000 bytes).
- **The executables:** their PE headers, imports, exports and strings.
- **The game running:** `nfs2sen.exe` loaded in Reluva's x86 emulator
  (`reluva-tool x86-run`), then run on the engine's host
  (`reluva-tool nfs host`), its calls logged, its frames saved and
  compared with the viewer's pages, its memory read and dumped, its
  routines disassembled (`objdump`).

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
| `reluva-tool nfs host <install> [-program file] [-glideboard board] [-deterministic N] [-frames dir] [-keys ms:key,...] [-keysfile file] [-roaddrive from[:to[:right[:grip]]]] [-joystick name] [-wheel] [-ff] [-forcelog file] [-wav file] [-net] [-netaddr a.b.c.d] [-broadcast a.b.c.d,...] [-netdrop N] [-serial listen:port or host:port] [-modem listen:port or host:port] [-log N]` | Runs the game's program on the host without a window: its calls, files and frames (each frame's moment in `frames.txt`), keys and mouse given at moments of its clock, or as they're added to a file (`-keysfile`, to drive it by what its frames show); `-deterministic` a clock of instructions; `-program` runs another (the CD's `NFS2SEA.EXE`, on Glide as a Voodoo Graphics, or the board `-glideboard` names); `-roaddrive` the player's car driven along the road ([research/road-driver.md](research/road-driver.md)); `-joystick`, `-wheel`, `-ff` a joystick, a wheel, with force feedback, `-forcelog` its forces; `-wav` the sound to a file; `-net` (`-netaddr`, `-broadcast`), `-serial`, `-modem` its links, `-netdrop` every Nth packet lost (two copies racing: [research/network-play.md](research/network-play.md)). `reluva-tool help nfs` lists the rest |
| `reluva-tool nfs track-sounds <game folder> <trNN> <road point> <seconds> <out.wav>` | A track's placed sounds as heard at a point of its road, as a WAV file ([formats/trk.md](formats/trk.md#in-reluva)) |
| `reluva-tool nfs dct-frames <file.dct> <output dir> [first] [count] [every]` | A movie's frames as pictures |

## Working files not included

- **The survey scripts** that checked each format over every retail
  file (the format pages' "How it was checked" says what each
  counted): made again by reading the files as the pages describe, or
  with `reluva-tool nfs survey`, which reads them all.
- **The prototype MDEC decoder in Python** the movies' frames were
  worked out with ([formats/dct.md](formats/dct.md#sources-and-checks)):
  `src/video/mdec.cpp` decodes them now, and `reluva-tool nfs
  dct-frames` writes them out.
- **The programs' disassembly** (`objdump` of `nfs2sen.exe` and
  `NFS2SEA.EXE`): made again from your own copy.
