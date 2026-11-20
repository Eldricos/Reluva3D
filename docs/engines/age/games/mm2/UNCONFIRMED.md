# Midtown Madness 2: unconfirmed items

What Reluva does that hasn't been checked against the running original.
The game's program can't be read (it's encrypted,
[REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-program)), so nothing
here is "from the code": the files give the numbers, and recordings
([research/footage.md](research/footage.md)) what can be seen.

## Read from the code, not seen running

None: the game's code isn't read.

## Seen running, not traced in the code

- **The HUD's layout**, the damage meter, the map's size and the race
  messages' places: measured on public videos
  ([hud.md](systems/hud.md)). Would be settled by the game's code, which
  can't be read; they match the recordings.
- **The drawbridges' timing and the music's changes**, looked at in the
  videos ([research/footage.md](research/footage.md#bridges-and-music-looked-at-again)).

## Inferred

- **The car simulation**: what each `.vehCarSim` number does is Reluva's
  model, checked against the game's figures by how the cars rank: top
  speed and the quarter mile follow them, 0-60 mph much less, as grip
  holds nearly every car below 60
  ([car-simulation.md](systems/car-simulation.md#against-the-games-figures)).
  A recording of a few cars' starts and turns on a known road would
  settle it.
- **Traffic density**: the files give a density, Reluva reads it as a
  share of a lane's capacity ([traffic.md](systems/traffic.md#how-many)).
- **Police, pedestrians and opponents**: when the police chase and bust,
  how pedestrians dive, how opponents follow their paths
  ([police.md](systems/police.md), [pedestrians.md](systems/pedestrians.md),
  [races.md](systems/races.md)).
- **What each Crash Course lesson asks**, read from its messages
  ([crash-course.md](systems/crash-course.md)).
- **Damage from a knock's impulse**, and when a car smokes
  ([damage.md](systems/damage.md)).
- **Props' strength** (`ImpulseLimit2` read as an impulse,
  [props.md](systems/props.md)).
- **Cops & Robbers' rules**: the bases and the gold, from the files
  ([cops-and-robbers.md](systems/cops-and-robbers.md)).
- **Unknown fields in the formats**: the road maps' and PSDL's open
  flags, texture flag 0x10000, two lesson columns
  ([bai.md](formats/bai.md#open-questions),
  [psdl.md](formats/psdl.md#open-questions), [tex.md](formats/tex.md),
  [race.md](formats/race.md)).
- **The archives' search order** where two hold the same name
  ([ar.md](formats/ar.md#open-questions)).
- **Cops & Robbers Settings dropped**: its pictures aren't shipped and
  the Host Race Menu has its choices, so the game likely never shows it
  ([menus.md](systems/menus.md#not-drawn)). A recording of hosting Cops &
  Robbers would settle it.

## Guessed

- **The engine sound's volumes**: Min and Max Volume read as the
  throttle closed and open ([car-sounds.md](systems/car-sounds.md)).
- **The smoke's texture** ([damage.md](systems/damage.md)).
- **The map's field of view** ([hud.md](systems/hud.md)).
- **The path sets' speeds** ([pathset.md](formats/pathset.md)).
- **The road maps' side kinds** where only where they fall says what they
  are ([bai.md](formats/bai.md)).
- **Control Warning's use**: the game ships neither its picture nor its
  words; Reluva shows it for a game controller chosen with none connected
  ([menus.md](systems/menus.md#control-warning)). A recording of the
  game's Control Options with no joystick would settle it.

## Settled

- **2026: the HUD's layout**, from public gameplay videos: the speed,
  gear and revs bottom left, the clock top middle, the race's list top
  left, the map bottom right ([research/footage.md](research/footage.md#what-they-settle)).
