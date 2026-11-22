# Jedi Knight: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

None yet.

## Seen running, not traced in the code

None yet.

## Inferred

None yet.

## Guessed

- **Which archive a name comes from:** Reluva looks in the install's
  `Resource` archives, then its `Episode` ones, each folder's in name
  order ([formats/gob.md](formats/gob.md#in-reluva)). The game's own order
  would be settled by tracing how `JK.EXE` opens them.
- **Which CD the game wants:** Reluva tells the CDs apart by `JK_.CD`'s
  bytes, and doesn't yet know how the game itself does
  ([formats/jk-cd.md](formats/jk-cd.md#behaviour)). Tracing its reads of
  the file in `JK.EXE` would settle it.

## Settled

None yet.
