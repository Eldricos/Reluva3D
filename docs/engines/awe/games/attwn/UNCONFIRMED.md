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

- **Some of the scenes' objects' fields mean what Orient Express's do**:
  the common loader (`0x411390`) reads them in Orient Express's order and
  sizes, and the code reads those it's seen to use as Orient Express's
  are (below); the rest (`alpha`, `lighting`, the scales, `b54`, `flat`,
  the sound's, `pickFirst` and each class's own parts) are taken to be
  as there. [formats/ac1.md](formats/ac1.md).
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

## Guessed

None yet.

## Settled

- **The objects' fields the code is seen to use** mean what Orient
  Express's do: the file `+0xbc` (`0x407ed0`), the name `+0x2bc`
  (`0x410250`), the script `+0x3bc` and `listB` `+0x5cc` (`0x40fff0`),
  `listA` `+0x5c0`, the position from `+0x10` and the depth `+0xb8`
  (`0x4122b0`), the rectangle `+0x608` (the pick, `0x42cea0`), the
  material from `+0x74` (the pictures' colour, `0x42e90e`), the cursor
  `+0x606` after the conditional cursors `+0x5d8` (`0x410405`).
  [formats/ac1.md](formats/ac1.md).
- **Words the evaluator hasn't** (`zero`, and three tags missing their
  `tag`) don't hold, and with `!` do: a word none of its tests takes
  falls to its "doesn't hold" (`0x40aaec` to `0x40ab53`), as Reluva has
  it. [systems/script.md](systems/script.md).
- **`word[0x485f10]` is the time of day**: `0x402b80` sets it, from the
  act's row through `0x485f16` as a scene is left (`0x402df5`,
  `0x40345b`), from the `time` verb at once (`0x40d9c2`) and from a saved
  game's `GLBT` (`0x414324`). [systems/acts.md](systems/acts.md).
- **The shadows don't darken the interface**: the interface's pictures
  are the scene's pictures' class (made at `0x415b1f`, z 0), whose draw
  (`0x42e660`, its vtable `0x474d6c`) clears the stencil under every
  picture that doesn't scroll and is less than 10000 deep while shadows
  are on, so the darkening drawn after them leaves them.
  [systems/shadows.md](systems/shadows.md).
- **An act's version over its base**: `ac.exe` adds every object, with
  no test for one the base has (`0x423ef0`); Reluva's merge, Orient
  Express's, drops none of the install's objects.
  [formats/ac1.md](formats/ac1.md).
- **A character's extra, once heard, isn't offered again** (Wargrave's
  "Can you help me build a parachute?"): read in `ac.exe`, which saves a
  borrowed extra under the generic name and act `0` and reads the
  generic parts' progress back at each talk's start (`0x433c49`,
  `0x432300`). [systems/dialogue.md](systems/dialogue.md).
- **A saved item's state and page** (1 new, 3 in the bag, 4 in hand, 5
  used, 12 items a page): read in the code that sets the states and in
  the `ITEM` loader (`0x413a30`).
  [systems/inventory.md](systems/inventory.md#in-a-saved-game).
- **The close-up's movie is centred**: `examine` places it so, as the
  pictures (`0x419c7c`).
  [systems/inventory.md](systems/inventory.md#examine).
