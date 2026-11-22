# Jedi Knight: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

None yet.

## Seen running, not traced in the code

None yet.

## Inferred

- **Transparent colours:** colour 0 in a material's texture marked
  transparent ([formats/mat.md](formats/mat.md#behaviour)), and the
  header's colour in a picture ([formats/bm.md](formats/bm.md)), from the
  files alone. Seeing them drawn in the game would settle it.
- **`\n` in strings** is taken for a new line
  ([formats/uni.md](formats/uni.md#layout)).

## Guessed

- **Which palette an 8-bit picture without its own takes:** palette mode
  1 the menus', 0 the level's ([formats/bm.md](formats/bm.md#behaviour)).
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
