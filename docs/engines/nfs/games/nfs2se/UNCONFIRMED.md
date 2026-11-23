# Need for Speed II SE: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

None yet.

## Seen running, not traced in the code

None yet.

## Inferred

- **An install with only `nfs2sea.exe`**, the program for 3dfx cards,
  is recognised as the game (`fallbackTable`, `src/engines/nfs/games.cpp`).
  The CD's set-up copies the programs on a condition, which by its
  script's strings is the display card: it looks through the PCI display
  devices and copies 3dfx's `glide2x.dll` for a Voodoo Rush or another
  Voodoo, and the install studied, made without one, has every icon but
  only `nfs2sen.exe` ([research/first-survey.md](research/first-survey.md),
  The set-up). Whether a 3dfx install also has `nfs2sen.exe` isn't known:
  an install made on a machine with a Voodoo card would settle it.
- **Pictures** ([fsh.md](../../formats/fsh.md), NFS II SE): an 8-bit
  picture without a palette uses its bank's palette entry; 4-bit
  pictures are the low half of each byte first; `0x29` is a 16-bit 5:6:5
  palette. Each from how the pictures look; the game's code would settle
  them.
- **The pause screens' palette** is the race's, `hud.fsh`'s: the 3dfx
  copies' colours agree with it ([fsh.md](../../formats/fsh.md), NFS II
  SE's pause screens). The software game, paused, would settle it.
- **The 3dfx program's board**: a Voodoo Graphics, by its date (1997,
  before the Voodoo2). It passes its vertices snapped (`x + 3 << 18`,
  never taken off again) and so relies on the board reading only their
  low bits, which Reluva's Glide does for the Voodoo Graphics and Rush
  (`src/graphics/glide/glide2.cpp`, `Glide2::convert`); on a Voodoo2
  every triangle would be off the screen. The program on a real board
  would settle it.
- **The front end's text records** ([text.md](../../formats/text.md),
  NFS II SE's front end): their first byte is the font's size, and two
  of their words the string's place on the screen. The front end drawn
  by the game would settle it; the other three bytes aren't worked out.

- **`demo.0`-`demo.2`** on the CD (three copies of one file) are used by
  neither program: neither names them, nor builds such a name, and the
  attract mode plays the `demoav*.dct` movies
  ([formats/dct.md](formats/dct.md)). A leftover of the game's making,
  perhaps; the game's code read through would settle it.

## Guessed

- **The CD's volume label**: the host gives both drives `NFS2SE`
  (`GameHost::Options::driveLabel`). The real CD's label wasn't read
  (a copy of its files keeps none); the program asks it
  (`GetVolumeInformationA`), but its game-files records are the same
  with NFS III's `NFS3`.
  The CD itself would settle it.

## Settled

- **The racing cars' green bodies are the paint** (November 2026, from
  the code): the program recolours the car palette's greens, by their
  shade, to the chosen colour as it sets up the car (0x444f80;
  [geo.md](formats/geo.md), The paint).
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
