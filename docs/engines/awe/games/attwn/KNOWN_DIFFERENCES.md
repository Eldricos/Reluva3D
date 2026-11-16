# And Then There Were None: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

None yet.

## Bugs fixed

None yet.

## Not yet as the original

In the dialogue manager, read but not followed yet
([systems/dialogue.md](systems/dialogue.md)):

- **A menu's count**: `ac.exe` counts a thread that's offered but has no
  line at its first nest (`0x432e52`, before the line is looked for);
  Reluva counts only those with a line. It can only differ when a
  thread's first nest has no line that may be said and a later one has.
- **A repeat's line** is picked without changing the last alternative
  (`0x431e70`'s fourth argument); Reluva's picking has no such case.

## Additions outside the game

None yet.
