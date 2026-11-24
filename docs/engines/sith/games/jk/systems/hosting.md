# Jedi Knight: running JK.EXE in the emulator

> **Status:** partial · **Code:** `src/engines/sith/host/`, `src/engines/sith/tools/host.cpp` · **Checked against:** the retail `JK.EXE` run in Reluva's x86 emulator, and its code · **Confidence:** seen, not traced unless marked

## Overview

The plan's way B ([PLAN.md](../PLAN.md#two-ways-to-play-the-game)), the
gate's choice: the retail `JK.EXE` run as it shipped in Reluva's x86
emulator ([docs/common/x86/](../../../../../common/x86/README.md)), with
`SMACKW32.DLL` from the install, played in Reluva's window. It gets
through its start-up, reading its
archives, strings, fonts and the menus' pictures, lists Direct3D's
devices, sets the display mode, shows the opening's title and crawl,
which it draws itself, and its menus, cut scenes and levels play, drawn
by its own software renderer or through Direct3D, with its sound and the
CDs' music, and multiplayer between copies of Reluva.

## How it works

### What it imports

`JK.EXE` (1,387,520 bytes, linked 11 September 1997, by Visual C++ 5's
linker, with no relocations) imports DirectDraw, DirectSound, DirectPlay
(by ordinal 4), DirectInput, `smackw32.dll` (9 functions by ordinal),
WINMM (timers, joysticks, the CD's audio through MCI, mixer volumes),
ole32 and the usual KERNEL32, USER32, GDI32 and ADVAPI32. The interfaces
it uses are DirectX 5's, by the identifiers in its data:

| Part | Interfaces |
|---|---|
| DirectDraw | `IDirectDraw`, `IDirectDraw2`; surfaces 1 to 3 |
| Direct3D | `IDirect3D`, `IDirect3D2`; devices 1 and 2; textures 1 and 2; the Ramp, RGB, MMX and HAL devices |
| DirectSound | 3D buffers and listener |
| DirectPlay | `IDirectPlay3`, `IDirectPlayLobby2A` |

Not all those identifiers are asked for: they come with the table of
them the program was built with. Its code asks `QueryInterface` for
`IDirect3D` (`0x429326`), `IDirect3DTexture` (`0x42a553`, `0x42a70d`)
and `IDirectDraw2` (`0x4296f0`, `0x42aa37`), and nothing refers to the
identifiers of `IDirect3D2`, `IDirect3DDevice2`, `IDirect3DTexture2`,
`IDirect3DViewport2`, `IDirect3DMaterial2` or `IDirectDrawSurface2`
*(from the code)*: the game uses Direct3D 1, as the runs did.

`SMACKW32.DLL` plays sound through WINMM's `waveOut`, and asks for
0x400000 as `JK.EXE` does (it can be relocated; `JK.EXE` can't).

The emulator stands in for every function the two import. Running
another program (`WinExec`, for the joystick control panel) answers as
Windows does when it fails, and dialog boxes are made but not drawn
([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).

### Its start-up, as run

1. It writes `Version` under
   `HKEY_LOCAL_MACHINE\Software\LucasArts Entertainment Company\JediKnight\v1.0`,
   and reads `CD Path`, `InstallType` and `bHighResGraphicsInstall` there,
   the names beside the key in its strings *(from the code)*.
2. It registers its window class (`RegisterClassExA`) and the common
   controls.
3. It opens `Resource\Res1hi.gob` and `Resource\Res2.gob`. For each file
   it wants, it first tries a loose file: at the install's root
   (`C:\ui\jkstrings.uni`), in its `resource` folder, then on the CD
   (`D:\gamedata\resource\ui\jkstrings.uni`); then the archives.
4. It reads `ui\jkstrings.uni`, and says so for a debugger: `Premature
   'END' found after only 787 lines in 'ui\jkstrings.uni'. Check number in
   'MSG xxx' header.`: the count is a limit, as
   [formats/uni.md](../formats/uni.md) has it.
5. It reads the fonts (`small0.sft`, `small1.sft`...) and the menus'
   pictures (`bkEsc.bm`, the arrows, sliders and buttons).
6. At `0x429310` it asks its DirectDraw for `IDirect3D` and lists
   Direct3D's devices with a callback at `0x42ba60`, which copies the
   hardware description (204 bytes, DirectX 5's) if its colour model isn't
   0, and the software one if it is *(from the code)*. With no `IDirect3D`
   it returned 0, its list of devices (`0x421fc0`, `0x4273e0`; entries of
   680 bytes) stayed empty, and it read through a null pointer at
   `0x414396`.
7. It picks the display mode nearest 640 by 480 in 8-bit colour from
   those DirectDraw listed (`0x424340`), and says `ERR_NEED_256_COLOR`
   ("Jedi Knight requires that your display is set to at least 256
   Colors.") if the nearest isn't 8-bit *(from the code)*.
8. It creates DirectPlay (`CoCreateInstance`, `IDirectPlay3`) and lists
   its connections (`0x42fa40`), without checking the object was made.
9. It sets up DirectInput's keyboard and mouse, and enters its main loop
   (`0x50e8f0`): `PeekMessageA`, and between messages a step of its
   state machine (`0x4037e0`). The machine stands still until the window
   procedure (`0x50ecb0`) has had `WM_ACTIVATEAPP` and `WM_ACTIVATE`
   *(from the code)*; the window is made with `WS_VISIBLE`, which
   activates it.
10. Active, it takes the focus, sets the display to 640 by 480 in 8-bit
    colour, makes its primary surface and palette and clears the screen,
    then makes an Arial font through GDI (`CreateFontA`, `0x422a0e`), for
    text it could write on a surface through its device context
    (`0x4240f0` to `0x4242ab`: `GetDC`, `SetBkMode`, `SetTextColor`,
    `TextOutA`). Nothing calls that code: run through the menus, a level
    and a cut scene, the game made its fonts four times and called
    `TextOutA` never; all its text is drawn in its own fonts.
11. It shows "A long time ago, in a galaxy far, far away....", in blue,
    and then the opening crawl over a field of stars, drawn by its own
    8-bit renderer into surfaces it locks, about 15 frames a second.
    It runs on with no input, past 40 seconds.
12. Esc ends the crawl. A first run asks for a player (*New Player*:
    a name, and Easy, Medium or Hard), then shows the main menu
    (*Single Player*, *Multiplayer*, *Quit*; *Players*, *Cutscenes*,
    *Setup*, *Credits*). The menus are drawn in 8-bit colour from the
    install's pictures and fonts, with no Direct3D; while they're up the
    main loop waits for messages (`GetMessageA`) rather than polling, and
    typing goes through `WM_CHAR`.
13. The menus answer the mouse: *Single Player*, then *New Game*, plays
    the first cut scene from CD 1 through `SMACKW32.DLL`, which streams
    its sound into a DirectSound buffer, and Esc skips it.
14. The first level loads and plays, drawn by the program's own 8-bit
    software renderer into DirectDraw surfaces, with the HUD; Esc brings
    up its in-game menu (*Objectives*, *Map*, *Jedi Powers*, *Return To
    Game*, *Restart*, *Load*, *Save*, *Setup*, *Abort*). No Direct3D
    device is made: software is the default, and Direct3D only draws when
    it's chosen in *Setup* (step 17).
15. Its sound is DirectSound's: the opening's soundtrack, the menus'
    clicks, the cut scenes', the level's. It makes about 30 buffers,
    plain ones (no DirectSound 3D buffers in the first level), setting
    their volume and pan itself as things move. SMACKW32 streams a cut
    scene's sound into a buffer of a quarter of a second, 16-bit stereo
    at 22,050 Hz, a quarter (62 ms) at a time, written about three
    quarters of the buffer ahead of the play position.
16. Its music is the CD's audio tracks, through MCI's `cdaudio` device
    *(from the code)*: at start-up (`0x4380d0`) it opens the device,
    sets the TMSF time format, and finds the auxiliary device whose
    technology is CD audio, for the music's volume. A level's script
    starts a song (`PlaySong(start, end, loop)`): `0x438220` plays from
    the start track to the start of the one after the end track, and if
    that's past the disc's end, from the start track on. Every five
    seconds (`0x4dafd0`) it asks the device's mode, and plays again if
    it has stopped. The volume (`0x4382a0`) is the player's music volume
    times the script's (`SetMusicVol`, `0x4ffaa0`), as left and right on
    the auxiliary device. Nar Shaddaa (`01_soundtrack.cog`) plays tracks
    2 and 3 from the start, at volume 0, and fades them in when Kyle
    leaves the bar: the bar is quiet.
17. With hardware drawing chosen in *Setup* (*Display*, *Enable 3D
    Acceleration*: the modes then offered are 16-bit; there's no list of
    devices to choose from, the game taking the HAL device), kept in the
    registry as
    `b3DAccel` 1, `displayDeviceGUID` (DirectDraw's, all zeros for the
    display's own), `3DDeviceGUID` (one of the devices Direct3D listed)
    and `displayMode` (an index into the modes it lists; 3 is 640 by 480
    in 16-bit colour), a level is drawn through Direct3D 1. It asks its
    back buffer for the chosen device (`QueryInterface` with the device's
    identifier), lists the device's texture formats, makes a viewport
    through `IDirect3D` and adds it to the device, and makes execute
    buffers. Each frame's polygons go into them as vertices already
    transformed and lit (`PROCESSVERTICES` copying them), triangles and
    render states, then `Execute`; it never uses Direct3D 2's
    `DrawPrimitive`. Its textures are surfaces it fills, and loads into
    the device's with `IDirect3DTexture::Load`, naming them by
    `GetHandle`. The render states it sets include modulated texture
    blending, linear filtering and the depth test (`ZFUNC` less-equal and
    always). The menus stay 8-bit, drawn as before. If a GUID can't be
    read, it goes back to software (`0x414424`) *(from the code)*.

    A frame in play is, in order (seen running, every one of 1,583 frames
    into Nar Shaddaa, its surface calls logged with their callers; the
    code at each address read):
    - the Z-buffer cleared over the view by `Blt` with
      `DDBLT_DEPTHFILL` (`0x42ac06`), never locked;
    - one scene: `BeginScene`, the 3D in execute buffers, `EndScene`;
      new textures' system-memory surfaces locked and loaded in it
      (`0x42a2f1`);
    - the back buffer locked whole for the automap, drawn as lines while
      the map is shown (`0x407d97`, `0x4d92c0`), unlocked;
    - the two gauges copied into the bottom corners, each with its
      transparent colour (`0x408c6f`, `0x408c9a`, through `0x4237b0` and
      `0x424e80`), and text the same way through the fonts' copy
      (`0x434d06`);
    - `Flip`.

    What's drawn after the scene only writes: the copies are plain or
    skip the transparent colour (`0x425176` to `0x425188`), and nothing
    in a frame of play reads the back buffer, copies from it, or locks
    the front buffer or the Z-buffer. Only the menus and loading screens
    copy from the back buffer to the front, a part at a time (`0x424ca3`),
    and colour-fill it (`0x42656e`), with no scene drawn.
18. **Multiplayer** goes through DirectPlay 3 (`IDirectPlay3A`, kept at
    `0x55d608`), its calls at `0x42fa40` to `0x4305d2` *(from the code)*:
    - It asks whether a lobby started it (`0x42f840`, from `0x4c993a`):
      `DirectPlayLobbyCreateA` (`DPLAYX.dll`'s ordinal 4: no service
      provider, no outer object, no data), `QueryInterface` for
      `IDirectPlayLobby2` (the Unicode one, its identifier at
      `0x5226b8`), and `GetConnectionSettings` of itself (application 0)
      with no buffer, to learn the size. Any answer but
      `DPERR_BUFFERTOOSMALL`, or the lobby not made, and it releases
      the lobby and goes on as not lobbied. Lobbied, it would read the
      settings, set `DPSESSION_KEEPALIVE` and `DPSESSION_NODATAMESSAGES`
      and clear `DPSESSION_MIGRATEHOST` in them
      (`SetConnectionSettings`), `Connect`, and ask what that gives for
      `IDirectPlay3`.
    - *Host Game* and *Join Game* list the connections
      (`EnumConnections`, up to 16, by name) for *Choose a network
      provider*, and the one chosen is initialized
      (`InitializeConnection`). The service providers' identifiers in its
      data (TCP/IP, IPX, modem, serial) are never used by its code.
    - A host makes its session (`Open`, `DPOPEN_CREATE`) from the game's
      settings: its name, the episode and level as `name:episode:level`
      (`kyle's Game:JK1CTF:c1.jkl`), its players' limit, a password if
      there's one, and the rank, score and time limits in the four user
      words; it asks for keep-alives and no data-change messages, and
      leaves host migration off. The settings it changes later go by
      `SetSessionDesc`.
    - *Join Game* asks for the sessions again and again without waiting
      (`EnumSessions` with `DPENUMSESSIONS_ASYNC`, all sessions, those
      with a password too), lists up to 32 by name, and shows the one
      picked's episode, level, players and rank, and its players before
      joining (`EnumPlayers` with `DPENUMPLAYERS_SESSION`); then it
      joins (`Open`, `DPOPEN_JOIN`).
    - Each end makes its player (`CreatePlayer`, the character's name, no
      event) and asks the caps (`GetCaps`: the players, speed and
      latency); teams are groups (`CreateGroup`, `AddPlayerToGroup`).
    - Every message is sent unguaranteed (`Send`, flags 0): the game
      resends what must arrive itself. It reads its messages with
      `Receive` (any, `DPRECEIVE_ALL`) until `DPERR_NOMESSAGES`; of the
      system's it acts on a player made (`DPSYS_CREATEPLAYERORGROUP`,
      players only), a player gone (`DPSYS_DESTROYPLAYERORGROUP`) and
      the session lost (`DPSYS_SESSIONLOST`), and passes over the rest.
19. **Joysticks** are WINMM's *(from the code)*. At start-up
    (`0x42df20`) it takes up to two of `joyGetNumDevs`, and for each that
    `joyGetPos` finds plugged in reads `joyGetDevCapsA`: X's and Y's
    ranges, Z, R, U and V's where `wCaps` says they're there, up to 12
    buttons (`wMaxButtons`), and a hat (`JOYCAPS_HASPOV`). An axis's
    middle is halfway along its range, with a dead zone of 5%. In play
    (`0x42e799`), only while an axis is bound to a control, it reads each
    joystick's `joyGetPosEx`: the axes, eight buttons, and the hat in
    hundredths of a degree (`JOY_RETURNPOVCTS`), cut into four directions.
    A new player's controls have the joystick disabled (`0x4d70d6` sets
    bit `0x20` of `0x8bd020`, which the controls then skip, `0x4d6dad`):
    *Setup*, *Controls*, *Joystick*, untick *Disable joystick*, and it
    lists the axes and buttons with their default bindings (X turns, Y
    moves forward and back, buttons 1 and 2 fire), which the player's file
    keeps. It has no force feedback: its DirectInput devices are the
    keyboard and mouse alone (`GUID_SysKeyboard` at `0x42dec6`,
    `GUID_SysMouse` at `0x42e12f`), and the joystick's and force effects'
    identifiers in its data are never used by its code.

20. **Windowed** (`-windowGUI` on its command line, `0x40158c`, which
    sets `0x860788`) *(from the code)*, it draws through GDI instead of
    DirectDraw: its 8-bit frame is a DIB section (`CreateDIBSection`,
    `0x436963`), its colours set with `SetDIBColorTable` (`0x436ad2`),
    selected into a memory device context; where full screen flips
    (`0x422920`), it blits the frame into its window's device context
    (`BitBlt`, `0x422971`) after `GdiFlush`. Run so in the emulator, it
    showed the opening, its menus answered the mouse, and the first level
    loaded and played at the same speed as full screen.

On the real Windows of the time Direct3D always had its software
devices, DirectPlay was installed with DirectX, and 8-bit modes were
offered, so the program never met their absence.

### Speed

The 51 million instructions of the start-up ran in 0.1 to 0.2 seconds, 200 to 500
million a second with the emulator's native code: well beyond the
Pentiums of 1997 the program was written for.

## In Reluva

The game is played from the launcher: the engine (`src/engines/sith/sith.cpp`)
runs `JK.EXE` on a thread of its own (`host/player.h`), shows each frame
it presents in the window, and passes the keyboard and mouse on. A frame
it draws windowed through GDI is shown the same way, and the pointer put
where the player points on it, within the game's window
([windows.md](../../../../../common/x86/windows.md)); `commandline` in
`reluva.ini` gives the game its command line.

- **The process** (`host/game_process.h`): the install as drive C, what
  the game writes going to Reluva's folder for the game (its player
  files and saves; it opens its archives for writing too, but only reads
  them, so they're never copied); a CD's copy as drive D, for the cut
  scenes and the music, CD 1 to start with; `SMACKW32.DLL` from the install; the registry's `CD Path` and
  `Install Path`. Its threads run on threads of this machine.
- **The registry:** what the game writes there (its *Setup* choices of
  display and Direct3D device among it) is kept in `registry.txt` in the
  game's save folder, and given back the next time it runs; `CD Path`
  and `Install Path` are Reluva's, set each time. The game writes its
  settings only as it ends (`0x4148f0`, in its end at `0x4013c5`), all of
  them as binary values. Leaving Reluva closes the game's window
  (`WM_CLOSE`): in the menus it then ends as on Windows, its window's
  `WM_DESTROY` running its end, settings saved. In play the game takes
  `WM_CLOSE` for itself and goes on (its handler at `0x41ede0` *(from
  the code)*), as it would on Windows; after three seconds Reluva stops
  it, then runs its own saving of the settings (`0x4148f0`, where its
  bytes are the US release's), so a choice made in *Setup* isn't lost.
- **Its devices:** DirectDraw, with the 8-bit modes the menus and the
  software renderer use, Direct3D 1's devices on the emulator's
  rasterizer ([directx.md](../../../../../common/x86/directx.md)),
  DirectInput, DirectPlay ([dplay.md](../../../../../common/x86/dplay.md)),
  DirectSound ([dsound.md](../../../../../common/x86/dsound.md)) and the
  CD's audio
  ([cdaudio.md](../../../../../common/x86/cdaudio.md)) are the
  emulator's, the sound card's output and the music played through
  Reluva's mixer. There's no `waveOut` device: the game is told so, as
  on a machine without one. DirectPlay's lobby is there, and tells the
  game no lobby started it, as when it's run from its own menu.
- **The joysticks:** the player's game controller, connected when the
  game starts or later, is its first joystick, and a second pad, or
  else a racing wheel, its second ([joystick.md](../../../../../common/x86/joystick.md)): the
  sticks, triggers, ten buttons and the D-pad as the hat. The game's
  defaults bind only the first; its *Setup* binds the second's axes and
  buttons like any other control. The `joystick` setting (*Game
  controller* in the launcher) leaves them out.
- **Multiplayer:** DirectPlay offers one connection, TCP/IP, over the
  machine's network. *Join Game* finds sessions hosted by other copies
  of Reluva on the local networks and on this machine, and at the host
  the `joinaddress` setting names (`reluva.ini`, the game's section:
  `host` or `host:port`), for a host beyond the local network; a host
  listens on UDP port 47624, which a router has to pass on for players
  from outside. Reluva answers the network as the game runs and while it
  waits, so a host in its menus still lets players join.
- **Changing CDs:** the episode's steps each name their CD (levels 9 on,
  and their cut scenes, are on CD 2), and before one starts the game
  checks for it (`0x40ebb0`, its argument the CD's number, 0 for either)
  *(from the code)*: it reads `gamedata\resource\jk_.cd` through its CD
  path and accepts that CD's marker (`0x69923284 | n << (n + 5)`: CD 1's
  `c4 32 92 69`, CD 2's `84 33 92 69`), either for 0, or `0x69973284`
  for any. Otherwise it closes its archives, asks "Insert Jedi Knight
  CD #n and press Enter", and checks again. Reluva puts the CD in drive D
  as the check starts: a jump at `0x40ebb0` (only where its bytes are the
  US release's) to a trampoline that calls the engine with the CD's
  number, runs the instruction the jump replaced and goes on. The game
  never asks; the CD device gets the new disc's music
  (`CdAudio::discChanged`).
- **The CD's music:** the CD in drive D's audio tracks, those beside its
  files: a cue sheet and its image's files, or the
  tracks ripped as `track02.wav` and on. Without them the game has a CD
  drive with no audio tracks, and plays without music, as it did with
  its CD missing.
- **Frames:** each one presented, no more often than a 60 Hz display's
  vertical blank, as the game's flips waited for it.
- **The mouse:** in the menus the game moves Windows' pointer, which it
  doesn't draw itself, so the window shows the system's pointer and
  passes its place on. In play it holds the mouse through DirectInput
  (exclusively acquired) and reads its movement: the window holds the
  mouse and passes the movement on, until the game lets go (its in-game
  menu).
- **Keys** go both to the window's messages, which the menus read, and to
  DirectInput, which play reads.

`reluva-tool sith host <install>` runs the same process without a
window, for up to `-seconds` seconds, and says what it opened, wrote to
the registry, printed for a debugger or showed in a message box, and
where it stopped (registers, and the return addresses on the stack).
`-d3d` writes *Setup*'s choice of Direct3D's hardware device and 640 by
480 in 16-bit colour to the registry before it starts, as the game writes
them. `-close <seconds>` closes the game's window then, as the player's
closing Reluva's does. A run stopped at its time rather than ended by
the game has its settings saved for it, as Reluva's player does
(below). `-skim` goes through the game unattended: each level and cut scene is
said as the game first looks for it; a cut scene is skipped after four
seconds, and in a level the game's own cheats are typed through its chat
key (T): `jediwannabe` (invulnerable) once it has loaded, then
`thereisnotry` (the level ends) every half minute until the next level
or cut scene comes; between levels the screens' *Ok* is clicked.
`-cheat <word>` types another cheat in each level (`sithlord`, the dark
side's powers, takes the game down the dark side's branch), and
`-frames <seconds> <folder>` saves a frame each so many seconds. The
cheats are the game's: 18 words, each byte XOR `0xc5`, from `0x53c680`,
each tied to a handler as the console starts (`0x41f240` to `0x41f5ff`)
*(from the code)*. `-pad "<seconds>:<control>=<value>,..."` connects a game controller and
moves it at those moments (`lx` `ly` `rx` `ry` -1 to 1, `lt` `rt` 0 to
1, the buttons `a` `b` `x` `y` `lb` `rb` `back` `start` `ls` `rs` `up`
`down` `left` `right` 1 or 0; `2.lx` and so on, a second controller's). `-net` gives the game the machine's network for multiplayer (and
`-join <address>` a host to look for sessions at), and says what
DirectPlay does: sessions made or joined, players come and gone, and the
datagrams sent and received. `-png` saves the last frame, `-calls` lists every stand-in called and the
last 40 in order, `-keys "<seconds>:<key>,..."` presses keys at those
moments (letters, digits, `esc`, `enter`, `space`, `tab`, the arrows, and
`click<x>x<y>` for the mouse's left button on the screen), `-wav`
saves what the sound card played, `-write`
names the folder for what it writes, and `-peek <address>` prints a word
of the program's memory at the end.
`-keys "8:esc,11:k,11.4:y,11.8:l,12.2:e,13:enter"` goes through the crawl
and the first player's name to the main menu;
`,16:click318x188,19:click294x108,25:esc` after it starts a new game and
skips its cut scene, into the first level. For multiplayer, with a
player who has a multiplayer character (*Multiplayer Characters*) and
`-net`: `-keys "8:esc,10:click556x448,13:click318x248,16:click458x100,19:click200x190,20:click518x448,23:click518x448,26:click573x448"`
hosts *The Duel* (*Players*' *Ok*, *Multiplayer*, *Host Game*, TCP/IP,
*Ok*, the game's settings' *Ok*, the character's *Ok*);
`-keys "8:esc,10:click556x448,13:click318x248,16:click180x100,19:click200x190,20:click518x448,28:click220x170,30:click518x448,33:click573x448"`
in a second copy, with a save folder of its own, joins it. `reluva-tool sith disasm
<JK.EXE> <address>` shows the program's instructions from an address.

## Sources and checks

- **Where the facts come from:** the runs of `reluva-tool sith host` on
  the retail install with CD 1's copy; `JK.EXE`'s imports, and the
  DirectX interface identifiers found in its data; its code at the
  addresses above, read with `reluva-tool sith disasm`.
- **Multiplayer:** two copies of `reluva-tool sith host -net` on one
  machine, each with its own save folder: one hosted *The Duel*; the
  other found "kyle's Game" (episode, level, "Players: 1 of 4" shown),
  joined it and played, seeing the host's Kyle in the level, about 20
  datagrams a second passing; each copy was told of the other's player,
  and the host of the joiner's leaving, 15 seconds after it stopped.
- **How it was checked:** each run's stop explained by the code where
  it stopped, as above.

## Open questions

None.
