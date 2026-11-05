# Human player selection and name history

The Players screen now shows the human player's name. Choose **Change Name**
or press N to open the name editor. Typing replaces the initially selected
name; Ctrl+A selects the whole name, and Backspace/Delete removes the selection
or the last character. Enter / Save confirms the change and returns to Players.
Escape / Cancel discards the draft. A separate Enter / Play starts the round,
so saving does not accidentally skip the Players screen.

Click the player-name dropdown or press **P** to recall a previous human name.
Use Up/Down, Home/End, Page Up/Page Down or the wheel to browse; eight names are
visible at once. Enter or a row click selects the highlighted name. Escape or
an outside click cancels the list. Selection/cancellation consumes the rest of
that input batch, so Enter cannot also start a game and Escape cannot also
leave Players. A failed selection save stays in the list for retry or cancel.
**Change Name / N** adds a new name using the existing editor.

The native history keeps up to 32 distinct, case-sensitive names, most recently
selected first. Selecting an old name moves it to the front without duplication.
Adding a name at capacity drops the least recently used one. The current name
is always first. Capacity, ordering, case policy and keyboard controls are
native choices; the original dropdown's existence and contents were observed,
but those exact policies have not been established.

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
Reluva version header and name lines. The name is data, never a filename.
Version 2 stores the active name first, followed by remembered names; loading
accepts at most 32 normalized, distinct entries and 1 KiB, with LF or CRLF.
Blank names, duplicates and invalid rows reject the whole profile. Existing
version-1 single-name files still load without being rewritten. The next
actual name change/selection saves version 2 and retains that legacy name;
older names absent from a version-1 file cannot be reconstructed.
The default is `PLAYER 1`; an unchanged default does not create the file.
If no save directory was supplied, the editor identifies the name as applying
only to this session. The history then also lasts only for this frontend session.

Saving writes `Player.txt.tmp`, checks the stream, moves any prior snapshot to
`Player.txt.bak`, then publishes the new snapshot. On failure, the prior name
and history remain active; the editor or dropdown offers retry or cancel. Loading recovers `.bak`
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
restart persistence and on-disk contract remain unverified. The native profile now implements bounded human-name history and selection
for its one supported human slot. It does not import the original history. Shared-word cancellation was
also confirmed for both human/human and human/computer rounds.

## Validation

Synthetic tests cover names and bounds, restart, missing/default profiles,
backup recovery, interrupted temporary files, malformed profiles, failed writes,
symlink rejection and original-folder protection. Scripted menu tests cover
replace/save/cancel, empty/overlong input, input-batch guards, retry/cancel on save
failure, replay, global exit and text-input cleanup. Both variants and both
round renderers are compared with an unnamed control run to verify the profile
reaches the displayed round.

The history tests additionally cover version-1 migration, no-op reads/selections,
case-sensitive recall, capacity eviction, malformed/duplicate version-2 rows,
restart, backup recovery, failed-write preservation, keyboard/mouse dropdown
selection, long-list paging, retry, cancellation and global exits.
Only Players/editor frames changed in existing synthetic checksum records.

All 32 Boggle CTest checks pass with the supplied install. The normal full suite
passes 41 checks and skips 24 optional data checks without configured paths.
Player/frontend tests also pass ASan/UBSan with LeakSanitizer disabled
for the execution tracer. Test files and names are invented; committed records
contain only their checksums. The owner confirmed the earlier name-changing stage works on Windows. Testing
of the new history dropdown on Windows is pending; original history persistence
remains unverified.
An external SDL dummy run also checks the first and last pages of a long
name dropdown; captures remain outside Git. All 737 supplied installed files
retain their manifest hashes. No new assets or re-import are required.
