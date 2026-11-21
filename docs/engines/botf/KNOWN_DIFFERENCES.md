# Star Trek: Birth of the Federation: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **No CD check, no SafeDisc.** The original won't start without its CD
  and the SafeDisc driver; Reluva reads the game's files from its folder,
  and from the CD's files wherever they are
  ([systems/game-files.md](systems/game-files.md)).

- **Esc on the main menu leaves the game,** as Quit Game does.

## Bugs fixed

None yet.

## Not yet as the original

- **On the main menu,** Save Game and Retire can't be used (as in the
  game with nothing in progress).
- **A loaded game is only shown:** Load Game's Accept and Continue open
  the galaxy screen with the save's map, zoomed out or in, the empire's
  panel and the selected sector's strip (a click on the map selects
  another), but its other buttons and playing the turn are still to come;
  of later turns' maps, the stars the empire has seen but not explored,
  other empires' scanned sectors and ships, and stellar objects' pictures
  aren't drawn yet. Escape goes back to the main menu. The strip leaves out a system's growth rate and orbitals,
  the icons over its planets and star, and the people's word for races
  other than the Federation's
  ([systems/galaxy-screen.md](systems/galaxy-screen.md#the-strip)).
- **No multiplayer:** Multiplayer Setup is there, but its Connect does
  nothing: Reluva can't play over a network, modem or serial cable yet,
  nor with the original game.
- **The Hall of Fame starts empty.** The game's list after a fresh install
  holds ten games ("Yobo the Tyrant" ...), which aren't in any of its files
  but its encrypted executable, so Reluva doesn't have them. Its own list
  will fill as games are won, once it can play them
  ([systems/screens.md](systems/screens.md)).
- **Nothing is saved:** Save Game's screen is there, but as in the game
  it needs a game in progress, which there can't be yet, and nothing
  would be written.
- **Accepting an empire comes back to the main menu:** there's no game to
  start yet ([systems/screens.md](systems/screens.md)).
- **The menu's sounds** are the ones a recording shows, but where the
  game has the same sound in several entries Reluva plays the first, the
  voice volume picks among only the three lines heard, and the video
  volume's sample is scaled by the effects volume too
  ([systems/sound.md](systems/sound.md)).
- **`Stbof.ini` is edited, not rewritten:** Reluva sets the options' and
  the last setup's keys in it as the game does, but leaves the rest of the
  file as it is, where the game writes all of it out in its own order
  ([systems/game-files.md](systems/game-files.md)). The volumes set the
  music, speech, effects and the opening movie; there are no tool tips
  yet to set.

## Additions outside the game

- **The data viewer** (`reluva-viewer`): the pictures, fonts, sounds, text
  and data tables, and window layouts ([README.md](README.md#using-it)).
- **`reluva-tool botf` commands:** `alt-list` and `snd-list` list and
  extract the two archives ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)).
