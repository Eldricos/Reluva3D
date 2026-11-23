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

- **The music banks' entries without headers are never played**: the
  last 40 and 15 of two worlds' `MusicHD.sdt` point at no header, and
  Reluva lists them as not readable ([sdt.md](formats/sdt.md#what-doesnt-decode)).
  Playing those worlds' music in the original would settle it.
- **The logo movie is shown twice as wide as its frames**, 320 × 480 at
  640 × 480, as its ring is round in the recording
  ([tgq.md](formats/tgq.md#the-logo-tgq)).
- **Where the park's ground lies**: flat at y = 0, its cells 2.5 units
  square, the file's first row at the far side, filling `base.MD2`'s
  hole ([lnd.md](formats/lnd.md#the-cells)): drawn so, its entrance notch
  and rivers meet the model's.
- **`scape.omp`'s objects are sound and particle emitters**: kind 1
  runs along the rivers, kind 3 sits at the entrance, and the file names
  a particle library's and the sound events' definitions
  ([omp.md](formats/omp.md)). Hearing a world's river by its bank in
  the running game would settle it.
- **Animations run 25 frames a second**: so the front end's camera path
  keeps time with the recording, its doors opening and its globe rising
  when the recording's do
  ([md2-animation.md](formats/md2-animation.md#how-fast)).
- **Ten challenges a world**: `Challenges.sam` says so of its first ten
  only; the Personnel File counts ten for each world
  ([personnel-file.md](systems/personnel-file.md#a-new-players-numbers)).
  Winning a challenge in another world would settle it.
- **The interface's templates are stretched with their edges kept**:
  drawn so, the Personnel File matches the recording
  ([ui-models.md](formats/ui-models.md#stretching)).
- **How many frames an animated texture has**: its names are read in
  runs of a stem and its numbers; read so, every model's names add up
  ([md2.md](formats/md2.md#the-textures)).
- **A player's first objective is set when they enter their first
  game**: a new player shows "None", one who had played "Operation
  Expand!" ([players.md](systems/players.md)).
- **A group's flag 1 draws it unlit, a texture's flag 2 blends it**:
  so the front end's beam and glow look as in the recording
  ([md2.md](formats/md2.md#a-mesh)).
- **A particle effect's word 29 is its particles' lifetime**, and the
  front end's script is set going 3.4 s into the path
  ([front-end.md](systems/front-end.md#the-effects)).
- **The globe's close view and timings**, measured on the recording
  ([globe.md](systems/globe.md#turning)).
- **Which level folder is which world**: `science` Land of Invention,
  `water` Polar Zone, `wIntro` Intro World, `arabian` Arabian Nights, from
  the string table's order and `Objects.txt`'s themes
  ([research/footage.md](research/footage.md#the-worlds-names-and-folders)).
  Entering each world, seen with its files, would settle it.
- **Each archive stands for the folder of its name**, which is how
  Reluva mounts them (`ui.wad`'s files as `ui/...`): no archived file has
  a loose namesake, and `ui.wad` sits beside a loose `ui/` folder
  ([wad.md](formats/wad.md#overview)). The names the game asks for would
  settle it.
- **Settings' names are matched ignoring case**, as two field names are
  spelt two ways ([sam.md](formats/sam.md#a-setting)).

## Guessed

- **How the front end's particles fly and look**: 20 a burst, small
  stars, up to 6 units a second; emitters 11 to 14 the `Fx` helpers, 1
  the well's middle ([front-end.md](systems/front-end.md#the-effects)).
  Decoding the library's other parameters would settle it.

- **The Personnel File's portrait is the personnel director's**: the
  board's fifth, as `EMAIL_FROM_HR_DIR` is the fifth sender
  ([personnel-file.md](systems/personnel-file.md#what-it-shows)).

- **Where a release's Bullfrog logos go on the loading screen**: over
  the scene's QSound logo, at the bottom left
  ([loading-screen.md](systems/loading-screen.md#the-loading-screen)).
  The US release has none; a recording of another release's loading
  screen would settle it.

- **Which animation a script's number plays**: 0 `c`, 5 `m`, 9 `b`, 10
  `r` from the scripts' own use, 2 `i` and 11 `o` from which files are
  there ([md2-animation.md](formats/md2-animation.md#which-animation-is-which)).
  A ride seen building, breaking and running in the game would settle
  them.
- **The front end's camera**: its field of view, 53° high, measured on
  the recording; the effects of its script's events
  ([front-end.md](systems/front-end.md)). The view drawn by the game at
  a known size would settle the first.

- **How the script machine runs** beyond what the sources show: turns
  ending after the header's 50 instructions, critical sections not
  keeping the turn, `BRANCH_PV` above zero, `CMP` an AND, `WAITABS`
  ([scripts.md](systems/scripts.md)). Rides seen running in the game,
  set beside a script's trace, would settle them.

- **How a world's sky is drawn** from its three pictures: a dome
  coloured by `sky_rgb.tga` and clouded by `sky.tga`, a band round the
  horizon from `sky_cyl.tga`, fog below ([sky.md](systems/sky.md)). A
  recording looking up at each world's sky would settle it.

- **How the park's ground is painted** from its textures and masks:
  each layer's two textures mixed by its blend mask, the middle and top
  layers laid over the base by a cell's bytes 2 and 3
  ([lnd.md](formats/lnd.md#behaviour)). The ground looks like the
  recordings', but a close recording of one patch of ground would
  settle the recipe.

- **How long the opening's splash stays**: 3 seconds, unless a key or a
  click skips it; the recording shows it for 0.6 s, probably clicked
  past ([opening.md](systems/opening.md#open-questions)). A recording of
  the opening left alone would settle it, and whether the logo movie
  stops with its sound.
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

- **Which `.tgq` is the EA GAMES logo** (November 2026): both are, being
  the same file, byte for byte ([tgq.md](formats/tgq.md#overview)).
