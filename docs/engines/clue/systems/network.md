# Clue: Murder at Boddy Mansion: network play

> **Status:** done · **Code:** `src/engines/clue/netplay.h`, `src/engines/clue/frontend_net.cpp` · **Checked against:** Reluva's own tests, not the original · **Confidence:** Reluva's own protocol; the original's screens confirmed (from its files)

## Overview

Start Game, **Networked**, then:

- **LAN TCP/IP**: **Host New Game** (a game name, your name, how many
  humans at most), or **Join Game**, which lists the games found on the
  local network (Refresh asks again).
- **Internet by address** (the original's "Internet Game Zone" button;
  the Zone closed in 2006): **Host New Game** as above, and tell the
  others your address; **Join Game** asks for the host's address,
  `a.b.c.d`, `a.b.c.d:port` or a name. The host's router must forward
  UDP port 23398 to the host's machine. That window is the Host Game
  window (the original has none of its own), relabelled, without the
  number of players: its row of boxes for them is in its picture
  (`menu:Ihostgmbg`), so Reluva covers it with the same piece of the Exit
  window's picture (`menu:Iexitbgnd`), the plain circle there, as both
  pictures are the same around it.

Then the original's players' picker, with its chat line (type, Enter or
the telephone sends it). Each player clicks a suspect to take it (again
to give it back). The host clicks any other suspect to make it a computer
player at a level, or none, and its **Options** to switch Classic / No
Die Roll; **Start Game** once there are three suspects and every machine
that joined has one. The game then deals the same cards on every
machine.

In the game each machine shows its own player's cards and notepad, and
only its own player's turns take clicks. A player who leaves (or whose
machine stops answering for 15 s) is played on by a Normal computer
player; if the host leaves, the others see "The Host Has Disconnected"
and the game ends. Saving isn't offered in network games.

## How it works

**Against the original.** The original speaks DirectPlay 5/6 (the
`DPLAYX` session, its own messages inside), which nothing but Windows
still has; to play against it, Reluva would have to speak DirectPlay's
protocol and the original's messages, which would need recording
network play of the original. Not planned unless it's wanted.

## In Reluva

**Unlike the original:** its readme says the host's "Suggest Movies" and
"Walk Animations" settings apply to every player in a network game. In
Reluva each player's own preferences apply on their own machine: whether
one watches the videos or the walks changes nothing for the others.

**How it works.** UDP, port 23398. Datagrams begin `CLU` and a version
(2: with the walk about a room).
Finding games: a broadcast "ask", each host answering with its game's
name, its host's name and its places. Joining: "connect" with the
player's name, answered "accept" (with the machine's number) or "refuse"
(full, or begun). After that, messages to and from the host go over a
channel that numbers them, resends them every 250 ms until acknowledged,
and delivers them in order; an acknowledgement goes at least once a
second, and 15 s without one ends the connection. The host keeps the
lobby and sends it whole on each change. The game: the start carries the
random seed; then each player's action (roll, walk with its squares,
passage, suggest, accuse, end the turn, a card shown, a player taken
over, a walk about one's room to a point, as `Clue.exe` sends that click
to every machine, 0x41BD90) goes to the host, which sends every action, in one order, to
everyone, itself included. Every machine plays the actions out from the
same deal and seed, so the die and the computer players come out the
same everywhere. The host checks each action is from the machine whose
player it is. `src/engines/clue/netplay.h`.

## Sources and checks

Tested: unit tests over Reluva's in-memory network (finding, joining,
the lobby, chat, starting, the order of actions, a lossy network, a
player gone, the host gone); whole games of three machines in one
process with the real game, each at its own frame rate, on a clean and a
lossy (one datagram in four lost) network, with the die and without, a
machine dropping half way: every machine ends in the same state; and two
copies of Reluva on one machine over its real network, through the
screens, into the same game. Not yet tried between two real machines.
