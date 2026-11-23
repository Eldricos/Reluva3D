# LEGO Loco: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

<!-- Each item: what the original does, what Reluva does, and why, with a
link to the page that has the details. Say whether a setting gives back
the original's way. -->

## By choice

- **The game's folders:** the program sees the copy as drive C:, itself
  as `C:\Exe\Loco.exe`, and reads a copy of `LEGO.INI` from the save
  folder whose folders are C:'s, where the original's name the folder it
  was installed in. The player's own settings in it are kept
  ([systems/running.md](systems/running.md)).
- **Network play** goes between copies of Reluva, over UDP with a
  protocol of Reluva's own, under the game's DirectPlay calls; the
  original's DirectPlay can't join them, nor they it. IPX is offered as
  the original offers it, and is the same network as TCP/IP. A
  meeting server (the `meeting_server` setting) and the router's port
  being opened while hosting are Reluva's
  ([common/x86/dplay.md](../../common/x86/dplay.md),
  [systems/running.md](systems/running.md#network-play)).

## Bugs fixed

<!-- The original's own bugs that Reluva doesn't reproduce. -->
None yet.

## Not yet as the original

<!-- Known gaps: behaviour not done yet, or done differently for now. -->
- **The window:** the original runs on the desktop, in windows filling
  it; Reluva shows its 800 by 600 desktop as the game's window, with
  nothing of Windows around it ([systems/running.md](systems/running.md)).
- **The screen's size:** the game fills the desktop at whatever size it
  is; a recording of the original was played at 1024 by 768, where a
  board (64 by 48 squares) fits without scrolling, the front screen's
  and the station master's places are as they'd be, and the Postcard
  Editor shows more of its room. Reluva's desktop is 800 by 600, so the
  boards scroll (`reluva-tool legoloco run -desktop 1024x768` plays it
  at the recording's size).
- **The player's name box:** the front screen's ticket has a box to type
  the player's name in, an `EDIT` control the game makes with
  `CreateWindowExA` (`0x42063d`). Reluva makes only the windows of the
  program's own classes and the controls of dialogs, so the box isn't
  made, nor drawn; the name is `LEGO.INI`'s `[USER]` `Name`. The mail
  room's and the save panel's boxes are the same kind.
- **The text's font:** the station master's bubbles and the game's other
  text ask for Arial, 12 pixels, bold (`0x446113`); Reluva draws GDI's
  text in a font of its own, smaller and plainer, and breaks lines
  differently.
- **The default name:** the original names a new player by Windows'
  user name (`GetUserNameA`, `User` in the recording); Reluva's is
  `Player`.
- **The windowed mode:** its frame is painted plainly (borders and a
  caption bar, without the title or buttons); the desktop around it is
  black.
- **Windows' pointer:** where the game shows Windows' own pointer rather
  than drawing its own (the busy pointer while saving), nothing is
  drawn; the animated cursors' files aren't read.
- **Not stood in for yet:** the Open and Save dialogs (the game's own
  panels serve instead), and opening a web page (`ShellExecuteA`).
- **No new host:** when a network game's host leaves, the others are told
  the game is lost, where DirectPlay could hand the session to another
  player if the game asked for that (`0x4`).

## Additions outside the game

- The data viewer, in `reluva-viewer` only
  ([systems/data-viewer.md](systems/data-viewer.md)).
- `reluva-tool legoloco` commands for studying the game's files
  ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)).
