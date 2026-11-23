# Star Trek: Armada: the start-up, traced

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

November 2026, the English retail 1.0 install, with
`reluva-tool storm3d host <game folder> -ms 20000 -frames <dir>`
([systems/hosting.md](../systems/hosting.md)).

## The files it opens, in order

What the program reads before it stops, from its first instructions
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
| `basic_istringstream`'s constructor | | not yet: the string streams' layouts are still to be measured from the program's code |
