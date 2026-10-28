# The script runner

Everything the game does when something happens is a script: a hotspot's
script, its conditional replacements (`listB`), a scene's events, an act's
cells in `acts.txt`, the item tables' events, dialogue event files.
[text.md](text.md) gives the commands' form (verbs and words, separated
by commas or lines). This page covers how they run.

## How this was worked out

From `motoe.exe`'s runner, read in a disassembler:

- `0x409024` adds an event: it splits a command into its words and queues
  it (expanding the macros below).
- `0x409ce0` runs the event at the front of the queue. It is one long
  chain of verb comparisons, each verb's handler ending at a common tail
  (`0x40b926`).
- `0x409a80` ("event done: %s current event: %s", in its debug strings)
  ends the event under way.

`reluva-tool awe-scripts <install>` reads every command in an install
(scene scripts and their conditional scripts, scene event lists, the text
tables, the dialogue event files) and counts them by verb, with their
argument counts and examples. The numbers below are from the English
1.0 install: 9,785 commands of 61 verbs.

## Events

An event is up to five words: a verb and four arguments, 64 characters
each. The runner keeps a queue of them and runs the one at the front.
When it starts, an event's handler either:

- finishes at once (`tag`, `add`, `setchar`, `hide`...): the runner ends
  it straight away; or
- starts something that goes on (a walk, a fade, a line of dialogue, a
  `wait`), hands that thing the runner, and returns. When the thing ends,
  it tells the runner, by the verb's name, that the event is done.

"Event done" for anything but the current event's verb is ignored. Once
the event is done, the runner drops it and runs the next on its next
update. The exception is a `fade`, which is run at once, so nothing is
drawn between.

Verbs that wait have a form ending in `a` that doesn't: `walka`, `runa`,
`talka`, `fadea`, `sounda`, `anima`, `turna`, `rotatea`, `playa`. Its
handler starts the same thing and ends the event at once, so the script
goes on while the walk (or sound...) does. Only `walka`, `rotatea`,
`sounda` and `anima` are used. (`turn` and `look` never wait, `a` or
not.)

`wait <seconds>` is the runner's own: it counts down, then ends.
`setchar <name> [n]` chooses the character that later events
(`walk`, `turn`, `anim`, `look`...) act on (0x40ae94). It is found as
`0x4367b0` finds anyone by name: the n-th object (the first by default)
whose model's file has `<name>_model` in it, any case; for `poirot`,
failing that, `poirotbed_model`, so `setchar poirot` in his compartment
acts on Poirot in his bed. Without a `setchar` a script acts on the
player (0x408cd3): the first `PCXX` or `NPCX` whose `player` field
(+0xb10) is set. A `PCXX` has it from its constructor (0x4331e8), an
`NPCX` doesn't, and none in the install sets it, so the table close-ups,
where Antoinette is an `NPCX`, have no player: a click's `turn hardman
down` there turns no one, in the game as in Reluva. A line's Event and
Prevent act on its speaker (0x43c247, 0x43c577: `0x43bb60`, which looks
in the place's other scenes too, so the game may animate a speaker off
screen; Reluva, holding only the scene shown, skips those), its Povent
on the player.

## Conditions in scripts

`0x408c80`, which every script goes through (a click's, a scene's event,
an act's cells), lets a condition in brackets stand among the commands:

    dooropen sp1 L 2, scene athens.hall_03 5 0, fade in, (tag hardman06bend >3, has printhardman), tag hardmanhide

The condition (which may hold commas: the commands are joined back until
the `)`) is tested there and then, as the script is added, not when the
commands run. Until the next condition in brackets, the commands after it
are added only if it held. `(!tag failsuspects), tag winsuspects, tag
challengescore +1` in the third solution's table works the same way.

## Clicks while a script runs

The runner keeps a lock (`+8`). When an event starts it's set, unless
the verb is `walk`, `run` or `rotate`, or the event done last was an
`unlock` (`0x409d06`; `+0x10` holds the verb done last). It's cleared
when the queue runs dry (`0x409c4e`), and `lock` and `unlock` set and
clear it.

A click is refused while the lock is set. One taken while a script runs
(during the walk that starts most hotspots' scripts, or after `unlock`)
drops what was queued (`0x409760`) and runs its own. The kimono scene
uses this: `wait 6.25, unlock, wait 6.25, lock`, the woman clickable only
in between.

## As events are added

`0x409024` treats three verbs specially:

- `script <name>` adds the commands of `text\scripts\<name>.txt`, one a
  line, each added the same way (`0x409600`). Only
  `dragomiroff06b.txt` exists.
- `dooropen <point> <L|R> <n>` is a macro (`0x4091f0`): `walk <point>`,
  `rotate -<point>`, `anima doorknob<L|R>`, `turn down`, `wait .25`,
  `fade out`, `sounda dooropen<n>`, `turn home`, `animate 2`.
- `doorlocked <point> <L|R> <n>` likewise (`0x409320`): `walk <point>`,
  `rotate -<point>`, `anima doorknob<L|R>`, `turn down`, `wait .75`,
  `sounda doorlocked<n>`, `wait 1`, `turn home`.

The words are split into a buffer out of order (the third word before
the second), which is why the macro takes the side from the second word
and the sound's number from the third.

With a flag set (when skipping ahead, it seems), `walk`, `run` and
`rotate` events aren't added at all.

## The verbs

By use in the install. "Waits" marks verbs whose event lasts until what
they start is over. The objects they work on are the game's: the scene
(`0x498858`), story state (`0x498810`: tags, timers), the inventory
(`0x49881c`), the screen (`0x498818`: fades, letterbox, interface
sounds), sound and music (`0x498828`), and the game (`0x498824`: acts,
scenes, menus).

| Verb | Uses | Arguments | What it does |
|------|-----:|-----------|--------------|
| `talk` | 1107 | conversation, code (`a`, `ab`, `a+`, `1`), options (`nogen`, `forcegen`, `nogreet`, a number) | starts a conversation ([dialogue.md](dialogue.md)); waits until it's over |
| `turn` | 1030 | degrees, a character's name, a cell, `home`, `up`/`down` | turns the character's head ([characters.md](characters.md)); doesn't wait |
| `scene` | 1026 | scene, walk point, a flag | goes to a scene (`0x423d40`): the act's variant of it, the player at its walk point (default 1); waits while it loads |
| `wait` | 853 | seconds | waits |
| `tag` | 841 | name, value / `+n` / `-n` | sets a story tag (`0x410940`): `tag x` sets 0, `+n` and `-n` add |
| `anim` | 778 | animation, weight, loops, ease | plays the character's `<who>_<anim>.gr2` ([characters.md](characters.md)); waits |
| `walk` | 433 | a cell (`walk 31 0`), a walk point (`sp3`), or a character | walks there along a path; waits |
| `fade` | 416 | `in`/`out`, a picture | fades the screen; waits |
| `setchar` | 361 | character | chooses who the next events act on |
| `rotate` | 356 | degrees, a character, a cell, `home`, `-sp<n>` | turns the character's body; waits |
| `look` | 297 | `left`/`right`/`home`, strength | the character's eyes; doesn't wait |
| `sounda` | 263 | sound | plays `Sound\<name>.wav` |
| `anima` | 229 | animation | `anim` without waiting |
| `take` | 219 | item, or `this` | adds the item and hides its object (`this`: the hotspot, its item named by its picture, `...\sce<item>.png`) |
| `act` | 175 | act (`06a`) | the act changes (`0x422580`): [acts.md](acts.md) |
| `terminate` | 174 | | ends the conversation (in dialogue event files) |
| `update` | 149 | | the scene looks at its objects' conditions again (`0x436280`) |
| `restore` | 130 | item, `all` | the first used-up item of the name back in the bag, in the lowest free slot (`0x41c320`); `all`: what `remove all` took, the small key gone and the tag `securityunlocked` removed (`0x41c430`) |
| `dooropen` | 113 | point, side, sound | macro: above |
| `stop` | 75 | sound | stops the first sound playing whose file has the name in it (`0x4251c0`) |
| `savescene` | 57 | `this`, walk point, flag | remembers a scene to come back to (`gotoscene`): [acts.md](acts.md) |
| `movie` | 51 | movie, `inscene`/`silent` | plays `Movies\<name>.bik`; waits |
| `pause`, `resume` | 48, 46 | character | stops and restarts a character's own behaviour |
| `advance` | 46 | | (dialogue) |
| `sound` | 39 | sound, `loop` | plays `sound\<name>.wav`, looping with `loop`; waits till it's over (`0x40bd45`, `0x425400`) |
| `hide`, `show` | 39, 5 | object, `this`, a picture's file | hides or shows a scene object |
| `removetag` | 36 | tag | removes a tag: `tag x` no longer holds |
| `doorlocked` | 34 | point, side, sound | macro: above |
| `timer` | 32 | name | starts a timer; `timer name >60` in conditions |
| `walka`, `rotatea` | 32, 30 | as `walk`, `rotate` | without waiting |
| `gotoscene` | 24 | | goes back to the scene `savescene` kept |
| `startprint`, `endprint` | 23 | | a fingerprint card held up: `card.png` with `inventory\interface\print.png` fades in over the darkened screen, `fingerprint.wav` (`0x417ba0`); fades out (`0x418090`); both wait ([interface.md](interface.md#the-cards-held-up)) |
| `action` | 21 | `left`, `right`, `load`... | the menus' buttons |
| `startpass`, `endpass` | 18 | character | a passport held up: `inventory\items\pass<who>\exapass<who>.png` fades in, `passport.wav`; both wait for the fade |
| `attach`, `detach` | 17, 15 | object | puts `models\objects\<name>_model.gr2` in the character's hand |
| `leavebox`, `nobox` | 16, 4 | | the dialogue box |
| `load`, `preload`, `menu` | 14, 10, 6 | slot | saved games and the menus |
| `use` | 12 | item | the first item of the name in the bag, just taken or in hand used up (state 4) (`0x41c160`) |
| `remove`, `add` | 7, 3 | item | takes the first item of the name away, whatever its state (`0x41c260`); `remove all` takes everything carried away, to state 7 (`0x41c3e0`); `add` gives another, in the bag, whatever's there already (`0x419ca0`, `0x41bc10`); `take` gives it as just taken (1). Passports and fingerprint cards (names starting `pass`, `print`) go to states 5 and 6 |
| `run`, `runa` | 6, 1 | cell | as `walk`, running |
| `previous` | 6 | | the scene before |
| `yesno` | 2 | text, value | the yes/no box (`yesno quit2 0`) |
| `interface` | 2 | `on`, `off` | the interface (the bar, the inventory) hidden for a cutscene (`[0x4986f4]`: [interface.md](interface.md)) |
| `accumulation` | 2 | `on`, `off` | snow gathering on the character (`+0x14dc`) |
| `lock`, `unlock` | 1, 2 | | clicks refused, or taken while what follows runs (above) |
| `advance`, `terminate`, `juncture`, `leavebox`, `nobox` | 46, 174 | | the dialogue's, read from a line's Povent ([dialogue.md](dialogue.md)); as events they end at once |
| `reset`, `time`, `test` | 1-2 | | `test` is a leftover of the earlier games (`0x40c650`: an apple press, radios) |

The runner also knows verbs that nothing in the install uses: `animate`
(only in the `dooropen` macro), `clear`, `globalhide`, `globalshow`,
`head`, `hideshadow`, `showshadow`, `juncture`, `letterbox`, `light`,
`loop`, `mainloop`, `musica`, `nogen`, `forcegen`, `nogreet`, `play`,
`quit`, `removestate`, `savepos`, `texture`, `lasttexture`,
`updatemusic`, `wear`, `weather`, `web`, and the `a` forms `fadea`,
`talka`, `turna`, `playa`.

Their arguments are as the install uses them; the handlers of the less
common verbs are still to be read in detail.

`letterbox on|off|start|end` (0x40a3db → the screen's 0x416ef0) puts
black bars along the top and the bottom, 70 pixels each: `on` and `off`
at once; `start` slides them in from nothing and `end` out to nothing, at
250 pixels a second (0x416f40), the event lasting till they're there.
They're drawn over the interface bar and under a card held up (0x416ff0,
from 0x4155ef), from y −1 down and up from the bottom; while they're up
(any mode but off) the cursor is hidden and nothing is picked or walked
to (0x4140b0, 0x415fa0). Reluva does the same.

## Movies

`movie <name> [inscene] [silent]` (`0x40a725`) plays a cutscene. The
game's movie object is `0x42ac00..0x42af10`.

1. **The file.** The game looks for `FMV\<name>.bik`, then
   `Movies\<name>.bik`, then the same on the CD. If there's none, the
   event ends at once.
2. **The scene.** Unless `inscene` is one of the arguments, the scene is
   left for the movie (`0x436410`) and every sound stops (`0x425250`).
   Nothing is drawn behind the movie, and a `scene` command normally
   follows.
3. **Sound.** `silent` plays the movie without its own sound.
4. **Drawing.** The movie plays as a 2D video object, full screen. All
   the movies are 1024×768, 15 frames a second, with stereo 44.1 kHz
   sound.
5. **Subtitles.** The game loads `Text\Movies\<name>.txt` into the
   dialogue and starts the conversation named after the movie
   (`0x42ae7d`) in its movie mode, where each line waits for its
   Duration's start time. Only rows whose Name is the movie's are shown:
   `logos.txt`, a copy of `intro.txt`, shows nothing with the logos.
6. **The end.** The event is done when the movie ends (`0x40c490`). The
   runner then deletes the movie object and resets the dialogue.
7. **Skipping.** Escape skips a movie (`0x409860`: it sets the runner's
   wait to 0.001 s, which ends the current event), unless its name
   contains `legal`. Clicks don't skip.

`musica <name>` plays `sound\music\<name>.mp3`, looping.

When the program starts, it queues `musica motoetheme, movie logos
inscene` before the main menu (see [acts.md](acts.md)).

## Items in hand

Clicking with an item in hand (state 3) on an object (`0x41c050`):

1. The object's conditional scripts are looked at in use mode
   (`0x40e7e0`): only those whose condition has `use` in it (so `tag x
   nouse` too). The first that holds runs; the item is used up (state 4),
   or back in the bag if the script has `restore this`. Most scripts
   `restore` the item themselves.
2. Otherwise, on a character: the first row of `Text\uselist.txt` for
   the item, whose character's name with `_model` is in the character's
   model file, and whose Condition holds (`0x41c4b0`). Its Event runs and
   the item is used up.
3. Otherwise Antoinette says she can't (`talk gen_cantuse 1`), and the
   item stays in hand. So does a click on nothing.

What matched runs as the inventory's scripts do (`0x419f20`): the player
stops, what was running is dropped, and the script runs. How an item is
picked up, and the cursor it makes, is in [interface.md](interface.md).

Combining (`0x418cf0`): the row of `Text\combinelist.txt` whose parts are
exactly the items put together, in any order. The parts are used up, the
results given (just taken), and the Event runs. The combine page, and
taking apart, are in [interface.md](interface.md#combine-windowcombinepng).

## In Reluva

`ScriptRunner` (`src/engines/awe/script.cpp`) is the runner: the queue,
done and waiting, the `a` forms, `wait`, the macros and script files. It
does the state verbs itself (`tag`, `removetag`, `add`, `remove`,
`restore`, `use`, `clear`, `timer`, `time`, `reset`, `savescene`,
`weather`, `setchar`, `lock`, `unlock`, and the dialogue's) on a
`GameState`, the one
[conditions](ac2.md#conditions) test. The rest go to a `ScriptHost`.

For now the Scenes page is that host. A click on a hotspot runs its
script, or the first of its conditional scripts whose condition holds.
It does `walk` and `run` (along the game's paths, to cells, walk points
or characters), `talk` (with the dialogue manager: [dialogue.md](dialogue.md)),
`act` and `scene`/`gotoscene` (with `Story`: [acts.md](acts.md)), `fade`,
`movie` and `musica` (above),
`sound`/`sounda` and `stop`, `hide`, `show`, `take` and `update`,
`startpass`/`startprint` and their ends (the cards held up:
[interface.md](interface.md#the-cards-held-up)), `interface`, and the
characters' verbs (`anim`, `turn`, `look`, `rotate`, `animate`, `pause`,
`resume`, `loop`, `attach`, `detach`: [characters.md](characters.md)).
It logs the rest in its info panel and ends them at once.

Items are used as above (`src/engines/awe/scene_inventory.cpp`), picked
up from the inventory window (`scene_interface.cpp`, with the bar, the
window's pages and the cards: [interface.md](interface.md#in-reluva)).
