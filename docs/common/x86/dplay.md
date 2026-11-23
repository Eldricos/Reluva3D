# DirectPlay

## What it is

`src/emulation/x86/dplay.cpp`: DirectPlay 3, Microsoft's multiplayer
API of 1997, for programs run in the [x86 emulator](README.md), made
through `CoCreateInstance` as the DirectPlay COM class. Its sessions are
played between copies of Reluva, over a protocol of Reluva's own on UDP;
it doesn't talk to Microsoft's DirectPlay.

## What Reluva supports

- `IDirectPlay2` and `IDirectPlay3`, each in its Unicode and ANSI form.
- **Connections:** one, TCP/IP, when the engine gives it a network
  (`DirectPlay::network`): a way to open UDP sockets, and where to look
  for sessions. Without a network `EnumConnections` lists none, as on a
  machine without one. `InitializeConnection` takes the address
  `EnumConnections` gave: a DirectPlay address whose service provider is
  TCP/IP's.
- **Sessions:** `EnumSessions`, waiting or not (`DPENUMSESSIONS_ASYNC`:
  each call lists what has answered so far, and asks again each second);
  `Open` to create one or to join one listed; `Close`;
  `GetSessionDesc`, and `SetSessionDesc` by the host, which reaches
  everyone. A session with a password is listed only with
  `DPENUMSESSIONS_PASSWORDREQUIRED`, and joined only with it.
- **Players and groups:** `CreatePlayer`, `DestroyPlayer`, `CreateGroup`,
  `DestroyGroup`, `AddPlayerToGroup`, `DeletePlayerFromGroup`,
  `EnumPlayers` (of the open session, or with `DPENUMPLAYERS_SESSION` of
  one listed, before joining), `EnumGroupPlayers`, `GetCaps`.
- **Messages:** `Send` to a player, a group or everyone, guaranteed or
  not; `Receive` (from or to a player, or peeking); `GetMessageCount`.
  A message is at most 1,300 bytes (`GetCaps`); a larger one is refused
  with `DPERR_SENDTOOBIG`.
- **System messages:** a player or group made or gone
  (`DPSYS_CREATEPLAYERORGROUP`, `DPSYS_DESTROYPLAYERORGROUP`), a player
  added to or taken from a group, and the session lost
  (`DPSYS_SESSIONLOST`), when the host has gone. They're for others'
  players and groups only, and not for those already there when joining,
  which `EnumPlayers` lists.
- **Not yet:** a player's event (`CreatePlayer`'s `hEvent`), names and
  data changed after a player is made, the other system messages, host
  migration, the lobby (`DirectPlayLobbyCreate`), and the other methods,
  which stop the run naming themselves.

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
  HRESULT `Open` returns) or welcomes it with a link number. Then it sends
  the session's players, groups and memberships, and a last message
  saying that's all; `Open` returns once it has come.
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
- **Leaving:** `Close` says goodbye to the other end. When a joiner leaves
  or is lost, the host removes its players and tells the others; when
  the host does, those who joined have lost the session.

Calls answer the network as they're made. `DirectPlay::update` does it
between calls (the engine calls it as the program runs and while it
waits), so a host whose program isn't calling still answers.

## Sources and checks

- **Where the facts come from:** the DirectX 5 documentation and SDK
  headers (`dplay.h`): the class and interface identifiers, the methods'
  order and arguments, the structures (`DPSESSIONDESC2`, `DPNAME`,
  `DPCAPS`, the system messages), flags and error codes. Jedi Knight's
  code confirms what it uses: `DPERR_BUFFERTOOSMALL` and
  `DPERR_NOMESSAGES`, the system messages' types and fields it reads, and
  the address identifiers in its data
  ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
- **The protocol** is Reluva's own, not Microsoft's.
- `testDirectPlay` (`common.unit.windows`): without a network, no
  connection; with the loopback network (`net::LoopbackNetwork`), two
  processes: one hosts, the other finds the session and its player, joins
  and makes a player; messages go to a player, to everyone (not back to
  the sender, guaranteed) and to a group; the host's change to the
  session reaches the other, who may not change it; the host sees the
  player come and go.
- Jedi Knight hosted and joined between two copies on one machine
  ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
- **Confidence:** published for the API; the protocol is Reluva's own.

## Used by

- **`sith`**: Jedi Knight's multiplayer
  ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
