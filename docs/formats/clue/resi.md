# Clue: Murder at Boddy Mansion: dialogInfo.res

The game's fonts and the layout of every menu and dialog: 7 fonts and 42
dialogs, 626 controls in all. It's in the install's folder (patched by 2.2)
and in the CD's `Data/US` and `Data/UK`.

Source: the order of the reads is from `EGuiResourceInfo::Load(CFile*)` in
Eagle's library (`!EagleM.dll` 2.2, at `0x1001BDF0`; its exported names give
it), read as an instruction listing (October 2026). What the fields mean
comes from the values across the file (which controls have which), and
from comparing the dialogs drawn by Reluva with recordings of the original
(the menus, the preferences). `reluva-tool clue-check` reads it and checks
every picture it names.

Numbers are little-endian. A string is a byte's length, that many
characters (Windows-1252) and a NUL; a length of 0 has neither.

## The file

| Size | |
|---|---|
| 4 | `RESI` |
| 1 | number of dialogs |
| 1 | number of fonts |
| | the fonts |
| | the dialogs |

A font:

| Size | |
|---|---|
| 4 | its number, by which controls name it |
| string | its animation, `largeu:Alargeu`: frame n is character n |
| 4 | space between characters (0 in all) |
| 4 | space between lines (1; 6 for `largeu`) |

The fonts: 0 `small`, 1 `notepad`, 2 `ticker`, 3 `14ptfont`, 4 `16ptfont`,
5 `18ptup`, 6 `largeu`. A glyph is an image whose pixels sit in a cell
([res.md](res.md)); a character without one (the space) has no image.

A dialog: a 36-byte block (its number first; the rest 0, a 6, and a
pointer as it was in memory), its background's animation (string), a
byte's count of controls, and the controls.

A control: a 112-byte block, two strings (its text, its animation), a
2-byte count of items and that many strings.

## A control's block

As 28 numbers of 4 bytes:

| # | |
|---|---|
| 0 | its type |
| 1 | its number: what the game knows it by; 9999 for decoration, 10000 Return/Cancel/No, 10001 OK/Yes |
| 2-5 | its rectangle: left, top, right, bottom. Right and bottom 0 when its picture gives its size; right −1 for the screen's whole width |
| 6, 7 | pointers, as they were in memory |
| 9 | 1: its text centred across; 0: from the left (the preferences' labels) |
| 10 | 1: its text centred down; 2 for the suggestion window's choices |
| 14 | its font |
| 15 | another font (the same in most) |
| 16 | 1: hidden at first, an alternative the game shows when needed (the computer player's difficulty, the "next player" title); else 0 |
| 12, 13 | 100 and 0 in most controls, 10 and 5 in a few, pointers in some: not understood |
| 20 | 32 in 397 controls of many dialogs, else 0: not understood |
| 21 | 2 for some two-line labels ("Locally\|on this machine") and a few names, else 0 |
| 8, 11, 17-19, 22-27 | the same in every control (a pointer, 0, 55, −1, 1) |

The types, by what the controls of each are:

| Type | Controls | Text | Animation |
|---|---|---|---|
| 0 | pictures | its animation or image | |
| 1 | text | the text | |
| 2 | picture buttons | its animation | |
| 3 | buttons with a label | the label | its animation |
| 5 | text boxes (names) | | |
| 7 | sliders (the volumes) | the knob's image | |
| 8 | scroll knobs | the knob's image | |

`|` breaks a text into lines. Buttons' animations have 3 frames (as they
are, under the pointer, pressed), or 4 with a disabled one.

## The dialogs

| # | Background | |
|---|---|---|
| 1 | `menu:AmainBgnd` | main menu: Start Game (1), Load Game (3), Preferences (2), How to Play (0), Credits (5), Exit Game (7) |
| 2 | `menu:A2ChoiceBgnd` | Start Game: Locally (0), Networked (1) |
| 3 | `menu:AloadGmBgnd` | Load Game: 8 slots |
| 4, 5 | `menu:ArlsMainBg`, `ArulesBgnd` | How to Play: its 7 topics, and a topic's text with a scroll bar |
| 6, 14 | `menu:AcreditsBgnd`, `ArlsMainBg` | Credits: Hasbro's and EAI's, and their text |
| 7 | `menu:AexitBgnd` | Exit Game: Yes / No |
| 9, 10 | | Connection Options: Internet Game Zone, LAN TCP/IP; Host New Game, Join Game |
| 11, 12, 13 | | hosting, joining, the network game's players |
| 15 | | Game Variants: Classic, No Die Roll |
| 17 | `menu:AcharPicker` | the six suspects, to choose who plays them |
| 18-23 | `menu:A…SetupWin` | a suspect's player: Human (a name), Computer (Novice, Normal, Expert), None |
| 24 | `menu:APrefsBgnd` | Preferences: 7 settings on/off, 2 volumes |
| 26 | `menu:ALoad_back` | "Entering the mansion ..." |
| 27-29 | | the network game lost, a player dropped |
| 30 | `dialog:AB_sugAcsWin` | the suggestion and accusation window: suspect, weapon, room |
| 31-34 | | disproving, the accusation |
| 35-39 | `dialog:Aesc…` | the in-game menu (Esc): Save, Preferences, How To Play, Exit To Main, Continue, and its own screens |
| 40-45 | | the end of a game, waiting for network players |

The preferences have two radio buttons a setting, each drawn by two
images in the same place: setting k's "on" column holds controls 4k (lit)
and 4k + 1 (not), its "off" column 4k + 2 (lit) and 4k + 3 (not).

## The rest of the screen

The menus' lower corners, the mansion on its hill and the cliff under the
storm, are `main_anim`'s animations, placed by the game: `Alt1anim`
(13, 239), `Alt2anim` (13, 317), `Art1anim` (421, 239), `Art2anim`
(396, 317), measured on a recording. The game's bar (its frames, buttons,
the suspects' portraits, the ticker) is `ui.a`'s, placed by the game too:
see [../../engines/clue.md](../../engines/clue.md).
