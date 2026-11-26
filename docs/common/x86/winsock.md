# Windows Sockets (WSOCK32)

## What it is

`src/emulation/x86/winsock.cpp` (`x86::Winsock`): Windows Sockets 1.1,
`WSOCK32.dll`, as games of the time import it, by ordinal, for programs
run in the [x86 emulator](README.md), on the host's network
(`net::Sockets`, `common/net.h`).

## What Reluva supports

- **Start-up:** `WSAStartup` (#115) answers version 1.1 when there's a
  network, and fails (`WSASYSNOTREADY`) without one, so a program offers
  no network play; `WSACleanup` (#116) closes what's left;
  `WSAGetLastError` and `WSASetLastError`, a value for each thread. An
  engine whose program uses Winsock through DirectPlay, which starts it
  on Windows, starts it itself (`start()`).
- **Sockets:** TCP and UDP over IPv4, and IPX datagrams (`AF_IPX`,
  `NSPROTO_IPX`) carried over UDP: an IPX node is an IPv4 address (its
  first four bytes), a socket number a UDP port, the broadcast node (all
  FF) a UDP broadcast. `socket`, `bind`, `listen`, `accept`, `connect`,
  `send`, `recv`, `sendto`, `recvfrom` (`MSG_PEEK`), `shutdown`,
  `closesocket`, `getsockname`, `getpeername`, `ioctlsocket` (`FIONBIO`,
  `FIONREAD`, `SIOCATMARK`), `setsockopt` and `getsockopt` (an IPX
  adapter and its address), `select` and `__WSAFDIsSet`.
- **Blocking:** a socket blocks unless the program makes it non-blocking
  (`FIONBIO`); a blocked call (`accept`, `recv`, `select`, a blocking
  `connect`) lets the program's other threads run while it waits. A
  non-blocking `connect` is under way (`WSAEWOULDBLOCK`) until `select`
  finds it writable.
- **Names:** `gethostname`, `gethostbyname` (one IPv4 address; the
  machine's own name with no network, 127.0.0.1), `inet_addr`,
  `inet_ntoa`, and the byte-order functions. Results go in a buffer of
  Winsock's own, as the specification allows.
- **For an engine** (`Winsock::Options`): the sockets, which may be given
  after the program's imports are bound (`setSockets`); an address its
  sockets get where the program asks for any, so copies on one machine
  each have one; where broadcasts go; and hooks to log the calls and the
  traffic, and to lose packets on purpose (Need for Speed III's
  `-netlog` and `-netdrop`).

Not there: SPX, Winsock 2 (`WS2_32`), the asynchronous `WSAAsync*`
calls.

## Sources and checks

- **Where the facts come from:** the Windows Sockets 1.1 specification:
  its functions' ordinals in `WSOCK32.dll`, its structures (`sockaddr_in`,
  `SOCKADDR_IPX`, `fd_set`, `hostent`, `WSADATA`), its error numbers and
  its blocking *(published)*.
- **How it was checked:** Need for Speed III's network play over TCP and
  IPX between copies of Reluva, its traffic logged
  ([net.md](../../engines/nfs/games/nfs3/systems/net.md)); Starfleet
  Command II's Dynaverse II login, which starts it, looks up its
  directory servers and, not finding them (the service has closed),
  says so ([hosting.md](../../engines/sfc/games/sfc2/systems/hosting.md#its-multiplayer)).
- **Confidence:** published unless marked.

## Used by

- **`nfs`:** Need for Speed III's network play, TCP and IPX
  ([net.md](../../engines/nfs/games/nfs3/systems/net.md)).
- **`sfc`:** Starfleet Command II's Dynaverse II login, and the
  machine's name and address for its DirectPlay games
  ([hosting.md](../../engines/sfc/games/sfc2/systems/hosting.md#its-multiplayer)).
