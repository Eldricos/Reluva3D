# DirectPlay

## What it is

`src/emulation/x86/dplay.{h,cpp}`: `x86::DirectPlay`, DirectX 6's
DirectPlay for programs run in the [x86 emulator](README.md): what
`DPLAYX.dll` exports, and its two classes through `CoCreateInstance`.
Written from the DirectX SDK's documentation of DirectPlay and its lobby;
the interfaces' methods are in the order `dplay.h` and `dplobby.h`
declare them. Its sessions between computers are
`src/emulation/x86/dplay_session.{h,cpp}`'s, over UDP, with a protocol
of Reluva's own: copies of Reluva play each other. With the system's
TCP given, they also speak DirectPlay 4's own protocol, as Microsoft
publishes it, to find, join and host the original program's games
([below](#directplay-4s-own-protocol-the-originals-games)).

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
    host leaves (`DPSYS_SESSIONLOST`), this object made the session's
    host when the old one leaves (`DPSYS_HOST`), and an asynchronous
    `SendEx` done (`DPSYS_SENDCOMPLETE`).
- **The lobby**, `IDirectPlayLobby` to `IDirectPlayLobby3A` on one object:
  the program wasn't started by one (`DPERR_NOTLOBBIED` from
  `GetConnectionSettings`, `Connect`, `ConnectEx` and the lobby
  messages); no programs are registered, no address types known.
  Addresses are made and read as documented: `CreateAddress` (the total
  size, the service provider, then the element given),
  `CreateCompoundAddress`, `EnumAddress`; too small a buffer gets
  `DPERR_BUFFERTOOSMALL` and the size needed.
- **A summary** for the engine's reports (`DirectPlay::summary`):
  whether the program hosted or joined, the most players its session
  had at once, and the program's messages sent and received, with the
  longest of each.
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
players and groups are destroyed and the others told. A joiner gone
silent is taken as left. When the host leaves or goes silent, the
joiners are told its players are gone, then:

- **in a session made with `DPSESSION_MIGRATEHOST`**, the endpoint first
  into the session of those left (the lowest node) takes over, and is
  told so (`DPSYS_HOST`); DirectPlay doesn't say which it picks. The
  host tells every endpoint the others' addresses as it sees them, so
  the rest go straight to the new host's and are taken back as they
  were, players, ids and all. Where the address is one a meeting server
  made, which only the old host could use, they find the new host's
  listing at the server instead (it lists the session again, saying
  which node hosts it) and are introduced. The new host answers
  searches at the port the old one listened on, if it can have it, as
  well as its own; one it waits for that doesn't come back within the
  timeout is taken as left. If the new host can't be reached either,
  the next is tried. Messages sent while the hosts change over may be
  lost: those on their way through the old host, and a joiner's before
  it has found the new one;
- **otherwise** they're told the others' players are gone and the
  session is lost; they stay in it, alone, until they leave.

### The protocol

Every datagram starts with `RLDP`, the protocol's version (1) and its
kind; numbers are little-endian, strings and bytes led by their length
(32 bits). Version 2 added the new host's messages and the port in an
answer.

| Kind | Holds | Means |
|---|---|---|
| 1 | an application GUID | Which sessions are there? A host of that application (or of any, for `GUID_NULL`) answers |
| 2 | the port to join at; a session: GUID, application, flags, most players, players, name, whether it has a password, the four user values | A host's answer |
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
| 8 | session GUID, its node | an endpoint of the session, back to the host that took over |
| 10 | the joiner's node, the host's node, the session, its players and groups, the other nodes' addresses | the host: welcome |
| 11 | | the host: turned down |
| 12 | from, to, guaranteed, data | the host, passing a message on |
| 13 | | the host, leaving: the session's lost, or another takes over |
| 14 | node, IP address, port (16 bits; 0 if it's gone) | the host: where another endpoint is |

Through a meeting server, a host lists its session as `dplay` and its
application's GUID, its description (as in an answer) as the listing's
own, followed by its node, and lists it again every 2 seconds with its
players then. A search
asks the server for the list too; a session found only there is
introduced (`MeetingSocket::connect`) when it's joined.

### DirectPlay 4's own protocol: the original's games

With the system's TCP given (`Transport::tcp`), an endpoint also speaks
DirectPlay 4's own protocol, as Microsoft publishes it ([MC-DPL4CS],
[MC-DPL4R]; `src/emulation/x86/dplay4_wire.cpp`,
`dplay4_reliable.cpp`), so it and the original program on Windows can
find, join and host each other's games. A session holds machines of
one protocol or the other, never both.

- **A search** also sends `DPSP_MSG_ENUMSESSIONS` (command 2, asking
  for sessions with room, and those needing a password) by UDP to port
  47624 at each broadcast address, or the address given. Its header
  gives a TCP port of ours in the range 2300 to 2400, where it listens;
  each `DPSP_MSG_ENUMSESSIONSREPLY` (command 1) that comes there is a
  session found, marked the original's, its host reached at the TCP
  port the answer's header gives. A Reluva host answers both ways, and
  its own answer is the one kept.
- **A host** listens on UDP 47624 too, if that isn't its own port
  already, and on TCP and UDP ports of its own in the range 2300 to
  2400. It answers a `DPSP_MSG_ENUMSESSIONS` of its application (or of
  any) with its session, by TCP to the port in the search's header at
  the address it came from, its own TCP port in the answer's header: not
  when it's full and only sessions with room are asked for, nor when it
  has a password the search hasn't, but for searches that want those
  too.
- **Joining** ([MC-DPL4CS] 3.1.4.2, 3.2.5.4 to 3.2.5.6, 4.3): the
  joiner asks the host for a system player (`DPSP_MSG_REQUESTPLAYERID`,
  its flags the system player's), and the host gives it an id
  (`REQUESTPLAYERREPLY`), or `DPERR_NONEWPLAYERS` when the session is
  full, closed to joiners, or has Reluva's own joiners in it. The joiner
  then sends its system player, its TCP and UDP ports (its address
  0.0.0.0, for the one it's heard from) and the password
  (`ADDFORWARDREQUEST`); the host checks the password (turning it down
  with `ADDFORWARDREPLY`), tells every other machine of it
  (`ADDFORWARD`, each answering `ADDFORWARDACK`), and once all have, or
  15 seconds on, sends it the session, its system players with their
  addresses and every player (`SUPERENUMPLAYERSREPLY`). These go by TCP,
  each machine sending on a connection of its own to the other's port.
- **Ids:** the host makes each as the specification says (3.2.5.4): an
  index no other id has in the low 16 bits, a count above, the whole
  masked with the session's `Reserved1`. A Reluva host's own players
  keep its own ids, their indexes counted among those taken.
- **Players:** a joiner asks the host for each player's id
  (`REQUESTPLAYERID`, waiting up to 5 seconds), then sends the player to
  every machine (`CREATEPLAYER`); destroying one, and leaving, send
  `DELETEPLAYER` (a machine leaving deletes its system player too, which
  the others take as its going). Groups aren't carried: a session of
  this protocol refuses to make one.
- **Messages** go straight from the player's machine to each player's.
  In a session made with `DPSESSION_DIRECTPLAYPROTOCOL` (`0x2000`, as
  LEGO Loco's `0xa040` is), everything between joined machines goes by
  the reliable protocol over UDP: DirectPlay's own messages without the
  header's size and address, starting at `play`, between the machines'
  system players' indexes, and players' data as it is, between the
  players' indexes, wrapped (`DPSP_MSG_PLAYERWRAPPER`) when it starts as
  DirectPlay's own messages do. Otherwise a guaranteed message goes by
  TCP and the rest by UDP, each a `DPSP_MSG_PLAYERMESSAGE`: the header,
  the players' ids unless the session has `DPSESSION_NOMESSAGEID`, the
  data.
- **The reliable protocol** ([MC-DPL4R]): each frame starts with the
  players' indexes (1 to 3 bytes each). A message is cut into frames
  under 1,200 bytes, each numbered in sequence (8 bits, from 1) and the
  message by its id (from 1); no more than 24 ids are out at once.
  Reliable frames go again until acknowledged: after a second at first,
  then after the measured round trip and three times its spread, each
  retry waiting longer, up to eight times. The receiver acknowledges the
  end of a message and any frame asking (`SAK`), and the same frame come
  again; with frames missing it says which (a `NACK`, its mask's clear
  bits those that came), and the sender sends those again at once.
  Reliable messages are handed on in order, unreliable ones as soon as
  they're whole.
- **Keeping in touch:** a machine not heard from in 2 seconds is pinged
  (`DPSP_MSG_PING`, answered with `PINGREPLY`); one silent past the
  transport's timeout is let go with its players. The host gone, the
  session is lost: DirectPlay 4's way of another taking over
  (`DPSP_MSG_IAMNAMESERVER`) isn't followed.
- **The messages:** a header of the size (20 bits) and token (`0xFAB`,
  12 bits), the sender's `SOCKADDR_IN` (its port and address in network
  order, the address 0 for the one the message came from), `play`, the
  dialect (14, DirectX 9's) and the command; then the command's fields,
  little-endian, GUIDs as Windows lays them out, text as UTF-16 with its
  terminator, offsets counted from the `play` signature. An answer holds
  a `DPSESSIONDESC2` (80 bytes; `Reserved1`, from which the host makes
  ids, a random number of the host's) and the session's name. Players
  go as `DPLAYI_PACKEDPLAYER` in the messages that make one, and as
  `DPLAYI_SUPERPACKEDPLAYER` in the session sent to a joiner, with the
  Winsock provider's data: the machine's TCP and UDP `SOCKADDR_IN`. On
  TCP a message's length is its header's size.
- **Not done:** secure sessions (`DPSESSION_SECURESERVER`: signing,
  encryption, logons), sessions through the host
  (`DPSESSION_MULTICASTSERVER`), client-server ones, groups, players'
  names and data changed, chat, voice, and the large messages of
  `DPSP_MSG_PACKET` outside the reliable protocol.

## Sources and checks

- **Where the facts come from:** the DirectX 6 SDK's documentation of
  DirectPlay, the lobby and DirectPlay addresses; `MAKE_DPHRESULT`'s
  codes. DirectPlay 4's own messages from Microsoft's published
  specification [MC-DPL4CS] (v20240423): their layouts (2.2), how a
  search is sent and answered (3.1.4.1, 3.2.5.3) and its two examples
  (4.1, 4.2), which settled that offsets count from the signature; how a
  machine joins, makes players and sends (3.1.4, 3.1.5, 3.2.5, 4.3); the
  reliable protocol from [MC-DPL4R] (v20240423), its frames (2.2) and
  rules (3.1). Where
  a player message's header ends, and that the `SOCKADDR_IN`'s port and
  address are in network order, from Wireshark's DirectPlay dissector,
  read for those facts only. The interfaces' IIDs, the classes' and the address types' are
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
  group; a joiner leaving, and the host and a joiner going silent; in a
  session that lets the host migrate, among four, the host leaving and
  the first joiner taking over, the others back in it, messages
  passing, a newcomer finding it at the old host's port and joining,
  then that new host going silent and the next taking over; and the
  same through a meeting server, the new host found by its listing.
  Its `original-messages` case builds the specification's two examples
  and checks their sizes, fields and offsets byte by byte, and reads
  them back; `original-on-tcp`, over the machine's own TCP, has the test
  stand in for the original with the same messages: a search answered
  and the game found (its joining under way), and a host answering a
  search. `original-reliable` and `original-plain` have two endpoints
  speak DirectPlay 4 to each other, each on a thread of its own, the
  joiner finding the host only by DirectPlay 4's search: joined, players
  made both ways, messages short, long and looking like DirectPlay's own
  both ways, guaranteed ones in order, a player destroyed, a joiner of
  Reluva's own protocol turned away, and the joiner leaving; by the
  reliable protocol and without it. `original-password` is turned down
  with the wrong password, then joins with the right one.
  `tests/common/dplay4.cpp` (`common.unit.dplay4`) builds the joining
  messages and reads them back, checking their offsets and the players'
  records against the specification, and runs the reliable protocol's
  link on a network that loses, repeats and reorders frames: messages
  in order, whole and once, the 24-message window, the retry times, a
  `NACK` answered, and an unreliable message lost.
  `reluva-tool legoloco pair` runs two copies of LEGO Loco on one network
  in memory or over the system's UDP, and `run -udp` one each in two
  processes, one hosting and one joining
  ([running.md](../../engines/legoloco/systems/running.md#network-play)).
- **Confidence:** published unless marked. DirectPlay 4's own protocol
  hasn't been tried against the original on Windows: only against the
  specification's examples, and Reluva against itself. Where the
  specification leaves room, these readings were taken *(inferred)*:
  everything between joined machines in a reliable session goes inside
  that protocol, DirectPlay's own messages bare and players' data
  without ids; a joined machine asks for a player's id inside it too;
  the extended flags' `nNACK` is bits 5 and 6; message ids start at 1
  (the specification's examples start at 0); a frame missing 3 seconds
  was an unreliable one, and a message never seen holding up a reliable
  one 5 seconds was too; a password turned down, like a full session,
  answers `DPERR_NONEWPLAYERS`; a host's players' records' size counts
  every part. How the sessions
  travel between computers is otherwise Reluva's own, as is what DirectPlay leaves to its
  service provider: how long a search waits, which endpoint takes over
  as host, ids and instance GUIDs numbered as it likes.

## Used by

- **`legoloco`**: LEGO Loco makes the lobby and DirectPlay at its start,
  for its network games, and plays them through it
  ([running.md](../../engines/legoloco/systems/running.md#network-play)).
