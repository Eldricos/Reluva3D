# Clue: Murder at Boddy Mansion: the computer players

> **Status:** done · **Code:** `src/engines/clue/ai.h` · **Checked against:** `Clue.exe` 2.2's code, and recordings of the original

## Overview

The computer players, at three levels (novice, normal, expert): each
keeps what it knows of who holds which card, as the original's notebook
does, and suggests, shows cards and accuses from it.

## How it works

As `Clue.exe` 2.2 plays them, read from its code (November 2026); before
that, Reluva's were its own, fitted to the recordings below. A computer
player (class `AI`) keeps what it knows as the original's notebook does
(0x42CC20 on), cards as bits: what each player is known to hold and to
lack, and for each player a list of "holds one of these three". It
takes in, at every level:

- its own hand: held by it, and nothing else;
- every suggestion made (the three cards, 0x42CFA0);
- who couldn't answer a suggestion: lacks all three (0x418935);
- the card shown to it (0x42CE90);
- nobody answering its own suggestion: what of the three isn't in its
  hand is the answer (0x42D1B0), as Professor Plum's accusation in the
  VM recording shows (below).

and, by level (0x421C10, 0x421C80: a novice never, a normal player half
the time, by `rand() * 100 / 32767 > 50`, an expert always):

- someone answering another's suggestion: holds one of the three
  (0x418D00);
- a wrong accusation: the accuser holds none of the three (0x41B26E),
  which needn't be true.

A "holds one of these" loses what its holder lacks and what others hold;
down to one card, that card is held (0x42CCE0). The answer is found only
by elimination: the one card of a kind not known to be held (0x42D0B0;
code there meant to use cards nobody holds tests constants instead and
never does). With all three found it accuses, at once (0x42CE90,
0x422220): only then, so never wrongly but for the wrong-accusation rule
above.

**Its turn** (0x42C740, 0x422570). In a room it may suggest in, it
suggests. Else it rolls and heads for a room: one neither known to be
held nor the answer, by chance (any room in its first three moves; not
the one it is in, 0x42D900); if it is in that room, the nearest other
(GetClosestRoom, 0x426710: through the passage if the room has one).
It goes the shortest way there, as far as the die goes, through the
passage when that leads there, and suggests on arriving. Without the die
it keeps three moves to suggest with.

**What it names** (0x42D8A0): a suspect and a weapon not known to be
held, or the answer once found; above a novice, not one a pending "holds
one of these" is about, if it can; by chance among them. That can be
itself, when it doesn't hold its own card. With nothing left of a kind,
one of its own (0x42D800).

**What it shows** (0x42D680): by chance, but above a novice a card the
suggester has been shown before if it has one, else one shown to anyone.

**The original's play.** The VM recording's game (8:20-23:48, Miss
Scarlet human, the others computer players; the answer Miss Scarlet,
the revolver, the billiard room), read off its ticker:

- A computer player suggests on about half its turns; the rest are
  spent walking. No computer player named itself.
- Once a kind is settled, it names that kind's answer and varies the
  rest: from 17:52 on, Mr. Green, Mrs. Peacock, Professor Plum, Colonel
  Mustard and Mrs. White all suggest "Miss Scarlet with the revolver in
  ..." (the hall, the dining room, the library, the study, the
  ballroom, the billiard room), as Reluva's experts do.
- Some repeat a suggestion another made earlier ("Mrs. White suggests
  Mr. Green with the pipe in the hall" at 14:05 and 17:38), so they
  don't take in everything others learn.
- Professor Plum, suggesting in the billiard room with nobody able to
  disprove it (23:24-23:38), accused at once, in the same turn, and was
  right: the game took 15 minutes, about ten rounds.

## Sources and checks

**The recordings.** Read off each game's ticker, every turn and
suggestion:

- *Expert*: the VM recording (8:20-23:48), five expert players and a
  human.
- *Normal*: "Clue: Murder at Boddy Mansion | Cluedo ... | Steam Deck
  OLED" (YouTube `OesVaOfek9M`, 4:00-24:10): five normal players and a
  human, who won at 24:00. Every computer player suggested on nearly
  every turn, a different room each time; none had accused. They named
  themselves three times in about thirty suggestions.
- *Novice*: "Playthrough 01" and "02" (`kXb0pdCPDuo`, 4:37-21:30, two
  novices and a human; `UCJ_YqBDIOQ`, 1:23-15:40, the same). The humans
  won both; neither novice had accused. Novices named themselves often
  (Colonel Mustard three times, Mrs. Peacock three, Professor Plum three
  of six) and kept naming cards they could know (Colonel Mustard the
  candlestick five times running).

**Against the recordings** (Reluva's figures from `reluva-tool
clue board <clue.lvl> 300 <level> <players>`, games of that level alone):

| Level | Original | Reluva |
|---|---|---|
| Novice (3 players) | no accusation in 14 and 12 rounds (two games); named itself 26% | 41.5 rounds a game, 7% won within 12; names itself 18% |
| Normal (6 players) | no accusation in about 10 rounds; named itself 10% | 20.3 rounds, 18% won within 12; 20% |
| Expert (5 and a human) | won in about 10 rounds; never named itself | 5 players 19.9 rounds, 6 players 15.1, 32% won within 12; 20-21% |

The novices' recorded games fit. Naming itself, looked at again (November
2026): the VM recording's experts made 15 suggestions before they knew
the answer's suspect (10:39-17:52, read off its ticker), none naming
itself; Reluva's experts have none in their first 15 in 9% of games
(`reluva-tool clue board ... expert 6` reports it), its normal players 3 or fewer in
30 about one game in eight. Each step of the choice was gone through
again in `Clue.exe` and matches: the cards known held (0x42CFF0, the
seats' hands together), the "holds one of" lists that experts and normal
players steer off (0x42D020, pared down by 0x42CCE0 as Reluva's are),
who adds to them (0x418D00: all but the suggester and the one who
answered), the deal (0x41C1C0: the answer, then the other 18 cards round
the table by chance, nothing kept back for a suspect's own player), the
pick by chance (0x42D050, `rand() % n`), and the levels (+0xCC: 2
novice, 3 normal, 4 expert). Nothing in the code keeps a player off its
own suspect, so Reluva doesn't either; the two recordings are short of
what it would take to say the original does otherwise. In mixed games of two of each level (`reluva-tool clue board ... 600`),
experts won 402, normal players 159, novices 39.
