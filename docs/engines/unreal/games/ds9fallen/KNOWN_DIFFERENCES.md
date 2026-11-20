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
- **Settings are written to Reluva's folder for the game** (`Config`),
  not into the game's `System` folder as the original writes them, so
  the install is left as it is ([script-vm.md](systems/script-vm.md)).
- **The recording's later build.** Where the recording differs from these
  files only by being a later build (its end credits' "Thanks to" block,
  which `DS9_Credits` here doesn't have), Reluva follows its own scripts
  ([research/playthrough.md](research/playthrough.md)).

## Bugs fixed

None yet. The game's own script mistakes (a `goto` to a label that isn't
there, dividing by 0) are reported and carried on from, not fixed
([level.md](systems/level.md)).

## Not yet as the original

- **Saved games:** the names and imports aren't trimmed to what's used,
  so a save's tables are larger than the original's
  ([save.md](formats/save.md)).
- **The AI's paths:** jumping and dropping reaches aren't done
  ([ai.md](systems/ai.md)).
- **The player's walk** ([movement.md](systems/movement.md)): a
  `CTVE_NavigationAssist` it touches doesn't pull it onto its line or
  keep walls from being told `HitWall`; the step a walk last went up
  isn't remembered for the next frame's floor test (Engine.dll keeps
  its BSP node, `0x1054aa40`); the two view heights the engine eases
  over a step (unnamed, at `0xc10` and `0xbfc`) aren't eased; and a walk
  whose physics a wall's `HitWall` changed ends there, where the
  engine finishes the move and the floor test.
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
