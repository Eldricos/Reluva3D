# Need for Speed II SE: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

- **The placed sounds:** the Tracks page and `reluva-tool nfs
  track-sounds` play each block's first sound over the camera's block's
  neighbourhood in its order, the first loop met on the loop voice, with
  the program's volume and pan (0x434e7d, 0x434f26, 0x434030;
  `src/engines/nfs/nfs2se/track_sounds.cpp`;
  [trk.md](formats/trk.md#extrablocks), Sounds). A recording of the
  hosted game at a road point of `TR03` to `TR07`, against
  `track-sounds` at the same point, would settle it.
- **The 3dfx program's fog and `TR04`'s tunnels:** the fog's colours and
  tables by the camera's road point, from the program's lists, and the
  black quad in `TR04`'s tunnels by the view's way (0x427690, 0x42c7a0;
  `src/engines/nfs/nfs2se/glide_sky.cpp`;
  [hrz.md](formats/hrz.md#the-3dfx-sky), [The 3dfx
  fog](formats/hrz.md#the-3dfx-fog-the-programs-own)). A frame of the
  hosted 3dfx program at a road point of `TR00`, `TR04`, `TR05` or
  `TR07` where the fog changes, against the Tracks page there, would
  settle it.
- **The software program's depth cueing** on every track but `TR00`: the
  curve, the buckets and the lists' biases, −12 and −16 on tracks 2 and
  3 near their start (0x470d10, 0x434f40;
  `src/engines/nfs/nfs2se/software_sky.cpp`;
  [hrz.md](formats/hrz.md#the-software-depth-cueing)). `TR00`'s agrees
  with the hosted game's frames; frames of the hosted software game on
  the others, `TR07` and `TR08` with their other colours among them,
  would settle it.
- **The cars' wheel and brake-light frames:** the wheels' faces by the
  car's speed and a count, the brake lights by the brake (0x45a220;
  [geo.md](formats/geo.md), The frames), as the Cars page shows them for
  the speed and brake it's given. Frames of the hosted game of a car at
  known speeds, and braking, would settle it.
- **The painted lines near and far:** each line drawn as its type's
  stripes, or as one quad when its vertex is more than 110 units deep
  (0x448a93; [trk.md](formats/trk.md#extrablocks), Painted lines). A
  frame of the hosted game on `TR02` or `TR03`, against the Tracks page
  at the same point, would settle it; their shapes are
  [inferred](#inferred).

## Seen running, not traced in the code

None yet.

## Inferred

- **The 3dfx program on other boards than the Voodoo Graphics.** It asks
  Glide nothing of the board (`NFS2SEA.EXE` 0x4920a0: only how many
  there are, and Glide's version), and passes its vertices snapped
  (`x + 3 << 18`, 84 times, never taken off), which a Voodoo Graphics or
  Rush reads by their low bits (`Glide2::convert`,
  `src/graphics/glide/glide2.cpp`); the CD has Glide only for those two.
  Whether a Voodoo2's Glide 2 takes such vertices isn't known: the
  program on a real board would settle it.
- **The painted lines' shapes:** "The shapes are as the code builds them
  *(inferred)*" ([trk.md](formats/trk.md#extrablocks), Painted lines):
  solid, broken, two solid, and solid on one side and broken on the
  other, as the Tracks page draws them. A frame of the hosted game close
  to the lines would settle it.
- **Which depth-cue list holds which objects:** "Which list holds which
  objects is *(inferred)*" ([hrz.md](formats/hrz.md#in-reluva)): the
  ground and extrablock 18's objects 0, the second group and 7's 6, the
  `.col`'s 10, 19's the fourth list's bias, as the Tracks page's
  software look takes them. Frames of the hosted software game near each
  kind of object would settle it.
- **The paint colours as hue, saturation and value:** "by how they're
  turned into RGB: 0x444af8 *(inferred)*" ([geo.md](formats/geo.md), The
  paint); the Cars page turns them so (`paintRgb`,
  `src/engines/nfs/nfs2se/formats/paint.cpp`). The F355 in its five
  colours on the Cars page against the hosted game's front end would
  settle it.
- **The unit is a metre:** "the unit looks like a metre, as the cars'
  sizes do ([geo.md](formats/geo.md)) *(inferred)*"
  ([trk.md](formats/trk.md#overview)); the F355's body 2.0 wide and 4.4
  long, the real car's 1.98 m and 4.25 m
  ([geo.md](formats/geo.md#overview)). The speeds these pages give in
  m/s, the wheels' frames' and the road driver's, read the unit so.
- **`demo.0`-`demo.2` of an earlier build:** they're in the layout of
  the game's replays, 4 bytes longer than what the program writes
  ([formats/dct.md](formats/dct.md)); that they're from an earlier build
  follows from that. That they're unused is [settled](#settled).

## Guessed

- **The CD's volume label**: the host gives both drives `NFS2SE`
  (`GameHost::Options::driveLabel`). Neither program reads it: their
  only `GetVolumeInformationA` (0x447bf5) asks for the file system's
  flags, and they find the CD as the CD-ROM drive holding their own
  program (0x471cc0). The real label is known only from the CD itself.

## Settled

- **An install for a 3dfx card** (November 2026, from the set-up's
  script, `SETUP.INS`): finding a Voodoo Graphics or Rush among the PCI
  display devices, the set-up copies `nfs2sea.exe`, and asks "The 3DFX
  version of Need For Speed II SE will be installed. Do you want to
  install the regular version also?": `nfs2sen.exe` is copied only on
  yes. Without such a card it copies only `nfs2sen.exe`. So an install
  with only `nfs2sea.exe` is one the set-up makes, as Reluva recognises
  (`fallbackTable`, `src/engines/nfs/games.cpp`).
- **The pictures' palettes** (November 2026, from the code): an 8-bit
  picture without a palette uses its bank's, 4-bit pictures are the low
  half of a byte first, `0x29` is 5:6:5, and `0x24` palettes are R, G,
  B, so the front end's emblems are gold, as the hosted game shows them
  ([fsh.md](../../formats/fsh.md), NFS II SE).
- **The pause screens' palette is the race's** (November 2026, on the
  host): the paused game's screen, pixel for pixel, is `pauseen.qfs`'s
  pictures through `hud.fsh`'s palette, the four with palettes of their
  own as well ([fsh.md](../../formats/fsh.md), NFS II SE's pause
  screens); the Textures page shows them so.
- **The front end's text records** (November 2026, from the code and on
  the host): the font's size, two colours, the alignment and shadow, and
  the place, x by the alignment and y the line's top
  ([text.md](../../formats/text.md), NFS II SE's front end).
- **`demo.0`-`demo.2` are unused** (November 2026, from the code):
  neither program opens them, nor builds a name like theirs, nor does
  the set-up script; the attract mode plays the `demoav*.dct` movies
  ([formats/dct.md](formats/dct.md)).
- **The racing cars' green bodies are the paint** (November 2026, from
  the code): the program recolours the car palette's greens, by their
  shade, to the chosen colour as it sets up the car (0x444f80;
  [geo.md](formats/geo.md), The paint). The Cars page so repainted
  shows the F355 in each of its five colours (82 to 86: red, yellow,
  black, blue, white), its shading kept. What `DELTA.SJH` holds is a
  guess from its size, and neither program reads it
  ([geo.md](formats/geo.md), The colours' files).
- **How the cars' and the tracks' polygons take their pictures**
  (November 2026, the cars' turn late November 2026; from the code and
  seen): the program gives a polygon's corners texels from (0, 0) on
  (0x46f841, 0x433bd0); a track's quads, drawn 1, 0, 3, 2 and turned by
  their texture's record, take their pictures so
  ([col.md](formats/col.md), Textures), a car's turned half round from
  that (0x462015; [geo.md](formats/geo.md), Texture corners). The
  Tracks page so shows `TR00`'s *SLOW* sign reading and its trees
  standing, and the Cars page the F355's wheel arches over its wheels.
  The beer crate reads backwards on one side either way, so it can't
  judge the turn.
- **The pages show the world as the hosted game does** (late November
  2026, on the host): mirrored left to right from how the renderer
  would draw the files' numbers, the game's world being left-handed. On
  `TR00`'s start straight the pits' building is on the left and the
  grass on the right, in the hosted game and on the page; the road
  driver held right of the road along the across vector drives on its
  right ([col.md](formats/col.md), The virtual road).
- **The 3dfx program's see-through texels** (November 2026, on the
  host): they aren't chroma-keyed. `nfs2sea.exe` uploads its track
  pictures to Glide as ARGB 1555 (`grTexDownloadMipMap`, format 11),
  each `0x07C0` texel made `0x0000`, alpha 0 and black, and draws with
  an alpha test that keeps alpha above `0x10`. The chroma key it also
  sets, `0x00FF00`, isn't what hides them: no texel it uploads is that
  green ([fsh.md](../../formats/fsh.md), NFS II SE). The viewer's pages,
  keying out `0x07C0`, show the same texels see-through.
