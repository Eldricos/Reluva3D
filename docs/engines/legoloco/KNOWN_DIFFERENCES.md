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
- **The screen's size:** the game fills the desktop at whatever size it
  is. Reluva gives it 1024 by 768, as a recording of the original was
  played, where a board (64 by 48 squares) fits without scrolling; the
  engine's "Screen size" setting gives 800 by 600 instead, the boards
  then scrolled ([systems/running.md](systems/running.md#in-reluva)).
  Other sizes the original's players had aren't offered.
- **The text's font:** the game's text asks for Arial (bubbles bold with
  a cell of 16, the name box bold of 12, `0x446113`). Arial isn't
  Reluva's to ship: the player's own is used where it's installed (the
  engine's "Text font" setting, "Arial if installed"); otherwise, or set
  to "Liberation Sans", Reluva's built-in Liberation Sans, as wide as
  Arial, at sizes matched to a recording of the original so its lines
  break as there, its letters' shapes a little different
  ([common/x86/gdi.md](../../common/x86/gdi.md)).
- **Windows' colours:** where the game shows Windows' own (the name box's
  selection, the windowed mode's frame, the desktop around it), Reluva
  gives Windows 98's standard scheme, of the game's time; the recording
  of the original was made on Windows XP, whose selection is a brighter
  blue. The engine's "Windows colours" setting gives XP's instead.
- **Windows' arrow:** where the game shows Windows' standard arrow
  rather than its own hand (over the scroll bars and frame, and after
  saving where its hand is off), Reluva shows an arrow of its own
  drawing, as Windows' picture isn't Reluva's to have
  ([common/graphics/cursors.md](../../common/graphics/cursors.md)). The
  busy pointer is the game's own file, as the original shows it.
- **The default player's name:** the original names a new player by
  Windows' user name (`GetUserNameA`; `User` in the recording), so on
  the player's own computer it's their login; Reluva's is always
  `Player`, keeping the computer's user name out of the game and the
  network games it's shown in. A name of the player's own can be set as
  `LEGO.INI`'s `[USER]` `Name` in the save folder.

## Bugs fixed

<!-- The original's own bugs that Reluva doesn't reproduce. -->
None yet.

## Not yet as the original

<!-- Known gaps: behaviour not done yet, or done differently for now. -->
- **The window:** the original runs on the desktop, in windows filling
  it; Reluva shows its desktop as the game's window, with nothing of
  Windows around it ([systems/running.md](systems/running.md)).
- **The windowed mode:** its frame is painted as Windows 98 paints an
  active window's, always active, without the window's icon (its
  picture isn't read); its minimize button does nothing, there being no
  taskbar to bring the window back from.
- **Not stood in for yet:** the Open and Save dialogs (the game's own
  panels serve instead), and opening a web page (`ShellExecuteA`).

## Additions outside the game

- The data viewer, in `reluva-viewer` only
  ([systems/data-viewer.md](systems/data-viewer.md)).
- `reluva-tool legoloco` commands for studying the game's files
  ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)).
