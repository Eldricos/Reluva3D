# And Then There Were None: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

None yet.

## Bugs fixed

None yet.

## Not yet as the original

The whole game: nothing of it is played yet ([PLAN.md](PLAN.md)).

In the dialogue manager, read but not followed yet
([systems/dialogue.md](systems/dialogue.md)):

- **A character's and a place's tables** (`Dialogue/General`,
  `Dialogue/Locations`) are loaded with the act's, not as the character
  or place is met (`0x403154`, `0x4033f4`). A conversation is found by
  its name and part either way.
- **The codes** are Orient Express's (a thread's second character, numbers
  from 0x60), not `ac.exe`'s; the English tables come out the same, but a
  save's `TALK` records hold them, so Reluva's saves of this game wouldn't
  load in the original, nor its in Reluva, until they're this game's.
- **A menu's count**: `ac.exe` counts a thread that's offered but has no
  line at its first nest (`0x432e52`, before the line is looked for);
  Reluva counts only those with a line. It can only differ when a
  thread's first nest has no line that may be said and a later one has.
- **A repeat's line** is picked without changing the last alternative
  (`0x431e70`'s fourth argument); Reluva's picking has no such case.

In lighting: a light whose 25th field is set, in an act's version, would
change the base's light with its index rather than be added
([systems/lighting.md](systems/lighting.md)); none is, and Reluva adds
it.

## Additions outside the game

None yet.
