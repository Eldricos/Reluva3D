# SimCoaster: unconfirmed items

What Reluva does that hasn't been checked against the running original.
The game's program can't be read (it's encrypted,
[executable.md](formats/executable.md)), so nothing here is "from the
code": the files give the numbers, and recordings what can be seen.

## Read from the code, not seen running

None: the game's code isn't read.

## Seen running, not traced in the code

None yet.

## Inferred

- **Each archive stands for the folder of its name**, which is how
  Reluva mounts them (`ui.wad`'s files as `ui/...`): no archived file has
  a loose namesake, and `ui.wad` sits beside a loose `ui/` folder
  ([wad.md](formats/wad.md#overview)). The names the game asks for would
  settle it.
- **Settings' names are matched ignoring case**, as two field names are
  spelt two ways ([sam.md](formats/sam.md#a-setting)).

## Guessed

- **`stexture` folders hold a lower detail setting's textures**, smaller
  copies of their `textures` folders' ([fsh.md](formats/fsh.md#overview)).
  The game's detail options, seen running, would settle which it uses.
- **The last of a setting's name in one file counts**, where a name
  comes twice ([sam.md](formats/sam.md#a-setting)). A ride whose two
  values differ, seen in the running game, would settle it.
- **The speech and postcards in the cabinet's `Data` group are another
  language's**, perhaps Theme Park Inc's British English: the CD's
  American ones replace them, as setup does
  ([cd.md](formats/cd.md#behaviour)). Theme Park Inc's CD would settle
  it.
- **`Data/Challenges.sam` is SimCoaster's own**, not in Theme Park
  World: the "unknown release" detection needs it, so that Theme Park
  World isn't taken for SimCoaster (`src/engines/tpw/games.cpp`). A
  Theme Park World install would settle it.

## Settled

None yet.
