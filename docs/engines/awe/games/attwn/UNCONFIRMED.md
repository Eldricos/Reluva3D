# And Then There Were None: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

- **The dialogue manager's rules** (`DialogueRules`): each read from
  `ac.exe` and played in the unit test, none seen in the running game.
  [systems/dialogue.md](systems/dialogue.md).
- **The characters' lighting**: read from `ac.exe` (the lights, when
  each is on, specular), the same code as Orient Express's; the night
  pictures and Armstrong's dark room have been compared with footage,
  a lit character hasn't. [systems/lighting.md](systems/lighting.md).
- **The saves' layout**: read from `ac.exe`'s writer; Reluva reads back
  what it writes, but no save made by the original game has been read.
  [systems/saves.md](systems/saves.md).

## Seen running, not traced in the code

None yet.

## Inferred

None yet.

## Guessed

None yet.

## Settled

- **The dialogue box and its fonts**, read from `ac.exe`: two lines of
  1A's talk with Narracott measured against gameplay footage, the text
  where the footage has it (its ends within 1 to 4 pixels, rows 1 to 2
  higher) and as wide within 1.5 %; the box's pictures, colours, the
  choices and the other fonts (the menus, the journal, the title cards)
  seen in the footage before. It showed one-row lines drawn as written
  (`0x434c26`), not with two spaces after a sentence: fixed.
  [systems/interface.md](systems/interface.md).
- **Wargrave's "Anything else you'd like to add?" in 4A** plays to its
  end and back to the dining room, Narracott shown again, though its
  line `f` was taken out of the table (`coldwargraveTEMPCUT`): seen in
  gameplay footage of chapter 4, and so in the code read again, which
  passes over a missing nest asked for (`0x431ee5`; Orient Express's
  `0x439ca0` too). Reluva stopped the thread there, leaving Narracott in
  Blore's close-up and hidden; it plays on.
  [systems/dialogue.md](systems/dialogue.md).
- **The objects' fields** mean what Orient Express's do, each the code
  reads ([formats/ac1.md](formats/ac1.md)); the rest are the editor's or
  unread. Among them: the file `+0xbc` (`0x407ed0`), the name `+0x2bc`
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
