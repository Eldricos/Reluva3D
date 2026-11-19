# And Then There Were None: chapters, acts, places and the game's start

> **Status:** done · **Code:** `src/engines/awe/attwn/acts.cpp` (this game's), `src/engines/awe/game/story.cpp`, `src/engines/awe/game/scene_page.cpp` (shared), `src/engines/awe/games.h` (`GameFlow`) · **Checked against:** the code (`ac.exe`); every act started in Reluva (`awe game-run`) · **Confidence:** from the code unless marked

## Overview

Orient Express's acts
([../../motoe/systems/acts.md](../../motoe/systems/acts.md)) come from one
table and each loads a location. This game's come from a table per
chapter, `Text/Chapter01.txt` to `Chapter10.txt`, and have no location of
their own: an act's start script shows its first scene, and the scene's
name says where it is.

## How it works

### The chapter tables

`Act, Event, Time of Day, Weather, Characters in Act, Completion
Conditions, Event, Music, What to do`, tab separated, a header row. The
act start (`0x402be0`) opens `text\Chapter<NN>.txt` for the act's number,
finds its row (its name read as Orient Express's: the number, then the
letter), and takes the cells by `strtok` (so an empty cell would be
skipped; none is):

| Column | Read as |
|---|---|
| Act | `1A`, `10b` |
| Event | the start script (`0x4859d0`), its quotes off |
| Time of Day | a number (`0x485f16`), taking effect when a scene is next left (`0x403420`), as Orient Express's |
| Weather | a number (`0x485f14`), likewise |
| Characters in Act | names (`0x485ad0`, their count `0x485bd0`); their models and `Dialogue/General` tables are loaded (`0x4030c0`) |
| Completion Conditions | a condition (`0x485bd2`), tested when the main runner runs dry, as Orient Express's (`0x40b51f`) |
| Event | run when it holds (`0x485cd2`) |
| Music | `sound\music\<cell>` (`0x485dd2`): see below |

The act's dialogue table, `dialogue\acts\act<NN><x>.txt`, is loaded too.

### `act <act> [0]`

The act changes (`0x40b5fd`) and its row is read (`0x402be0`), which
empties the dialogue manager and gives it Narracott's table and the act's
(`0x4174b0`). Then a loading step (`0x416370`, run each update by
`0x4164f0`, drawn by `0x416390`):

1. The screen goes black at once and fades in (the scripts' `fade in`:
   8 levels every 0.01 s) on `interface\loading.jpg`; meanwhile the
   inventory panel is shut, a new item put in the bag (`0x417f70`).
2. A character of the act's a step (`0x4030c0`): a model not loaded
   (`people\<name>\<name>_model.gr2`) and its `dialogue\general\<name>.txt`
   table; once all are, the models of the ten who aren't in the act are
   let go and their objects hidden (`0x403270`). A saved game being
   loaded takes a step more (`0x414310`).
3. It fades out to black, and then the start script is queued on the
   main runner, unless the third word is `0` (`0x40b644`), and the `act`
   event is over (`0x4165e2`); for a saved game, its `load` event is.

Over the picture, all white and centred across: the interface text
`loading` ("LOADING") in font 2 with its top at y 225, `wait` ("Please
Wait") in font 8 at 305, and how far the characters have got, `%d%%`, in
font 8 at 345, placed by the width of "50%" so it doesn't move; none of
the three when the act has no characters.

### The game's start

`movie adventure, movie awe, movie agathalogo, movie soundblaster`, then
`scene mainmenu01` (`0x40279b`): the main menu, `Interface/Scenes`.

### Places and scenes

A scene's place is its name up to its first `_`, less its last two
characters (`0x403310`): `kitchen01_closeup02` is in `kitchen`
(a cutscene's, `[0x485f04]` set, keeps them). A place's scenes are
loaded together (`0x4035d0`): those of
`Locations\<place>\Act<NN><x>\*.ac1` for the act, then those of
`Locations\<place>\Base\*.ac1` it hasn't already. A new act drops the
current place's scenes first (`0x403540`), so its versions come in. An
act's version names its base (`BASE`,
[../formats/ac1.md](../formats/ac1.md)).

### Cutscenes and title cards

`cutscene <name>` is `scene` with `[0x485f04]` set (`0x40ddd9`): its
place keeps its two last characters (`chapter01_title` is in
`chapter01`) and is looked for in `Cutscenes` (`0x4035fc`):
`Cutscenes\Chapter01\Act01a\Chapter01_title.ac1`. A cutscene is a
still picture scene; its scripts show its pictures (`fade in
chapter01title`: the chapter's title card over it), say its lines and go
on with `scene` or `cutscene`.

### Objects hidden everywhere

`globalhide <name>` sets the tag `hide_<name>` and `globalshow` takes it
away (`0x40ba9a`); an object whose file's name (less its extension), or
that name up to its `_`, has such a tag is hidden in every scene
(`0x410558`). Orient Express's code has the same (`0x40e9d6`) but no
script uses it; this game's 5A does.

### Music

`musica <name>` plays `sound\<name>.wav`, looping (`0x40c42a`; Orient
Express: `sound\music\<name>.mp3`). A scene's music entries play as Orient
Express's; one whose file has `music` in its name stops the act's music,
and when none has, the act's music plays, looping (`0x42d742`).

## In Reluva

`GameFlow` (`games.h`) holds the start script, the scenes' extension, the
acts' reader, how a scene's place is found and whether the act folders
are used, the act's music and the `musica` path; Orient Express's are the
defaults, this game's in `attwn/attwn.cpp` and `attwn/acts.cpp`. A row
with no place loads none (`Story::startAct`). Reluva reads a scene as
it's shown, the act's version over its base, so a place's scenes aren't
loaded together; where characters stood is kept as for Orient Express.
`GameFlow::actLoading` names the loading step's picture, fonts and rows:
the scene page's `act` waits through it (`updateActLoad`), a character
a step, and the dialogue's tables are the act's, its characters' and the
places met since it began (`DialogueTables::characters`, `places`).
Reluva loads every character's model when a scene shows it, so none are
let go.

## Sources and checks

- **Where the facts come from:** `ac.exe`'s act start (`0x402be0`), its
  `act` verb (`0x40b5fd`), the loading steps (`0x4164f0`), the place and
  scene loaders (`0x403310`, `0x4035d0`, `0x403540`), the start
  (`0x40279b`), the music (`0x40c411`, `0x42d6c5`).
- **How it's checked:** `awe.unit.attwn` reads two chapters' rows and
  places; `awe game-run <game> -v` starts every act: each plays its start
  script to where it waits for the player, with no command not done. A
  title card was looked at in the Scenes page (`awe page-shot <game>
  scenes out.tga "run:act 1a 0, cutscene chapter01_title, fade in
  chapter01title" ms:3000`).
