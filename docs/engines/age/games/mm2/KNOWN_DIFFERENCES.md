# Midtown Madness 2: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **Multiplayer is Reluva to Reluva.** The original plays over DirectPlay
  (the MSN Gaming Zone, IPX, TCP/IP, a serial cable or a modem); Reluva
  speaks a protocol of its own over UDP, and can't join or host a game
  with the original ([network-protocol.md](systems/network-protocol.md)).
  The serial and Gaming Zone dialogs aren't drawn
  ([menus.md](systems/menus.md#not-drawn)).
- **At most 8 players** in a session, where the original allows more
  ([menus.md](systems/menus.md#not-drawn)).
- **Options in Reluva's own file.** The original keeps them in the
  Windows registry; Reluva in `options.txt` in the game's save folder
  ([options.md](systems/options.md)).
- **Drivers' records in Reluva's own files.** The game's `.plr` files
  aren't read or written
  ([record-and-unlocking.md](systems/record-and-unlocking.md)).
- **Replays are Reluva's own files**: the race run again from what was
  recorded, not the original's replay files
  ([replays.md](systems/replays.md)).
- **Passwords are proved, not sent** when joining a session
  ([network-protocol.md](systems/network-protocol.md#passwords)).

## Bugs fixed

None yet.

## Not yet as the original

- **The rules are Reluva's reading of the game's files.** The game's
  program is encrypted, so how it drives the cars, the traffic, the
  police and the opponents, how its music changes and how it judges the
  lessons are Reluva's, written to match the files' numbers and the
  recordings ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-program)).
  Each system page says what rests on what.
- **The handling** hasn't been compared with the game's yet
  ([car-simulation.md](systems/car-simulation.md)).
- **No police radio**: the game has no recordings for one, and Reluva
  plays none ([police.md](systems/police.md)).
- **Multiplayer's gaps**: no hole punching between two players both
  behind routers ([multiplayer.md](systems/multiplayer.md#not-done)).
- **Some dialogs aren't drawn** where their pictures aren't shipped:
  Cops & Robbers Settings, `xassign_dlg`, Control Warning
  ([menus.md](systems/menus.md#not-drawn)).

## Additions outside the game

- **The data viewer** (`reluva-viewer`): the Race, Drive, World, Models,
  Textures and Files pages: any car round either city, the cities flown
  through at each time and weather, every model and texture, every file
  of the archives.
- **Computer players in multiplayer** to fill a session or play alone,
  and joining a game already playing
  ([multiplayer.md](systems/multiplayer.md)).
- **San Francisco's stunt mode**, from the game's unused `accel0` files
  ([cruise-and-stunt.md](systems/cruise-and-stunt.md)).
- **`reluva-tool age`** commands for the game's files
  ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)).
