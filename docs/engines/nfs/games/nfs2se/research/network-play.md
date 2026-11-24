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

## Races to their end (later in November 2026)

Two copies on the serial link, each driven by the road driver
([road-driver.md](road-driver.md)), Proving Grounds, 4 laps, 8 cars:

| Run | Result |
|---|---|
| Both driven | The race to its end; the race summary the same on both: Player 1 4th, 4:07.53, Player 2 5th, 4:07.68 |
| B losing every 10th packet it sent (`-netdrop 10`) | The same: Player 2 2nd, 4:07.12, Player 1 3rd, 4:07.40, on both |
| B's process killed after A's second lap | A's race ended there: the race summary at once, both players *aborted* |
| Both the 3dfx program (`-program NFS2SEA.EXE`), Up held | Linked, the lobby, the race: the two race clocks within 0.06 s, each car seen in the other copy |

What was found on the way:

- **The controls go round the link, not the cars.** Each copy keeps a
  ring of every player's controls by frame (0x4fd5e8, 0x200 bytes a
  player), from which every car is driven (0x45e8a0, by 0x45e8d0). The
  local player's controls, sampled into a record (0x4ddb10), go into it
  and over the link each frame (0x448720; 0x45dae0 on a link); a frame
  whose remote controls haven't come takes the frame before's
  (0x45e780). The first driver, writing the controls after this, drove
  only its own copy's car: in the other, that car stood at the start,
  every copy waited for it to finish, and the race never ended (a
  still picture after the last car home). Written into the record
  before 0x448720, as the keys are, the controls reach both.
- **The lobby wasn't shown**: one thread polls the link, so no thread
  changes, and its drawing into the front surface never paused for the
  4 ms the host waited for before showing it. The host now shows it
  too before a native function, and after 25 ms at most of drawing.
- **Connecting** needs both *connect...* clicked within a few seconds
  of each other: clicked about 15 s apart, the waiting copy stayed on
  *connecting...* and then stopped drawing and taking keys, and the
  other *failed to connect*; the same with a software and a 3dfx copy.
  Not worked out whether the game or the host stops so.
- **Keys** from `-keysfile` are often lost in the menus on the real
  clock (a press or two in ten): each step was checked on a frame.

## Left open

- A tournament over a link; IPX and the modem to a race's end (the
  serial link only).
- The software and the 3dfx programs linked to each other, and the hang
  connecting late.
- Copies on two machines; DirectPlay (not installed on the host, and
  the program offered no other protocol).
