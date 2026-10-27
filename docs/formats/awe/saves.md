# Saved games

This page covers the save files of *Murder on the Orient Express*, the
save and load screens, and how a save is restored.

There are three kinds of save file:

- the player's saves, `Saved.Games\slot<N>.ac2`, each with a thumbnail
  `slot<N>.bmp`;
- the quick save, `Saved.Games\quick.ac2`;
- the developers' starting points, `Text\Scripts\slot<N>.ac2`, in the
  install.

## How this was worked out

This comes from `motoe.exe` (English 1.0), read in a disassembler to make
Reluva work with the game:

- the writer `0x411230` and the reader `0x411c60`;
- the save screen's and load screen's objects (`0x4347a0`, `0x424260`);
- the keys (`0x4164e0`);
- the load sequence (`0x422470`, `0x422da0`);
- the quick save's message: F2 (`0x4168c0`), the screen's timers
  (`0x414400`), its drawing (`0x415511`, in the screen's draw
  `0x414e40`) and the clicks and keys that put it away (`0x416000`,
  `0x416450`, `0x4164e0`);
- the caret (`0x434c91`, in the save screen's draw `0x434b90`).

`reluva-tool awe-save` reads the 15 developer saves to their last byte
with the layout below, and writes each back to the same size.

## The file

Each record is a tag followed by its data. Tags are four characters, like
the scenes' (stored as a little-endian u32 whose value spells the name, so
`DESC` is the bytes `CSED`). Unlike the scenes, records have no lengths
and there is no `DONE`: they run back to back to the end of the file.

Text is UTF-16 in fixed buffers (`w[N]`, N code units), ending at the
first 0; whatever follows it is leftover memory. Counts are u16.

The records always come in this order:

| Tag | Data | What |
|-----|------|------|
| `DESC` | w[32] | The name typed on the save screen (empty for the quick save). |
| `TIME` | w[32] | `"10:41"`, local time. |
| `DATE` | w[32] | `"11-02-06"`, month-day-year. |
| `GLBT`, `GLBW`, `GLBM` | s16 each | Time of day, weather and the train's moving, as they were. |
| `TIMR` | u32 | Always 0; unused. |
| `INFO` | w[32] location, w[256] scene, s16 act number, s16 act letter (A = 1) | Where the game was: `calais.hall`, `Locations\Calais.Hall\Calais.Hall_03_06b.ac2`, act 06B. |
| `ACTI` | w[256] PreLoad, w[32] location, w[256] Post Load, w[256] Completion Conditions, w[256] Event, w[256] Music, s16 weather, s16 time of day, s16 moving | The act's row as the game held it. Loading takes it from here, not from `acts.txt`. |
| `CHAR` (optional) | 3 f32 position, 3 f32 rotation | The player. |
| `FLLW` (optional) | the same | A character following the player. |
| `TAGS` | n × (w[64] name, s16 value) | Story tags. These include the dialogue's "met them here" tags (`Sirkeci.Market_02.ac2 dia_cleaver 0`). |
| `TIMS` | n × (w[64] name, s16 seconds) | Timers. |
| `STAT` | n × (w[64] name, w[256] value) | Text tags: each scene visited and its acts (`Sirkeci.Market_01.ac2` = `01a`), and `savedscene`. |
| `POSI` | n × (w[64] scene file, w[64] model file, 3 f32 position, 3 f32 rotation) | Where each non-player character was in every scene the game kept in memory (`Sirkeci.Market_01.ac2`, `klaus_model.gr2`). |
| `TALK` | n × (w[32] conversation, w[32] act, s32 part, s32 thread, s16 status, s32 nest) | The conversations' progress (see [dialogue.md](dialogue.md)). |
| `ITEM` | s16 page shown; n × (w[32] item, s16 state, s16 slot, s16 page) | The inventory, 16 slots a page. State 2 is in the bag, 4 taken away but kept for `restore`, 5 a passport, 6 a fingerprint card. |

Nothing else is saved:

- objects hidden by `hide` or `take` (only `globalhide`, which is a tag);
- lights, music, the camera;
- what a script was doing.

The scenes rebuild their state from the tags and conditions.

The thumbnail is a 256×192 BMP of the view as it was when the menu was
opened. The game writes it with D3DX, probably as a 32-bit BMP.

## The save and load screens

The two screens are the scenes `Menu.Save` and `Menu.Load`, opened with
`menu save` and `menu load`. Their `PUZZ save` and `PUZZ load` records
create the screen object, which scripts drive with `action` commands and
ask with `action` conditions.

**Layout**

- Slots are numbered from 0 with no gaps. A screen counts them until the
  first missing file.
- Three rows show per page, at y = 63, 287 and 511. Each row's box is
  x 703..957 and 190 pixels high.
- In each row:
  - the thumbnail is at (704, top + 1), dimmed (alpha 92) unless the row
    is picked;
  - the name is centred on x = 624;
  - the date sits at x = 708 and the time is right-aligned to x = 952, at
    the row's foot.
- The text is in the font for "save, load, and options screens", picked
  or greyed out. An empty slot reads `EMPTY`.

**Opening.** The save screen opens on a new slot after the last. The load
screen opens on the newest save.

**Clicking.** A click picks a row; a second click within 0.33 s is OK.

**Typing the name** (save screen):

- A–Z (as capitals), digits and space are accepted.
- The name is limited to 31 characters and 125 pixels.
- Backspace and Delete remove the last character.
- The name is centred on its own width. A caret `|` follows it, in the
  same font and colour, and blinks: it shows for 0.33 s and hides for
  0.33 s. The blink is one timer on the game's screen object (+0xa4,
  +0xa8): it runs all the time, from hidden at the start, and typing
  doesn't restart it.

**`action` words**

| Word | Save screen | Load screen |
|------|-------------|-------------|
| `up` | page −3 | the same |
| `down` | page +3 | the same |
| `save` | writes the slot and its thumbnail. Over an existing one, it asks "Overwrite?" first and drops the rest of OK's script; Yes runs `action save confirmed, fade out, previous, fade in`. | — |
| `load` | — | reads the slot into the story state, as `preload` does |

**Buttons.** OK runs `action save, fade out, previous, fade in` on the
save screen and `fade out, action load, load, fade in` on the load
screen. Cancel runs `fade out, previous, fade in`. Enter and Escape press
OK and Cancel.

## Keys in the game

- **Escape** (or P), S and L open the in-game menu, the save screen and
  the load screen, outside the menus. The view is kept first for the
  thumbnail.
- In the main menu, Escape asks "Quit program?". In the in-game menu,
  Escape resumes.
- **F2** quick-saves to `quick.ac2`, outside the menus. **F3** loads the
  quick save: `fade out, preload quick, load, fade in`.

**The quick save's message.** When F2's save succeeds, the screen shows
"Save Game" (interface text `ingamemenu2`) for **1.5 s**:

- The box is `Interface\placard.tga` (270 × 114), centred, its foot at
  y = 766: at (377, 652).
- The text is in the font for "location descriptions" (Calisto MT 14,
  with its shadow) and the "navigation text" colour (230, 170, 75),
  centred on x = 512, and vertically centred in the box 2 pixels down:
  y = 652 + (114 − h)/2 + 2, where h is the text's height.
- It is drawn at full opacity the whole time, with no fade, over the
  scene and under the yes/no box: while that box is up, it isn't drawn.
- A key or a mouse button (left or right) pressed while it shows only
  puts it away: the game does nothing else with it. So F2 again doesn't
  save again until the message has gone.

The placard is the one the hover captions use; the message's text is
looked up once and then kept.

## Restoring a save

`preload <x>` reads a save into the story state. `quick` is the quick
save; any other name is the developers' `Text\Scripts\<x>.ac2`. Reading
works like this:

1. The story state is reset, as `reset` does.
2. The save's lists are taken in.
3. The game is marked "restoring": until the load is over, writes to the
   story state are ignored and scene events don't run.

`load` then goes to the save:

1. The act becomes the saved one, and its dialogue tables are loaded.
2. Once the screen has faded out, the saved location is loaded behind the
   loading screen ([acts.md](acts.md)). Neither PreLoad nor Post Load runs.
3. The saved time of day, weather and moving apply.
4. The saved scene is entered at walk point −1, without its events.
5. The player is put where they were, and the characters where `POSI`
   says.
6. The `load` event ends, and the script's `fade in` follows.

## In Reluva

`savegame.cpp` reads and writes the format.

**Where saves go.** The Scenes page keeps the saves in Reluva's folder
for the game, `<save path>/Saved.Games`. The files are the game's own: a
save copied from the game's `Saved.Games` folder loads in Reluva, and
Reluva's saves are in the format the game reads.

**What works:**

- the in-game, save and load menus with the game's scenes;
- the slots, paging, typing a name with its blinking caret, overwriting
  with confirmation, and thumbnails;
- F2 with its message, and F3;
- the developers' `preload slotN`;
- restoring a save as the game does.

**How the game's state maps to Reluva's.** Reluva doesn't keep scenes in
memory as the game does:

- characters' positions are recorded when a scene is left (as `POSI` holds
  them), and put back when it is shown again;
- the player's position is kept per scene.

Inventory slots and pages are numbered in the order the items were
gained.

While the message shows, a key or a click only puts it away, the Scenes
page's own keys (Left, Right, H, I...) included.
