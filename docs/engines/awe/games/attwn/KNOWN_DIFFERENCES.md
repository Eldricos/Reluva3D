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

- **The box** is drawn as Orient Express's (its window, where the
  choices go, `fonts.txt`'s colours), not with this game's three windows
  and colours; that waits on the interface. Its heights are this game's.
- **The codes** are Orient Express's (a thread's second character, numbers
  from 0x60), not `ac.exe`'s; the English tables come out the same.
- **A menu's count**: `ac.exe` counts a thread that's offered but has no
  line at its first nest (`0x432e52`, before the line is looked for);
  Reluva counts only those with a line. It can only differ when a
  thread's first nest has no line that may be said and a later one has.
- **A repeat's line** is picked without changing the last alternative
  (`0x431e70`'s fourth argument); Reluva's picking has no such case.

## Additions outside the game

None yet.
