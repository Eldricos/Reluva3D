# And Then There Were None: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

- **The dialogue manager's rules** (`DialogueRules`): each read from
  `ac.exe` and played in the unit test, none seen in the running game.
  [systems/dialogue.md](systems/dialogue.md).
- **Lighting, the dialogue box, the fonts and the saves' layout**, each
  read from `ac.exe` and none compared with the running game or a
  recording yet. [systems/](systems/).

## Seen running, not traced in the code

None yet.

## Inferred

- **The scenes' objects' fields** are read by Orient Express's lists.
  Every scene reads to its end with them, so their layout is the same,
  but each class's loader in `ac.exe` isn't read yet: what each field
  means is Orient Express's. [formats/ac1.md](formats/ac1.md).
- **Words the evaluator hasn't** (`zero`, and three tags missing their
  `tag`) don't hold, as in Orient Express's evaluator; `ac.exe`'s way
  with them isn't read. [systems/script.md](systems/script.md).
- **`word[0x485f10]` is the time of day**, as Orient Express's
  `word[0x4986f8]`: the goodbye's "Good evening" goes by it as there,
  and it's set with `0x485f16` by `0x402b80`; what sets it isn't read.
  [systems/dialogue.md](systems/dialogue.md).
- **The shadows don't darken the interface**, though `ac.exe` draws the
  darkening after it: inferred from the scenes' pictures clearing the
  stencil, the interface's pictures' draw not read.
  [systems/shadows.md](systems/shadows.md).
- **An act's version over its base** is merged as Orient Express merges
  them: an object the base has already isn't added again. `ac.exe` reads
  the base, then the rest of the file (`0x42ca40`); whether it skips
  duplicates isn't read yet. [formats/ac1.md](formats/ac1.md).
- **Wargrave's "Anything else you'd like to add?" in 4A strands
  Narracott** in Blore's close-up, as Reluva plays it: the thread's line
  `f` was taken out of the table (it's `coldwargraveTEMPCUT`), and a
  thread ends at its first missing line (`0x431e70`, read), so the lines
  after it, the last of which goes back to the dining room, never play.
  Taken before thread `b`, which goes back to the dining room, it leaves
  Narracott hidden everywhere (its line `b` says `globalhide narracott`,
  the cut-off `k` the `globalshow`): he can't be clicked again, and 6C
  wants the embrocation used on him. Whether the original strands the
  player there too isn't seen in the game itself. The route search keeps
  out of both.
- **A character's extra, once heard, isn't offered again** (Wargrave's
  "Can you help me build a parachute?"): Reluva reads its saved progress
  back where Orient Express's doesn't; without it, Wargrave's summing-up
  in 4A offers it for ever with no way out. Not read in `ac.exe`.
  [systems/dialogue.md](systems/dialogue.md).

## Guessed

None yet.

## Settled

- **A saved item's state and page** (1 new, 3 in the bag, 4 in hand, 5
  used, 12 items a page): read in the code that sets the states and in
  the `ITEM` loader (`0x413a30`).
  [systems/inventory.md](systems/inventory.md#in-a-saved-game).
- **The close-up's movie is centred**: `examine` places it so, as the
  pictures (`0x419c7c`).
  [systems/inventory.md](systems/inventory.md#examine).
