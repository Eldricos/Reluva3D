# Meeting server for network play

## What it is

A server any engine's network play can use when its players can't reach
each other: both behind routers that pass nothing on to them, so neither
can host for the other. Both reach the server; it introduces them so they
can try to reach each other directly, and passes their datagrams on where
that fails. In `src/common/meeting.cpp`: `net::MeetingServer`, and
`net::MeetingSocket` for the players' side. `reluva-tool meet-server`
runs one (`src/devtools/meeting.cpp`).

## What Reluva supports

- **Running one.** `reluva-tool meet-server [port] [-for seconds]` serves
  on UDP port 28530 unless given another, until stopped (Ctrl+C), and
  prints the sessions listed as they come and go. It has to run where
  every player can reach it: a machine on the Internet (a cheap virtual
  server), or one in the group whose router passes its port on. It needs
  nothing of any game.
- **Sessions listed by game.** A host publishes its session with a
  game id, a name and the game's own description of it (at most 1,000
  bytes, passed on as it is), again every few seconds while it's open;
  asking for a game's sessions gets that game's only. A host silent for
  15 seconds is taken off.
- **Introductions.** A player asks to be introduced to a session's host;
  the server tells each the other's address as it sees it (each one's
  router's side). Both then send to that address at once, which opens
  each router's way for the other's datagrams (UDP hole punching), and
  once either hears the other, their datagrams go straight between them.
- **Relaying.** Until then, and for good where the routers won't let the
  punches through, the datagrams go through the server, which passes
  each on to the other side. It only passes them on: a game that seals
  its datagrams (as `age`'s do) keeps them sealed end to end, and the
  server can't read or change them.
- **To the game, a socket like any other.** `MeetingSocket` wraps the
  game's UDP socket. Each connection gets an address of its own (an
  address `0.x.x.x` on port 65535 or 65534, which nothing else uses:
  `isMeetingAddress()`), and what's sent to it goes straight or through
  the server, whichever works; what arrives either way comes in as from
  that address. Datagrams to any other address go out as they would, so
  the same socket still finds and serves players on the local network.

Not done: an account, a password or a limit on who may use a server (any
one who can reach it can list sessions and pass datagrams through it);
IPv6; a list of public servers.

## How it works

Every datagram of its own is `RLVM`, a type and its fields,
little-endian; strings are a length byte and their bytes, byte strings a
16-bit length and their bytes. Anything else a `MeetingSocket` receives
is the game's own and passed up as it is.

| Type | From, to | Fields |
|---|---|---|
| Publish (1) | host, server | game, name, description |
| Published (2) | server, host | the session's id |
| Unpublish (3) | host, server | |
| List (4) | anyone, server | game |
| Sessions (5) | server, asker | how many, then each one's id, name and description (as many as fit 1,200 bytes) |
| Connect (6) | joiner, server | the session's id |
| Peer (7) | server, both | the connection's id, the session's, the other side's address and port, and whether this side hosts (0) |
| NoSession (8) | server, joiner | the session's id |
| Punch (9), PunchBack (10) | side, side | the connection's id |
| Relay (11) | side, server | the connection's id, a datagram |
| Relayed (12) | server, other side | the connection's id, the datagram |
| Keep (13) | side, server | the connection's id |
| Data (14) | side, side | the connection's id, a datagram |

- **Timing.** A host publishes every 3 seconds; a joiner asks to connect
  every second until answered; each side punches every quarter second
  for 5 seconds after being introduced, and tells the server it's still
  there every 5. The server forgets a connection silent for a minute.
- **Who may use a connection.** The server passes a datagram on only
  from one of the connection's two addresses, and a side takes a
  connection's datagram straight from the other side only from the
  address the server gave it.
- **Limits.** 256 sessions and 1,024 connections at once, datagrams of
  1,400 bytes. Ids stay below 2^24, so each fits a connection's address.

## Sources and checks

- **Sources:** Reluva's own design. UDP hole punching is the general
  technique described in public, among others in RFC 5128 (*State of
  Peer-to-Peer Communication across Network Address Translators*).
- **Checks:** `common.unit.meeting`: a session published, listed for its
  game only, a player introduced and their datagrams going straight
  between them both ways; with the two walled off from each other on the
  loopback network, everything going through the server; sessions and
  connections forgotten when silent; a game's other datagrams untouched.
  `age.unit.network` plays a session through one. The command was also
  run on real sockets and answered a publish and a list.
- **Confidence:** Reluva's own, so as the code does unless marked.

## Used by

- **DirectPlay** ([x86/dplay.md](x86/dplay.md#the-protocol)): a
  program's DirectPlay sessions are listed there, by the program's
  application GUID, and joined through it; LEGO Loco's when its
  `meeting_server` is set
  ([running.md](../engines/legoloco/systems/running.md#in-reluva)).
- **`age`**: Midtown Madness 2's network menus take a meeting server on
  the sessions screen, list its sessions and host and join through it
  ([network-protocol.md](../engines/age/games/mm2/systems/network-protocol.md#through-a-meeting-server)).
