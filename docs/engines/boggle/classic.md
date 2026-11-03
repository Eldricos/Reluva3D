# Classic: playable single-player slice

Scope: one local player, U.S. English, Original (4x4) or Master (5x5), typed
or mouse-selected words, configurable timed/untimed rounds, scores and results.
This is not complete support for Boggle's five modes or the original menu, animation, sound and networking systems.

## Rule provenance

The owner checked the original game in a Windows VM and reported Classic
Original's defaults: three minutes, minimum word length three, points by length
3/4/5/6/7/8+ = 1/1/2/3/5/11. This independently corroborates the executable
with SHA-256 `5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`:

- Classic's vtable at VA `0x474c34` selects options loader `0x40d400` and
  scorer `0x41a040`. The loader reads `GameLength` with default 180 seconds
  and converts to milliseconds; the scorer switches on complete string length.
- Board selection at `0x40be03` sets 4x4 and minimum three for Original;
  the alternate branch sets 5x5 and minimum four for Master. Both variants
  are now selectable; see [options evidence and tests](options.md).
- Classic array adjacency uses `0x407fb0`: horizontal, vertical and diagonal
  neighbors with row/column edge checks, with no wrapping.
- Word traversal `0x443790` marks a cube used at `0x4437e8`, tests that mark
  before revisiting, and clears it at `0x443900`. A cube cannot be reused
  within a word. It concatenates complete face strings, including multi-letter
  faces, before dictionary and word-length checks.

The native implementation uses independent row/column arithmetic, path search,
word-set lookup and a clock-driven round model. It does not execute the EXE.
The model rejects duplicate submissions after ASCII case normalization and
scores accepted words once. Original multi-player duplicate-word cancellation
is outside this single-player slice.

## Deliberate prototype behavior

The initial board uses the retail `EngOrg.dst` or `EngMas.dst` dice with a deterministic
native generator and seed. It is fixed across launches for reproducible
validation. New boards use the same independent generator with a new seed;
it is not the original executable's random algorithm or shuffle sequence.
The original generator at `0x443530` uses a different swap/face-selection
procedure. No seed-identical original behavior is claimed.

The screen is a simple native interface using the user's letter texture
atlases and an independently drawn layout/debug font. It does not recreate
PE dialog layouts or TrueType typography. Classic lid AVI animation and WAV
effects are now integrated: [presentation evidence](presentation.md). The
clock advances while the round is active, including when focus is lost;
matching original pause/focus behavior remains future work. Saved games,
definitions, AI, multiplayer and general game-state persistence are absent.
[English user dictionaries and rejected-word review](user-dictionary.md) are
implemented separately in the per-target save folder.

All original data and reference harnesses stay outside Git. Synthetic boards,
word trees, event-driven system tests and checksum records provide the
repository's regression coverage without retail assets.

## Validation

An external reference harness invoked the original scorer for lengths 0..32
with minimum lengths three and four (66 comparisons), and original adjacency
for every ordered cell pair on 4x4 and 5x5 boards (881 comparisons). All matched
the independent native model. A default-returning profile hook confirmed the
original options loader's 180,000 ms default. The harness and original bytes
remain outside Git.

For the supplied English dictionary and initial native seed `0x1997`, the
metadata-only `classic-check` finds 130 valid words, totaling 179 points. It
checks each acceptance and duplicate, verifies each path, and expires the
round at its deadline. Both supplied expanded trees produce checksum
`a91ea21b964e624c58011d9af99f0259`. This tests the native round and retail
resources; it does not establish original shuffle equivalence.

Synthetic parser, model and scripted-engine tests and both real-data reader
commands pass AddressSanitizer and UndefinedBehaviorSanitizer with leak
checking disabled because the execution tracer does not support it. The
native screen was also rendered with the owner's external letter atlases
and visually checked; no capture is committed. The owner subsequently compiled
and ran the playable slice in Visual Studio 2022 and completed one round
without issues. The first original-game VM report subsequently confirmed
immediate scoring, duplicate/minimum-length behavior and loss of unfinished
input at expiry. After the owner corrected one transcribed word, native replay
accepted all nine words and matched the original total of nine points without
any rule changes. Two additional owner rounds also match: eleven words for
eighteen points, and seven words for eight points on a Qu board. The latter
corroborates two-letter Qu faces and letter-based scoring; the former exercises
word lengths through six. Seven/eight-plus-letter play and bare-Q behavior
remain untested in the VM. Rejected-word history, deferred dictionary choices
and isolated persistence are now implemented; their additional provenance and tests are documented in
[user-dictionary behavior](user-dictionary.md). Findings and remaining checks
are in the
[VM comparison procedure](comparison.md).


Master and configurable duration support is documented separately in
[Classic options](options.md), including provenance, default/range checks and
metadata-only real-data verification. The owner reports the preceding English
user-dictionary workflow also works in the Windows build; this does not yet
establish Windows validation of the native Master build. The owner has now
confirmed the original's Master defaults, four-letter minimum, 30-second
duration increments, no-timer option and scoring table in the VM; see
[Classic options](options.md).
