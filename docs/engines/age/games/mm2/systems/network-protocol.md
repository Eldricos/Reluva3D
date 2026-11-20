# Midtown Madness 2: the network protocol

> **Status:** done · **Code:** `src/engines/age/game/net_session.cpp`, `src/common/portmap.cpp` · **Checked against:** the code only · **Confidence:** Reluva's own unless marked

## Overview

The session protocol between copies of Reluva, passwords, joining late, the host's checks, and reaching a host on the Internet. The game's program is encrypted ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md#the-program)), so the numbers below are the game's own and what they do is Reluva's reading of them, to be compared with the original running.

## The protocol

Every datagram is `RM`, a type byte and its fields, little-endian:
strings are a length byte and their bytes, floats IEEE, keys 32 bytes.
Only the first five below and the Challenge go as they are; every other
message goes sealed inside a Sealed datagram ([Encryption](#encryption)).

| Type | From | Fields |
|---|---|---|
| Query | anyone, broadcast | version, the asker's time (ms) |
| Info | host | version, session name, event, city, players, most, flags (playing, password), the asker's time back |
| Join | player | version, net name, the password's proof (none until challenged), the player's public key |
| Challenge | host | 16 bytes, and the host's public key, for a session with a password |
| Welcome / Refuse | host | the player's id, the host's public key and, with a password, the host's proof / why (version, full, password) |
| Status | player, 4 a second | round, name, car, paint, team, flags (ready, loaded, team chosen), ping, its time, a chat line and its number |
| Lobby | host, 4 a second to each | phase, round, seed, session name, settings, the player's id, every player (with the address the host reaches it at, whether it's been called a cheater, and its public key), the last 8 chat lines, the player's chat taken, the player's time back and how long the host held it |
| Car | player, 20 a second | its race clock, position, velocity, rotation (4 x int16), front wheels' angle, wheels' compression (cm), lamps (head, brake, reverse, siren), next checkpoint, finish time, engine revs, throttle, damage, how many dents and the last four (in 5 cm steps), times put back |
| Cars | host, 20 a second to each | every other car's newest state |
| Game | host, 10 a second | the race clock, over, the game's state (Cops & Robbers' gold, as `GoldGame::encode` writes it; in a race each player's next checkpoint and finish as the host counts them) |
| Leave / Kick / Bye | player / host / host | a player going, ejected, the host going (and who's to take over) |
| Sealed | anyone in the session | from (id), to (id), a 64-bit count, then one of the messages above sealed |

The version is 3 (version 2 sent everything in the clear, version 1 the
password itself and none of the Car fields after the finish time).

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
own, a count and the time), with the host's public key. The player
answers with HMAC-MD5 (RFC 2104, keyed by the password) of the
challenge, its net name, its own public key and the host's, and the
host checks it with its own copy. Each challenge is good for one answer.
A wrong answer is refused as the game's "Incorrect password"
(`badp_dlg`).

The host proves it too: its Welcome carries HMAC-MD5 of the challenge,
"host" and the two keys. A player that asked with a password and gets a
welcome whose proof is wrong leaves, as for a wrong password: whoever
answered doesn't know it.

Since both proofs cover both public keys, nobody between the two can put
keys of their own in place of theirs without knowing the password.

## Encryption

Everything after the Welcome goes sealed, so nobody on the way can read
the chat or the game, or change a message or send one of their own:

- **Keys.** Each machine makes an X25519 key pair for the session
  ([common/crypto.md](../../../../../common/crypto.md)). A player's public key goes in
  its Join, the host's in its Welcome (and Challenge), and the Lobby
  passes on every player's, so any two players share a secret, and a
  player taking over as host already has one with each of the others.
- **Each pair's keys.** From two players' shared secret, a ChaCha20 block
  under it (nonce `RMkeys` and zeros) gives two keys: its first half for
  messages from the lower id to the higher, its second the other way.
- **A Sealed datagram.** `RM`, the Sealed type, the sender's id, the
  receiver's, and a count the sender never uses twice (its 13 bytes the
  associated data, and the count, after four zero bytes, the nonce),
  then the message sealed with ChaCha20-Poly1305. One that doesn't open
  under the pair's key is dropped, and so is any message that should be
  sealed and isn't.
- **Each count is taken once.** The receiver keeps the highest count it
  has opened from each sender and which of the 64 before it it has seen;
  a count seen already, or older than those, is dropped. So a datagram
  recorded and sent again (an old lobby, a Kick) does nothing.
- **Who it's from.** The host takes a sealed message only from the
  address the player joined from, and with the player's own id; a player
  only from the host.
- **Without a password,** nothing says whose a public key is: the keys
  keep out anyone who's only listening, not someone who can stand between
  a player and the host from the start. A password closes that.

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
  - It happens only if two players are left, or one with the old host's
    computer players; otherwise the session ends ("The Host has quit").
  - The new host keeps the session as it was: the settings, the players,
    the phase, the race clock (its own copy of the old host's), the chat,
    and in Cops & Robbers the gold as last heard.
  - The old host's computer players stay, and the new host drives them
    on from where their cars were last heard: in a race through the
    checkpoints they hadn't reached, along the roads; in Cops & Robbers
    or a cruise with their minds started afresh. Their cars come to the
    others from the new host, with the count of times put back carried
    on, so taking them over doesn't look like a jump. In a race in any
    order, the new host knows only how many checkpoints each had
    cleared, not which, so it sends them through those after that many
    in the race's order.
  - The new host sees the game's "You are now the Host" (39); the others
    "*name* is the host now".
  - The players' addresses are those the old host saw: on one network
    that's always right; across the Internet, only if each player can be
    reached at the address its router showed the old host.

## The host's checks

The host doesn't take a player's word for everything:

- **Cars.** A state is thrown away, unless the car says it has been put
  back since (fallen out of the city, or stuck: a count in each state),
  if it's:
  - stamped more than a second past the host's clock;
  - moving faster than 110 m/s, or further from the last state than 2 m
    plus 110 m/s for the time between (no car goes that fast: the
    fastest top out near 70 m/s);
  - moving along the ground faster than its own car can, or further
    along the ground than that would take it. A car's limit is its top
    speed (`.vehCarSim`'s top gear at the redline, `High`) a quarter
    over, and 10 m/s more, for a hill or a knock from another car: a
    Beetle at 40 m/s is held to 60. Falling isn't held to it, as a jump
    can be steep. Every machine reads the cars' top speeds from its own
    files, as any player may take over as host.
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

## Through a meeting server

Where neither router passes a port on, the players meet through a
meeting server instead ([common/meeting-server.md](../../../../../common/meeting-server.md)),
given on the sessions screen:

- **Listed.** The host's socket is wrapped in a `MeetingSocket` for the
  game `age`, and it publishes the session every few seconds with its
  Info message as the server's description of it (what a search on the
  network would get), so its players, event and password show as they
  change.
- **Found.** The sessions screen asks the server for the game's sessions
  every two seconds and reads each description as it would an Info.
- **Joined.** Joining one wraps the joiner's socket too, asks the server
  to introduce it to the host, and joins the address the connection is
  given. Everything above then goes as it would: the game's own
  messages, sealed, carried straight between the two once they've
  reached each other, else through the server. Either way the server
  sees only sealed datagrams.
- **Taking over.** A player the host reaches only through the meeting
  server has a connection's address in the Lobby, which only the host's
  socket knows, so the others can't reach it there: such players aren't
  counted among those who could take over, and with none left the
  session ends as it would.

## Sources and checks

- **Where the facts come from:** Reluva's own design (the original's is DirectPlay); UPnP's and NAT-PMP's published specifications for the router; RFC 7748 and RFC 8439 for the encryption; the meeting server is Reluva's own.
- **How it was checked:** `age.unit.network` (with a test that listens in: the chat isn't on the wire, and a datagram sent again, changed, or made up in the clear does nothing), `common.unit.crypto`, `common.unit.portmap` and `common.unit.meeting`; a session hosted and joined through a meeting server with the players walled off from each other (`age.unit.network`).
