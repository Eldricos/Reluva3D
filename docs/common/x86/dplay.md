# DirectPlay

## What it is

`src/emulation/x86/dplay.cpp`: Microsoft's DirectPlay, the networking
part of DirectX, for programs run in the [x86 emulator](README.md). So
far only its lobby, which programs ask for when they start, to learn
whether a lobby program (a matchmaking service's client) started them.

## What Reluva supports

- `CoCreateInstance` of DirectPlay's lobby class
  (`{2fe8f810-b2a5-11d0-a787-0000f803abfc}`) as `IDirectPlayLobby3A`
  (`{2db72491-652c-11d1-a7a8-0000f803abfc}`). Other classes go on to
  whatever stood in for `CoCreateInstance` before.
- `GetConnectionSettings` fails with `DPERR_NOTLOBBIED` (0x8877042E),
  leaving the size it's given as it was: Reluva starts programs itself,
  never from a lobby.
- The interface's other methods are listed, with their arguments, and
  not answered: a program calling one stops, naming it.

Not there yet: DirectPlay itself (`IDirectPlay4`), its service providers
(TCP/IP, IPX, modem, serial), and launching or being launched by a lobby.

## Sources and checks

- **Where the facts come from:** the class and interface IDs, the
  methods' order and arguments, and the error's value from Microsoft's
  published DirectX 6 documentation of DirectPlay; how a program uses
  them from Starfleet Command II's calls (`reluva-tool sfc host -calls`)
  and its code. Its code checks for DirectPlay's own errors: given no
  buffer, it takes 0x8877001E (`DPERR_BUFFERTOOSMALL`) to mean there are
  settings to fetch, and anything else to mean there are none
  *(from the code)*.
- `testDirectPlayLobby` (`tests/common/windows.cpp`) makes the lobby
  through `CoCreateInstance`, checks another class still goes on, and
  that `GetConnectionSettings` answers `DPERR_NOTLOBBIED` with the size
  left alone.
- **Confidence:** published unless marked.

## Used by

- **`sfc`**: Starfleet Command II asks the lobby as it reaches its main
  menu, and on each frame after
  ([hosting.md](../../engines/sfc/games/sfc2/systems/hosting.md#its-main-menu)).
