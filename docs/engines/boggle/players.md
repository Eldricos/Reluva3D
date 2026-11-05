# One human player and a native name

The Players screen now shows the human player's name. Choose **Change Name**
or press N to open the name editor. Typing replaces the initially selected
name; Ctrl+A selects the whole name, and Backspace/Delete removes the selection
or the last character. Enter / Save confirms the change and returns to Players.
Escape / Cancel discards the draft. A separate Enter / Play starts the round,
so saving does not accidentally skip the Players screen.

Names use 1–18 printable ASCII characters with outer spaces trimmed. Case is
preserved. Unsupported characters, an empty name or excess length keep the
editor open with a message. This is a native English input limit, not a verified
original name-length or multilingual contract. Editing is at the end of the
field; arbitrary caret movement, IME names and clipboard editing are unfinished.

The name appears in the artwork's player strip and word-panel header, and in
the prototype round view. Longer artwork headers use smaller text or a shortened
strip label to fit. Original/Master, replay and New Board retain the chosen
name. Direct Classic entry also loads it; re-enable the selection menu to edit.
The supported session remains one human, with no AI, additional player slots or
network players.

## Native persistence

A changed name is stored in `Player.txt` in the launcher's per-target save
directory, alongside the independent English user dictionary. This file has a
Reluva version header and one name line. The name is data, never a filename.
The default is `PLAYER 1`; an unchanged default does not create the file.
If no save directory was supplied, the editor identifies the name as applying
only to this session.

Saving writes `Player.txt.tmp`, checks the stream, moves any prior snapshot to
`Player.txt.bak`, then publishes the new snapshot. On failure, the prior name
remains active and the editor offers retry or cancel. Loading recovers `.bak`
only when the main file is absent; incomplete `.tmp` files are ignored. A corrupt
main file reports an error rather than silently replacing a user's name.
This is recovery from interrupted native writes, not a power-loss durability
guarantee.

All three file paths must be regular files when present; symbolic links and
non-file destinations are rejected. The resolved save directory must be outside
the original game directory. Original player files, registry settings and game
resources are neither imported nor changed. Renaming does not clear the English
user dictionary, change rules or introduce per-player word dictionaries. The
private `classic_player_name` session value carries the selected name from the
frontend into a round; it is not a new launcher control or original setting.

## Evidence and limits

The inspected English DLL, identified by SHA-256 in the
[install manifest](../../plans/boggle-1997/install-manifest.tsv), contains dialog
1600 at file offset `0x3048` (188 bytes), with name edit control 1601, an OK
button and a name-entry prompt. Classic
word-panel dialogs 260/360 include name label 1400. These establish original
name entry and display. The owner's earlier VM observation establishes the
Players stage between version selection and play.

The new one-human editor, 18-character limit, save/cancel interaction and profile
format are independently implemented native policies. Exact original name-entry
navigation, player-file persistence, multiple human players and AI timing
still need verification. The [competition research and scoring foundation](competition.md)
now identifies four original player slots, the opponent dropdown labels and
shared-word cancellation; it does not yet add live opponents or turns. This
does not read or emulate the original player database.

The owner subsequently confirmed that all four original dropdowns list previously
used human names plus Beginner, Intermediate, Advanced and Mr. Boggle computer
opponents. Original human-name history is therefore an observed feature; its
restart persistence and on-disk contract remain unverified. The current one-name
native profile does not implement that history yet. Shared-word cancellation was
also confirmed for both human/human and human/computer rounds.

## Validation

Synthetic tests cover names and bounds, restart, missing/default profiles,
backup recovery, interrupted temporary files, malformed profiles, failed writes,
symlink rejection and original-folder protection. Scripted menu tests cover
replace/save/cancel, empty/overlong input, input-batch guards, retry/cancel on save
failure, replay, global exit and text-input cleanup. Both variants and both
round renderers are compared with an unnamed control run to verify the profile
reaches the displayed round.

All 25 Boggle CTest checks pass with the supplied install. The normal full suite
passes 36 checks and skips 22 optional data checks without configured paths.
Player/frontend/layout tests also pass ASan/UBSan with LeakSanitizer disabled
for the execution tracer. Test files and names are invented; committed records
contain only their checksums. The owner confirmed name changing works on Windows; detailed restart/recovery
observations remain separate from the native tests.
No new assets or re-import are required.
