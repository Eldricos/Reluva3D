# LEGO Loco: plan

## Goal

The CD's release (version 1.0.18.2) played from the player's own copy,
in English first and then in the CD's other eight languages: the board
and building on it, the trains, cars and minifigures, the date-based
surprises, saving and loading, the screen saver's layouts, the movies,
and postcards and network play between copies.

## Phases

The readers and a data viewer first, as they're needed whichever way the
game is run; then `loco.exe` run in the x86 emulator, with what it calls
of Windows and DirectX stood in for. What the CD and the executable hold
is in [the first survey](research/first-survey.md).

1. **Start** (done): the engine from the template, detection of
   the CD and of an install, and this plan.
2. **Formats** (in progress; the resource file is done,
   [formats/resource.md](formats/resource.md)): readers for the resource file (`resource.RFH`, its index,
   and `resource.RFD`, its data, many entries compressed), then the
   object definitions (`.dat`, text), `.but`, `.lay`, the saves (`.sav`),
   the postcards (`.crd`) and `EE.INI`, a page each in `formats/`.
   `reluva-tool legoloco` commands to list and extract the resource
   file's entries.
3. **Data viewer:** the pictures and their frames, the sounds, the object
   definitions, and a save drawn as its board.
4. **Running `loco.exe`:** load it in the emulator and follow it with
   `reluva-tool x86-run` to see what it asks for, then stand in for what
   isn't there yet. Shared stand-ins go in `src/emulation/x86/`, since no
   engine may rely on another's code. The likely list:
   - DirectSound, which so far only the `nfs` engine stands in for;
   - DirectPlay (`DPLAYX.dll`);
   - an MCI window playing the AVI movies (`MSVFW32.dll`'s
     `MCIWndRegisterClass` and `mciSendCommandA`), on the shared Cinepak
     decoder;
   - dialogs from the executable's own templates (`DialogBoxParamA`,
     `CreateDialogParamA`);
   - GDI's text and shapes (`CreateFontA`, `DrawTextA`, `Ellipse`, `LineTo`);
   - the Open and Save dialogs (`comdlg32.dll`), as Reluva's own;
   - `PlaySoundA`, animated cursors (`LoadCursorFromFileA` with
     `busy.ani`), and INI files (`GetPrivateProfileStringA`).
5. **Playable:** the intro movie, the main screen, building and running
   trains, saving and loading; the basic engine test taken through them.
6. **Postcards and network play:** DirectPlay stood in for over Reluva's
   own networking, its meeting server for players who can't reach each
   other.
7. **Checking against the original:** recordings of the game running,
   [TESTING.md](TESTING.md)'s playthrough,
   [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md), and speed.

## Decisions

| Date | Decision | Why |
|---|---|---|
| November 2026 | Engine id `legoloco`, an engine for one game | `loco` alone could be taken for OpenLoco, which is Chris Sawyer's Locomotion, an unrelated game. Intelligent Games' engine isn't known to have run another game |
| November 2026 | Run `loco.exe` in the x86 emulator rather than reimplement the game natively | The executable is small (1.1 MB), 2D only (DirectDraw, no Direct3D), not protected, and links its C runtime in, so it needs no C++ library stood in for. Its trains', cars' and minifigures' behaviour and its surprises then come out as the original's. What it calls is publicly documented (Windows, DirectX 5). A native engine stays the fallback; the readers and viewer of phases 2 and 3 serve both |
| November 2026 | One detection entry for the CD, whichever language it was installed in | The installer copies the same files for every language and writes the choice into `Exe/LEGO.INI` (`[Locale] Language=`); English first, the others later |
| November 2026 | Community write-ups of the resource file's layout and compression may be read as documentation; no code from them is used | They're published notes of the format; every fact taken from one is checked against the CD's files and the page says so ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)) |
