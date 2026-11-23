# Classic completion work

The owner requested frontend appearance, dictionary features and presentation
comparisons after confirming the version-panel background. Item 2, dictionary
features, is now functionally complete for supported U.S. English Classic
Original/Master. Appearance and presentation comparisons retain their own gaps.

| Area | Current implementation/evidence | Still open |
| --- | --- | --- |
| Frontend appearance | Original/Master roster PN backgrounds, NP turn-handoff backgrounds, High1 ranking background; optional Nelson title rasterization; fallback and import coverage | Original button skins, exact font roles/metrics, remaining dialog styling; mode hover/audio triggers awaiting reference checks |
| Dictionary features | Complete for supported U.S. English Classic Original/Master: transactional add/remove/Clear; all four policies and original default/labels; immediate Yes/No refusal/retry/clock guards; authored lookup with exhaustive successful multi-hit ordering comparison; rich UTF-8 definitions, pronunciation, sense/emphasis layout, literal/root references and history; in-round/results navigation | Functional milestone closed. Owner Windows regression of this pass remains; exact 1997 typography/dialog appearance stays in the appearance item. |
| Presentation comparisons | Region-based frame comparison tool; all 814 frames and intro PCM match FFmpeg; rational frame-boundary checks | Original lighting/rasterization, idle cursor wandering and real application/device latency |

See [frontend](../../engines/boggle/frontend.md),
[dictionary editor](../../engines/boggle/dictionary-editor.md),
[definitions](../../engines/boggle/definitions.md) and
[presentation comparisons](../../engines/boggle/presentation-comparison.md).
The original VM question about mode hover/sounds is separate. Dictionary
labels/defaults and immediate behavior are now independently corroborated by
resources/control flow; an optional VM reply remains additional evidence. This tracker supersedes historical “pending Windows test” wording
for stages the owner has subsequently confirmed. The preceding frontend,
editor and presentation batch passed the owner’s Windows test.
The owner also confirmed the initial definition viewer works. Later lookup
extensions have native checks; detailed Windows coverage has not been separately
reported. The completed dictionary milestone still needs the owner’s Windows regression. Main, other branches and original files remain
untouched. All original bytes, screenshots and research artifacts stay outside
Git; implementations and synthetic fixtures are independently written.
