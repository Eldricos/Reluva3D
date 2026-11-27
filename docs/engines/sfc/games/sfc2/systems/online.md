# Star Trek: Starfleet Command II: online play (Dynaverse II)

> **Status:** partial · **Code:** none of its own: the game's program, hosted ([hosting.md](hosting.md)), on the shared Winsock ([winsock.md](../../../../../common/x86/winsock.md)) · **Checked against:** English retail 1.0 and 2.0.3.6, in Reluva's x86 emulator, against a listener that records and never answers · **Confidence:** seen, not traced, unless marked

## Overview

Dynaverse II was the game's online campaign: players' ships on one
shared map, run by servers on WON.net (later Flipside, and by 2.0.3.6
GameSpy Arcade), which have long closed. The game still looks for them
when Game Connections' Dynaverse II LOG IN is pressed, and says it can't
find them. This page says how to point it at a server of one's own, and
what it says to one first. No server is part of Reluva: one would be a
program of its own.

## How it works

### Pointing it at a server

Where it looks is in its settings, not its program:
`MetaAssets/ServerProfiles/Multiplayer/WONServerSetup.gf`.

- **1.0:** `[WONDirectoryServer]`, its keys `0`, `1`, `2` each a
  directory server as `"host:port"`: `sfc2.west.won.net:15101`,
  `sfc2.central.won.net:15101` and `sfc2.east.won.net:15101`.
- **2.0.3.6:** the same servers under `[WONDirectoryServer/Addresses]`,
  with `[WONDirectoryServer] ServerPath = "/StarFleetCommand2/Game/Release"`;
  `[RTI] IPAddr = "209.69.154.13:2601"`, another server (the
  `ImportExportTable.dll` the patch adds is its client, by the names it
  exports, `RTI::...` *(inferred)*); and `[Clients] SecurityClient = 1`
  besides `ChatClient`.

A host is a name or a dotted address: the game tries `inet_addr` first,
then `gethostbyname`. Reluva reads a file from the save folder before
the install's ([hosting.md](hosting.md)), so a copy of the file there,
its addresses changed, points one copy of the game elsewhere without
touching the install. Each address is tried; one that isn't found is
passed over (`gethostbyname` failing).

Ports: 15101 (TCP) for each directory server as the retail file gives
them; 2601 (TCP, by the address) for 2.0.3.6's RTI server, not yet seen
used. What the directory servers send the game to after them, and on
which ports, isn't known: it would be in their answers.

### What 1.0 says first

Pointed at `127.0.0.1:15101`, where a listener records and never
answers, 1.0 does this from LOG IN, the Dynaverse II Login showing
"Welcome to Online Dynaverse II. Checking for a patch...":

1. A TCP connection to the server, nothing sent on it.
2. Then, for each directory server named (those not found left out), a
   socket: `socket` (TCP), `setsockopt` (`SO_LINGER`), `ioctlsocket`
   (non-blocking), `connect`; `select` until each is writable,
   `getsockopt` (`SO_ERROR`), then one request sent on each, the three
   at once.
3. Each then waits on `recv` for 4 bytes, an answer's length *(inferred:
   the requests start with theirs)*.
4. With no answer for about 43 seconds, it closes the connections and
   says "Failed to locate the directory servers. Please check your
   connection or talk to your system administrator."

The three requests, as they were sent (all numbers little-endian):

| Request | Bytes |
|---|---|
| `/TitanServers/Auth` | 55: `37000000 05 0200 6700 37080206 0000 1200` and the path in UTF-16, then `0000` |
| `/StarFleetCommand2` | 55: `37000000 05 0200 6700 770e0206 0000 1200`, the path, `0000` |
| `/StarFleetCommand2/Version` | 71: `47000000 05 0200 6700 010a0000 0000 1a00`, the path, `0000` |

Each, field by field:

| Offset | Size | What | Meaning |
|---|---|---|---|
| 0 | 4 | 55 or 71 | the message's length, these four bytes included *(inferred)* |
| 4 | 1 | 5 | the kind of header *(guessed)* |
| 5 | 2 | 2 | the service: the directory *(guessed)* |
| 7 | 2 | 0x67 (103) | the request's type: the same for the three *(guessed)* |
| 9 | 4 | 0x06020837, 0x06020e77, 0x00000a01 | what to give back of each entry, bit by bit *(guessed)* |
| 13 | 2 | 0 | |
| 15 | 2 | 18, 18, 26 | the path's length, in characters |
| 17 | 2 × that | the path | a directory, UTF-16 |
| then | 2 | 0 | an empty second string (a name in the directory?) *(guessed)* |

So it asks for the authentication servers' entries, the game's
directory, and its version's: the patch check the screen names.

### What 2.0.3.6 says first

2.0.3.6's login (GameSpy Arcade's logo; an email address, password and
nickname) shows "Getting the messages of the day... Checking for a
patch... Welcome to Online Dynaverse II." and, pointed the same way,
connects to each directory server in turn, with long gaps between,
non-blocking as 1.0 does; then it
calls no socket function again and sends nothing in the time run. Why
isn't known: its connections wait in an event-driven part of the program
1.0 doesn't have *(seen, not traced)*. It never called
Windows Sockets 2 (`WS2_32`, which `ImportExportTable.dll` imports) or
connected to its RTI server.

Its login wasn't reached at all until Reluva listed the program's
module before starting the DLLs it imports: `ImportExportTable.dll`'s
start-up asks the program for its exports (`CreateIET` among them), and
found none. Entering Multiplayer still raises a `tDirectPlayError`
("Windows API Call Failed") the game passes over; which call fails isn't
traced.

## In Reluva

- **The host tool's `-netlog`** prints each socket call Winsock logs
  (sockets made, bound, connected, names looked up) and each packet sent
  and received, in hex (`Winsock::setLog`).
- **A listener** of one's own records what the game sends: any program
  that accepts TCP on the ports and writes down what arrives. The runs
  here used a few lines of Python, kept out of the repository.
- **The steps**, headless:

  ```
  mkdir -p <save>/MetaAssets/ServerProfiles/Multiplayer
  (the install's WONServerSetup.gf, its addresses changed, copied there)
  reluva-tool sfc host <game> -save <save> -netlog -click <frame> 560 348 -click <later frame> 310 322
  ```

  (Multiplayer on the main menu, then Dynaverse II's LOG IN.) The real
  clock, not `-fixed-clock`, so its time-outs run as they would.

## Sources and checks

- **Where the facts come from:** the game's settings files; what it sent
  to the listener, byte for byte; its socket calls traced (`-calls`, `-netlog`); and the names
  in its program and DLL (`/TitanServers/Auth`, the `RTI` classes).
- **Not checked:** against a server of the time (none is left), so what
  each field means, and what an answer would hold, is guessed from the
  requests alone.

## Open questions

- What the directory servers answered, and what the game asks next: an
  answer would show, if one could be made that the game takes.
- Why 2.0.3.6 sends nothing once connected.
- What 2.0.3.6's RTI server (port 2601) and `SecurityClient` were.
