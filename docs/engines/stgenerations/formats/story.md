# The story (STATES.INI)

From STG-Engine's notes on the 2 CD Windows release ([README](README.md) says how they were made). All values are little-endian.

## The story (`STELLAR.DAT:DESIGN/STATES.INI`): decoded, `src/engines/stgenerations/game/campaign.h`

sgens.exe's game manager (`0x419250`) runs this script. Each `[Section]` is a
list of commands, one a line, with `;` starting a comment; a line can start
with conditions (`IfFlag n`, `IfNotFlag n`, `IfActionGamePlayed n`,
`IfSunsDestroyed n`, `IfEnterpriseAt place`, `IfClue`, `IfObjectFlag`) that
must all hold. `[Start]` shows the title and jumps to `[NewGame]`.

| Command | Effect |
|---------|--------|
| `Jump S`, `JumpRandom S1 S2 ...`, `Call S`, `Pop` | go to a section |
| `Intro` | INTRO1.BMP for 5 seconds |
| `Movie n` | `MOVIE/MOV_nn.AVI` |
| `ActionGame n Won Lost` | the away mission whose location type is n (the second column of the location table), then `Jump Won` or `Jump Lost` by whether its objectives were met |
| `SpaceCombat n Won Lost` | space combat; a result of 2 jumps to `[EnterpriseDestroyed]` |
| `ScrollText n` | `OPTIONS.DAT:TEXTn.TXT` (see below) |
| `Options` | the menu, with no game to return to: the game is over |
| `EndGame` | the credits, then quit |
| `AutoSave` | save to the automatic slot |
| `OnTransport S` / `OnTactical S` | on the bridge, go to S when the player beams down / engages; otherwise the section goes on |
| `SetFlag n TRUE/FALSE`, `SetActionGamePlayed n`, `Destroy place`, `SetObjectFlag` | state for the conditions |

Everything else (`ShowStellar`, `Move`, `WarpFleetIn`, `QueAudio`, `QueMusic`,
`Wait`, `SDTimer`, `Scanner`, ...) drives the bridge. Without it, this engine
takes `OnTransport` at once and never `OnTactical`.

`TEXTn.TXT` is the background picture, the music (in MUSIC.1/2), the font,
then the text, centred by hand with spaces. The credits are a table in the
exe at `0x5401CC`: records of a heading, a name and a style (0 a blank line, 1
more names follow, 2 the last, 3 a section title), with music from
`Credits.WAV` on (`0x540198`).
