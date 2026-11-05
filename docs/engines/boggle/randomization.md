# Native board variety and reproducible checks

Normal Classic play now chooses a fresh native seed on each launch into a game,
including returning through Players from the menu. **New Board / F2** chooses
another seed. **Replay / Enter** retains the completed board and its cube
orientations, with the existing score, input and timer reset. Both Original
and Master, installed artwork and the fallback renderer use this behavior.

The existing independently written dice shuffle and face selection are
unchanged. They read the installed English dice sets and generate a complete
board without executing the original executable. A seed changes both the board
and the presentation-only quarter turns when auto-orient is off. Different
seeds can produce the same board; no guarantee of unique letter arrangements
is made. New Board still returns to Ready, and timer/animation start rules are
unchanged. Generating a board does not write to the original game folder or
create a save file.

## Seed source and controlled runs

The process initializes a native seed sequence from the high-resolution clock
and, when available, `std::random_device`. Failure of the optional entropy
service falls back to the clock. Each ordinary board takes the next value of
an atomic unsigned 32-bit sequence with an odd increment, so the process does
not reuse a seed until the 32-bit sequence wraps. The sequence is independent
of game timer ticks, input polling, animation frames and audio callbacks.
Across separate processes the entropy/clock start can repeat; this is ordinary
game randomization, not a cryptographic source or original RNG emulation.

Developers can set the target setting `classic_seed` to a decimal unsigned
32-bit integer (`0` through `4294967295`) for a controlled run. It is deliberately
not a launcher checkbox or ordinary gameplay option. An absent setting means
normal fresh seeds. Empty, signed, padded, non-decimal and overflowing values
are rejected before graphics start. A controlled New Board increments the seed
by one, wrapping from `4294967295` to zero; Replay retains it. Returning through
the menu starts again from the configured seed, making a replayable test case.
The target setting passes through the existing menu target copy.

Existing scripted tests explicitly request `classic_seed=6551` (`0x1997`),
retaining their previous boards and checksum records. The resource/model tools
such as `classic-check`, `artwork-check` and `all-words-check` continue to use
their explicit reference seeds; a fresh ordinary game does not change those
checksums. Original VM board comparisons should continue using externally
recorded boards, not an assumed equivalent seed.

## Evidence and scope

The previous fixed starting seed was a native prototype choice, recorded in
[classic.md](classic.md). This change completes the plan's ordinary-play seed
selection while preserving deterministic checks. The retail generator at
`0x443530` was previously identified as having a different swap/face selection
procedure; its executable identity and evidence are recorded in the Classic
page and inspection manifest. Its precise seed source, shuffle distribution
and random orientation assignment remain unverified. No additional retail
logic, face lists or third-party implementation was copied for this change.

`boggle.unit.seed` checks strict seed parsing, the process sequence's distinct
values, controlled restart, Replay, New Board, unsigned wraparound and menu
re-entry. Invented dice and coordinate-colored synthetic textures make seed
propagation observable in both variants and renderers. Tests check deterministic
boards explicitly, without asserting that random boards can never coincide.
Normal seed selection also completes scripted game/menu lifecycles. Existing
scripted records should remain unchanged because they now declare their seed.

All 30 Boggle checks pass with the supplied install. The normal full suite
passes 40 checks and skips 23 optional data checks. Seed and Classic model tests
pass ASan/UBSan with LeakSanitizer disabled for the execution tracer. External
SDL dummy runs using the installed resources completed two fresh games in
each variant, observed different rendered boards and verified matching Replay
frames. Only counts/checksums and native source are committed; the harness and
raw output remain outside Git. All 737 supplied installed files retain their
manifest hashes. Owner Windows testing of this stage is pending. No re-import
or save-file migration is needed.
