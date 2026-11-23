# DirectPlay

## What it is

`src/emulation/x86/dplay.{h,cpp}`: `x86::DirectPlay`, DirectX 6's
DirectPlay for programs run in the [x86 emulator](README.md): what
`DPLAYX.dll` exports, and its two classes through `CoCreateInstance`.
Written from the DirectX SDK's documentation of DirectPlay and its lobby;
the interfaces' methods are in the order `dplay.h` and `dplobby.h`
declare them. Its sessions between computers are
`src/emulation/x86/dplay_session.{h,cpp}`'s, over UDP, with a protocol
of Reluva's own: copies of Reluva play each other, not the original
DirectPlay.

## What Reluva supports

Without a network, DirectPlay answers as on a machine where no service
provider can connect. Given one (`DirectPlay::network`, an
`x86::DirectPlay::Network`), the TCP/IP and IPX service providers
connect, both over the same UDP, and the DirectPlay objects on it find
each other's sessions, players, groups and messages. The network says:

- how to open a UDP socket: the system's, or `Network::inMemory()`'s
  (`net::LoopbackNetwork`), for programs in one Reluva process;
- where a search goes when the program gives no address: each network's
  broadcast address (and the machine's own, `127.0.0.1`, for copies on
  it);
- where hosts listen when the program doesn't say: port 47624, the one
  DirectPlay's own TCP/IP provider finds sessions on;
- a meeting server, if any, to list hosted sessions and introduce
  players across routers ([meeting-server.md](../meeting-server.md));
- how to ask the router to pass a host's port on (`net::PortMapper`), if
  wanted;
- how long a silent peer is waited for: 10 seconds.

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
  `InitializeConnection`, with the host's address, `DPAID_INet`, and
  port, `DPAID_INetPort`, if it gives them):
  - `GetPlayerAddress` of the local machine: the service provider and,
    for TCP/IP, `127.0.0.1`;
  - sessions: `EnumSessions` asks for sessions (at the address given,
    else by broadcast, and at the meeting server), waits for answers (the
    timeout given, else 300 ms, at most 5 seconds), then calls back with
    each session of the application asked for (any, for `GUID_NULL`;
    with `DPENUMSESSIONS_AVAILABLE` only those that can be joined) that
    has answered in the last 3 seconds, then with the timeout, asking
    again while the callback says to; `Open` creates a session
    (`DPOPEN_CREATE`, its new instance GUID written back into the
    description), listening on the port given, or joins one found
    (`DPERR_NOCONNECTION` if it's not found or the host turns it down:
    full, closed to new players, or the wrong password); a join under way
    says `DPERR_CONNECTING` with `DPOPEN_RETURNSTATUS`, and is waited
    for, up to 10 seconds, without; `Close` leaves it; `GetCaps` says
    whether this object hosts it (`DPCAPS_ISHOST`);
  - players and groups: `CreatePlayer`, `CreateGroup`, `DestroyPlayer`,
    `DestroyGroup`, `AddPlayerToGroup`, `DeletePlayerFromGroup`,
    `EnumPlayers`, `EnumGroups`, `EnumGroupPlayers`, with their names and
    data;
  - messages: `Send` and `SendEx` to a player, a group's players or
    everyone (`DPID_ALLPLAYERS`), never back to the sender, guaranteed
    (`DPSEND_GUARANTEED`: they arrive, in order) or not (sent once);
    `Receive`,
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

Each DirectPlay object on a network has an endpoint
(`dplay::Endpoint`): its UDP socket, its session's copy of the players
and groups, and its queue of messages waiting. A thread of the
endpoint's own sends, sends again and receives, every 5 ms, so a
program that's busy elsewhere still answers. A system message is laid
out for the program when it's received, its pointers (to names and data)
made its own.

The host keeps the session: a joiner asks it to join, and sends it what
its players do (players and groups made and gone, players added to
groups, messages), which the host passes on to the others; the host's
own go to everyone straight away. So joiners need only reach the host,
as across routers through a meeting server. Each changes its own copy at
once, and ids are its node's number (the host's is 1) above a count, so
a new player's id needs no answer. Every copy sees the session's
changes in the host's order.

When an object leaves its session, by `Close` or being released, its
players and groups are destroyed and the others told. When the host
leaves or goes silent, the joiners are told the others' players are
gone, then that the session is lost; they stay in it, alone, until they
leave (no new host takes over). A joiner gone silent is taken as left.

### The protocol

Every datagram starts with `RLDP`, the protocol's version (1) and its
kind; numbers are little-endian, strings and bytes led by their length
(32 bits).

| Kind | Holds | Means |
|---|---|---|
| 1 | an application GUID | Which sessions are there? A host of that application (or of any, for `GUID_NULL`) answers |
| 2 | a session: GUID, application, flags, most players, players, name, whether it has a password, the four user values | A host's answer |
| 3 | a link, a sequence number, whether it ends a message, a piece of one (up to 1,024 bytes) | A guaranteed message, in pieces |
| 4 | a link, a sequence number | Every piece up to that one has come |
| 5 | a link, a message | A message not guaranteed, whole |
| 6 | a link | Still there (after a second with nothing sent) |
| 7 | a link | Gone (sent three times) |

A link is a joiner's connection to its host, numbered at random by the
joiner. Pieces not acknowledged within 200 ms are sent again; pieces
come in out of order are kept until the ones before them arrive.

The messages on a link start with their type:

| Type | Holds | From |
|---|---|---|
| 1 | session GUID, password | a joiner asking to join |
| 2 | id, player or group (names, data, flags, a group's players, its node) | either: made |
| 3 | id | either: destroyed |
| 4, 5 | group, player | either: added to the group, taken out |
| 6 | from, to, guaranteed, data | a joiner, for the host to pass on |
| 7 | | a joiner leaving |
| 10 | the joiner's node, the session, its players and groups | the host: welcome |
| 11 | | the host: turned down |
| 12 | from, to, guaranteed, data | the host, passing a message on |
| 13 | | the host, leaving: the session's lost |

Through a meeting server, a host lists its session as `dplay` and its
application's GUID, its description (as in an answer) as the listing's
own, and lists it again every 2 seconds with its players then. A search
asks the server for the list too; a session found only there is
introduced (`MeetingSocket::connect`) when it's joined.

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
  objects on one network in memory, each in its own process: one hosts a
  session and the other finds it and joins; the host is told of the
  other's player; a message, a group and its member, and an asynchronous
  send and its completion; then the host leaves, and the other is told
  its player and group are gone and the session lost.
  `tests/common/dplay_session.cpp` (`common.unit.dplay_session`) runs
  endpoints on a clock of its own: sessions found by broadcast, at an
  address and only through a meeting server across routers that pass
  nothing; joining turned down (full, the wrong password); a hundred
  messages in order and one of 40,000 bytes whole between two joiners
  with a third of the datagrams lost; messages to everyone and to a
  group; a joiner leaving, and the host and a joiner going silent.
  `reluva-tool legoloco pair` runs two copies of LEGO Loco on one network
  in memory or over the system's UDP, and `run -udp` one each in two
  processes, one hosting and one joining
  ([running.md](../../engines/legoloco/systems/running.md#network-play)).
- **Confidence:** published unless marked. How the sessions travel
  between computers is Reluva's own, as is what DirectPlay leaves to its
  service provider: how long a search waits, no host taking over when
  the host leaves, ids and instance GUIDs numbered as it likes.

## Used by

- **`legoloco`**: LEGO Loco makes the lobby and DirectPlay at its start,
  for its network games, and plays them through it
  ([running.md](../../engines/legoloco/systems/running.md#network-play)).
