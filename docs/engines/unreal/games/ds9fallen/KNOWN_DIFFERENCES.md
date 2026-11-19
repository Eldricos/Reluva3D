# The Fallen: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **Large cylinder lights light without their shadow bits.** The
  original's renderer gives every light in a surface's lightmap list its
  shadow bits, so with these files its code shows Hass'terral's snowfield
  (M10_SiskoL1A) mostly in the shadow of cliffs and buildings. The
  recording shows the whole field evenly lit; it is of a later build,
  whose map was likely lit again. So Reluva lights cylinder lights of
  radius 1000 and more (the outdoor maps' sky lights) without their
  shadow bits, a rule fitted to the recording, until this build's own
  footage or the later build's map says which
  ([lighting.md](systems/lighting.md)).
- **The recording's later build.** Where the recording differs from these
  files only by being a later build (its end credits' "Thanks to" block,
  which `DS9_Credits` here doesn't have), Reluva follows its own scripts
  ([research/playthrough.md](research/playthrough.md)).

## Bugs fixed

None yet. The game's own script mistakes (a `goto` to a label that isn't
there, dividing by 0) are reported and carried on from, not fixed
([level.md](systems/level.md)).

## Not yet as the original

- **Settings aren't written.** A console command's `set` (the options
  windows) changes a setting for as long as the VM runs; the `.ini` files
  aren't written ([script-vm.md](systems/script-vm.md)).
- **Saved games:** not restored on loading are the IK targets, bones'
  adjustments and attachments in the mesh states (the scripts set them
  again each tick), the probe masks, and the dialogue's lines under way;
  not written on saving are the dialogue's lines under way, the IK
  targets and bone adjustments of new mesh states, and the Level's
  model and reach list renumbered ([saves.md](systems/saves.md),
  [save.md](formats/save.md)).
- **The player's own swimming** is Reluva's, falling once out of the
  water ([movement.md](systems/movement.md)).
- **The AI's paths:** jumping and dropping reaches aren't done
  ([ai.md](systems/ai.md)).
- **Mission 12** has no dialogue table in any copy of the game: its
  voices are in `m12_Dialogue.uax`, their text nowhere
  ([dialogue-tables.md](formats/dialogue-tables.md)).

## Additions outside the game

- **The data viewer** (`reluva-viewer`): the Maps, Meshes, Textures,
  Sounds, Movies and Packages pages. In the Maps page, V lets the camera
  loose while playing and P turns the scripts off. Developer builds keep
  the viewer's keys in the game.
- **Reluva's own older save format** still loads
  ([saves.md](systems/saves.md)).
- **`reluva-tool unreal`** commands for the game's files and levels
  ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)), and
  `RELUVA_BENCH` for timing a map's frames ([README.md](README.md#speed)).
