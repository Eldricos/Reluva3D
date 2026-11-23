# Personal dictionary and unknown words

On Players, choose Dictionary / D to edit words you have added. Type 3–64
English letters and use Add / Enter; words normalize to uppercase. Up/Down,
Home/End, Page Up/Page Down, the wheel or a row click selects a word. Typing
also jumps to the nearest prefix. Remove / Delete removes the selected draft
row. Clear / F4 asks for confirmation before emptying the draft. No / Escape
closes that confirmation; Yes / Enter clears the draft. Backspace edits the input and Ctrl+A replaces it on the next typing.
Thirteen sorted rows are visible at once, up to 32,768 words.

Done / F2 saves the whole draft. Cancel / Escape discards all changes. A save
failure keeps the editor open for retry/cancel and preserves the prior file.
Emptying the draft is supported; a successful Done then saves an empty personal
dictionary. Global exits discard drafts and stop text input. Focus loss blocks
editing, and the focus-return batch cannot edit/save. Save/Cancel consume their
navigation batch, so queued Enter cannot start a round. The next round reloads
the saved dictionary. Player profiles, scores and the main dictionary are not
edited. No original personal dictionaries or registry files are imported.

`UserDictionary::replace` shares the existing bounded, symlink-rejecting snapshot
writer with round approvals. Both additions and replacements persist before
changing memory, preserve backup recovery and reject save paths within game
data. Cancel performs no write. Native limits/file format remain unchanged;
this is a native editor, not an emulation of the original dictionary dialog.

## Unknown-word choices

The launcher exposes Unknown words; Players' Words / W cycles the same four
policies for the session. The private value is `classic_word_policy`:

| Value | Traceable word missing from both dictionaries |
| --- | --- |
| `deferred` (default) | Count provisional points and review at round end, as before. |
| `ask` | Count provisional points and ask immediately; Yes keeps them, No removes them. |
| `add` | Save the word to the personal dictionary before counting it. |
| `reject` | Reject it without a list entry, score change or turn handoff. |

Known words, board paths, minimum lengths and duplicates retain their rules.
An automatic-add save failure adds neither points nor a list entry and keeps
input available for retry. Main-dictionary words never require a personal save.
Immediate review holds the shared gameplay clock, including Traditional/Chess
budgets and background computers. Focus holds are independent; returning to the
window cannot answer the prompt in the same event batch. A successful answer
releases the notice hold and consumes its batch. Immediate **Yes** persists and
counts the word, plays its recording cue and performs the Chess handoff.
Immediate **No** removes provisional points/history, keeps the current turn
and allows another attempt. It does not play the recording cue. Yes/No are the
only immediate choices; All buttons/keys are available at end of round.
Persistence failures keep the prompt open and preserve prior data. Existing
deferred play and all four end-of-round decisions remain unchanged.

The original All Games resource 10816/Unknown Words group supplies the exact
labels: **Prompt to add when entered**, **Prompt to add at end of game**,
**Always add**, **Never add**. The launcher uses those labels. The registry
load at 0x41df87–0x41df9f selects default 1 (end-of-game review). Control 15602
is **Clear**. Resource 16003 has visible Yes/No controls and initially hidden
All controls; the immediate validator tests No (ID 7) at 0x414ebe and returns
failure at 0x414eeb, before word insertion. Its pause/resume virtual calls
surround the dialog. These resource/control-flow observations close the
options/default/immediate-edge questions independently of a VM reply.

The supplied original UI has a Clear action rather than the native full word
editor. Add, remove, sorted browsing and transactional Done/Cancel are native
conveniences with explicit save semantics. The supported milestone is U.S.
English Classic Original/Master. Multilingual gameplay, automatic import of
original personal dictionaries and reproducing registry files are separate
engine work; none is required by the original English dictionary controls.

Word definitions have a separate archive reader and in-round/results viewer; see [definitions](definitions.md).

## Validation

Synthetic cases cover transactional additions/removals/clear, normalization,
restart, failed-write preservation, Done/Cancel and input cleanup; policy cases
cover provisional scoring, rejected words, automatic addition, save failures
and per-round All resets. Scripted Traditional and Chess rounds leave an
immediate prompt open across 90 seconds of wall time and focus changes, then
verify accepted words and final scores. Main-dictionary and ordinary deferred
regressions pass. SDL software runs exercise the installed editor in both
variants. The owner has validated the earlier controls on Windows; the completed pass
adds Clear confirmation, immediate refusal/retry and in-round lookup checks.
