# Need for Speed II SE: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **Where it writes.** What the game writes (`config.dat`,
  `TmpTrk/`, its replay) goes to Reluva's save folder for the game,
  never into the install, which is left untouched ([PLAN.md](PLAN.md), Phase 6).
- **The CD.** The tracks, music and movies are read from a copy of the
  CD's files (in the install, beside it, or at `cdpath=`); no disc is
  needed ([README.md](README.md#using-it)).
- **Ctrl+F5** goes back to the launcher.
- **Network play** uses the machine's network: IPX carried over UDP, its
  searches on the local network or to the machines `broadcast=` names, and the
  serial link and the modem each as one TCP connection between two
  machines (`serial=`, `modem=`;
  [systems/network.md](systems/network.md#in-reluva)).
- **The 3dfx program** is told it has a Voodoo Graphics, the board it was
  made for, unless `glideboard=` names another
  ([systems/host.md](systems/host.md#in-reluva)).
- **The registry** has DirectX 5.2's key (`Software\Microsoft\DirectX`,
  `Version` "4.05.01.1998"), as Windows 98 came with, so `dsetup.dll`
  reads the version from it
  ([research/hosted-spike.md](research/hosted-spike.md)).
- **File reads take a disk's time** on the host's clock of instructions:
  2,000 bytes a millisecond (`GameHost::Options::diskBytesPerMs`), so a
  race's load passes the program's race timer check as on a machine of
  the time. Only in `reluva-tool nfs host`'s deterministic runs; the
  game played on the real clock reads at the machine's speed
  ([systems/host.md](systems/host.md#in-reluva),
  [research/road-driver.md](research/road-driver.md)).

## Bugs fixed

None yet.

## Not yet as the original

None yet.

## Additions outside the game

- **The data viewer's pages**, in `reluva-viewer`: besides Tracks and
  Cars, the Status page (what was found of the game), and its pictures
  (Textures), strings, fonts, sounds, music and movies.
- **The data viewer's Tracks and Cars pages** draw the tracks and models
  more plainly than the game: both faces of every quad; the tracks with
  the 3dfx program's sky, fog and shades, or (G) the software program's
  pictures, sky and depth cueing, its colours not snapped to the
  palette,
  the painted lines and placed sounds as the game has them, but the
  sounds' frame count at 30 a second; the
  cars lit from one side, their paint the viewer's pick of
  each car's own colours, and their
  wheels' and brake lights' frames chosen as the game does for a speed
  and a brake the viewer sets, the wheels' count stepping 30 times a
  second ([trk.md](formats/trk.md), [hrz.md](formats/hrz.md),
  [geo.md](formats/geo.md), In Reluva).
- **`reluva-tool nfs host -roaddrive`** drives the hosted game's car
  along the virtual road, for tests and network races without a player
  ([research/road-driver.md](research/road-driver.md)).
- **`reluva-tool nfs track-sounds`** mixes a track's placed sounds, as
  heard at a road point, into a WAV file.
- `reluva-tool nfs` commands for studying the game's files and running
  the game without a window ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)).
