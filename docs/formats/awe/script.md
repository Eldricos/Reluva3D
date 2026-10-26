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
`setchar <name>` chooses the character that later events
(`walk`, `turn`, `anim`, `look`...) act on.

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
| `restore` | 130 | item | the inventory (`0x419fb0`); to read |
| `dooropen` | 113 | point, side, sound | macro: above |
| `stop` | 75 | sound | stops a looping sound |
| `savescene` | 57 | `this`, walk point, flag | remembers a scene to come back to (`gotoscene`): [acts.md](acts.md) |
| `movie` | 51 | movie, `inscene`/`silent` | plays `Movies\<name>.bik`; waits |
| `pause`, `resume` | 48, 46 | character | stops and restarts a character's own behaviour |
| `advance` | 46 | | (dialogue) |
| `sound` | 39 | sound | plays a sound; waits |
| `hide`, `show` | 39, 5 | object, `this`, a picture's file | hides or shows a scene object |
| `removetag` | 36 | tag | removes a tag: `tag x` no longer holds |
| `doorlocked` | 34 | point, side, sound | macro: above |
| `timer` | 32 | name | starts a timer; `timer name >60` in conditions |
| `walka`, `rotatea` | 32, 30 | as `walk`, `rotate` | without waiting |
| `gotoscene` | 24 | | goes back to the scene `savescene` kept |
| `startprint`, `endprint` | 23 | | the fingerprint puzzle's screens |
| `action` | 21 | `left`, `right`, `load`... | the menus' buttons |
| `startpass`, `endpass` | 18 | character | the passports' screen |
| `attach`, `detach` | 17, 15 | object | puts `models\objects\<name>_model.gr2` in the character's hand |
| `leavebox`, `nobox` | 16, 4 | | the dialogue box |
| `load`, `preload`, `menu` | 14, 10, 6 | slot | saved games and the menus |
| `use` | 12 | item | uses an item |
| `remove`, `add` | 7, 3 | item | takes an item away, gives one (`remove all`) |
| `run`, `runa` | 6, 1 | cell | as `walk`, running |
| `previous` | 6 | | the scene before |
| `yesno` | 2 | text, value | the yes/no box (`yesno quit2 0`) |
| `interface`, `accumulation`, `unlock`, `lock`, `reset`, `time`, `test` | 1-2 | | |

The runner also knows verbs that nothing in the install uses: `animate`
(only in the `dooropen` macro), `clear`, `globalhide`, `globalshow`,
`head`, `hideshadow`, `showshadow`, `juncture`, `letterbox`, `light`,
`loop`, `mainloop`, `musica`, `nogen`, `forcegen`, `nogreet`, `play`,
`quit`, `removestate`, `savepos`, `texture`, `lasttexture`,
`updatemusic`, `wear`, `weather`, `web`, and the `a` forms `fadea`,
`talka`, `turna`, `playa`.

Their arguments are as the install uses them; the handlers of the less
common verbs are still to be read in detail.

## In Reluva

`ScriptRunner` (`src/engines/awe/script.cpp`) is the runner: the queue,
done and waiting, the `a` forms, `wait`, the macros and script files. It
does the state verbs itself (`tag`, `removetag`, `add`, `remove`,
`clear`, `timer`, `time`, `reset`, `savescene`, `weather`, `setchar`) on
a `GameState`, the one
[conditions](ac2.md#conditions) test. The rest go to a `ScriptHost`.

For now the Scenes page is that host. A click on a hotspot runs its
script, or the first of its conditional scripts whose condition holds.
It does `walk` and `run` (along the game's paths, to cells, walk points
or characters), `talk` (with the dialogue manager: [dialogue.md](dialogue.md)),
`act` and `scene`/`gotoscene` (with `Story`: [acts.md](acts.md)), `fade`,
`sound`/`sounda`, `hide`, `show`, `take` and `update`, and the
characters' verbs (`anim`, `turn`, `look`, `rotate`, `animate`, `pause`,
`resume`, `loop`, `attach`, `detach`: [characters.md](characters.md)).
It logs the rest in its info panel and ends them at once.
