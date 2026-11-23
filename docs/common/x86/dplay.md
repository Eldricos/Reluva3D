# DirectPlay

## What it is

`src/emulation/x86/dplay.{h,cpp}`: `x86::DirectPlay`, DirectX 6's
DirectPlay for programs run in the [x86 emulator](README.md): what
`DPLAYX.dll` exports, and its two classes through `CoCreateInstance`.
Written from the DirectX SDK's documentation of DirectPlay and its lobby;
the interfaces' methods are in the order `dplay.h` and `dplobby.h`
declare them.

## What Reluva supports

Without a network, DirectPlay answers as on a machine where no service
provider can connect. Given one (`DirectPlay::network`, an
`x86::DirectPlay::Network` shared by the programs that play together),
the TCP/IP and IPX service providers connect, and the DirectPlay objects
on it find each other's sessions, players, groups and messages. The
network is in memory, for programs in one Reluva process; it doesn't yet
reach another computer.

- **The exports**, by name or by ordinal as programs import them: 1
  `DirectPlayCreate` (an `IDirectPlay`, whatever service provider is
  asked for), 2 and 3 `DirectPlayEnumerateA` and `W` (no service
  providers), 4 and 5 `DirectPlayLobbyCreateA` and `W`.
- **The classes**: `CLSID_DirectPlay` and `CLSID_DirectPlayLobby`, through
  `CoCreateInstance`; other classes go to the `createComObject` set
  before.
- **`IDirectPlay`, `IDirectPlay4` and `IDirectPlay4A`** on one object.
  Without a network: `EnumConnections` finds none; `EnumSessions` times
  out at once, its callback told so (`DPESC_TIMEDOUT`);
  `InitializeConnection`, `Open`, `SecureOpen`, `GetPlayerAddress` (its
  size set to 0), `GetCaps` and the message counts are unavailable
  (`DPERR_UNAVAILABLE`); `Close` and `Initialize` succeed.
- **On a network**, through TCP/IP or IPX (the service provider
  `DirectPlayCreate` was asked for, or the one in the address given to
  `InitializeConnection`):
  - `GetPlayerAddress` of the local machine: the service provider and,
    for TCP/IP, `127.0.0.1`;
  - sessions: `EnumSessions` calls back with each open session of the
    application asked for (any, for `GUID_NULL`; with
    `DPENUMSESSIONS_AVAILABLE` only those that can be joined), then with
    the timeout, looking again every 100 ms while the callback asks;
    `Open` creates a session (`DPOPEN_CREATE`, its new instance GUID
    written back into the description) or joins one by its GUID
    (`DPERR_NOCONNECTION` if it's gone); `Close` leaves it; `GetCaps`
    says whether this object hosts it (`DPCAPS_ISHOST`);
  - players and groups: `CreatePlayer`, `CreateGroup`, `DestroyPlayer`,
    `DestroyGroup`, `AddPlayerToGroup`, `DeletePlayerFromGroup`,
    `EnumPlayers`, `EnumGroups`, `EnumGroupPlayers`, with their names and
    data;
  - messages: `Send` and `SendEx` to a player, a group's players or
    everyone (`DPID_ALLPLAYERS`), never back to the sender; `Receive`,
    from anyone or to or from a player, or a peek; too small a buffer
    gets `DPERR_BUFFERTOOSMALL` and the size needed; `GetMessageCount`
    and `GetMessageQueue`;
  - the system messages, from `DPID_SYSMSG`: a player or group made
    (`DPSYS_CREATEPLAYERORGROUP`) or gone (`DPSYS_DESTROYPLAYERORGROUP`),
    a player added to or taken from a group, the session lost when its
    host leaves (`DPSYS_SESSIONLOST`), and an asynchronous `SendEx` done
    (`DPSYS_SENDCOMPLETE`).
- **The lobby**, `IDirectPlayLobby` to `IDirectPlayLobby3A` on one object:
  the program wasn't started by one (`DPERR_NOTLOBBIED` from
  `GetConnectionSettings`, `Connect`, `ConnectEx` and the lobby
  messages); no programs are registered, no address types known.
  Addresses are made and read as documented: `CreateAddress` (the total
  size, the service provider, then the element given),
  `CreateCompoundAddress`, `EnumAddress`; too small a buffer gets
  `DPERR_BUFFERTOOSMALL` and the size needed.
- A method not listed ends the run, naming it.

## How it works

A DirectPlay address is a list of elements, each a `DPADDRESS` (the
element's type, a GUID, and its data's size) followed by its data. Those
made here start with `DPAID_TotalSize`, holding the whole address's size.

The network holds the sessions open, by their instance GUIDs: each one's
description, its host, the DirectPlay objects in it and its players and
groups, which have ids unique on the network. Each object has its own
queue of messages waiting; a message sent is copied into its receivers'
queues at once, so guaranteed messages arrive in order and none are
lost. A system message is kept with its pointers (to names and data) as
offsets into itself, and made the receiver's own when it's received.
One lock guards the network, as the programs' threads call it at once;
it's let go while a program's callback runs.

When an object leaves its session, by `Close` or being released, its
players and groups are destroyed and the others told; if it was the host
the session is lost to them, though they stay in it until they leave
(there's no new host); the last to leave ends it.

## Sources and checks

- **Where the facts come from:** the DirectX 6 SDK's documentation of
  DirectPlay, the lobby and DirectPlay addresses; `MAKE_DPHRESULT`'s
  codes. The interfaces' IIDs, the classes' and the address types' are
  those in LEGO Loco's `loco.exe`, which holds them; `IDirectPlay4`'s is
  its ANSI twin's less one *(inferred)*, as such pairs are numbered.
- **How it was checked:** `tests/common/dplay.cpp`
  (`common.unit.dplay`) makes the lobby and DirectPlay, asks for their
  later interfaces, builds an address and reads it back through a
  callback, and checks the answers without a network. Two DirectPlay
  objects on one network, each in its own process: one hosts a session
  and the other finds it and joins; the host is told of the other's
  player; a message, a group and its member, and an asynchronous send
  and its completion; then the host leaves, and the other is told its
  player and group are gone and the session lost.
  `reluva-tool legoloco pair` runs two copies of LEGO Loco on one network,
  one hosting and one joining
  ([running.md](../../engines/legoloco/systems/running.md#network-play)).
- **Confidence:** published unless marked. The network's behaviour
  beyond the methods' documented answers is Reluva's own: messages
  delivered at once and in order, no host taking over when the host
  leaves, ids and instance GUIDs numbered as it likes.

## Used by

- **`legoloco`**: LEGO Loco makes the lobby and DirectPlay at its start,
  for its network games, and plays them through it
  ([running.md](../../engines/legoloco/systems/running.md#network-play)).
