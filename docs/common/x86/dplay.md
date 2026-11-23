# DirectPlay

## What it is

`src/emulation/x86/dplay.cpp`: Microsoft's DirectPlay, the networking
part of DirectX, for programs run in the [x86 emulator](README.md): its
lobby, which programs ask whether a lobby program (a matchmaking
service's client) started them, and DirectPlay itself
(`IDirectPlay4A`): its connections, and a session with its players and
their messages. So far a session is one program's own: it hosts it, and
its players (a person's, and the computer's) talk to each other.

## What Reluva supports

- `CoCreateInstance` of DirectPlay's lobby class
  (`{2fe8f810-b2a5-11d0-a787-0000f803abfc}`) as `IDirectPlayLobby3A`
  (`{2db72491-652c-11d1-a7a8-0000f803abfc}`), and of DirectPlay's class
  (`{d1eb6d20-8923-11d0-9d97-00a0c90a43cb}`) as `IDirectPlay4A`
  (`{0ab1c531-4745-11d1-a7a1-0000f803abfc}`). Other classes go on to
  whatever stood in for `CoCreateInstance` before.
- **The lobby:** `GetConnectionSettings` fails with `DPERR_NOTLOBBIED`
  (0x8877042E), leaving the size it's given as it was: Reluva starts
  programs itself, never from a lobby. `CreateCompoundAddress` and
  `EnumAddress` make and read DirectPlay addresses: chunks of a type (a
  GUID), the data's size and the data, the first the address's total
  size (`DPAID_TotalSize`).
- **Connections:** `EnumConnections` lists the four providers Windows
  has, IPX, TCP/IP, modem and serial, each by its GUID and Windows' name
  for it ("Internet TCP/IP Connection For DirectPlay"), its connection an
  address naming it (`DPAID_ServiceProvider`). Programs expect all four:
  Starfleet Command II looks up the modem's to list modems, and read
  through a null pointer without it. `InitializeConnection` takes any
  address naming a provider, the program's own additions too (a host to
  reach). `GetPlayerAddress` gives the connection's address alone: a
  modem connection has no modems. None of them reaches another machine
  yet.
- **A session:** `Open` with `DPOPEN_CREATE` (2) makes a session the
  object hosts, from the `DPSESSIONDESC2` it's given, with a new instance
  GUID; joining one (`DPOPEN_JOIN`) finds none (`DPERR_NOSESSIONS`).
  `EnumSessions` finds none: the callback is called once, told the time
  is up (`DPESC_TIMEDOUT`, 1), and not again if it asks to go on.
  `GetSessionDesc` (the structure, its name and password after it) and
  `SetSessionDesc` (the host's alone); `Close`. `GetCaps` gives Reluva's
  own numbers: messages up to 64 KB, sent whole and in order.
- **Players:** `CreatePlayer` (its name, the data every player sees,
  flags, and an event set when a message comes for it), `DestroyPlayer`,
  `EnumPlayers` (each as a player, `DPPLAYERTYPE_PLAYER`, 1),
  `GetPlayerName`, `GetPlayerData` and `SetPlayerData` (the player's own
  local data with `DPGET_LOCAL` or `DPSET_LOCAL`, 1, else what every
  player sees), and `GetPlayerFlags` (`DPPLAYER_LOCAL`, 8, for the
  object's own, and its spectator and server-player flags). IDs start at
  0x100001, clear of DirectPlay's own (0 for itself, 1 for a server
  player).
- **Messages:** `Send` and `SendEx` to a player, or to every one but the
  sender (`DPID_ALLPLAYERS`, 0); `SendEx` sent asynchronously
  (`DPSEND_ASYNC`, 0x200) answers that it's under way (`DPERR_PENDING`,
  which is `E_PENDING`). `Receive` gives the first message waiting, or
  the first to or from a player (`DPRECEIVE_TOPLAYER` 2,
  `DPRECEIVE_FROMPLAYER` 4), and leaves it with `DPRECEIVE_PEEK` (8); a
  buffer too small is told the size (`DPERR_BUFFERTOOSMALL`).
  `GetMessageCount` and `GetMessageQueue` count what waits; nothing waits
  to be sent, so `CancelMessage` has nothing to cancel.
- **System messages** (from player 0), with their pointers made
  addresses in the buffer `Receive` fills: a player created
  (`DPSYS_CREATEPLAYERORGROUP`, 3) or destroyed
  (`DPSYS_DESTROYPLAYERORGROUP`, 5), each with its name and data, and the
  data every player sees changed (`DPSYS_SETPLAYERORGROUPDATA`, 0x102),
  each to the session's other local players.
- Other methods are listed, with their arguments, and not answered: a
  program calling one stops, naming it.

Not there yet: another machine. The session's players are all one
program's; joining a session hosted by another copy of Reluva, over
TCP/IP, is next. Also not there: groups, the messages for a session
lost, a new host, names or the session's description changed, chat, and
the message saying an asynchronous send is done (`SendEx` without
`DPSEND_NOSENDCOMPLETE` stops); launching or being launched by a lobby.

## Sources and checks

- **Where the facts come from:** the class and interface IDs, the
  methods' order and arguments, and the structures (`DPSESSIONDESC2`,
  `DPNAME`, `DPCAPS`, `DPMSG_CREATEPLAYERORGROUP` and the other system
  messages, the address chunks) from Microsoft's published DirectX
  documentation of DirectPlay *(published)*; how a program uses them from
  Starfleet Command II's calls (`reluva-tool sfc host -calls`) and its
  code. From its code *(from the code)*: given no buffer, it takes
  `DPERR_BUFFERTOOSMALL` (0x8877001E) to mean there's something to
  fetch; the system message types it tells apart (3, 5, 0x31, 0x101,
  0x102, 0x104), and a player's type, 1; that its enumeration callback
  stops when told the time is up (1); the spectator flag it reads (0x200);
  and the results it takes from `SendEx`, `E_PENDING` among them. The
  flags it passes (`DPOPEN_CREATE` with `DPOPEN_RETURNSTATUS`, 0x82;
  `SendEx`'s 0x601, guaranteed, asynchronous, no message when sent) are
  read as the documentation's names *(inferred)*. The other error codes
  and flag values (`DPERR_NOMESSAGES` 0x887700BE, `DPERR_INVALIDPLAYER`
  0x88770096, `DPPLAYER_LOCAL` 8, `DPRECEIVE_*`) aren't seen in the
  program's use *(guessed)*.
- **How it was checked:** Starfleet Command II's Multiplayer: its Game
  Connections screen, HOST, its Create Multiplayer Game screen with the
  eleven multiplayer scenarios, ANNOUNCE GAME opening the session, its
  Multiplayer Startup screen listing the player, a computer player
  ("Plato") added as a second local player, the two exchanging messages
  over and over (24 bytes to every player, 36 back), ships chosen, and
  START: the AI's player destroyed, its data read from the message saying
  so, the game's own destroyed and the session closed
  ([hosting.md](../../engines/sfc/games/sfc2/systems/hosting.md#its-multiplayer)).
- `testDirectPlayLobby` (`tests/common/windows.cpp`) makes the lobby
  through `CoCreateInstance`, checks another class still goes on, and
  that `GetConnectionSettings` answers `DPERR_NOTLOBBIED` with the size
  left alone. `testDirectPlaySession` initializes the TCP/IP connection,
  opens a session, creates two players (the first told of the second, its
  name read through the message's pointer), sends to every player and
  asynchronously, sets and reads the data every player sees (the other
  told), and destroys a player (the other told; a second time refused).
- **Confidence:** published unless marked.

## Used by

- **`sfc`**: Starfleet Command II asks the lobby as it reaches its main
  menu, and on each frame after; its Multiplayer screens use DirectPlay
  itself
  ([hosting.md](../../engines/sfc/games/sfc2/systems/hosting.md#its-multiplayer)).
