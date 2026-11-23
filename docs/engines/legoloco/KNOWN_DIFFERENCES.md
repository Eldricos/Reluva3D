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

## Bugs fixed

<!-- The original's own bugs that Reluva doesn't reproduce. -->
None yet.

## Not yet as the original

<!-- Known gaps: behaviour not done yet, or done differently for now. -->
- **The window:** the original runs on the desktop, in windows filling
  it; Reluva shows its 800 by 600 desktop as the game's window, with
  nothing of Windows around it ([systems/running.md](systems/running.md)).
- **Not stood in for yet:** the Open and Save dialogs, opening a web
  page (`ShellExecuteA`), the animated cursors (`LoadCursorFromFileA`),
  and network play (it finds no network).

## Additions outside the game

- The data viewer, in `reluva-viewer` only
  ([systems/data-viewer.md](systems/data-viewer.md)).
- `reluva-tool legoloco` commands for studying the game's files
  ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)).
