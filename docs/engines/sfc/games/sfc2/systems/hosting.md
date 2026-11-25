# Star Trek: Starfleet Command II: running the game's own program

> **Status:** partial · **Code:** `src/engines/sfc/host/game_process.cpp`, `src/engines/sfc/host/player.cpp` · **Checked against:** the English 1.0 program run in Reluva's x86 emulator · **Confidence:** from the code unless marked

## Overview

Reluva means to run the game's own `StarFleet2.exe` in its x86 emulator,
supplying the parts of Windows and DirectX it calls
([PLAN.md](../PLAN.md#decisions)). This page records how far the
program gets, what it asks for, and how fast it runs.

It gets through its start-up, reads its first files, passes its 3D test
on Reluva's Direct3D 6, sets an 800x600 mode and shows its first frame,
"Loading...", after about 45 million instructions. Loading, it throws
and catches C++ exceptions, which Reluva dispatches, reads
`sprites.q3`, and between 0.6 and 1 billion instructions shows its title
screen with its CD key dialog ("Authentication Protocol"), where it
waits for a key to be typed. Run from the launcher it's in Reluva's
window, and the mouse and keys reach it: a key typed into the dialog is
checked by the game. Given the owner's key, it writes it to the
registry, which Reluva keeps in the game's save folder, shows its full
title screen, and starts its sound, through Miles, and its menu music.
Run again, it finds the key and doesn't ask. Told by DirectPlay's lobby
that no lobby started it, it shows its main menu, whose buttons work.
The program the 2.0.3.6 patch leaves runs as far as its main menu too
([patching.md](patching.md#in-reluva)).

## How it works

What the program does as it starts, in the order the hosted run shows.
The addresses are in the English 1.0 `StarFleet2.exe`.

### The C runtime's start-up

From its entry point (0x6c6a9d), Visual C++'s C runtime, linked into the
program: thread-local storage, its heap, the locale (it asks for the
ANSI code page), the time zone. About 15.8 million instructions, then
WinMain.

### Its Windows version, and InterlockedCompareExchange

The program keeps InterlockedCompareExchange behind a wrapper of its own,
`tOSVersion::_OS_InterlockedCompareExchange` (named in the error it
builds at 0x52830d, with `OSVersion.cpp` and line 0x143). Told it's
running on Windows XP, it asks KERNEL32 for `InterlockedCompareExchange`
by name, and throws `nException::tInvalidAccess` when it can't have it.
It also loads `OSSpecific98.dll`; the install has a `95` and a `98`,
each exporting `OS_InterlockedCompareExchange`, so it picks one by the
Windows version *(inferred)*.

### Its files and settings

It reads `Assets/specs/ftrlist.txt` first, then looks six times each for
the folders `dmp3dDLL_Release`, `DLL` and `DRIVERS` in its own (the
install has `Drivers`, with Miles' sound providers), then reads
`SFC.INI` and `assets/strings/strings.txt`. Files are opened with the C
runtime's `open`, which closes a handle whose type `GetFileType` doesn't
say is a disk file. The loader at 0x62f580 opens a file whole through a
reader object, and throws a byte (0) when the reader can't open it.

### Its resolution

`SFC.INI`'s `[3D]` `lowres` picks the resolution (0x40a9ae): 0 is 800x600,
2 is 960x720, 3 is 1024x768, 4 is 1152x864, 5 is 1280x1024, 6 is
1600x1200, any other 640x480. `SFC2Setup.exe` writes `lowres` and
`driver` there, as its strings name only those *(inferred)*. The mode is set at 16 bits a pixel and 60 Hz, its size
passed through a rectangle made with `SetRect` (0x6ab01c).

### Its window, DirectDraw and its 3D test

It makes its window (with a menu, which it later takes off) and
subclasses it (`SetWindowLongA`, `GWL_WNDPROC`), then:

1. `DirectDrawCreate`, and from it `IDirectDraw2`; it sets the
   co-operative level and makes a surface, asking it for
   `IDirectDrawSurface3` and `IDirectDrawSurface4`.
2. Through `DirectDrawEnumerateExA`, each device's DirectDraw, asking
   it for `IDirectDraw4`, then `IDirect3D3`
   ({bb223240-e72b-11d0-...}): its devices (`EnumDevices`, whose
   callback at 0x6baa70 skips Direct3D's null device and takes a
   device's hardware description if it has flags, its software one if
   not) and display modes (`EnumDisplayModes`, whose callback at
   0x6bacc0 keeps a mode whose depth is among the device's render target
   depths).
3. `IDirect3D3::FindDevice` and `EnumZBufferFormats`, the display mode,
   then the device (`CreateDevice`), a viewport (`SetViewport2`,
   `SetCurrentViewport`, `Clear2`), the texture formats and the
   texture stages' states, checking them with `ValidateDevice`.

Without Direct3D 3 its 3D test fails, and it throws its error type
`tErrInfo` holding "Line: 149, Module: ...\shellApp.cpp" and
"fTest3D.mInit() failed".

### Its first frame

"Loading..." in white in the middle of a black 800x600 frame, after about
45.08 million instructions.

### Errors as C++ exceptions

The program reports failures by throwing C++ exceptions: the two types
above, and plain values. It also throws in normal running, and catches
what it throws: just after its first frame its file loader (0x62f580) is
asked for `assets/uibitmaps.zip`, which neither CD installs, and throws
a byte when it can't open it; its handler unwinds (`RtlUnwind`) and it
goes on. Four exceptions are raised before its title screen, all caught.

### Its paths

It finds its own folder from the drive's root: `GetVolumeInformationW`
for `C:\`, then `FindFirstFileW` for each folder of the path
(`C:\SFC2`), then looks there for `Assets` and `Q3`. Without its folder
found, it builds every path from `C:\`. Its files are opened by an
opener (0x6366d0) that tries `CreateFileW`, and `CreateFileA` when that
fails with `ERROR_CALL_NOT_IMPLEMENTED`, as Windows 95 and 98 do; it uses
the UTF-16 functions whatever version of Windows it's told it runs on.
It looks for `uq3.txt` in its folder and in `Assets` (no install has
one) before `sprites.q3`, which it names `./Assets/Sprites/sprites.q3`.

### What it writes

As it loads, it writes `tBitmapFontEx.log` in its folder
(`CreateFileA` with `CREATE_ALWAYS`), and closes it without checking it
was opened: where it can't write, it fails, reading through a null
`FILE` at 0x6ca498.

Given a CD key it takes, it writes one registry value,
`HKEY_LOCAL_MACHINE\SOFTWARE\WON\CDKeys`, named `StarfleetCommand2`:
16 bytes of binary (`REG_BINARY`), not the key's text, so encoded in
some way *(seen, not traced)*. Started with that value there, it shows
no dialog.

Reluva has no registry on any system, so what the program writes to it
is kept in the save folder's `registry.txt`, a line a value (key, name,
type and bytes in hex, separated by tabs), and read back each time it
starts.

### Its title screen

The Starfleet Command logo over stars, Taldren's and Paramount's, the
copyright lines and "v2.0.0.2" at the bottom right; over them the CD
key dialog, its ENTER and EXIT buttons, and the game's own mouse
pointer.

### Its sound

It plays its sound through Miles (`mss32.dll`), which Reluva stands in
for ([miles.md](../../../../../common/x86/miles.md)). After its title
screen it starts Miles (`AIL_set_redist_directory`, `AIL_startup`), opens
a wave-out driver with a format of its own (`AIL_waveOutOpen`), asks the
smallest buffer for 22,050 Hz 16-bit stereo, sets the master volume to
127, and allocates 16 sample handles. For its music it allocates two
128 KB buffers (`AIL_mem_alloc_lock`), sets a sample to 22,050 Hz,
format 3 (16-bit stereo) and a volume of 38, and fills the buffers in
turn from the music's WAV files, as `AIL_sample_buffer_ready` offers
them, checking `AIL_sample_status` between.

The music is the music zip's WAVs, played in the order
`Assets/Sound/SFCMusic.txt` gives: at the menu, `MNU_A1.WAV` and then
`MNU_A2.WAV` with nothing between, read and passed to Miles as they are
*(confirmed)*, by `reluva-tool sfc host -sound`. The game reads the zips
itself; its effects and voices, which are IMA ADPCM
([sound-zips.md](../formats/sound-zips.md)), aren't played before it stops.

### Its main menu

After its title screen and the start of its music, it asks COM for
DirectPlay's lobby (`IDirectPlayLobby3A`, at 0x619c94), which Reluva
stands in for ([dplay.md](../../../../../common/x86/dplay.md)), and asks it
for its connection settings (`GetConnectionSettings`, at 0x619d46) with
no buffer: told `DPERR_NOTLOBBIED`, that no lobby started it, it shows
its main menu, "Select Game": Single Player and Multiplayer, and Film
Room, Credits, Hot Keys, Vessel Library and Exit Game. It asks again on
each frame. Single Player leads to Tutorials, Skirmish and Campaign.

Without the lobby, its code reporting that `CoCreateInstance` failed
went on to ask the object it didn't get, and read through a null
pointer at 0x619d3f.

### Its missions

Opening the Skirmish screen, the game looks for `assets/scripts/*.scr`
and loads each as a DLL (`LoadLibraryA`), which starts its own C runtime
and loads `OSSpecific98.dll`; it asks for `ScriptEntry`, which returns
the mission's object, gives it five callbacks of its own, and keeps what
the object's method at +0x28 describes of it in a list of missions
(0x888650), freeing the DLL again (0x4d4431 to 0x4d44ea). It keeps a
cache of them in `assets/scripts/scripts.lst`. Each mission sets its own
kind in that description *(from the code)*: the `Met_` missions 0, the
campaign missions 1, `Gen_Playback` 8. The Skirmish screen lists those
of kind 4 that its empire may play (a bit of the description's word at
+0x48, at 0x4d4938, asked from 0x4765c6); without one, it says "No
missions available", and PLAY GAME (0x475902) does nothing, there being
no mission chosen.

The CDs install 104 missions, `Ski_1Free4All.scr` the only skirmish
mission. Run on a copy of the install missing some of its files (only
49 missions, few ship models: the copy, not the install, was short), the
screen listed none; with the whole install, imported from the CDs
([README.md](../README.md#using-it)), the Skirmish screen lists "Hostile Skirmish"
("An all out brawl ensues until one team is left standing"), and PLAY
GAME starts it: the player's Federation heavy cruiser in space, the
tactical interface, the mission's clock running. The Klingon ship comes
in close, and the weapons line under the view changes from 4 photon
torpedoes and 6 phasers to 2 and 1, whether for damage or the weapons
grouped isn't traced. 35 billion instructions (about
13 minutes) ran without a failure. On the short copy, the Klingons'
models missing, the run stopped at a null part of a ship the game
couldn't build (0x415269).

A skirmish won: CUSTOMIZE on the Select Skirmish screen opens the
Advanced Custom Skirmish, which sets each of up to five AI opponents'
team, empire, smallest and largest hull (Frigate, Destroyer, War
Destroyer, Light Cruiser, Heavy Cruiser, New Heavy Cruiser, Heavy
Battlecruiser, Carrier, Dreadnought, Battleship, each arrow stepping to
the next and round), BPV (LOW, MED, HIGH, MAX) and number of units, and
the skill level (CAPTAIN, COMMODORE, ADMIRAL), the start positions and
the era. With one Klingon frigate at LOW and CAPTAIN against the
player's heavy cruiser, red alert, the enemy targeted (Y) and the
weapons fired (Z) every 120 frames: the Klingon closed, the phasers hit
it, and at 4 minutes 21 of the mission's time it was destroyed. "Hostile
Skirmish: Congratulations! You are the winner!", its battle report: 136
damage scored on the enemy (59 of it internal), its shields' efficiency
56%, its fleet's losses 40 (its BPV); 65 on the player, none internal,
no losses. 60 billion instructions (20 minutes headless) ran without a
failure. DONE on the debriefing goes back to the Select Skirmish screen.
The same input from a fresh save folder played another battle (other
surroundings, the Klingon met later): what the save folder holds from
earlier runs changes the game's choices, so a run repeats only from the
same folder's contents.

The six tutorials (`Tut_*.scr`), listed under Single Player's
Tutorials (Command 190 and 290, Science 310, Weapons 380, Weapons
Advanced 480, Command 590, all the Federation's), each load and run:
Captain Wells' orders shown and spoken (the voices through Miles, a mono
sample beside the music), the lesson moving on as an order is followed
(T targeting a container brings the next, on firing), his prodding when
it isn't ("C'mon Lieutenant"), and, with orders left undone, "Mission
incomplete. Closing Session." and the Mission Debriefing: the battle
report, and "you have failed to follow orders. You must repeat course",
with VIEW REPLAY and DONE. Passing one is a matter of flying the ship
(Command 190 wants three containers destroyed, the phasers brought to
bear on each), not yet done by a scripted run.

A single-player campaign starts from Campaign's Campaign Selector (its
saved games, LOAD GAME, NEW GAME, DELETE GAME): NEW GAME lists the twelve
campaigns (Federation vs ISC first), the skill level, the era and the
character's name; BEGIN NEW CAMPAIGN shows its introduction, pages of
text and pictures with a progress bar while it loads, each page left
with a click. Then the Dynaverse: the hex map around the player's
homeworld, the player's rank, ship (a frigate) and prestige (50), the
empires and the help topics, MAP, MISSION, NEWS, SUPPLY, SHIPYARD and
MEDALS, and the stardate moving on (2273.0 at the start). A hex clicked,
the ship moves there (a MOVEMENT bar filling), and the map scrolls with
it. The map runs at about 500 frames a billion instructions, a third of
a mission's rate. After a move, the button reads MISSIONS while missions
are offered (a minute of the map's time, then it dims again); its
Mission Selector lists them (Convoy Escort and Patrol, the first time),
each with ACCEPT. Once one is offered, moving is refused ("You have
missions that must either be played or forfeited"). Accepted, its
Mission Briefing shows the orders and a map of the start, with READY;
then LOADING, and the mission: Convoy Escort's captain's log ("Our
convoy has been intercepted by an enemy raiding party"), the freighters
and their attackers, and the mission's reports as it goes ("They have
destroyed one of our freighters"). The mission and the Dynaverse
together need more than 256 MB: given that, the game stopped with its
SmartHeap's "Out of memory" as the mission loaded, so it's given 512.

Esc in a mission pauses it with a menu: FORFEIT MISSION, EXIT TO
WINDOWS, CANCEL. Forfeited, the mission closes its session and shows
the Mission Debriefing (the mission, "Forfeit", the prestige gained, 0,
and the battle report); DONE returns to the Dynaverse map, where the
stardate goes on, the ship moves again and missions are offered again.
The game keeps the mission's recording (`Auto1.rec`) and the campaign's
state in its folder, so in the save folder: its database,
`MetaAssets/db/Sfc2Spd.sds` and `db.info`, and on finishing a mission an
auto-save, a copy of both in `MetaAssets/db/saves/Auto Save/`. The
Campaign Selector lists the folders under `saves/` (its name, the
empire, the date it was saved); LOAD GAME copies the save's files back
over the database (`CopyFileA`), shows "LOADING... Please Wait." and
returns to the Dynaverse as it was saved: the same commander, ship and
prestige, the stardate going on from where it was. Saving needs folders
made (`CreateDirectoryA`) and found where only the program made them
(the save folder): without either the auto-save came out without its
database, and the load read through a null pointer at 0x52781b.

On the map, SAVED GAMES shows the saves (name, empire, time) with a
NEW SAVED GAME name box and SAVE, LOAD and DELETE. SAVE makes
`saves/<name>/` (`CreateDirectoryA`) and copies the database into it
(`CopyFileA`); DELETE deletes the save's two files and removes its
folder (`DeleteFileA`, `RemoveDirectoryA`). Deleted, Auto Save is made
again straight away, of the campaign as it is; a save by name stays
gone.

A campaign mission won: Convoy Escort, from a new Federation campaign
at CAPTAIN, its game speed raised (`]`), red alert, full speed, and the
nearest enemy targeted (the backquote) and fired on every 120 frames.
After 5 minutes 5 of its time: "Convoy Escort: Astounding Victory. You
managed to get the convoy through without a single loss. This is an
outstanding performance." The battle report: 379 damage scored on the
raiders (79 internal), the player's ship 380 (83 internal), no losses on
either side; 300 prestige gained, which the Dynaverse's prestige shows on
DONE (50 to 350). Its outcomes, as the mission's own text has them, go
by the freighters that get through, from "This is a dark day for us
all" to "Astounding Victory". The same run from a fresh save folder
plays the same way.

Before missions were unloaded (`FreeLibrary` did nothing), the same
input ended in a draw ("Some of the freighters have made it to their
destination...", 150 prestige): the mission was played by the copy of
its DLL its listing had left, not a fresh one. A second mission played
that way, Patrol (`Met_17Patrol.scr`), read through a null pointer as it
loaded (+0x1b8cd). Without the game's speed raised, 23 minutes of
Convoy Escort's time went by without it ending; Y, which targets each
ship in turn, the convoy's too, left the player firing at nothing. The
next missions offered after the victory were Convoy Escort and Monster;
against Monster the frigate was destroyed within a minute: "Devastating
Defeat", 0 prestige.

A skirmish is played through DirectPlay, single-player too: starting
one, the game throws a `TLogicError` naming `fDirectPlayObject` each
time it finds no DirectPlay object, and goes on. As it starts it calls
`MoveFileA`, and writes `Auto1.rec` and `WeaponDamage.txt` in its
folder.

The game keeps its list of missions in `assets/scripts/scripts.lst`,
which it writes in that folder, so in the save folder's copy of it. A
search of the folder lists the install's and the save folder's together:
when it listed only the save folder's copy, the game found no missions
but the cache, nearly empty, and its multiplayer scenarios weren't
listed.

### Its multiplayer

Multiplayer, on the main menu, asks COM for DirectPlay itself
(`IDirectPlay4A`) and lists its connections (`EnumConnections`, at
0x6176fb), making a DirectPlay object for each to try it
(`InitializeConnection`); it numbers them its own way, TCP/IP 1, IPX 2,
modem 3, serial 4 (0x61d170). It then asks the modem connection which
modems there are (`GetPlayerAddress` and the lobby's `EnumAddress`, at
0x619890), and opens `COM1` to `COM4` (none there) for the serial one.
With all four connections, the Game Connections screen shows: Dynaverse
II and standard multiplayer through online services (flipside.com,
MPlayer), TCP/IP and IPX on a LAN, and an address book ("Local Area
Network").

HOST makes the TCP/IP connection's address (`CreateCompoundAddress`),
initializes it, and first clears any game it had: cancels what's to be
sent, reads what's waiting, destroys its player (0x2710, one it never
made, which DirectPlay refuses) and closes (0x6184f0). The Create
Multiplayer Game screen lists the eleven `Mul_` scenarios (Base Assault,
Battlefest, Free For All, Hockey...), the map types, the most players
(2 to 6), the BPV limit, the game's speed and the technology era.
ANNOUNCE GAME asks DirectPlay for its capabilities and looks for other
games (`EnumSessions`, asynchronous, at 0x617c88), then opens its own
(`Open` with `DPOPEN_CREATE`, at 0x617fa6) and makes its player
(`CreatePlayer`, with an event, at 0x6187f2), naming it by the machine
(Winsock's `gethostname`, to its first dot) and keeping its address
(`gethostbyname` of that name) in the player's data. Reluva answers
those two Winsock calls itself, with `GetComputerNameA`'s name and the
machine's own address, 127.0.0.1: the rest of Winsock the game imports
(25 functions, sockets among them) it hasn't called.

The Multiplayer Startup screen lists the players, each with its type,
empire, team and BPV, and a chat. A slot set to AI ("Plato") is a second
DirectPlay player of the host's own; the human player sends 24 bytes to
every player over and over, and the AI answers with 36. SELECT chooses a
ship (Choose Vessel), which is sent to the AI (`SendEx`, asynchronous).

START MULTIPLAYER GAME looks at each of the six slots (0x46586f): one in
use whose READY isn't ticked gets the `MultiError` table's eighth line,
"Please press READY button!!" (`strings.txt`). An AI player is readied
by the host, once it has a ship: the icon beside its BPV shows its
fleet (A.I. Fleet), whose SELECT chooses its ship; READY without one
says "Choose a ship first by clicking a Select button on the right."
With both players' ships chosen and both ready, START updates the
session (`SetSessionDesc`), destroys the AI's DirectPlay player, reads
its data from the message saying so (0x61db66), destroys its own and
closes the session (0x6184f0): played alone, with AI players only, the
game goes on without DirectPlay. The mission starts, Base Assault here:
the tactical view, the player's ship (a Federation police ship, the
*Benjamin H. Ford*) and the AI's in space, the mission's clock running.

Joining from another copy of Reluva, over TCP/IP: CONNECT on the Game
Connections screen (with "Local Area Network") opens Select Game, which
asks DirectPlay for sessions on each frame (`EnumSessions`,
asynchronous) and lists them: the game's name, scenario, most players
and speed ("Starfleet2", Base Assault, 5, 7). JOIN opens the session
(`Open` with `DPOPEN_JOIN` and `DPOPEN_RETURNSTATUS`, 0x81, at
0x617fa6) and makes the joining player as the host does, its name and
address from Winsock. Both copies' Multiplayer Startup screens then list
the two human players, each with its ping to the other (27 to 29). Each
chooses a ship and ticks READY, and the host's START starts the mission
in both: both in the tactical view, the joining player on the
defenders' side by a planet. Through the battle they send each other
messages (`SendEx`, 30 to 111 bytes, a few a second) for as long as
both run; when the joining copy stopped, the host was told its player
had gone, and its sends to it after were refused
(`DPERR_INVALIDPLAYER`, which it throws and catches). The game's other
Winsock functions weren't called: it plays through DirectPlay alone.

A game played to its end: Esc in a multiplayer mission shows FORFEIT
MISSION, EXIT TO MAIN MENU, EXIT TO WINDOWS and CANCEL, and doesn't
pause it (both copies' mission clocks ran on). The joining player's
FORFEIT MISSION sends a last message, destroys its player and closes
the session ("Closing Session. Please wait."), and its Mission
Debriefing says "Disconnect. You have disconnected early." In Free For
All ("until one (1) side is left standing") the host is then told
"Mission complete. Player Left: Player", destroys its own player,
closes the session and shows its debriefing: "Congratulations! You are
the winner!", the enemy fleet's losses 40 (the BPV of the ship that
left). In Base Assault the host is told "Defending ship has been
destroyed." and plays on, the starbase still standing; it goes on
sending to the player that left, each send refused and caught.

Between two machines on a network, as far as the network goes: the
two copies each in a Linux network namespace of its own (its own
network stack and address, 10.9.0.1 and 10.9.0.2, its own loopback), the
two joined by a virtual Ethernet link and nothing else. The joining copy
found the host's session by broadcast, the only way it could (its
question to this machine's own address reached only itself), joined
it, saw a ping of 31, and the Free For All game went as between copies
on one machine: started in both, the joining player forfeiting, the
host winning. Two physical machines haven't been tried.

### Its mouse and keys

The game asks where the pointer is with `GetCursorPos` (and
`ScreenToClient`) twice each frame, and takes the buttons and keys from
the window's messages (`PeekMessageA`): keys through `TranslateMessage`,
whose `WM_CHAR` types into the CD key dialog. Up to its title screen it
doesn't create DirectInput, which it imports. Capitals need Shift and `-` its
own key, as on a US keyboard. A wrong key gets "ERROR! We're sorry, but
the CD Key you entered was incorrect. Please try again." The key the
owner's copy came with, typed, is taken: the dialog closes, and the
full title screen shows, the logos of Taldren, 14 East, Interplay and
Paramount under the game's own.

In a mission the keys are those `Assets/specs/bindings.ini` names (A
and S for speed, Y to select an enemy, R red alert, Z to fire, the
keypad for the helm: 5 a high-energy turn, + to turn back, and others),
taken from the window's messages as in the menus; the helm's and the
weapons' panels take clicks. Checked in a skirmish, headless and in
Reluva's window: speed set with S, the Klingon selected with Y, red
alert with R, the ship turned by the helm's turn arrow, phasers and
photons fired with Z and with the FIRE button (the hits on the Klingon
shown with their damage), and keypad 5's high-energy turn charging.
Running a Z press for the first time runs the game's code from 0x49b7f9
through 0x4cf62b *(seen, not traced)*. The keypad's +, -, * and / came
through only once Reluva mapped them to Windows' keys. Pause, which the
game binds to pausing, has no key in Reluva yet.

It draws its own pointer, from `Assets/mouse/cursors.txt` and the `.cur`
files it names, over its frame each time it shows one: it keeps what's
under the pointer, then copies the pointer on with a blit keyed by the
source's colour key (`DDBLT_KEYSRC`, flags 0x1008000 at 0x6aa537). It
never sets a colour key on any surface. Where the blit fails it calls
0x6a5520 with the same arguments (at 0x6aa5ef), which draws the pointer
with its background left out *(inferred)*: with `sfc.ini`'s `[Mouse]
hwblit=0` it always does, and the pointer is drawn right. With
`hwblit=1`, as the install has it, a blit that copied the pointer
unkeyed showed its magenta background; the blit now fails as
DirectDraw's documentation says it does for a source without a colour
key, with `DDERR_NOCOLORKEY` *(published)*, and the game draws it
itself. What the original's display drivers did with such a blit isn't
known.

## In Reluva

- **The engine** (`src/engines/sfc/sfc.cpp`): starting the game in the
  launcher runs the program on a thread of its own (`Player`,
  `src/engines/sfc/host/player.h`), shows each frame it shows in the
  window, scaled to fit, plays its sound through the mixer at the
  launcher's master volume, and passes on the mouse, where it is on the
  game's 800x600 screen, its buttons and wheel, and the keys. The
  window's own pointer is hidden, as the game draws its own. Closing the
  window or Ctrl+F5 asks first, then stops the program.
- **`GameProcess`** (`src/engines/sfc/host/game_process.h`): a
  `Win32Process` with the install as `C:\SFC2\`, read-only; host threads
  (`useHostThreads`); the shared DirectDraw, Direct3D 7 and DirectInput
  stand-ins; and the Windows functions the shared layer lacks, in
  `src/engines/sfc/host/system.cpp`: interlocked operations, global
  memory, locale and clock queries, INI files (`GetPrivateProfileString`,
  reading `sfc.ini`'s comments after a value as comments), disk space,
  the window's menus, rectangles, and GDI's text calls as no-ops. What
  the program writes in its folder goes to a save folder
  (`setSaveFolder`): its files are read there first, its folders in the
  install first (a search lists both); the install isn't written to.
  Winsock's `gethostname` and `gethostbyname`, for the name and address
  of the machine.
  `describeException` names an exception raised: for a C++ throw, the
  types thrown and the text in the object (a `tErrInfo`'s message).
- **What it needed of the shared layer** (`src/emulation/x86/`):
  semaphores (`CreateSemaphoreA`, `ReleaseSemaphore`, and waiting on
  them), Miles' sample functions
  ([miles.md](../../../../../common/x86/miles.md)), DirectPlay's lobby
  ([dplay.md](../../../../../common/x86/dplay.md)), a `GetFileType` for
  files opened through the path mapping, a window's longs and subclassing (`GetWindowLongA`, `SetWindowLongA`,
  `CallWindowProcA`), `TranslateMessage` typing capitals and
  punctuation with Shift as a US keyboard does, a keyed blit failing
  from a source with no colour key, Direct3D 6
  ([directx.md](../../../../../common/x86/directx.md)), structured
  exceptions dispatched, KERNEL32's UTF-16 file functions, a search for
  an exact name, `.` resolved in full paths, and volumes by their roots
  alone ([windows.md](../../../../../common/x86/windows.md)).
- **Commands:** `reluva-tool sfc host <game folder>` runs the program
  headless and reports what it looks for, the imports nothing stands in
  for, where it stopped, and how many instructions it ran; `-calls
  <address>` lists every call into a stand-in from that address on, with
  the interface each `QueryInterface` asks for and each call's first
  arguments; `-watch <address>` shows the registers, the stack and what
  its first arguments point to each time the program gets there;
  `-frame <out.png>` saves the last frame; `-save <folder>` is where its
  writes go (by default `reluva-sfc-host` in the system's temporary
  folder). Each exception raised is listed as it is. Input is given at
  the frames the program shows, a step a frame: `-type <frame> <text>`
  types (Shift held for capitals, `\n` for Enter), `-click <frame> <x>
  <y>` clicks with the left button, `-shot <frame> <out.png>` saves that
  frame; `-sound <folder>` writes what each Miles sample played, one WAV
  a sample; `-writes <address>` shows each change to a word of memory
  and the instruction that made it; `-newcode <from> <to>` lists the
  program's code first run between two frames. `-watch` also shows what
  EAX points to, and text where a pointer has some; `-calls` shows the
  names `LoadLibraryA` and `GetProcAddress` are given, and the path each
  file or folder function is.
  `reluva-tool sfc disasm <game folder or program> <start> <end>`
  disassembles the program, or another (a mission's DLL). `-key <frame>
  <name>` presses a key by name (`num5`, `numplus`, `left`, `f1`...);
  `-fixed-clock` runs the program's clock by the instructions run (40,000
  a millisecond), its sleeps, and a millisecond each time every thread
  waits (without that, a tutorial waiting on the clock stood still for
  good), and the date and time of day it's told (`GetLocalTime` and the
  like) from 1 January 2001 moving on with that clock (the game seeds its
  random numbers from them: a campaign's ship names and missions came out
  different each run), so a run with the same input repeats
  frame for frame and input lands on the same screens.
- **Multiplayer:** DirectPlay's TCP/IP between copies of Reluva
  ([dplay.md](../../../../../common/x86/dplay.md#the-network)); the
  engine gives it the system's sockets, the host tool `-net`.
- **Not yet:** a multiplayer game between two physical machines (only
  copies in network namespaces of their own, as two machines on a
  network; [Its multiplayer](#its-multiplayer)), or ended in battle
  rather than by a player leaving. The opening movie isn't
  hosted: the engine plays it itself before the game
  ([formats/intro.md](../formats/intro.md)).

## Sources and checks

- **Where the facts come from:** the hosted run of the owner's English
  1.0 `StarFleet2.exe`: the stand-ins called, the files looked for, and
  the exceptions' types and texts as the program builds them; the
  program's code at the addresses given, disassembled; the exception
  layout of Visual C++ (a type's name at its type descriptor's +8), as
  published write-ups on reversing Visual C++ programs describe it.
- **How it was checked:** each run repeated after each change, the
  instruction count steady (44,063,247 three times running to the 3D
  test; a few hundred more when threads change over differently); the
  first frame saved and looked at. Input checked headless (`-click` on
  the dialog's text box, `-type`, `-click` on ENTER, the frames saved)
  and in the launcher's window under a virtual X display, with the
  mouse and keys sent by `xdotool`: the text typed appears in the box,
  capitals and `-` included, ENTER gets the game's answer, the pointer
  follows the mouse without its background, and Ctrl+F5 and its
  question stop the program.

### The numbers

| What | Measured |
|---|---|
| Instructions to the 3D test, without Direct3D 6 | 44.06 million |
| Instructions to the first frame | 45.08 million |
| Instructions to the first exception caught in normal running | 48.12 million |
| Instructions to the title screen and its CD key dialog | between 600 million and 1 billion (11 to 18 seconds) |
| Speed | 52 to 62 million instructions a second (the shared JIT on, 4 cores), drawing included |
| Imports | 111 of the program's not stood in for at the 3D test: 37 KERNEL32 (wide-character files, consoles, processes, file mappings), 10 USER32 (dialogs, clipboard, `GetMessageA`), 3 GDI32 (DIB sections), 25 WSOCK32 and 1 DSOUND (by ordinal), 25 Miles (`mss32.dll`), 5 COMDLG32, 4 ADVAPI32, `DirectDrawCreateClipper` |

How fast the game needs to run once it's in a mission isn't measured
yet: nothing past its title screen has run. Hosted, loading takes 11
to 18 seconds; how long the original takes on the hardware of its time
isn't measured.

## Open questions

- Whether the game also asks for other files no install has, as it does
  `uibitmaps.zip` and `uq3.txt`.
- Why the title screen says "v2.0.0.2" when the program's version
  resource says 1.0.0.2.
- How the 16 bytes kept for the CD key are made from it.
- What the original's display drivers did with a blit keyed by a source
  with no colour key, as the game's pointer is drawn with `hwblit=1`.
- What the game uses DirectInput for, past its title screen.
- What the folders `dmp3dDLL_Release`, `DLL` and `DRIVERS` are looked for,
  and what it loads from them.
- How many instructions a second a mission needs.
- What the game's own Winsock functions are for (sockets, `connect`,
  `select`...): a game played over TCP/IP didn't call them; perhaps the
  online services' (Dynaverse II, MPlayer).
