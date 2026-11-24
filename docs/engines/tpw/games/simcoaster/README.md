# SimCoaster

Bullfrog's theme park game of 2001, published by Electronic Arts: build
parks in three themed worlds (Land of Invention, Polar Zone, Arabian
Nights)
around roller coasters of the player's own design, meet the challenges
of its missions, and keep the guests happy. Released in Europe as Theme
Park Inc. Game id `simcoaster`, on the
[Theme Park World engine](../../README.md); its code is in
`src/engines/tpw/`.

## Releases

| Release | Recognised by | State |
|---|---|---|
| US, English, Windows (data version `Beta_21`) | `Data/ui.wad` (MD5 `f8caf97175f18ef3675b3609b03413f4`, 2,817,221 bytes) and `Data/Language/American/OBJECT_NAMES.str` (MD5 `04d5bf6ef8a6d85f15a25a33716fd895`, 10,200 bytes), with `Game.exe` | Recognised; plays its opening, then its loading screen |
| Any other (Theme Park Inc, other languages) | `Data/ui.wad`, `Data/Challenges.sam` and `Game.exe` by name | Shown as "unknown release"; not checked |

An installed folder is what's recognised: it holds every file the game
reads, speech included. The CD can be imported instead, laid out as its
setup installs it ([cd.md](formats/cd.md)).

## Where it stands

| Part | State |
|---|---|
| Detection | The US release by its data files' hashes; other releases by file names, as "unknown release" |
| Importing | From the CD: its InstallShield 6 cabinet and its language's folders, as setup installs them; checked byte for byte against an install ([cd.md](formats/cd.md)) |
| Archives | done: `.wad` ([wad.md](formats/wad.md)); all 303 archives' 15,007 files read; the `Data` folder mounted, archives and loose files together; `reluva-tool tpw wad-list`, `wad-extract` and `data-check` |
| Settings | done: `.sam` ([sam.md](formats/sam.md)); all 369 files' 15,825 settings read; `reluva-tool tpw sam-check` |
| Text | done: string tables (`.str`) and the character set ([str.md](formats/str.md)); all 37 tables' 3,775 strings read; `reluva-tool tpw str-check` |
| Fonts | `.bf4` ([bf4.md](formats/bf4.md)): all 35 fonts' 8,750 glyphs read, but for the postcard font's 160 of a storage not worked out; `reluva-tool tpw bf4-check` and `bf4-show` |
| Pictures | done: `.fsh` banks (compressed pictures among them), `.tga`, `.png` and `.jpg` ([fsh.md](formats/fsh.md)); all 10,344 decoded; `reluva-tool tpw picture-check` and `picture-dump` |
| Movies | done: the EA GAMES logo (`.tgq`, TQI video, [tgq.md](formats/tgq.md)), decoded and played; `reluva-tool tpw tgq-check` and `movie-check` |
| The opening | the logo, then the Bullfrog splash, as a recording shows them ([opening.md](systems/opening.md)) |
| The front end | its 3D opening, the camera's path through the hall and boardroom to the globe, drawn from the Intro World's camera path model as the recording shows it ([front-end.md](systems/front-end.md)); then the Personnel File drawn from the interface's flat models, its fonts and strings, as the recording shows it, with a new player's record from the settings ([personnel-file.md](systems/personnel-file.md), [ui-models.md](formats/ui-models.md)), over the globe turning, its players kept in the game's save folder ([players.md](systems/players.md)); then choosing a world on the globe, the camera flown down to its gate ([globe.md](systems/globe.md)); its effects, the beam and glow as the model draws them and the sparkles its script starts from the particle library ([plb.md](formats/plb.md)), the particles' look guessed, and the sounds it starts; the advisor's portrait, his three lines and the skip button; the Intro World's music; `reluva-tool tpw front-end-shot`, `personnel-shot`, `globe-shot`, `front-end-run` |
| The loading screen | put together from `Data/Init/` as the recording shows it, pixel for pixel ([loading-screen.md](systems/loading-screen.md)), while the park loads; `reluva-tool tpw loading-screen` |
| The park | a new park set up from the settings and its mission, its world drawn under a camera that moves, its money and date running, visitors arriving, drawn by their sprites, under its HUD, its compass turning and zooming the camera, its bottom buttons' menus opening ([park.md](systems/park.md)); path laid with the path tool ([paths.md](systems/paths.md)); the mission's briefing over it ([briefing.md](systems/briefing.md)); `reluva-tool tpw park-shot`, `briefing-shot`, `park-run` |
| Sounds | the banks (`.sdt`, [sdt.md](formats/sdt.md)): 5,315 of their sounds decode, MPEG and PCM; the rest are damaged in the game's own files; `reluva-tool tpw sdt-check`. Their categories (`.map`, [sound-maps.md](formats/sound-maps.md)): every effect's sounds found in the banks, played by number as scripts start them; `reluva-tool tpw sfx-check` |
| Sprites | `.TPC`, `.FPC` and `.ESP` ([sprites.md](formats/sprites.md)): all 71 sheets' 7,893 frames and 45 animation sets read; the visitors drawn by them; `reluva-tool tpw sprite-check` |
| Models | `.MD2` ([md2.md](formats/md2.md)): all 698 models' 7,262 meshes read, placed by their node trees; their 1,209 animations' moves of whole parts and of vertices read and played ([md2-animation.md](formats/md2-animation.md)), not their moves of textures; `reluva-tool tpw md2-check`, `anim-check` |
| Height maps | `.hmp` ([hmp.md](formats/hmp.md)): all 411 read: a model's height over each part of its ground; `reluva-tool tpw hmp-check` |
| Terrain | Each world's surroundings (`base.MD2`), its park's ground (`.lnd`, [lnd.md](formats/lnd.md)) and attribute map ([attribute-map.md](formats/attribute-map.md)) read, with its placed objects (`scape.omp`, [omp.md](formats/omp.md)), sound and particle emitters it seems; the ground painted by a guess; `reluva-tool tpw terrain-check`, `world-shot` |
| Skies | Each world's sky pictures, fog colour and light read ([sky.md](systems/sky.md)); drawn behind its terrain by a guess |
| Scripts | The rides' and shops' scripts (`.RSE`, [rse.md](formats/rse.md)): all 288 read and written out as source, in the language of the sources the game ships, and run on a machine ([scripts.md](systems/scripts.md)) against a stand-in for the park, every instruction they use worked out ([script-instructions.md](systems/script-instructions.md)); `reluva-tool tpw rse-check`, `rse-dis`, `rse-run` |
| Viewer | `reluva-viewer`: pictures, fonts, text, settings, models and worlds in 3D, scripts, sounds, movies and files (pages above) |
| The executable | Surveyed: SafeDisc 2, its code encrypted, so the game is to be rebuilt from its data, not run ([executable.md](formats/executable.md)) |
| Everything else | Not started: see [PLAN.md](PLAN.md) |

## Using it

In the launcher, *Import Game...* and choose the CD (or a copy of its
files): Reluva copies what the game needs into a folder of its own, as
its setup would install it. A folder the game's own setup installed can
be added instead (*Add Game...*). Reluva plays the game's opening (a
key or a click skips each part), then shows its loading screen for now;
Esc leaves it.

The data viewer, `reluva-viewer` (`reluva-viewer <its name in the
launcher>` or `reluva-viewer <the game's folder>`), shows what the game's
files hold, at 640 × 480. Tab and Shift+Tab move between its pages, Esc
leaves it. On every page Left and Right go to the previous and next
thing, PageUp and PageDown 10 at a time, Home and End to the first and
last; each page has its own keys besides:

| Page | Shows | Its own keys |
|---|---|---|
| Pictures | Every `.fsh`, `.tga`, `.png` and `.jpg`, zoomed to fit, over a check where it's clear | Z: its own size |
| Fonts | Sample text in the font, then each glyph with its code | |
| Text | A string table's strings by number, the one picked whole below | Up/Down: the string; F: in the game's font (`GAME12.bf4`) |
| Settings | A settings file's settings, values and grids | Up/Down: scroll |
| Models | Each model drawn in 3D with its textures, lit from above, turned about its middle; its animations played, by hand or by its script | A/D: turn; W/S: tilt; Z/X: nearer, further; T: textures on or off; H: its height map over it; N: play its next animation; R: run it by its script |
| Worlds | Each world's terrain in 3D: the land around the park and the park's ground, before anything is built, its placed objects as markers, under its sky | A/D: turn; W/S: tilt; Z/X: nearer, further; T: textures on or off; G: the ground on or off; O: the placed objects on or off; K: the sky on or off |
| Scripts | Each ride and shop script as source, or run with guests coming, its variables and its calls to the game shown | Up/Down: scroll; PageUp/PageDown: a screen; R: run it |
| Sounds | A bank's sounds, their kind and length | Up/Down, PageUp/PageDown: the sound; Space or Enter: play or stop |
| Movies | The logo and the training video | Space or Enter: play (a key or a click stops it) |
| Files | Every file of `Data/`, where it is, its start as text or bytes | Up/Down, PageUp/PageDown: the file |

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| `formats/` | The game's own files, a page each: [the executable](formats/executable.md), [the CD](formats/cd.md), [archives](formats/wad.md), [settings](formats/sam.md), [text](formats/str.md), [fonts](formats/bf4.md), [pictures](formats/fsh.md), [movies](formats/tgq.md), [the particle library](formats/plb.md), [sound banks](formats/sdt.md), [sound categories](formats/sound-maps.md), [sprites](formats/sprites.md), [models](formats/md2.md), [their animations](formats/md2-animation.md), [height maps](formats/hmp.md), [the park's ground](formats/lnd.md), [attribute maps](formats/attribute-map.md), [placed objects](formats/omp.md), [scripts](formats/rse.md) |
| `systems/` | How the game behaves, a page per part: [the opening](systems/opening.md), [the front end](systems/front-end.md), [the Personnel File](systems/personnel-file.md), [keeping the players](systems/players.md), [the globe](systems/globe.md), [the sky](systems/sky.md), [how scripts run](systems/scripts.md), [their instructions](systems/script-instructions.md), [the loading screen](systems/loading-screen.md), [the park](systems/park.md), [its paths](systems/paths.md), [the briefing](systems/briefing.md) |
| `research/` | Working notes: [what a recording of the game shows](research/footage.md) |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Terms

- **World:** one of the game's three themed lands, each with its own
  rides, shops and scenery: Land of Invention, Polar Zone and Arabian
  Nights (`science`, `water` and `arabian` in `Data/levels/`), and the
  Intro World (`wIntro`) ([research/footage.md](research/footage.md#the-worlds-names-and-folders)).
- **Challenge:** a goal set by the game's advisor, from
  `Data/Challenges.sam`.
