# Classic rejected words and user dictionary

Scope: U.S. English, single-player Classic Original or Master, with the native round's
existing independent board/path model. Game data remains read-only. This adds
word review and per-target persistence, not original menus, multiplayer word
cancellation, a dictionary editor or multilingual user-word normalization.

## Evidence

The owner observed the original scoring words immediately, showing unknown
words in red, and offering Yes / No / Yes to all / No to all after the round.
Repeated words visibly do nothing, and two-letter input triggers a minimum-
length notice. The supplied executable has SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
The following addresses are executable virtual addresses:

- Validation `0x414d50` checks minimum length, then the main dictionary at
  game-object offset `0x1fe4` and user dictionary at `0x2004`. In its deferred
  setting, the unknown word is queued at `0x414f31`, the pending flag is set,
  and validation succeeds. Other original settings prompt immediately, add
  unconditionally, or reject. The prototype implements the observed deferred
  workflow, without exposing those extra settings.
- Player insertion `0x44a6c0` prevents duplicates, inserts the candidate and
  invokes Classic's length scorer. Its points are provisional even if the
  word awaits dictionary approval. Red-list marking is handled by `0x45aac0`.
- Round-end handling around `0x41b0a0` considers submitted words absent from
  both dictionaries. The dialog dispatch at `0x41b20c` implements the four
  choices. Yes adds the word; Yes to all also sets a flag for remaining
  candidates. No removes the entry; No to all also declines remaining entries.
  Player scores are recomputed at `0x41b29f` through `0x44af00`.
- Text loading `0x432bd0` reads lines through `0x463640`, uppercases them with
  `0x467360`, and adds them to the word tree. Saving `0x432d60` traverses words
  and writes each word plus a newline through callback `0x432d30`. The original
  game saves the user dictionary during destruction at `0x41175f`. These are
  text files, distinct from the binary main dictionary format.

An external reference harness executed validation and player insertion in the
owner's EXE using Reluva's x86 interpreter, with bounded dictionary/list/CString
callbacks. Main and user hits bypassed the queue; a deferred unknown word was
queued; reject/add settings took their respective branches; a five-letter
candidate received two provisional points. This is a scoped routine check,
not execution of the original full round or persistence UI. The harness,
retail bytes, disassembly and raw logs remain outside Git. Native implementation
uses its own containers, round states and text snapshots; it executes no EXE.

The owner subsequently corroborated the deferred rules in the VM: pending
words receive points immediately, Yes keeps those points, No removes them,
and words that cannot be made on the board cannot enter the list. Native
eligibility therefore requires a valid board path; an addition cannot make
an impossible path valid. This confirms both the provisional scoring and
path-first eligibility used by the implementation. Prompt layout, complete
original settings and multilingual behavior remain outside this slice.

## Native behavior

A normalized word meeting the variant's minimum (Original three letters,
Master four) with a valid path is submitted
once. Main/user dictionary words count normally. An unknown word stays in the
history in red and carries provisional length-based points until reviewed.
Invalid characters, too-short words and words without a board path never enter
the approval queue. A repeated submitted word creates no second entry or score.
The history shows fourteen entries at once; Page Up / Page Down reaches older
entries. New submissions return to the most recent entries.

At timeout or manual finish, pending words must be reviewed before replaying
or changing the board. Use the four buttons or keys **1 = Yes**, **2 = No**,
**3 = Yes to all**, **4 = No to all**. Enter does not approve a word, and input
arriving before the dialog is drawn cannot select a choice. Yes persists the
word and keeps its points. No removes it from the visible list and score. All
choices apply only to remaining candidates in the current round. Declined
words remain in the model's internal round history for testing, not in the
user dictionary. Escape leaves the game without approving remaining words;
already approved additions remain saved.

A save failure leaves the word and score pending and displays an error. Retry
after fixing the save location, or choose No / No to all. A failed Yes to all
does not partially consume decisions. The prototype retains inline minimum-
length feedback instead of the original notice dialog.

## Persistence

The native file is `EnglishUser.dic` under the launcher's **per-game save
path** (`GameTarget::savePath`), outside the original game folder. It contains
sorted, deduplicated uppercase ASCII words, one per CRLF-terminated line. The
reader also accepts LF, blank lines, lowercase and a final unterminated line.
Limits: 64 bytes per word, 32,768 unique words and 2 MiB of text. Invalid text or
unreadable files fail explicitly, rather than being overwritten as empty data.
An absent save path allows play/declining, but cannot approve additions.

The engine never writes the installed `Dicts/EnglishUser.dic`, and does not
automatically import or merge it. Both Classic variants share that target's native user list; a saved three-letter
word still cannot bypass Master's four-letter minimum. The list is loaded on
each engine start and consulted alongside the owner's main English dictionary.
The original dictionary files remain authoritative for main-list membership;
a user addition changes only that target's native word list.

Writes build a complete `.tmp` snapshot, check flushing/closing, move an
existing snapshot to `.bak`, then install the new file. This sequence works
with Windows rename semantics. If installation fails, the previous snapshot
is restored when possible. If the process stopped between renames, a missing
main file is read from `.bak` on the next load; unfinished `.tmp` is not trusted.
Memory and review decisions change only after successful installation. This
is recoverable replacement, not a guarantee against power loss or concurrent
writers. Non-regular files/symlinks at dictionary/snapshot paths are rejected,
and a save directory resolving inside the original game directory is refused.

## Verification

Synthetic unit tests cover eligibility, duplicates, provisional points, all
four decisions, blocked replay, failed approval, decline totals and use of
saved words after restart. Persistence tests cover normalization, corrupt and
oversized input, missing saves, snapshot recovery, write failure, symlinks
where supported, and unchanged synthetic original data. Scripted engine tests
exercise red history, prompt buttons/keys, all four choices, no Enter approval,
save-error recovery, scrolling, and a separately constructed engine loading
saved additions. Checksum records contain only metadata; no retail words,
boards, user dictionaries or captures are committed.
