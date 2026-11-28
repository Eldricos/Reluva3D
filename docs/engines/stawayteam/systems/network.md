# Star Trek: Away Team: network games

> **Status:** partial · **Code:** `src/engines/stawayteam/ui/net_menus.cpp`, `src/engines/stawayteam/game/netplay.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

## Overview

PLAY NETWORK GAME on the main menu opens Choose Netgame, a window over
the menu where the player types their name and makes a game or joins one
found on the local network. Either way they come to the Net Lobby, where
the two players talk, the one who made the game chooses the mission, and
START GAME counts down five seconds to it. The program's games are
DirectPlay sessions, over IPX or TCP/IP; a network game is two players.
Reluva has its own protocol, between copies of Reluva, and plays the
missions themselves alone for now: the countdown runs out in the lobby.

## How it works

### Choose Netgame

Its layout is `Rules.txt`'s `Choose Netgame` menu
(`CStarTrekChooseNetGameMenu`, set up at `0x5119f0`); its pictures are
`Cache/Interface/Screens/Start Netgame Menu/`, the background 428 by 247.
On it:

- **The games found**, under "Available Games" (`0x640e3c`), the label
  at `List Pos` and the list `List Size`, starting its font's height and
  2 below the label (`0x512150`). A game is named for the one who made
  it, "%s's Game" (`0x640e10`).
- **The name**, "Name: " (`0x640e4c`) at `Name Pos`, the field 2 after
  it, taking 16 characters (`0x511aae`). It's kept between runs, under
  `Net Game`, `Net Player Name` (`0x511e91`). "Please enter your name"
  (`0x640e24`) is centred on `EnterNameText Pos`.
- **The protocol**, the TCP/IP and IPX buttons, under "Select type of
  LAN Game" (a static text of the menu).
- **CREATE GAME**, wanting a name, and **JOIN GAME**, wanting a name and
  a game chosen (`0x512240`); **REFRESH LIST**; and **CANCEL**.

The list is looked for again every 20 seconds (`0x51210b`). Without a
network it says "No Network Detected" (`0x640dfc`); a game that can't be
joined, "Cannot join selected game.  The selected game is full, already
started, or has exited." (`0x640da4`).

### Net Lobby

Its layout is `Rules.txt`'s `Net Lobby` menu (`CStarTrekNetLobbyMenu`);
its pictures are `Cache/Interface/Screens/Net Lobby/`, each window's
background at its place:

| Part | Place | What's in it |
|---|---|---|
| Chat Window Background | `Chat Pos` | The chat, inside `ChatMargin Size` |
| Player List Background | `PlayerList Pos` | The players' names, from `PlayerName Pos` in it |
| Mission List Window | `MissionList Pos` | The missions |
| Mission Description Window | `MissionDesc Pos` | The chosen one's description, from `MissionDescText Pos` in it |
| Chat Input Background | `ChatInput Pos` | What's typed, at `ChatFieldInput Pos` in it, and SEND at `SendChatButton Pos` in it |

The missions listed are those whose `.MDS` has `Available in Net Game`
(all 18); a mission's description is the entry `Net Lobby Text` of the
script `Net Lobby Script` (`Levels/Net Game Descriptions.RDS`, an entry
for each mission: its place, the mission, the team's size and its
objectives).

Only the host has START GAME: it's made only when the session was
created here, not joined (`0x5155de`, the flag set at `0x512900` and
cleared at `0x512a90`). The mission list, in the menu's font
(`0x514f4a`), can be clicked only by the host (`0x515047`), and starts
with none chosen: START GAME is on only once one is, and not while
counting (`0x5162b2`).

START GAME counts down: it's turned off, and "Mission starting in %d
seconds." (`0x642368`) shows at `Countdown Pos` from 5 seconds
(`0x515c6c`), counted in whole seconds (`0x5160a7`). The host tells
every player, itself too, the seconds left and the chosen mission as
each second passes (`0x516111`), and each answers (`0x51414d`). Nothing
stops the countdown: only making the lobby clears it (`0x5159b9`). A
mission chosen meanwhile goes in the next second's message; a player
who leaves isn't waited for. At nought the host waits for every player
still there to have answered (`0x51617a`), tells them the mission
(`0x5161f7`) and shuts the game to joining (`0x512c30`). Before that it
shuts it with two players and opens it again with fewer (`0x516254`).
EXIT TO MAIN MENU leaves.

A chat line is sent to the host as typed (`0x515cc2`). The host puts
the sayer's name and ": " (`0x63be40`) before it and sends it to every
player, itself too (`0x5c1080`), and each adds it to its chat window
(`0x515c10`). The field takes any length: the lobby doesn't set its
limit (`0x515365`), and none is kept (`0x4e5202`, `0x4e5880`).

The program keeps a table of two players (`0x72e738`, 44 bytes each):
whether the place is taken, a number, and the name.

### Reluva's protocol

DirectPlay's sessions can't be reached from Reluva, so Reluva's copies
speak their own protocol, over UDP on port 23418. Every datagram starts
`RSAT` and a type; numbers are little-endian, strings a length byte and
their bytes:

| Type | Sent by | What it holds |
|---|---|---|
| 1 Ask | One looking | Nothing: broadcast, and to this machine |
| 2 Here | The host | Players, whether it can be joined, the game's name |
| 3 Join | One joining | A number of its own, its name |
| 4 Welcome, 5 Refuse | The host | The join's number |
| 6 State | The host | The players' names, the mission, whether it's started, the countdown's ms left, how many of the other's lines it has, and the chat's last 16 lines (the first's number, then each one's speaker and text) |
| 7 Chat | The other | The number of its first line not yet had, and up to 8 lines |
| 8 Bye | Either | Nothing: gone |

The host sends its state four times a second and as it changes; the
other sends its lines not yet had (or none) as often. Each takes the
other to have gone after 10 seconds without a word, and a join without
an answer for 5 is refused.

## In Reluva

`ChooseNetGameScreen` and `NetLobbyScreen` (`ui/net_menus.cpp`) are the
two screens, over `netplay::Session` (`game/netplay.cpp`). The main menu
opens Choose Netgame with its network: sockets from `System::openUdp`,
and games looked for at the local network's broadcasts and this machine.
TCP/IP is the one protocol chosen; IPX can't be. The name typed is kept
in the options' file. A chat line is at most 200 characters, Reluva's
own limit, and while typed shows its end that fits before SEND. As the
countdown runs out, the lobby says Reluva can't play
missions over the network yet, and stays.

## Sources and checks

- The layouts and the words from `Rules.txt` and the program, at the
  addresses above.
- That a network game is two players: the program's table of them.
- The unit tests: two copies over the in-memory network, finding,
  joining, a third refused, the chat, the mission and countdown, losing a
  third of what's sent, and either leaving; the screens with them
  ([TESTING.md](../TESTING.md#unit-tests)).
- Both screens drawn from the real files and looked at.

## Open questions

- The chosen game's and mission's marking: Reluva's is the font Small
  Blue *(inferred)*.
- How the field shows a line longer than it: Reluva shows its end
  *(inferred)*.
- The program's own messages as a player comes or goes aren't traced.
