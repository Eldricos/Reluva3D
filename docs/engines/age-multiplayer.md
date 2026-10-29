# Multiplayer in the Angel Game Engine

How Reluva plays Midtown Madness 2 between several computers: the game's
own multiplayer menus, a session protocol of Reluva's own over UDP, and
the multiplayer events, Cops & Robbers among them. The code is
`net_session.cpp` (the protocol), `net_menus.cpp` (the menus),
`cops_robbers.cpp` (the gold) and `race_net.cpp` (the game on the Race
page).

The original plays over DirectPlay, Microsoft's networking library of the
time, on its MSN Gaming Zone, IPX, TCP/IP, serial cable or modem. Playing
against the original isn't a goal: Reluva speaks only to Reluva, over
UDP. What the files say is used throughout: the menus' layout, pictures
and words, the Cops & Robbers places, the gold's model and the
announcer's lines. How the game plays its multiplayer events is in its
encrypted program ([age.md](age.md)), so the rules here are Reluva's
reading of those files.

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
same files as the single-player races ([age-racing.md](age-racing.md)),
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

## Cops & Robbers

The event's description (`jpg/race_cop`) reads: "Get the gold to the
bank or your hideout – without getting robbed by your opponents!"

**What it's played between.** `race/<city>/multicopwaypoints.csv` (44
places in San Francisco, 46 in London) is in the waypoint files' form:
x, y, z and a heading, the other columns 0. The places are spread across
each city at the roadside, and each heading runs along its road. From
them, by the session's seed, Reluva picks:

- The gold's home, at random.
- A base 300 m or more from it.
- In the team games, a second base 300 m or more from both, and about as
  far from the gold as the first, so neither side starts nearer. Which
  places the original uses for what isn't in the file.

**The games** (the Host Race Menu's GAME TYPES, with `mmlang.dll`'s
names):

| Choice | Picture | Sides |
|---|---|---|
| Free-For-All | `free_cr` | each player on their own; one hideout for everyone |
| Cops & Robbers ("Cops vs. Robbers") | `cpsvr_cr` | the robbers (red) deliver to their hideout, the cops (blue) to the bank. The cops drive the police car (`vpcop`), with its siren |
| Robbers Teams | `robrs_cr` | red and blue, each to its own hideout |

**The rules**:

- **Picking up.** The gold (`geometry/wpobj_gold`) lies turning at its
  home. A car within 5 m of it, and 4 m above or below, picks it up; the
  nearest wins.
- **Carrying.** The gold rides above the car that has it, and adds the
  host's GOLD MASS to that car's weight. The choices are the game's three
  (`mmlang.dll` 506-508): Weightless, Quarter Ton and Half Ton, taken as a
  US ton's (0, 227 and 454 kg). Half Ton is the default.
- **Stealing.** A car of another side that rams the holder knocks the
  gold loose where it is:
  - "Rams" means their middles come within 4.5 m, closing at 4 m/s or
    more.
  - A new holder keeps it for a moment and a half whatever hits it.
  - The car that dropped it can't take it back for 3 seconds.
- **Delivering.** Within 12 m of your side's base, the gold is delivered:
  a point for the side, and the gold is back at its home.
- **Returning home.** Left lying for a minute, or fallen out of the city,
  the gold goes back home.
- **Limits** (GAME LIMITS):
  - NONE: the host ends the game.
  - TIME: the game's 5, 10, 20 or 30 minutes (`mmlang.dll` 510-513),
    with its "N minutes remaining" at 20, 15, 10, 5 and 1.
  - POINT: the first side to that many deliveries (1-50) wins. The game's
    own point limits are 100, 250, 500 and 1,000 points (514-517). What a
    delivery is worth in them isn't in the files, so Reluva counts
    deliveries.

The host runs the gold. The others hear where it is, who has it, the
scores and the last few events, and say them with the game's words
(`mmlang.dll`):

- "You have the Gold!" / "*name* has the Gold!"
- "You dropped the gold!" / "*name* dropped the Gold!"
- "Gold delivered!" / "*name* delivered the Gold!"
- "Time's up!" and "Point limit reached".

**The announcer.** Each event plays one of the announcer's lines, from
`aud/spchdata/CNRSF.csv` and `CNRLONDON.csv`:

- The robber teams' lines name the team: BLUETEAMHASGOLD,
  REDTEAMDROPPEDGOLD, ...
- Cops & Robbers' lines name the side: ROBGETLOOT, COPRECOVERLOOT,
  COPSTASHLOOT, ...
- A free-for-all uses the robbers' lines.

Each table names a header row, `<EVENT> header`, then a recording's
prefix (`AS1\AS1ROBROB`), the last number and the number before the
first. London's tables add how many are used. So the lines are
`aud/aud11/as1/AS1robrob10.11k.wav` and so on. All 34 recordings the two
tables name are there.

**On screen.** The HUD shows the score: each side's, or the best four
players'. The arrow above the car points to the gold, or to your base
when you have it. The map shows the gold and the bases, yours in the
finish's colour. The checkpoint stands mark each base.

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
  their cars on as its own, so everyone sees them alike.
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
([age-menus.md](age-menus.md)).

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

## The protocol

Every datagram is `RM`, a type byte and its fields, little-endian:
strings are a length byte and their bytes, floats IEEE.

| Type | From | Fields |
|---|---|---|
| Query | anyone, broadcast | version, the asker's time (ms) |
| Info | host | version, session name, event, city, players, most, flags (playing, password), the asker's time back |
| Join | player | version, net name, the password's proof (none until challenged) |
| Challenge | host | 16 bytes, for a session with a password |
| Welcome / Refuse | host | the player's id / why (version, full, password) |
| Status | player, 4 a second | round, name, car, paint, team, flags (ready, loaded, team chosen), ping, its time, a chat line and its number |
| Lobby | host, 4 a second to each | phase, round, seed, session name, settings, the player's id, every player (with the address the host reaches it at, and whether it's been called a cheater), the last 8 chat lines, the player's chat taken, the player's time back and how long the host held it |
| Car | player, 20 a second | its race clock, position, velocity, rotation (4 x int16), front wheels' angle, wheels' compression (cm), lamps (head, brake, reverse, siren), next checkpoint, finish time, engine revs, throttle, damage, how many dents and the last four (in 5 cm steps), times put back |
| Cars | host, 20 a second to each | every other car's newest state |
| Game | host, 10 a second | the race clock, over, the game's state (Cops & Robbers' gold, as `GoldGame::encode` writes it; in a race each player's next checkpoint and finish as the host counts them) |
| Leave / Kick / Bye | player / host / host | a player going, ejected, the host going (and who's to take over) |

The version is 2 (version 1 sent the password itself, and had none of
the Car fields after the finish time).

- **Nothing depends on one datagram.** Each message is sent again and
  again, the newest replacing the last. A chat line is sent until the
  host's Lobby says it has it. A player's readiness counts only for the
  round the host is in, so an old Status can't make a player ready for
  the next game.
- **Timeouts.** Silent 5 seconds, a player is dropped, or the host is
  taken as gone. While a game loads (and before its countdown) the limit
  is a minute.
- **The clock.** The host's race clock reaches 0 three seconds after
  everyone has loaded. A player's clock is the host's, as the last Game
  said, plus half the time there and back. That time is the player's
  Status time, echoed in the Lobby less the time the host held it.
- **The others' cars** are drawn where they were a tenth of a second
  ago:
  - Between the two states either side of then, position and velocity
    straight, the rotation by the shorter way.
  - Past the newest, carried on along its velocity for up to half a
    second.
  - A collision with another player's car pushes and knocks only your
    own: their machine does the same for theirs, so the two agree.

## Passwords

A password isn't sent. A player joining a session that has one is sent a
challenge: 16 bytes the host hasn't used before (MD5 of a secret of its
own, a count and the time). The player answers with HMAC-MD5 (RFC 2104,
keyed by the password) of the challenge and its net name, and the host
checks it with its own copy. Each challenge is good for one answer. A
wrong answer is refused as the game's "Incorrect password" (`badp_dlg`).
That keeps the password off the network; it isn't encryption: the game
itself goes in the clear.

## Joining late, leaving, taking over

- **Joining late.** The host takes a player in whatever the phase. The
  joiner's first Lobby says the game's playing, so its machine loads it,
  takes the clock from the Game messages and starts in its grid place (or
  base, or Cops & Robbers place) with no countdown. A late joiner in a
  race starts the race from the beginning.
- **The cars follow the session.** Every machine adds a car when a
  player appears and takes it away when they go.
- **Taking over** ("Migrate Host", `mmlang.dll` 480). The Lobby passes
  on every player's address as the host reaches it, so each player can
  reach the others:
  - The host leaving names who takes over: the player who's been in
    longest (the lowest id). A host that just stops answering is replaced
    the same way, by each player's own reckoning, after 5 seconds.
  - It happens only if two players are left; otherwise the session ends
    ("The Host has quit").
  - The new host keeps the session as it was: the settings, the players,
    the phase, the race clock (its own copy of the old host's), the chat,
    and in Cops & Robbers the gold as last heard. The old host's computer
    players go with it.
  - The new host sees the game's "You are now the Host" (39); the others
    "*name* is the host now".
  - The players' addresses are those the old host saw: on one network
    that's always right; across the Internet, only if each player can be
    reached at the address its router showed the old host.

## The host's checks

The host doesn't take a player's word for everything:

- **Cars.** A state stamped more than a second past the host's clock,
  moving faster than 110 m/s, or further from the last state than 2 m
  plus 110 m/s for the time between, is thrown away, unless the car
  says it has been put back since (fallen out of the city, or stuck: a
  count in each state). The fastest cars top out near 70 m/s.
- **Cheaters.** Five states thrown away and the player is called a
  cheater, in the game's words: "*name* is a cheater" (41) to the others,
  "You are a cheater" (46) to them.
- **Races.** The host counts each player's checkpoints itself: a
  checkpoint counts when the way the car went between two states passes
  within its radius (or 15 m) plus 4 m, and within 10 m of its height.
  The finish is the time of the state that reached the last. Everyone's
  positions and results, and when the race is over, go by the host's
  count, which it sends with the Game.
- **The gold.** A car that's been put back drops the gold it had where
  it was, and for 3 seconds can't take it or ram it loose, so being "put
  back" can't carry the gold home or onto it.

## Others' cars: sound and damage

Each car's state carries its engine's revs and throttle, its damage, and
the dents its body has taken. On the others' machines:

- **Engine.** Its own engine sound (its `aud/cardata` table, as for the
  player's car), quieter with distance (silent at 120 m) and to the side
  it's on.
- **Damage.** The dents where its own machine made them, its body's
  damaged textures within its `TextelDamageRadius`, and its smoke once
  it's as damaged as smokes. Mended (a new game), it's clean again.

## Reaching a host on the Internet

A host behind a router can only be reached from the Internet if the
router passes UDP port 28520 on to it. When a session is hosted on the
network (or taken over), Reluva asks the router itself, on a thread of
its own (`common/portmap.cpp`):

1. **UPnP**, from the UPnP Device Architecture and its Internet Gateway
   Device's WANIPConnection service (both published by the UPnP Forum):
   - An SSDP search to 239.255.255.250:1900 for an
     `InternetGatewayDevice:1` (else a `WANIPConnection:1`). The router's
     answer gives its description's address.
   - The description (over HTTP) names the WAN connection's service type
     and control address.
   - The SOAP action `AddPortMapping`: UDP 28520 to this machine's
     address, for good (else for a day). Then `GetExternalIPAddress`.
2. **NAT-PMP** (RFC 6886), if UPnP found nothing: a mapping request to
   port 5351 of the gateway, for two hours, asked again at half that,
   and the external address request. The gateway is the system's
   (Linux's `/proc/net/route`, Windows' adapters), else the first address
   of each network the machine is on.

The lobby's lines say what came of it: the address players on the
Internet join, or that the router didn't answer and the port has to be
passed on by hand. The mapping is taken away when the host leaves
(`DeletePortMapping`, or a NAT-PMP lifetime of 0).

## Not done

- **Hole punching**: two players both behind routers that won't pass a
  port on can't reach each other; that needs a server of Reluva's own on
  the Internet to meet through, which it hasn't.
- **Encryption**: the game's messages, chat included, go in the clear.
- **What the host can't see**: the checks catch impossible movement and
  progress, not a car tuned faster than it should be, within the limits.
- **Computer players over a change of host**: they go with the old host.

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
