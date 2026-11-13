# Need for Speed III: Hot Pursuit

Game id `nfs3`, on the [Need for Speed engine](../../README.md); its code
is in `src/engines/nfs/` (the game's own executable run in the x86
emulator in `host/`).

## Releases

| Release | Recognised by | State |
|---|---|---|
| US "Need for Speed III Classics" CD (English, Windows), and an install from it; `nfs3.exe` file version `27, 2.0` | `nfs3.exe` and the first track's `tr00.frd` (`detectionTable`, `src/engines/nfs/games.cpp`) | Plays: the retail game hosted, from the menus to every race type, single player and network; every file read by the viewers |
| Other releases (the 1998 retail, other languages, patched) | `nfs3.exe` and `tr00.frd` present (`fallbackTable`): "unknown release" | Not checked |

Detection signatures (MD5 of the first 5000 bytes, size):

| File | MD5 (first 5000 bytes) | Size |
|---|---|---|
| `nfs3.exe` | `5cbf7d80f6b993926f7b41a0aa3ffc99` | 1589248 |
| `GameData/Tracks/trk000/tr00.frd` | `42ac1185877f2c7fbaa0de8c7b0d6351` | 1422739 |
| `FeData/text/text.eng` | `9295f24e9ca2d3b193fc39ec2429d7cd` | 35402 |
| CD `gamedata/audio/pc/alpirock.mus` | `612967aa448ea3538b53e63e2061b3dc` | 14376248 |

## Where it stands

- **The data viewer:** the folders found, every track and car in 3D, the
  pictures, strings, fonts, sounds, music and movies.
- **The game itself:** the retail `nfs3.exe` runs in the x86 emulator, on
  the Play page: its menus, movies, every race type, network play over
  TCP, IPX, serial and modem links, force feedback and game controllers.
  It draws through Reluva's own THRASH renderer (on the GPU, or natively
  in software), or its own Voodoo renderers on the Glide stand-in.
- Plans, phases and decisions: [PLAN.md](PLAN.md).

## Using it

Need for Speed III is found from its install folder or a copy of its CD. It reads
the music and movies from the CD's files: in the install if they were copied there,
in a copy of the CD beside the install (a folder with `nfs3.exe` and
`GameData/Audio/pc`), or wherever `cdpath=` in the game's section of `reluva.ini`
says. It opens as a data viewer: **Tab** switches between what it found (the folders,
tracks and cars), the game itself, the tracks and cars in 3D, textures, strings,
fonts, sounds, music and movies. The **Play** page runs the retail `nfs3.exe` in the
x86 interpreter, starting it when first shown. The keyboard and mouse go to the game,
**Esc** included; **Ctrl+F5** goes back to the launcher. It writes its saves and
settings to Reluva's save folder for the game, never to the install. It plays its
own movies (Esc skips the intro), and races run at about real time. With a GPU
the game is drawn with it, at twice its 640x480 unless `renderscale=` (1 to 4) in
the game's section of `reluva.ini` says otherwise. Its textures are filtered, as
the game's renderers ask for; `texturefilter=point` point-samples them instead. The game is told it has a Voodoo 2 and draws as for one (with fog);
`renderer=direct3d` tells it it has its Direct3D renderer instead, and it draws its
Direct3D look (no fog, flatter clouds, filtered textures), and `renderer=glide` runs
its own Voodoo 2 renderer on Reluva's Glide stand-in. A game controller connected when the game starts is offered to it as a
joystick (a racing wheel as a wheel), and the game's own Controllers screen binds it
(`joystick=off` leaves it out). The window holds the mouse while the game is shown,
the game drawing its own pointer. The black lines the game leaves between a movie's
are filled in (`movielines=keep` keeps them). Exit Game goes back to the launcher. The game's force feedback goes to a wheel's motor or a
pad's rumble (`forcefeedback=off` leaves it out); `wheelpedals=2,3` names a wheel's
accelerator and brake axes if they aren't found.

Its network play (Connect Players,
Network (TCP)) uses the machine's network, other players joining on TCP port 1030
(`network=off` leaves it out); Network (IPX) works on the local network too.
`serial=listen:2300` on one machine and `serial=host:2300` on the other make
Serial a link between them, and `modem=listen:2400` / `modem=host:2400` do the
same for Modem (Answer and Dial). `positionalupdates=on` has network races use the
game's positional updates, meant for slow links and the internet (each copy runs its
own car and places the others from their reports); every player needs it alike, and
the game then leaves Hot Pursuit out of the race types.

The game's Dolby Surround sound mode
encodes sounds behind into the stereo; `surround=prologic` decodes it with a model
of a Pro Logic decoder, written from the public description, and plays it on a 4.0
or 5.1 device when the default one has that many channels (stereo, unchanged,
otherwise). `surround=passive` uses the passive matrix alone, and
`surround=headphones` folds the decoded channels back to stereo. Without the
setting the sound plays as mixed.

The 3D pages
need a GPU with Vulkan (Linux, Windows) or Metal (macOS). On tracks, **PageUp/PageDown**
pick one, **W/A/S/D** move, the arrows or a drag look, **Space/C** go up and down,
**Shift** goes faster, **N/B** jump to the next or previous piece of road, **1/2/3** pick
the road's detail, **H** cycles day, night, weather and night with weather, **G** shows
the walls the game keeps cars within, **X** steps through the texture sets (the two
16-bit ones, then their 8-bit versions), and **T/V/L/O/F/Y/M** turn textures, vertex
lighting, lane markings, objects, back faces, fog, and the lights and placed sounds off
and on. The sky has its clouds, sun or moon, fog and lightning; the lights glow and the
placed sounds play as the camera nears them. On cars,
**Left/Right** pick one, drag or **W/A/S/D** turn it, the wheel zooms, **L** changes the
detail, **T** the skin, **C** back faces and **Space** spins it. For textures, **Up/Down** pick a file
(**PageUp/PageDown** by 10), **Left/Right** a picture in it (**Home/End** the first and
last), **B** changes the background and **A** turns alpha off and on. For strings,
**Left/Right** pick a language, **Up/Down**, **PageUp/PageDown** and **Home/End** scroll,
and **F** switches between the game's font and Reluva's. For fonts, **Left/Right** pick
one and **+/-** change the size. For sounds, **PageUp/PageDown** pick a bank, **Up/Down**
a sound in it (played as it's picked; **P** turns that off), **Space** plays it and **S**
stops it; sounds with a loop (engines) loop until stopped, and **Left/Right** move the
pitch a semitone. For music, **Up/Down** pick a stream and **Space** plays it from its
start section (a race's intro), each section leading straight into the next as its map
says for the intensity, which
**Left/Right** change; **N** moves on at once and **L** switches between the map and the
linear order. For movies, **Up/Down** pick one, **Space** pauses, **Left/Right** jump 5
seconds and **PageUp/PageDown** step a frame.

The settings with a few choices (`renderer`, `renderscale`,
`texturefilter`, `surround`, `joystick`, `forcefeedback`, `network`,
`positionalupdates`, `movielines`) are in the launcher, under
**Settings...** for all Need for Speed games and with the game; the ones
naming something of the machine's (`cdpath`, `serial`, `modem`,
`wheelpedals`, `glidedll`, `glideboard`) go in the game's section of
`reluva.ini`. The master volume is the launcher's, as every engine's.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [formats/](formats/) | The game's own files, a page each: [viv.md](formats/viv.md), [refpack.md](formats/refpack.md), [fsh.md](formats/fsh.md), [text.md](formats/text.md), [ffn.md](formats/ffn.md), [fce.md](formats/fce.md), [frd.md](formats/frd.md), [col.md](formats/col.md), [hrz.md](formats/hrz.md), [tracks.md](formats/tracks.md), [bnk.md](formats/bnk.md), [mus.md](formats/mus.md), [mad.md](formats/mad.md), [3ddata.md](formats/3ddata.md) |
| [systems/](systems/) | How the game behaves, a page per part: [threads.md](systems/threads.md) (threads and the timer), [physics.md](systems/physics.md) (car handling), [net.md](systems/net.md) (network play), [thrash.md](systems/thrash.md) (the renderer interface) |
| [research/](research/README.md) | Working notes |
| [PLAN.md](PLAN.md) | The plan, its phases (with the hosted game's progress in detail) and decisions |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

Shared code it uses: the x86 emulator ([docs/common/x86/](../../../../common/x86/README.md)),
with its host threads and drives, and the Glide stand-in
([docs/common/graphics/glide.md](../../../../common/graphics/glide.md)).

## Terms

- **THRASH:** the game's renderer interface, 34 functions every renderer
  DLL exports ([systems/thrash.md](systems/thrash.md)).
- **iSND:** the game's sound interface, `eacsnd.dll` over DirectSound.
- **Hosted:** the retail `nfs3.exe` run in Reluva's x86 emulator, with
  stand-ins for Windows, DirectX, THRASH and iSND ([PLAN.md](PLAN.md)).
- **Virtual road:** the track's road as a line of points with their
  lanes and walls, in the `.col` files ([formats/col.md](formats/col.md)).
- **Pathfinder map:** a `.map` choosing the next music section as the race
  goes ([formats/mus.md](formats/mus.md)).
