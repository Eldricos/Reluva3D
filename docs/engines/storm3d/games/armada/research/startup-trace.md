# Star Trek: Armada: the start-up, traced

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

November 2026, the English retail 1.0 install, with
`reluva-tool storm3d host <game folder> -ms 20000 -frames <dir>`
([systems/hosting.md](../systems/hosting.md)).

## The files it opens, in order

What the program reads from its first instructions to its main menu
(`no`: not there):

1. `3D_Paths\Defpath1.pth`, `f5ap1.pth`, `f5ap2.pth`, `f5ap3.pth`,
   `p2.pth`, `p3.pth`: all six paths, first of all.
2. `shell.set`, `mshell.set`: the front end's settings.
3. `splash.bmp`, through `LoadImageA`: the splash screen.
4. no `Armada.map`.
5. `Logfile.cfg`, then writes `Logs\logfile.txt` (its log's header: the
   log classes it knows, `PHYSICS` to `Diagnostic`).
6. no `lang.txt`; no `xyzzy` (a file it looks for and doesn't need: a
   developers' switch, by its name).
7. `startrek.zix`.
8. `RTS_CFG.h`, `ART_CFG.h`, `game_cfg.h`: the settings in C.
9. no `ARMADA.PRF`: the player's settings, not there on a first run.
10. Sets a 640 × 480 display mode and shows black frames.
11. `SOD\material.SOD`: the "lighting material palette" its message names.
12. The sprite tables (`font_jag.spr`, `font.spr`, `title.spr`,
    `sprites.spr` and those it includes), each followed by its textures,
    all from `Textures\RGB\` (none from `Index8\`): for each it opens
    `<name>.tga`, then `<name>.1`, `.2` and on until one isn't there
    (`MBuild.tga`, `.1`, `.2`, no `.3`): its mipmap levels.
13. `Dynamic_Localized_Strings.h`, then `assets.zfs`, then `events.odf`.
14. `sounds\effects\shellsd0.wav` (played with `PlaySoundA`, which fails
    here: no wave device).
15. The main menu, a modal dialog (template 0x123, a 640 × 480 pop-up with
    no controls; the program draws all of it): `bitmaps\main\mainbk.bmp`,
    its background, drawn with GDI.
16. Eight Bink movies, two for each of the four panels:
    `animations\single01.bik`, `multi01.bik`, `const01.bik`,
    `credit01.bik`, then the `02`s. The panels play the first of each
    (Bink through DirectDraw); the menu's frames show them moving.
17. The labels, three bitmaps each (`bitmaps\ms\msintro1.bmp` to `3`,
    `mscredit`, `mssingl`, `msmulti`, `msoption`, `msquit`), with
    `label.map` among them. Only the first of each is drawn at rest;
    the other two are for the pointer over the label and pressed
    *(guessed: by their order and the menu's look)*. Each is blitted
    through a mask made against the colour key `0xff00ff`.

The menu then waits, its panels playing, at about 59 frames a second of
the program's clock.

## Into a mission

With `-input`, clicks at "Single Player" (90, 18), then at the
Federation's panel (130, 150), then at the mission panel's OK button
(268, 395):

1. `sounds\effects\cornron.wav`; the single-player screen, another modal
   dialog (template 0x73), over `bitmaps\single\singlebk.bmp` and
   `bitmaps\sp\sptitle.bmp`, with ten Bink movies: `fed01.bik`,
   `kling01.bik`, `rom01.bik`, `borg01.bik`, `omega01.bik`, then the `02`s.
   The race under the pointer plays its movie (the Federation's emblem
   lit when the pointer was over it).
2. Its buttons (`spprev`, `spload`, three bitmaps each).
3. Clicking a race opens the mission panel inside it, a third dialog
   (template 0x124), which the program moves to (224, 46), 192 × 405:
   `sppanel.bmp`, `spselect.bmp` (the chosen mission's bar), `spok` and
   `spgame`. The race's movies stop meanwhile. The missions' names are
   text, not drawn here yet.
4. OK: `sounds\effects\cornrclk.wav`, then the first mission's map,
   `bzn\4first.mdf`.
5. The loading screen's models, each looked for in `addon\` before `SOD\`
   (`mbgbaku.sod`, `stalogo.sod`), with their textures. `addon\` isn't in
   the install: a place for add-ons' models to replace the game's
   *(guessed: by the order)*.
6. Then the CD check, below.

## The CD check

Before the mission, the program checks for its CD (0x4fbb00). It reads
`texture.dat` beside `Armada.exe` (the path from `GetModuleFileNameA`;
its size has to be a multiple of 4) and XORs what follows its first 4
bytes with the 400-byte key the `.ldl` scripts use (0x4eb810 returns the
key's address, 0x604548; [formats/ldl.md](../formats/ldl.md)). Then for each
drive letter that `GetDriveTypeA` calls a CD-ROM drive it opens MCI's CD
audio there (`mciSendCommandA` `MCI_OPEN` with flags 0x3302: by type
number, 516, the drive as the element) and, in 0x4fbdd0, sets the time
format (`MCI_SET`, 0x80d) and asks for the disc's number of tracks
(`MCI_STATUS` 0x814 with `MCI_STATUS_ITEM`, item 3), then about each track
(`MCI_STATUS` with `MCI_TRACK`). The registry calls after it (0x4fbff2 on)
weren't traced. What it compares with what isn't known: `texture.dat`'s
8 bytes are the likely reference *(guessed: it's read and decoded just
before)*.

Run with the disc's table of contents (from its Alcohol 120% `.mds`: a
data track at 00:02:00 and an audio track at 67:39:13, the lead-out at
74:58:61), it asks, in milliseconds (`MCI_SET`, format 0): the number of
tracks (2), each track's length (4,057,173 and 439,640 ms), the disc's
(4,496,813 ms); then it plays the audio track (`MCI_PLAY` with
`MCI_FROM`) and asks the mode (`MCI_STATUS`, item 4) until the drive says
it's playing (`MCI_MODE_PLAY`, 526): "Track verification" is a disc that
plays. Then `MCI_STOP` and `MCI_CLOSE`, and `Track verification
successful.`

When it fails, it writes `Track verification failed.` and
`[Armada] Please insert Armada CD` to the debugger and asks again, without
end. A run's first try at this found `texture.dat` a folder too high: the
host had given the program's folder without its last backslash, which the
emulator now adds. With the disc's files on D: (`-cd`) but no table of
contents, MCI finds no disc, and the check fails the same way.

## The mission

Past the check, the mission first on the Federation's panel (its map
`bzn\rom2.bzn`) loads and runs (90 seconds of the program's clock
without a stop, the last frame its 3D scene: Romulan warbirds by a
planet, drawn by the program's Direct3D 7 through Reluva's rasterizer).
What it reads: `CCA.cfg`, `input.map`, `gamekey.map` (no
`giddi\keyboard.std` or `mouse.std`), `Collision.cfg`, the map
`bzn\rom2.bzn` and its tech tree `rom2.tt` (from `assets.zfs`), every
computer player's personality (`AI\AIPs\*.aip`, 87 files), 94 models and
517 textures, the minimap's textures (`shroudTexture`,
`cachedMapTexture`, `mapTexture`, made at run time: looked for and not
there), `SOD\font.sod`, and `credits.txt`.

On the way, `>>` into a word read the whole tech tree as one word and
wrote past its buffer: the host hadn't started Reluva's C++ library, so
its character table, built in its `DllMain`, was empty. The host starts
it now, before the install's DLLs.

## Where the run stops

Each stop was a function the emulator didn't stand in for; each was
added, and the run went on to the next:

| Stop | What it is | Added to |
|---|---|---|
| `TlsAlloc` | thread-local storage | already there, with the host threads |
| `InterlockedExchange` | WON's start-up | KERNEL32 |
| `fscanf` | reading the 3D paths | MSVCRT |
| `SetRect` | | USER32 |
| `CreateFontIndirectA` | | GDI32 |
| `LoadImageA` | the splash screen | USER32 |
| `GetUserNameA` | | ADVAPI32 |
| `_unlink` | before writing its log | MSVCRT |
| `bsearch` | | MSVCRT |
| `SetSystemCursor` | | USER32 |
| `IDirectDraw7::EnumSurfaces` | | DirectDraw |
| `DirectSoundCreate` | | the host: it fails, as with no sound device |
| `__RTtypeid`, `type_info::name` | reading the sprite tables | MSVCRT |
| `std::getline` | reading `events.odf` | MSVCP60 |
| `basic_string`'s substring constructor | | MSVCP60 |
| `basic_istringstream`'s constructor | reading `events.odf` | MSVCP60, with the string streams, their layouts measured from the program's inline constructors |
| `InitCommonControls` | | COMCTL32 |
| `PlaySoundA` | the shell's sound | WINMM: no wave device |
| `DialogBoxParamA` | the main menu | USER32: modal dialogs from templates, with `WM_PAINT` and `WM_TIMER` |
| `GetWindow` | the menu's start | USER32 |
| `SetDIBitsToDevice` | the menu's background | GDI32 |
| `DPtoLP` | the labels' masked blits | GDI32 |
| (no stop) a nested dialog waiting | the mission panel | the waiting dialog returns when the process ends |
| (no stop) the mission panel not seen | | GDI's painting shown when the program next looks for a message |
| `CoCreateGuid` | starting the mission | ole32 |
| `__RTDynamicCast` | loading the mission | MSVCRT |

With those, nothing stops it: at the time asked, the run ends it.
Until GDI drew, the frames showed only the panels; with GDI the whole
menu shows.
