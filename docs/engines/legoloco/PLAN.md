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
2. **Formats** (done): [the resource file](formats/resource.md),
   [object definitions](formats/dat.md), [boards](formats/sav.md),
   [`EE.INI`](formats/ee-ini.md) and [area arrangements](formats/lay.md),
   each with `reluva-tool legoloco` commands. The `.but` files are BMPs
   and the `.wav` files standard. Left for the phases that need them:
   the postcards (`.crd`, phase 6; [a first look](research/postcards.md))
   and the animated cursors (`.ani`, phase 4).
3. **Data viewer** (done; [systems/data-viewer.md](systems/data-viewer.md)):
   the boards drawn with their pieces, the pictures cut into frames with
   their frame sets played, the definitions and the sounds; on the way,
   how the game draws ([systems/drawing.md](systems/drawing.md)) and
   names its resources ([formats/resource-numbers.md](formats/resource-numbers.md)).
4. **Running `loco.exe`** (in progress; [systems/running.md](systems/running.md)):
   load it in the emulator and follow it with
   `reluva-tool x86-run` to see what it asks for, then stand in for what
   isn't there yet. Shared stand-ins go in `src/emulation/x86/`, since no
   engine may rely on another's code. The likely list (stood in for so
   far: DirectPlay, the movie window, DirectSound and `PlaySoundA`, the
   dialogs, GDI and the INI files;
   what's left is in [running.md](systems/running.md#how-far-it-gets)):
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
     `busy.ani`, read by a shared reader of the published `.ani`
     format), and INI files (`GetPrivateProfileStringA`).
5. **Playable** (largely: the movies, the front screen, the layouts,
   the Toybox, laying track, starting and building trains, the post
   office and postcards, saving and loading play, with the mouse and
   keys; [systems/running.md](systems/running.md)): the basic engine
   test taken through them.
6. **Network play** (step 1 done; [a first look](research/network.md),
   [what's followed](systems/running.md#network-play)):
   the game's own DirectPlay network code run as it is, with DirectPlay
   stood in for over Reluva's networking. In steps:
   1. *Watching it:* two copies of `loco.exe` in one Reluva process,
      their DirectPlay objects joined by an in-memory network, so the
      game's own network screens, sessions, players, groups and
      messages can be followed and stood in for one call at a time,
      with no real network in the way. Done: `reluva-tool legoloco
      pair` takes one copy hosting and one joining through the network
      screens into a game.
   2. *DirectPlay's model, in the shared stand-in* (`x86::DirectPlay`,
      for any program that uses it): sessions with a host, players and
      groups, the system messages (players and groups made, gone,
      joined; the session lost), guaranteed messages in order and
      others as they come, the message queue, `SendEx` and
      `CancelMessage`; tested with two objects in one test. Largely
      done with step 1: what's left is a new host taking over when the
      host leaves.
   3. *Over the network:* one service provider (the game's TCP/IP)
      over UDP (`net::UdpSocket`), Reluva to Reluva, with a small
      protocol of its own (a guaranteed, ordered channel for DirectPlay's
      guaranteed messages): sessions found on the local network by
      broadcast, a host's address typed in reaching it directly, and the
      meeting server listing sessions and introducing players across
      routers; port mapping for a host on the Internet.
   4. *Played:* two copies over the loopback and over a network: the
      session screens, the players' boards joined, trains crossing,
      postcards sent and received; `reluva-tool legoloco run` able to
      host and join, and an engine test with two copies over the
      in-memory network.
   5. *Later, if wanted:* playing with the original game on Windows,
      over DirectPlay's own protocol as Microsoft has published it.
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
| November 2026 | Network play Reluva to Reluva first; the original game on Windows later, if wanted | Reluva's own protocol under DirectPlay's interfaces is far smaller than DirectPlay's own wire protocol, and needs no copy of Windows to test against |
| November 2026 | IPX is offered as the local network: the same network as TCP/IP | No computer has IPX now; the game offers it, and a player picking it should still find the others |
