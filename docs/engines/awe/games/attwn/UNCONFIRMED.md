# And Then There Were None: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

- **The dialogue manager's rules** (`DialogueRules`): each read from
  `ac.exe` and played in the unit test, none seen in the running game.
  [systems/dialogue.md](systems/dialogue.md).

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
- **An act's version over its base** is merged as Orient Express merges
  them: an object the base has already isn't added again. `ac.exe` reads
  the base, then the rest of the file (`0x42ca40`); whether it skips
  duplicates isn't read yet. [formats/ac1.md](formats/ac1.md).

## Guessed

None yet.

## Settled

None yet.
