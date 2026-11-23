# Need for Speed II SE: network play on the host (working note)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

A working note from November 2026: Phase 8 of [the plan](../PLAN.md),
two copies of the retail `nfs2sen.exe` on one machine, linked by each
of the game's three ways to play another machine, on the stand-ins NFS
III's network play was built on
([its plan](../../nfs3/PLAN.md), Network play).

## How it was run

Two `reluva-tool nfs host` runs on the real clock, each driven by a file
of keys its run reads as it goes (`-keysfile`), the keys written by
looking at each copy's latest frame:

```text
reluva-tool nfs host <install> -save <dir> -net -netlog <file> -frames <dir> -every 15
    -keysfile <keys.txt> [-netaddr 127.0.0.2 -broadcast 127.0.0.3]
    [-serial listen:5000 | -serial 127.0.0.1:5000]
    [-modem listen:5001 | -modem 127.0.0.1:5001]
```

A clock of instructions (`-deterministic`) didn't do: each copy's clock
then runs at its own speed, and the two drift apart by seconds while
one waits for the other. The game's network code times out by its own
clock, and copies joined in some runs and not others.

## What the game offers

*Game setup*'s **game type**: one player, split screen, modem, serial
link, network. A type other than one player or split screen makes the
main menu's RACE into CONNECT, which asks the player's name and then
the link's screen:

- **Network**: the protocol (IPX the only one offered), *create game*,
  *join game*. The program also has DirectPlay's names (`DirectPlay IPX`,
  `DirectPlay TCP`) and EA's TCP player code, but imports only
  `WSOCK32` and offered IPX alone here, with no DirectPlay installed.
- **Serial link**: the COM port (4 by default), *connect...*.
- **Modem**: the modem (TAPI's lines; here "Reluva TCP modem"), a phone
  list kept by the game, *delete number*, *dial...* (greyed until the
  list has a number), *answer...*. Up and Down don't move along this
  screen; the mouse does.

Then the same lobby for all three: the race's settings on the left, the
players in rows on the right (two rows for the serial link and modem,
eight for the network), each player's light red, then green once they've
pressed RACE. The host's RACE starts the race once everyone is ready;
both copies then show *synchronizing* and load the track.

## What was seen

| Link | Run | Lobby | Race |
|---|---|---|---|
| Network (IPX over UDP, `-netaddr` 127.0.0.2 and .3) | A creates, B lists A's game and joins | Both players in both lobbies; B's ready light green on A's | Proving Grounds, the two race clocks within 0.03 s; B driven, A standing; about 1,600 race packets each way a minute (43 and 16 bytes) |
| Serial link (one TCP link, COM4 on both) | Both *connect...* | Two rows, both players | The same, the clocks within 0.01 s |
| Modem (TAPI over TCP) | A answers; B adds 555 to its list and dials it | Two rows, both players | The same, the clocks within 0.03 s |

No stand-in needed changing: IPX's game finding (a broadcast to socket
`0x452`, `EAGAME.` names), its reliable layer (`ea` + `srv`, `dec`,
`add`, `rsp`, `dat`, `ola`, `out` messages), the serial line's `+++`/`ATE1`
exchange and TAPI's lines and calls all worked as NFS III's do. The
game's network library is EA's, as NFS III's (the same message names).

## Left open

- A race run to its end, and a tournament, over a link.
- Losses (`-netdrop`) and a copy leaving mid-race.
- Copies on two machines; DirectPlay (not installed on the host, and
  the program offered no other protocol).
- The 3dfx program on a link.
