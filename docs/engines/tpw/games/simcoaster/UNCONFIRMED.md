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

- **An attraction's shown name is the pair of `OBJECT_NAMES` whose
  old name is its settings'**, whole or by its first word
  ([building.md](systems/building.md#their-names)). Where the game finds
  the pair would settle it.

- **A new park of Land of Invention starts as the prebuilt park**: its
  path, queue and objects, the shape of the recording's at 2:34
  ([paths.md](systems/paths.md#where-path-can-go)); the other worlds
  with their attribute maps' path. A new park in another world, seen
  running, would settle it.

- **A sprite's pixel is a tenth of a unit**: the recording's visitors
  half a path's cell high, and the settings' umbrellas over each kind
  of visitor ([sprites.md](formats/sprites.md#how-large)). The game's
  own scale, if a file gives it, would settle it.

- **`.FPC` sheets are the first-person view's**: the same frames as the
  `.TPC`'s, seen from lower down, beside only those walking about
  ([sprites.md](formats/sprites.md)). The first-person view, once it
  runs, would settle it.

- **The music banks' entries without headers are never played**: the
  last 40 and 15 of two worlds' `MusicHD.sdt` point at no header, and
  Reluva lists them as not readable ([sdt.md](formats/sdt.md#what-doesnt-decode)).
  Playing those worlds' music in the original would settle it.
- **The logo movie is shown twice as wide as its frames**, 320 × 480 at
  640 × 480, as its ring is round in the recording
  ([tgq.md](formats/tgq.md#the-logo-tgq)).
- **A day lasts 4 seconds, from 1 January 2000**: the recording's
  dates read every few seconds ([park.md](systems/park.md#time)). A
  recording at another speed would settle it.

- **How visitors arrive**: as many as the park's points pay for, one
  every `TimeBetweenArrivals` seconds, walking from the bus stop through
  a booth ([park.md](systems/park.md#visitors)). Counts of visitors
  set beside a park's building, over a recording, would settle it.

- **The mission's boundary is measured from the entrance**: its clicks
  run from (0, 0) to (−50, 66) ([park.md](systems/park.md#what-a-new-park-has)).
  Drawing it over the world, beside the recording's park fence, would
  settle it.

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

- **The prebuilt park's objects are past their building as it
  opens**: their scripts run 20 seconds on at once
  ([building.md](systems/building.md#running)). The recording's robot
  and fountain, already standing at 2:35, fit it.

- **How visitors choose, walk and ride**: what they make for, scored
  from the settings' weights and needs; how fast their needs grow; how
  long they stay; three to a queue's cell; off at the exit and straight
  to the nearest path; a park's points with its attractions' values
  ([visitors.md](systems/visitors.md), [park.md](systems/park.md#visitors)).
  The game's own visitors, watched, would settle it.

- **Where riders are drawn**: a seat's helper as the rider's head, a
  bouncer's as their middle ([md2.md](formats/md2.md#the-nodes)). A
  recording close to a ride would settle it.

- **How a sign is lettered**: its lines centred on a canvas 512 × 256
  split into its two textures, their letters widened to the font
  description's width and stretched by the line's percent
  ([sgn.md](formats/sgn.md#lettering-it)). A recording close enough to
  read a sign would settle it.

- **The Buy window's ratings and the price while placing**: the
  settings' excitement and working life as bars, out of 100; the price
  in the money's font at half size
  ([building.md](systems/building.md#the-buy-window)). The game's own
  sums, or a closer recording, would settle it.

- **Which of the queue's two bends is which way, and how a line is
  laid**: `quebnd1` where the bend's east side faces the path; a line
  laid with the path tool from an entrance
  ([paths.md](systems/paths.md#queues)). A queue seen close in the
  running game, or a save with more pieces, would settle it.

- **How the path tool lays a length**: straight, along whichever way
  is the longer, from the last click to the next
  ([paths.md](systems/paths.md#laying-it)). Laying path in the running
  game would settle it.

- **The Finance and Maps menus' order, and when a tooltip shows**: the
  table's order, the recording never opening them; a tooltip after half
  a second's rest, a little left of the pointer and above it
  ([park.md](systems/park.md#menus-and-tooltips)). A recording of them
  would settle it.

- **How fast a visitor walks, and steps**: a child 12 units a second,
  the others at their type's `SpeedMultiplier`; a walking frame a unit
  walked ([sprites.md](formats/sprites.md#in-reluva)). A recording
  closer in, with the camera still, would settle it.

- **How the front end's particles fly and look**: 20 a burst, small
  stars, up to 6 units a second; emitters 11 to 14 the `Fx` helpers, 1
  the well's middle ([front-end.md](systems/front-end.md#the-effects)).
  Decoding the library's other parameters would settle it.

- **How an effect picks its sound**: a group by the shares, a sound in
  it by a roll, at a volume between the group's two; the links not
  followed ([sound-maps.md](formats/sound-maps.md#the-effects-cat_sfxmap)).
  The game's own choices, heard over many plays, would settle it.

- **The front end's sounds come late in the recording because its
  computer slowed**: the script's waits run at half speed while the
  sparkles fly, the camera's path at its own
  ([front-end.md](systems/front-end.md#the-sounds)). A recording on a
  faster computer would settle it.

- **The front end's advisor speaks at the recording's times**: his
  three lines, found in the recording, and the portrait about them;
  the Personnel File opens 54 s in, after the last
  ([front-end.md](systems/front-end.md#the-music-and-the-speech)). What
  the game times them by would settle it.

- **The music's groups follow a value of the game's through their links'
  ranges**: the park's music climbs from group 1 to group 3 as its
  visitors grow, half their number fitting the readings there are; the
  front end's value is set to 0 on the path and 20 over the globe, as
  its music goes
  ([front-end.md](systems/front-end.md#the-music-and-the-speech)).

- **The Personnel File's portrait is the personnel director's**: the
  board's fifth, as `EMAIL_FROM_HR_DIR` is the fifth sender
  ([personnel-file.md](systems/personnel-file.md#what-it-shows)).

- **Where a release's Bullfrog logos go on the loading screen**: over
  the scene's QSound logo, at the bottom left
  ([loading-screen.md](systems/loading-screen.md#the-loading-screen)).
  The US release has none; a recording of another release's loading
  screen would settle it.

- **Which animation a script's number plays**: 0 `c`, 5 `m`, 9 `b`, 10
  `r` from the scripts' own use; 2 `i`, 3 `l`, 4 `s`, 6 `e`, 7 `u` and 11
  `o` from which files are there; a variant v the file numbered v + 1
  ([md2-animation.md](formats/md2-animation.md#which-animation-is-which)).
  A ride seen building, breaking and running in the game would settle
  them.
- **The front end's camera**: its field of view, 53° high, measured on
  the recording; the effects of its script's events
  ([front-end.md](systems/front-end.md)). The view drawn by the game at
  a known size would settle the first.

- **How the script machine runs** beyond what the sources show: turns
  ending after the header's 50 instructions, critical sections not
  keeping the turn ([scripts.md](systems/scripts.md)); what the game's
  instructions do, from how the scripts use them
  ([script-instructions.md](systems/script-instructions.md)). Rides seen
  running in the game, set beside a script's trace, would settle them.

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
