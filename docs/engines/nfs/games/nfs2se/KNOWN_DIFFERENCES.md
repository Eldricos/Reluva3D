# Need for Speed II SE: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

None yet.

## Bugs fixed

None yet.

## Not yet as the original

- **The registry has no DirectX key** (`Software\Microsoft\DirectX`):
  the game asks `dsetup.dll` the DirectX version, finds none, and goes
  on. What it would do with one isn't known
  ([research/hosted-spike.md](research/hosted-spike.md)).

## Additions outside the game

- **The data viewer's Tracks and Cars pages** draw the tracks and models
  more plainly than the game: both faces of every quad; the tracks with
  the 3dfx program's sky, fog and shades, not the software program's,
  the painted lines near or far a block at a time, and the placed
  sounds' one loop the nearest; the
  cars lit from one side, their paint the viewer's pick of
  `CARHSVT.TXT`'s colours without `DELTA.SJH`'s changes, and their
  wheels' and brake lights' frames chosen as the game does for a speed
  and a brake the viewer sets, the wheels' count stepping 30 times a
  second ([trk.md](formats/trk.md), [hrz.md](formats/hrz.md),
  [geo.md](formats/geo.md), In Reluva).
