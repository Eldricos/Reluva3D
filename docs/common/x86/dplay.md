# DirectPlay

## What it is

`src/emulation/x86/dplay.cpp`: DirectPlay 3, Microsoft's multiplayer
API of 1997, for programs run in the [x86 emulator](README.md), made
through `CoCreateInstance` as the DirectPlay COM class; and its lobby,
made by `DPLAYX.dll`'s `DirectPlayLobbyCreateA` and `W`. Its sessions are
played between copies of Reluva, over a protocol of Reluva's own on UDP;
it doesn't talk to Microsoft's DirectPlay.

## What Reluva supports

- `IDirectPlay2` and `IDirectPlay3`, each in its Unicode and ANSI form,
  every method. Text is kept as 8-bit characters: a Unicode character
  past 255 reads as `?`.
- **Connections:** one, TCP/IP, when the engine gives it a network
  (`DirectPlay::network`): a way to open UDP sockets, and where to look
  for sessions. Without a network `EnumConnections` lists none, as on a
  machine without one. `InitializeConnection` takes the address
  `EnumConnections` gave, or any DirectPlay address whose service
  provider is TCP/IP's. `Initialize` answers
  `DPERR_ALREADYINITIALIZED`, the object having been made through COM.
- **Sessions:** `EnumSessions`, waiting or not (`DPENUMSESSIONS_ASYNC`:
  each call lists what has answered so far, and asks again each second);
  `Open` to create one or to join one listed, and `SecureOpen` as `Open`
  when no security is asked for (a secure session it can't make:
  `DPERR_UNSUPPORTED`); `Close`; `GetSessionDesc`, and `SetSessionDesc`
  by the host, which reaches everyone. A session with a password is
  listed only with `DPENUMSESSIONS_PASSWORDREQUIRED`, and joined only
  with it.
- **Players and groups:** `CreatePlayer`, `DestroyPlayer`, `CreateGroup`,
  `CreateGroupInGroup`, `DestroyGroup` (and with it the groups made in
  it), `AddPlayerToGroup`, `DeletePlayerFromGroup`, `AddGroupToGroup` and
  `DeleteGroupFromGroup` (a group added to another is listed there as a
  shortcut, `DPENUMGROUPS_SHORTCUT`); `EnumPlayers` (of the open session,
  or with `DPENUMPLAYERS_SESSION` of one listed, before joining),
  `EnumGroups` (those not made in another; a listed session tells its
  players but not its groups, so with `DPENUMGROUPS_SESSION` it lists
  none), `EnumGroupsInGroup`, `EnumGroupPlayers`, `GetCaps`,
  `GetPlayerCaps` (`DPCAPS_ISHOST` for the host's players,
  `DPPLAYERCAPS_LOCAL` for this end's), `GetPlayerFlags` and
  `GetGroupFlags` (the flags it was made with, and `DPPLAYER_LOCAL`),
  `GetGroupParent`.
- **Names and data:** `Get`/`SetPlayerName`, `Get`/`SetGroupName`,
  `Get`/`SetPlayerData`, `Get`/`SetGroupData`. Names, and the data
  everyone sees (`DPSET_REMOTE`), are their owner's to change
  (`DPERR_ACCESSDENIED` for another's), and reach everyone. This end's
  own data (`DPSET_LOCAL`, read with `DPGET_LOCAL`) may be kept for any
  player or group, and is never sent; it comes back in
  `DPSYS_DESTROYPLAYERORGROUP`.
- **A player's event:** the Win32 event `CreatePlayer` is given is set
  when a message for that player is queued: its own, a chat to it, or
  any system message (which is for all of this end's players).
- **`GetPlayerAddress`:** for another end's player, a DirectPlay address
  of the TCP/IP service provider and that end's IP (`DPAID_INet`, or
  `DPAID_INetW` through the Unicode interfaces), as it's reached from
  here. For this end's own players, and for an end this one has no word
  of, it answers `DPERR_UNAVAILABLE`: the network gives no socket's own
  address.
- **Messages:** `Send` to a player, a group or everyone, guaranteed or
  not; `SendChatMessage`, always guaranteed; `Receive` (from or to a
  player, or peeking); `GetMessageCount`. A message, or a player's or
  group's data, is at most 1,300 bytes (`GetCaps`); a larger one is
  refused with `DPERR_SENDTOOBIG`.
- **System messages:** a player or group made or gone
  (`DPSYS_CREATEPLAYERORGROUP`, with the group it was made in, and
  `DPSYS_DESTROYPLAYERORGROUP`); a player added to or taken from a group,
  and a group to or from another (`DPSYS_ADDGROUPTOGROUP`,
  `DPSYS_DELETEGROUPFROMGROUP`); a name or data changed
  (`DPSYS_SETPLAYERORGROUPNAME`, `DPSYS_SETPLAYERORGROUPDATA`, unless
  the session has `DPSESSION_NODATAMESSAGES`); the session's description
  changed (`DPSYS_SETSESSIONDESC`); a chat (`DPSYS_CHAT`); this end made
  the host (`DPSYS_HOST`); and the session lost (`DPSYS_SESSIONLOST`),
  when the host has gone from a session without host migration. They're
  for others' changes only, and not for what was there when joining,
  which `EnumPlayers` lists. Their layouts are dplay.h's.
- **Host migration:** in a session made with `DPSESSION_MIGRATEHOST`,
  when the host leaves or is lost, the one who joined with the lowest
  link number hosts from then on, and the others play on through it
  ([The protocol](#the-protocol)).
- **What needs security or a lobby's staging areas:**
  `GetPlayerAccount` answers `DPERR_UNSUPPORTED` (no session here is
  secure); `Get`/`SetGroupConnectionSettings` and `StartSession`
  answer `DPERR_UNSUPPORTED` for a group (`DPERR_INVALIDGROUP` for none),
  so no `DPSYS_STARTSESSION` is ever sent.
- **The lobby:** `DirectPlayLobbyCreateA` (ordinal 4, as Jedi Knight
  imports it) and `DirectPlayLobbyCreateW` (by name) make an
  `IDirectPlayLobby`, which answers for `IDirectPlayLobby2` too, ANSI or
  Unicode. No lobby started the program, and none is installed:
  `GetConnectionSettings`, `Connect` and the program's own
  `SetConnectionSettings`, lobby messages and message event answer
  `DPERR_NOTLOBBIED` (for another application, `DPERR_INVALIDPARAMS`);
  `RunApplication` answers `DPERR_UNKNOWNAPPLICATION`;
  `EnumLocalApplications` lists none. Its address helpers work:
  `CreateAddress`, `CreateCompoundAddress`, `EnumAddress`, and
  `EnumAddressTypes` (TCP/IP's is `DPAID_INet`; the other service
  providers answer `DPERR_UNAVAILABLE`, as not installed).
- **Not yet:** a secure session (`DPSESSION_SECURESERVER`, `SecureOpen`'s
  security and credentials), which needs Windows' security packages, and
  a lobby's staging areas and the starting of applications through it,
  which need a lobby server: no program run here has asked for either.
  `DirectPlayLobbyCreateW` is offered by name only, its ordinal not
  having been seen in a program's imports.

### The protocol

Datagrams on UDP, each starting `RvDP`, the protocol's version (1) and
its kind. A session's host listens on port 47624, DirectPlay's own
TCP/IP port; the others use any port.

- **Finding sessions:** an enumeration request (the application's GUID,
  or none for any) goes to each address of `network.search`: the local
  networks' broadcast addresses, this machine and a host the player
  named. A host of that application answers with its session (its
  description and players).
- **Joining:** a join (the session's instance, its password and a number
  of the joiner's) goes to the host, again every half second until it's
  answered, for up to five seconds. The host refuses it (with the
  HRESULT `Open` returns) or welcomes it with a link number (and its own:
  1, unless it became the host by migration). Then it sends where each
  other who joined is, the session's players, groups and memberships,
  and a last message saying that's all; `Open` returns once it has come.
- **Links:** each joiner has one, to the host. Guaranteed messages, and
  the session's changes, go over it numbered, each resent every quarter
  second until acknowledged, and taken in order. Other messages go as
  they are, and may be lost, as DirectPlay's were. A link with nothing to
  send sends a keep-alive each second; one not heard from for 15 seconds
  is lost.
- **Players:** an id is the link's number and a count
  (`link << 16 | n`), so each end makes its own without asking. A joiner
  sends its players' changes to the host, which passes them on to the
  others. Messages go through the host too: it passes each one to the
  links of the players it's for.
- **The changes sent over a link,** each an event, its kind first: a
  player or group made (its id, link, name, data, then the group it was
  made in and its flags), destroyed, a player into or out of a group,
  the session's description, a guaranteed message, the session complete
  (when joining); a name changed (id, short and long name), data changed
  (id, bytes), a group added to or taken from another (the group, the
  one added, 1 or 0), a chat (from, to, text), and another who joined
  (link, IP, port) or has gone (link). The last six kinds, the fields
  after a player's flags, and the host's link in a welcome came after the
  protocol's first form, in the same version: an end of the first form
  passes over the kinds it doesn't know and the fields after those it
  reads, so the two still play together, without what's new.
- **Leaving:** `Close` says goodbye to the other end. When a joiner leaves
  or is lost, the host removes its players, tells the others, and tells
  them that link has gone. When the host does, those who joined have
  lost the session, unless it was made with `DPSESSION_MIGRATEHOST`.
- **Host migration:** every joiner knows the others' links and addresses
  (the host tells each of the others as they come and go), so when the
  host goes each chooses alike, without asking: the one with the lowest
  link number, or itself if it's the lowest. The old host's players and
  groups go, as for any who leave. The new host keeps its socket, which
  the others know, makes a link for each of them, says `DPSYS_HOST` to
  its program, and opens a second socket on port 47624 for those still
  to find and join the session (if the port can't be had, as when the
  old host is lost but still holds it on the same machine, the session
  goes on but can't be found). The others link to the new host's
  address, numbering afresh from 1 at both ends. If the one chosen has
  gone too, the others lose their link to it in 15 seconds and choose
  again. A change sent to the old host that hadn't reached the others
  when it went is lost. A joiner keeps the password it joined with, for
  should it become the host.

Calls answer the network as they're made. `DirectPlay::update` does it
between calls (the engine calls it as the program runs and while it
waits), so a host whose program isn't calling still answers. A player's
event is set from there too, so `update` is called where the process's
events may be set: on the program's own turn (the engine's `onSlice`),
or by a test's one thread.

## Sources and checks

- **Where the facts come from:** the DirectX 5 documentation and SDK
  headers (`dplay.h`, `dplobby.h`): the class and interface identifiers,
  the methods' order and arguments, the structures (`DPSESSIONDESC2`,
  `DPNAME`, `DPCAPS`, `DPCHAT`, `DPCOMPOUNDADDRESSELEMENT`, the system
  messages), the address data types, flags and error codes. Jedi
  Knight's code confirms what it uses: `DPERR_BUFFERTOOSMALL` and
  `DPERR_NOMESSAGES`, the system messages' types and fields it reads,
  the address identifiers and the lobby's interface identifiers in its
  data, `DirectPlayLobbyCreateA` as `DPLAYX.dll`'s ordinal 4 with its
  five arguments, and the lobby's methods it calls by their place in the
  vtable ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
- **The protocol** is Reluva's own, not Microsoft's.
- `common.unit.windows`'s DirectPlay cases, over the loopback network
  (`net::LoopbackNetwork`), each end a process of its own:
  - `directplay`: without a network, no connection; one process hosts,
    the other finds the session and its player, joins and makes a
    player; messages go to a player, to everyone (not back to the
    sender, guaranteed) and to a group; the host's change to the session
    reaches the other, who may not change it; the host sees the player
    come and go.
  - `directplay changes`: names and data changed by each side reach the
    other with their system messages, and not with
    `DPSESSION_NODATAMESSAGES`; another's may not be changed; local data
    stays; flags, caps and the host's address; chat to everyone and to a
    player; groups made in and added to groups, listed, taken out,
    renamed and destroyed; the session's description changed; the
    player's event set by a message; `SecureOpen`, `Initialize`,
    `GetPlayerAccount` and the staging areas' methods.
  - `directplay migration`: four ends of a migrating session; the host
    leaves, the first who joined hosts and the other plays on through
    it; a newcomer finds and joins the new host; that host leaves, and
    the two left play on.
  - `directplay lobby`: not lobbied, as Jedi Knight asks; no
    applications; an address made, read, made again from its elements
    and taken by `InitializeConnection`.
- Jedi Knight hosted and joined between two copies on one machine; with
  the lobby it asks whether a lobby started it, is told not, and goes on
  to its menus ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
- **Confidence:** published for the API; the protocol is Reluva's own.

## Used by

- **`sith`**: Jedi Knight's multiplayer
  ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
