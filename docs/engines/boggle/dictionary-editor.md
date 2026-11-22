# Personal dictionary and unknown words

On Players, choose Dictionary / D to edit words you have added. Type 3–64
English letters and use Add / Enter; words normalize to uppercase. Up/Down,
Home/End, Page Up/Page Down, the wheel or a row click selects a word. Typing
also jumps to the nearest prefix. Remove / Delete removes the selected draft
row. Backspace edits the input and Ctrl+A replaces it on the next typing.
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
releases the notice hold, consumes its batch and then performs the normal Chess
handoff. Yes All/No All govern subsequent unknown words in that round and reset
on replay. Declined immediate entries remain reviewed entries for duplicate
checking. Exact original immediate rejection/duplicate/handoff edges and original
labels/defaults still need VM comparison. Existing deferred play is unchanged.

The original validator at `0x414d50` and unknown-word branch at `0x414f31`
provide static evidence for deferred, immediate, unconditional-add and rejection
routes. These native controls implement those four concepts with explicit save
failure and clock contracts; they do not claim exact original UI policies.
Word definitions are a separate archive-decoding task; see [research](definitions.md).

## Validation

Synthetic cases cover transactional additions/removals/clear, normalization,
restart, failed-write preservation, Done/Cancel and input cleanup; policy cases
cover provisional scoring, rejected words, automatic addition, save failures
and per-round All resets. Scripted Traditional and Chess rounds leave an
immediate prompt open across 90 seconds of wall time and focus changes, then
verify accepted words and final scores. Main-dictionary and ordinary deferred
regressions pass. SDL software runs exercise the installed editor in both
variants. Windows owner testing of the new controls is pending.
