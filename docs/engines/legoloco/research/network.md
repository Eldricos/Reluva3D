# LEGO Loco: network play, a first look (working note)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

November 2026, for planning phase 6 ([PLAN.md](../PLAN.md)). From
`loco.exe`'s code read statically and the game run in Reluva without a
network.

## What DirectPlay it uses

- **DirectX 6's DirectPlay:** the lobby (`IDirectPlayLobby3A`) and
  `IDirectPlay4A`, made with `DirectPlayCreate` once for each service
  provider it offers, each asked for the local machine's address
  (`GetPlayerAddress`); one that can't give one isn't there **seen**
  ([running.md](../systems/running.md)).
- **The methods its network code calls** (`0x45e000` to `0x461000`),
  by the vtable offsets of `IDirectPlay4A`'s methods **seen**; a call at
  an offset could be another object's **guess**:
  - sessions: `EnumSessions` (4 calls), `Open` (4), `Close`,
    `InitializeConnection`, `GetCaps` (2);
  - players and groups: `CreatePlayer` (2), `CreateGroup` (2),
    `AddPlayerToGroup`, `DeletePlayerFromGroup` (2), `EnumGroups` (6),
    `EnumGroupPlayers` (4), `GetPlayerAddress` (4);
  - messages: `Send` (2), `SendEx`, `Receive` (2), `GetMessageCount`,
    `GetMessageQueue`, `CancelMessage`.
- **No event:** `CreatePlayer` (`0x45e916`) is given no event handle, the
  player's name from `0x4851d0` (`[USER] Name`); the game polls
  (`GetMessageCount`, `Receive`) **seen**.
- **Files in pieces:** it sends files to other players in pieces of up
  to `0x7fdc` bytes, message `0x3fc` (`0x439df0`) **seen**: postcards,
  and likely layouts **guess**.

## Its screens

- The front screen's case for two heads shows two more buttons: a
  coloured grid, and another **seen**. The tick then shows the network
  screen: `IPX` and `TCP` to choose from, "PLEASE SEL..." across its
  foot **seen**.
- The resource file's `startup` folder names the rest: `net_ipx`,
  `net_tcp`, `netInput` (an address typed?), `net_MATX`, `net_Go`,
  `net_Exit`; `apsearch`, `apGo`, `apoption`, `apExit`, `APmatrix`,
  `aplayer0` to `aplayer8` **seen**: a search for sessions, and a matrix
  of up to nine players' places **guess**.
- `track\global-e/w/n/s` pieces: where one player's board joins a
  neighbour's, trains crossing **guess**.

## Reluva's networking to build on

- `net::Sockets` (TCP and UDP, non-blocking, as `nfs` uses for Winsock)
  and `net::UdpSocket` (`src/common/net.h`).
- The meeting server (`src/common/meeting.cpp`,
  [meeting-server.md](../../../common/meeting-server.md)): sessions
  listed by game, introductions through routers, relaying; used by
  `age`'s Midtown Madness 2, which also played over DirectPlay
  originally but was rebuilt natively with a protocol of Reluva's own.
- Port mapping (`src/common/portmap.cpp`), as `age` uses for a host on
  the Internet.

## Open questions

- Which of the four connections the screens offer when they're there
  (IPX, TCP/IP, modem, serial), and what `netInput` asks for.
- How the players' boards join (`APmatrix`), and what crosses between
  them: trains, minifigures, postcards.
- Whether the host's program differs from a joiner's (name server, the
  matrix's owner).
- How the game uses groups (one per board?) and its messages' layout.

## Later: two copies on one network

November 2026, phase 6's step 1 ([PLAN.md](../PLAN.md)): two copies run
in one Reluva process on an in-memory network; what they did is in
[running.md](../systems/running.md#network-play). Of the questions
above:

- The connections it offers are IPX and TCP/IP **seen**; the `I.P.`
  field is `netInput` **guess**.
- `APmatrix` is the grid of nine places; the host picks the
  arrangement of places from the `.lay` files' names **seen**.
- The host makes the session (`Open` with `DPOPEN_CREATE`), a joiner
  searches and joins; the program is the same, its path set by a flag
  **seen**.
- No groups were made getting into a game **seen**.

