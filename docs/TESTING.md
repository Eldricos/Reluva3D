# Testing Elite Force: the playthrough list

What still needs trying in the real game, by a person, with the retail
files. The automated tests (`ctest`) and the render check
(`reluva-render-check`, run by CI on macOS and Linux) cover the parts that
can be checked without them. See [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)
for how those work.

Each item names what to do and what should happen. When something differs
from the original game, note:

- the release (1.0, 1.2 or 1.2 + Expansion Pack) and the system;
- the map, or the menu screen;
- what the original does, and what Reluva does.

Reluva's console messages and the `menus-stopped.txt` file in the game's
save folder help.

## Already confirmed (October 2026, Windows)

- Turbolift and moving machinery collision.
- Autosave pictures.
- Leaving a match returns to the main menu.
- Setup changes made in-game: resolution, textures.
- Demos: recording and playback.
- Holomatch game modes, from the menus' mode buttons.
- The scoreboard (TAB) during a match and at its end, and moving on to the
  next map after a match.
- Network play against the retail game, Reluva as server and as client.

## Getting the game in

1. **Import Game...** from the English Windows CD alone: gives 1.0.
2. **Import Game...** from the CD with **Add patch...** (the 1.2 patch
   installer): gives 1.2. The description says so.
3. **Import Game...** from the CD with the patch and **Add expansion...**
   (the Expansion Pack's CD): gives "1.2 + Expansion Pack".
4. **Add Game...** on an existing install made by the game's own setup, with
   and without the Expansion Pack.
5. **Apply Patch...** on a 1.0 game already added: it becomes 1.2.
6. **Add Expansion...** on a 1.2 game already added: it becomes
   "1.2 + Expansion Pack".
7. **The same folder added twice,** with *Use the Expansion Pack* off on one
   entry. The list tells them apart (`[Use the Expansion Pack: off]`).

## Single player: the story (1.0 or 1.2)

1. Start a new game from the main menu. The intro cinematic plays, then
   the first level loads, with its loading screen.
2. Play through each level, and the cinematics between them. Watch for:
   - scripted scenes that stall, such as a door that doesn't open or a
     character who doesn't move;
   - wrong or missing sounds and music;
   - effects drawn wrong.
3. Every weapon, including alternate fire.
4. The turbolift menu on the Voyager decks, and the holodeck.
5. Saving from the in-game menu, and loading:
   - the save's picture and description appear in the Load menu;
   - loading returns to the same place, with the same health, ammo and
     weapons.
6. Autosaves when a level starts. Dying then reloads the last save of
   that level (`load *respawn`), or the autosave.
7. Changing level keeps health, ammo, weapons and Borg adaptation.
8. The settings menus: video, sound, controls (rebinding keys) and
   brightness. They should still apply after quitting and starting again.

## Single player: the Expansion Pack

1. With *Use the Expansion Pack* on: the main menu shows the Expansion's
   additions.
2. **Virtual Voyager** (the tour): walk the decks, the turbolift between
   them, the computer terminals, and the new screens (astrometrics,
   library, logs, PADDs).
   - Dischler should appear on deck 15. He comes from the Expansion's extra
     NPC file.
3. **The holodeck missions** (the `_holodeck_*` maps).
   - Boothby should appear in the garden.
4. **Saving and loading with the Expansion,** in the tour and in a
   mission, including autosaves.
5. **The two kinds of save kept apart.** The Expansion's single player
   can't read 1.2's saves, nor 1.2's the Expansion's.
   - With the setting on, the Load menu lists only Expansion saves. With it
     off, only 1.2 saves.
   - Expansion saves made before this separation existed should move to the
     game's save folder's `expansion/saves/` the first time single player
     starts.
6. With the setting off: the game is plain 1.2, with no Expansion maps or
   menus.

## Holomatch (multiplayer)

1. A solo match against bots, in each game mode.
2. The Expansion Pack's new maps (with its `xpack.arena` list), alone and
   with bots.
3. Hosting a LAN game that another machine joins, and joining another's
   server from the server list (Local and Internet).
4. Joining a server that uses paks the player doesn't have, with
   `cl_allowDownload 1`: they download, and the level loads.
5. A pure server (`sv_pure 1`): the retail game joins Reluva's, and Reluva
   joins a retail one.
6. Chat, team chat, votes, and the in-game menu.

## Mods

1. A mod folder beside `baseEF`, with a `.pk3` and a `description.txt`:
   - the launcher's **Mod** setting lists it;
   - the game's **Mods** screens (single player and Holomatch) list it, with
     its description.
2. Choosing it in the launcher: the game starts with the mod. The window
   title names it.
3. **Load Mod** in Holomatch's Mods screen: the menus start over with the
   mod. Do the same in single player's Mods screen.
4. A mod's saved games and settings are its own (the save folder's
   `mods/<mod>/`). The base game's are untouched.
5. A real Holomatch mod that brings its own game code (`vm/*.qvm`): its
   rules and menus are used.
6. Joining a server running a mod. Reluva should switch to the mod and join
   again. If the player doesn't have the mod, it should download its paks
   (with `cl_allowDownload 1`).

## Input

1. A joystick or gamepad, with `in_joystick 1`: movement, looking, the
   buttons, and binding them in Controls.
2. Mouse settings: sensitivity, invert, smoothing, free look.

## Other systems

1. **Linux:** import from the CD (or a disc image), then play single player
   and Holomatch.
2. **macOS (on a real Mac):** the same. The Metal renderer passes the
   render check on GitHub's Mac runners, but hasn't been played on.
3. **High refresh rates and fullscreen,** at the desktop's resolution.
