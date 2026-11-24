# Need for Speed II SE: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

None yet.

## Seen running, not traced in the code

None yet.

## Inferred

- **The 3dfx program on other boards than the Voodoo Graphics.** It
  asks Glide nothing of the board (`NFS2SEA.EXE` 0x4920a0: only how many
  there are, and Glide's version), and passes its vertices snapped
  (`x + 3 << 18`, 84 times, never taken off), which a Voodoo Graphics
  or Rush reads by their low bits (`Glide2::convert`,
  `src/graphics/glide/glide2.cpp`); the CD has Glide only for those two.
  Whether a Voodoo2's Glide 2 takes such vertices isn't known: the
  program on a real board would settle it.

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
  half of a byte first, `0x29` is 5:6:5; and `0x24` palettes are R, G,
  B, not B, G, R as Reluva had read them, so the front end's emblems are
  gold, as the hosted game shows them ([fsh.md](../../formats/fsh.md),
  NFS II SE).
- **The pause screens' palette is the race's** (November 2026, on the
  host): the paused game's screen, pixel for pixel, is `pauseen.qfs`'s
  pictures through `hud.fsh`'s palette, the four with palettes of their
  own as well ([fsh.md](../../formats/fsh.md), NFS II SE's pause
  screens); the viewer now shows them so.
- **The front end's text records** (November 2026, from the code and on
  the host): the font's size, two colours, the alignment and shadow, and
  the place, x by the alignment and y the line's top
  ([text.md](../../formats/text.md), NFS II SE's front end).
- **`demo.0`-`demo.2` are unused** (November 2026, from the code):
  neither program opens them; they're recorded races of an earlier build
  ([formats/dct.md](formats/dct.md)).

- **The racing cars' green bodies are the paint** (November 2026, from
  the code): the program recolours the car palette's greens, by their
  shade, to the chosen colour as it sets up the car (0x444f80;
  [geo.md](formats/geo.md), The paint). The Cars page so repainted
  shows the F355 red with colour 2 and blue with colour 9, its shading
  kept. What `DELTA.SJH` changes is a guess from its size
  ([geo.md](formats/geo.md), The colours' file).
- **How the cars' and the tracks' polygons take their pictures**
  (November 2026, from the code and seen): a car's vertices take the
  picture's bottom left, bottom right, top right and top left
  ([geo.md](formats/geo.md)); a track's quads are turned and mirrored
  by their texture's record, unturned their vertices taking the top
  left first ([col.md](formats/col.md), Textures). The Tracks page so
  shows `TR00`'s billboards' lettering reading and its trees standing
  as the hosted game does, and the Cars page the beer crate's lettering
  reading; either way round, they don't.
- **The pages show the world the right way round**, not mirrored
  (November 2026): with the lettering reading, `TR00`'s billboards stand
  on the same side of the road as in the hosted game.
- **The 3dfx program's see-through texels** (November 2026, on the
  host): they aren't chroma-keyed. `nfs2sea.exe` uploads its track
  pictures to Glide as ARGB 1555 (`grTexDownloadMipMap`, format 11),
  each `0x07C0` texel made `0x0000`, alpha 0 and black, and draws with
  an alpha test that keeps alpha above `0x10`. The chroma key it also
  sets, `0x00FF00`, isn't what hides them: no texel it uploads is that
  green ([fsh.md](../../formats/fsh.md), NFS II SE). The viewer's pages,
  keying out `0x07C0`, show the same texels see-through.
