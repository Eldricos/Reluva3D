# Windows Sockets (WSOCK32, WS2_32)

## What it is

`src/emulation/x86/winsock.cpp` (`x86::Winsock`): Windows Sockets 1.1,
`WSOCK32.dll`, as games of the time import it, by ordinal, and Windows
Sockets 2, `WS2_32.dll`, built on it, for programs run in the
[x86 emulator](README.md), on the host's network (`net::Sockets`,
`common/net.h`).

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

- **Windows Sockets 2** (`WS2_32`): the same functions at its ordinals,
  which are WSOCK32's but for `ioctlsocket`, `inet_addr` and `inet_ntoa`
  (10, 11 and 12 there; 12, 10 and 11 in WSOCK32); and its own:
  `WSASocketA` and `WSAConnect` (`socket`'s and `connect`'s),
  `WSASend`, `WSASendTo`, `WSARecv`, `WSARecvFrom` (gathering from and
  scattering into several buffers), events (`WSACreateEvent`,
  `WSASetEvent`, `WSAResetEvent`, `WSACloseEvent` and
  `WSAWaitForMultipleEvents`, KERNEL32's events and waits, as Windows
  has them), `WSAEventSelect` and `WSAEnumNetworkEvents` (`FD_READ`,
  `FD_WRITE`, `FD_ACCEPT`, `FD_CONNECT`, `FD_CLOSE`, the event set while
  one is due, with host threads), and addresses as text
  (`WSAStringToAddressA`, `WSAAddressToStringA`, IPv4). An overlapped
  call is done before it returns, its event set, as Windows may do one;
  a completion routine, `WSAIoctl` and `WSAJoinLeaf` are refused
  (`WSAEOPNOTSUPP`).

Not there: SPX, the asynchronous `WSAAsync*` calls, IPv6.

## Sources and checks

- **Where the facts come from:** the Windows Sockets 1.1 specification:
  its functions' ordinals in `WSOCK32.dll`, its structures (`sockaddr_in`,
  `SOCKADDR_IPX`, `fd_set`, `hostent`, `WSADATA`), its error numbers and
  its blocking *(published)*; Microsoft's documentation of Windows
  Sockets 2's functions, and `WS2_32.dll`'s ordinals as Wine lists them
  (`dlls/ws2_32/ws2_32.spec`) *(published)*.
- **How it was checked:** Need for Speed III's network play over TCP and
  IPX between copies of Reluva, its traffic logged
  ([net.md](../../engines/nfs/games/nfs3/systems/net.md)); Starfleet
  Command II's Dynaverse II login, which starts it, looks up its
  directory servers and, not finding them (the service has closed),
  says so ([hosting.md](../../engines/sfc/games/sfc2/systems/hosting.md#its-multiplayer)).
  Windows Sockets 2 by `testWinsock2` in `common.unit.windows`: two
  sockets connected on the loopback through WS2_32's ordinals and
  `WSAConnect`, five bytes sent from two buffers and received into two,
  the event set and `FD_READ` told, then `FD_CLOSE` when the peer closes.
  No game's own use of it has been played: Starfleet Command II 2.0.3.6's
  `ImportExportTable.dll` imports it, for its online play, whose servers
  have closed.
- **Confidence:** published unless marked.

## Used by

- **`nfs`:** Need for Speed III's network play, TCP and IPX
  ([net.md](../../engines/nfs/games/nfs3/systems/net.md)).
- **`sfc`:** Starfleet Command II's Dynaverse II login, and the
  machine's name and address for its DirectPlay games
  ([hosting.md](../../engines/sfc/games/sfc2/systems/hosting.md#its-multiplayer)).
