# Midtown Madness 2: multiplayer

> **Status:** partial · **Code:** `src/engines/age/game/race_net.cpp`, `src/engines/age/ui/net_menus.cpp` · **Checked against:** the code only · **Confidence:** Reluva's own unless marked

## Overview

Playing between several computers: the menus, the events and the computer players. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## Playing

On the Race page, the main menu's **Multiplayer** button opens the
sessions screen.

- **NET NAME**: type the name the others see.
- **HOST**: the host's options (an optional password, at most 2 to 8
  players, and how many computer players: below), then the host's
  settings, then **DONE** opens the session and goes to the lobby. The
  host listens on UDP port **28520**. With no network (or the port
  taken), the session is on this computer alone: a game against the
  computer players.
- **JOIN**: joins the session chosen in the list below it. The list shows
  the sessions found on the local network, asked for by a broadcast
  every second. With none in the list, JOIN asks for an address: a host
  name or `a.b.c.d`, with `:port` if it isn't 28520. Over the Internet,
  the host's router has to pass UDP port 28520 on to the host, which
  Reluva asks it to do (below).
- **A game already playing** can be joined: the joiner loads straight
  into it, and the others see their car arrive. A player who leaves (or
  is ejected) takes their car with them.
- **The lobby**:
  - The host's settings and the race's map.
  - The players, with their cars, teams, readiness and ping.
  - The chat: type in the box under YOU and press Enter.
  - **SELECT VEHICLE**: the garage, to choose a car and its colour.
  - **RED** / **BLUE**, or **ROBBERS** / **COPS**: choose a side in the
    team games.
  - **READY**: a player says they're ready.
  - The host also has **EJECT PLAYER**, **HOST SETTINGS** (change the
    event and its settings) and **GO DRIVE**, which starts once every
    player is ready.
- **In the game**:
  - **T**: type a line to the others.
  - **H**: the horn, and on the police car its siren's lights.
  - **Esc**: the host takes everyone back to the lobby; a player leaves
    the session.
- **The host leaving.** When the host leaves the session, or its machine
  stops answering for 5 seconds, the session goes on if two players are
  left (below, "Taking over").
  - **Enter**: the host, after the results, takes everyone back to the
    lobby.

Every machine loads the game when the host starts it. The countdown
begins once all have loaded, or after a minute for any still loading.

## The events

The Host Race Menu offers the game's five: Cruise, Blitz, Checkpoint,
Circuit and Cops & Robbers. Each player drives their own car on their
own machine, and Reluva doesn't check what the others report.

- There's no traffic and no police in any of them: the host's settings
  have the pedestrians' density but neither of those.
- The pedestrians are each machine's own.
- The time of day and the weather are the host's choice.

**Races** are the city's Blitz, Checkpoint and Circuit races, from the
same files as the single-player races ([races.md](races.md)),
with the host's number of laps in a circuit.

- The players start two abreast behind the start, 8 m between rows.
- Each player reaches their own checkpoints and sends how far they've
  got.
- Positions and the results count everyone's progress.
- The race is over, as the host decides, when:
  - everyone has finished,
  - a minute has passed since the first to finish, or
  - a Blitz's time limit has passed.
- A Blitz uses the time limit of the race's amateur row.
- The race files' `cir<n>_strtpnts` (San Francisco only: one to eight
  positions with headings) look like start grids, but they don't match
  the circuits' starts by number (`cir1`'s are 30 m from circuit 1's start
  and face the other way). Reluva doesn't use them.

**Cruise**: the players are spread over the Cops & Robbers places
(below), facing along the road, and drive the city with no end. The host
ends it with Esc.

## Computer players

The host can fill the session with computer players: Host Options'
COMPUTER PLAYERS, up to one fewer than the most players. The game has
none in its multiplayer; this field is Reluva's, drawn below MAX
PLAYERS the way that one is.

- **Who they are.** They join as players do: "Opponent 1", "Opponent 2"
  and so on (`mmlang.dll`'s names for opponents), in cars from the list.
  They're sided like anyone, always ready, and in the lobby's list like
  anyone; the host can eject one.
- **Where they're driven.** The host's machine drives them and sends
  their cars on as its own, so everyone sees them alike. If the host leaves,
  the player who takes over drives them on
  ([network-protocol.md](network-protocol.md#joining-late-leaving-taking-over)).
- **In a race** they drive through the race's checkpoints, laps
  unrolled, along the roads as the single player's opponents do, at 0.9
  of their cars' top speed.
- **In a cruise** they drive from one of the city's Cops & Robbers places
  to another, at random.
- **In Cops & Robbers** each looks at the game every few seconds (every
  second while chasing) and drives at:
  - the gold, while it lies;
  - its base, while it has the gold;
  - the opponent nearest a teammate who has the gold, to stop them;
  - the car of another side that has the gold, a second ahead of it.

  Within 40 m it drives straight at its goal, through and 20 m past a
  car it's ramming. A cop flashes its siren while it chases.
- **Their roads** are the road map's (`city/<map>.bai`): every road's
  middle line, a point every 20 m, joined to the junctions at its ends.
  The roads not for traffic (the Underground, some ramps and alleys) cost
  four times their length. The Cops & Robbers places are up to 141 m off
  the roads (in squares, parks, on raised ground), so their routes may
  begin and end up to 250 m from a road. The single player's road graph,
  from the city's geometry, is in pieces too far from many places: every
  place had routes between them that failed on it, some 290 m from its
  nearest point.
- **Stuck.** One that hasn't moved 4 m in six seconds with its goal still
  away, or that's fallen out of the city, is put back on the nearest
  road point, facing its goal. A new path never puts it nearer its goal
  than that: the opponents' own way back, onto their path's points, would
  have put a computer player onto the gold it was driving at.

Played out on the real cities without drawing (four computer players,
ten minutes, each city and each game type), they chase, ram the gold
loose and recover it. Each game had a delivery or two: the gold's home
and the bases are a kilometre or more apart, a minute and a half's
drive.

## The menus

The multiplayer screens are `tune/widget.csv`'s, drawn as the others are
([menus.md](menus.md)).

| Screen | Background | Controls |
|---|---|---|
| Net Select Menu | `jpg/sess_bk` | `sess_zn`, `sess_ipx`, `sess_ser`, `sess_mod` (not available), `sess_tcp`, NET NAME, `sess_hst`, `sess_jn`, the sessions list (`textscroll`), BACK (`mnav_prev`, picture `mnav_prv`) |
| Enter an Address | `jpg/tcp_dlg` (400 x 330 at 120, 75: `menu.csv` gives it 300 x 225 at 170, 127) | TCP/IP Address, `dlg_can`, `dlg_done`. Left blank, back to the sessions (its words: "or leave blank to search for available session") |
| Enter a valid password | `jpg/pass_dlg` | Password, `dlg_can`, `dlg_done` |
| Host Options | `jpg/host_dlg` | Password, Max Players, Computer Players (Reluva's), `dlg_can`, `dlg_done` |
| Host Race Menu | `jpg/host_bk`, with `jpg/host_cr` over the right for Cops & Robbers | `cruise_m`, `blitz_m`, `cp_m`, `circt_m`, `cops_m`; RACE NAME and LAPS; `free_cr`, `cpsvr_cr`, `robrs_cr`, `none_cr`, `time_cr`, `point_cr`, LIMIT VALUE, GOLD MASS; LOCATION, TIME, WEATHER, PEDESTRIAN DENSITY; `aren_strt` (picture `host_dn`) |
| Network Arena | `jpg/lobbh_bk` (the host's), `jpg/lobbj_bk` (a player's) | Chat Entry, the players (`compscroll`), `lobb_red`/`lobb_blu` (pictures `lobb_rob`/`lobb_cop` in Cops & Robbers), `lobb_ejt`, `lobb_hst`, `mpst_veh` (picture `lobb_veh`), `lobb_rdy` (pictures: `lobb_srt`, GO DRIVE, for the host; `lobb_nr`, READY, and `lobb_rdy`, READY ticked, for a player) |
| eject_DLG | `jpg/ejct_dlg` | the players, `dlg_done` |

The dialogs' widgets are placed from the dialog's corner, which
`tune/menu.csv` puts at (120, 75), 400 x 330. A message box is at (120,
202), 400 x 76:

- `rtrv_dlg`, "Retrieving game settings...", while joining.
- `boot_dlg`, "You have been ejected".
- `badp_dlg`, "Incorrect password", when the host refuses one.
- `msg_dlg`, the others: "The Host has quit", why a join was refused.

Where a widget's name isn't a picture's, the picture was found by its
words and its size: the host's DONE, the lobby's GO DRIVE and SELECT
VEHICLE, the cops' and robbers' team buttons. The MSN Gaming Zone's
picture has five states, though `widget.csv` says four.

Other parts of the screens are boxes on the backgrounds, not widgets,
and were measured on the pictures:

- The lobby's host settings, "you" and chat boxes.
- The eject dialog's list.
- The heading's box, where the lobby says "Waiting for *n* players"
  (`mmlang.dll` 31-37) or "waiting for host to start...".

The descriptions at the bottom left are the game's pictures:

- `mpst_tcp` for TCP/IP.
- `race_hrm`, `race_hbt`, `race_hcp`, `race_hct` and `race_cop` for the
  events.
- `lobb_set` for HOST SETTINGS.

## Not done

- **Hole punching**: two players both behind routers that won't pass a
  port on can't reach each other; that needs a server of Reluva's own on
  the Internet to meet through, which it hasn't.
- **What the host can't see**: a car is held to its own top speed, but
  not to its acceleration, grip or weight: one tuned to corner or pull
  away better than it should, within its top speed, isn't caught.

## How it's tested

`tests/test_age.cpp` has three tests, `testNetSession`, `testNetSessionMore`
and `testGoldGame`; `tests/test_common.cpp`'s `testPortMap` asks routers
made in the test (one with UPnP, one with NAT-PMP, one with neither).

**`testNetSessionMore`**: a wrong password refused and the right one
proved (and HMAC-MD5 against RFC 2104's published example); a player
joining a game already playing and loading straight in, on the host's
clock; the new Car fields passed on; a car that jumps 500 m thrown away
until it says it's been put back, and five jumps calling it a cheater
on every machine; the host leaving and the player in longest taking over
with the clock going on and chat still passing; that host going quiet
and the next taking over; and the session ending when only one is left.

**`testNetSession`** plays a host and players over the in-memory network,
a third of the datagrams lost for the middle of it:

- Discovery by broadcast, joining, the host's sides.
- A full session refusing a third player.
- Chat delivered in order.
- Readiness by rounds, loading, the clocks agreeing.
- Cars passed on, the game's state.
- Back to the lobby, leaving, ejecting, and both kinds of timeout.

**`testGoldGame`** covers the gold's rules and their encoding, the
computer players' goals, and the announcer tables. `testNetSession` also
adds a computer player: ready, loaded, its car passed on, kept ready
back in the lobby.

**The real game.** Two Race pages, a host and a player, were run in the
same process on the real game's files, drawn by a software renderer:

- A circuit race.
- Cops & Robbers.
- A free-for-all whose places file put the gold and the base together,
  so it scored to its limit at once.
- The host alone with three computer players: Cops & Robbers (a point
  each side in five minutes) and a circuit race (all three finished).
- Joining by address, and ejecting.
- A player joining a circuit 11 seconds in: loaded straight into it,
  each seeing the other's car.
- Three Race pages in a cruise, the host leaving: the player in longest
  took over and the third followed, both playing on with the host's car
  gone and their clocks 0.05 s apart.

## Sources and checks

- **Where the facts come from:** the multiplayer menus' files, pictures and words; the rules are Reluva's, as the original's are in its encrypted program.
- **How it was checked:** `age.unit.network`; two copies of Reluva on one machine.
