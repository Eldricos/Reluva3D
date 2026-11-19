# The Fallen: the menus and the console

> **Status:** done · **Code:** `src/engines/unreal/vm/vm_console.cpp` · **Checked against:** the code and the game running in Reluva · **Confidence:** from the code unless marked

## Overview

The menus work: the title, credits, main menu and info pad, New Game
travelling to the opening, the UWindow options window with the mouse.

## How it works

The Fallen's menus are the HUD's own, drawn on its canvas, and UWindow's
options windows, drawn by the console (`UMenu.DS9_Console`).

Keys reach the console first, as the engine hands them on
(`LevelPlay::consoleKey`, the console's `KeyEvent` with the key's
`EInputKey` code: 13 Enter, 27 Escape, 37 to 40 the arrows). The
Fallen's console passes the arrows and Enter to the HUD
(`GetKeyEvent`), which acts on them while a menu shows. What the console
doesn't take goes on to the bindings: Escape is `ShowMenu`. While the
menu shows, the Maps page leaves the arrows and Enter to the game
whatever `KeyEvent` answers, as the HUD's answers false having acted on
Enter. The page's own Escape (leaving) waits until play stops. The keys
held are kept for `IsKeyDown`.

`ShowMenu` opens the info pad and pauses (`SetPause`: `Level.Pauser`
set). Paused, time stands still: only the player's `PlayerTick` and the
actors with `bAlwaysTick` run (the HUD, whose `Tick` runs the menu). The
FLC textures and the console still tick, the dialogue doesn't.

On M04_KiraL1:
- **The info pad:** Resume Game, Mission Objectives, Tactical Data,
  Communications Log, Retrieved Files, Main Menu. Mission Objectives
  lists them and shows the selected one's document, its text and
  picture, in the pad's lower window.
- **The main menu:** New Game, Load Game, Save Game, Options, Quit, with
  the star map scrolling in its window above. New Game asks the
  character (Sisko, Kira, Worf), then the game or the training mission,
  then confirms. Saying yes fades out and travels (`ClientTravel`
  "CM01a_SW/CM01_A_Sisko01"), and the page loads that map and plays it
  from the start the portal tags: the opening cut scene.

The game's own start is `Title.dsm` (Default.ini's `LocalMap`): the
publishers' logos, a cut scene. Escape during a cut scene fast-forwards
it (`FastForward`): the page runs the level as fast as it can, up to a
twentieth of a second's work a frame, until the game turns it off when
the letterbox goes. Then `CM_SpaceStation/intro`, whose start tagged
`Intro` makes it the intro level (`bIntroLevel`). The credits roll over
the station fly-by, and Escape skips them to the main menu, with no Save
Game.

Options opens UWindow (the console's `Menuing` state, then `UWindow`):
the engine calls the console's `PostRender` after the player's, and its
`Tick` every tick, paused or not, and the windows are all script, drawn
through the same canvas. The options window has Game, Video, Audio,
Controls and Input pages. While it's open the mouse is the windows': the
page puts the console's `MouseX`, `MouseY` where its pointer is, and its
buttons are keys 1 and 2. Escape closes the window.

Console commands (`ConsoleCommand`, `vm_console.cpp`):
- `get Section Key` gives a setting (`ini:Engine.Engine.ViewportManager
  Brightness`: the section of the class the ini names), else a class's
  default; `get ... Class` the class it names.
  - The render device (`get ini:Engine.Engine.GameRenderDevice Class`)
    is Direct3D's (`D3DDrv.D3DRenderDevice`) where the `.ini` names the
    software one, as the shipped `Default.ini` and `Unreal.ini` do.
    Reluva draws as the hardware drivers do. The scripts ask for it
    (`Actor.InSoftwareMode`) to hide the wrist flashlight, the breather,
    the armour belt and the drill's inductor, and the tricorder grid, in
    software mode. So the flashlight shows on the raised forearm
    (`C3DAttach(Player, 'Wrist', 'Arm')`), the breather at the mouth
    ('Face'), and the video options name "Direct3D Support".
  - Picking items up: standing still within 52 of one (`ScanForItemsToPickup`),
    the player turns to it and plays the pick-up (crouching to one under
    the feet), then takes it. M05_SiskoL1a's breather at (3205, -2204)
    is in a nook open only to +Y, and is taken from there.
- `set` changes one ([script-vm.md](script-vm.md)), and the class's property in its defaults and
  every object of it.
- `KEYNAME n`, `KEYBINDING Key` and `SET Input Key Command` read and change
  the bindings. The controls page lists them ("LEFTMOUSE OR CTRL"), and
  the page's keys follow what it sets.
- `GetCurrentRes`, `GetRes` give the page's size; the colour depth is 32.
- The world and skin detail (`[WinDrv.WindowsClient]` `TextureDetail`,
  `SkinDetail`) are the engine's own settings, which no shipped `.ini`
  holds. They read as High, the first of the game's choices (Reluva's
  reading).
- `SaveGame [QuickSave]` saves the level ([saves.md](saves.md)).
- `Exit` stops playing.
- The rest (sound, detail settings) do nothing yet.

The mouse in the HUD's own menus: its buttons, as the game binds them.
The left (`Fire`) selects, as Enter does, and the right (`AltFire`)
steps the choice on. Its movement does nothing: the HUD's
`HandleMenuMouse` is compiled to return at once.

On the intro level the player's health is 94: its `Intro` start is 410
units above the floor, the player falls hidden from it, and the landing
costs 6 (`TakeFallingDamage`, past −850 a second). Nothing at that start
stops the fall, so this is the game's own doing; the menu hides it, and
New Game starts a new player.

## In Reluva

`vm/vm_console.cpp` hands keys to the console and works its commands
(`LevelPlay::consoleKey`).

## Sources and checks

- **Where the facts come from:** the console's and HUD's scripts, and
  the engine's console commands.
- **How it was checked:** in play, through each menu.
