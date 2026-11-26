# Classic completion work

The owner requested frontend appearance, dictionary features and presentation
comparisons after confirming the version-panel background. Item 2, dictionary
features, is now functionally complete for supported U.S. English Classic
Original/Master. Item 1, frontend appearance, is now implemented for the same
scope; owner Windows regression is pending. Exact rendering comparisons retain their separate gaps.

| Area | Current implementation/evidence | Still open |
| --- | --- | --- |
| Frontend appearance | Complete for supported U.S. English Classic Original/Master: PN/NP/High1 art; transparent glass and native classic controls; runtime captions and dialog-unit layouts; sans/serif roles and alternating Nelson/KPSI handwriting; proportional name editing; hover clips, Battle startup and menu cues; optional imports and fallback coverage | Implementation milestone closed. Owner Windows regression remains. Exact Win95 bitmap-font/GDI pixel identity and device latency belong to presentation comparisons; five original rollover WAVs are not restored locally (playback logic covered with synthetic PCM). |
| Dictionary features | Complete for supported U.S. English Classic Original/Master: transactional add/remove/Clear; all four policies and original default/labels; immediate Yes/No refusal/retry/clock guards; authored lookup with exhaustive successful multi-hit ordering comparison; rich UTF-8 definitions, pronunciation, sense/emphasis layout, literal/root references and history; in-round/results navigation | Functional milestone closed. Owner Windows regression of this pass remains; the appearance implementation is covered by item 1, with exact pixel comparisons in item 3. |
| Presentation comparisons | Region-based frame comparison tool; all 814 frames and intro PCM match FFmpeg; rational frame-boundary checks | Original lighting/rasterization, idle cursor wandering and real application/device latency |

See [frontend](../../engines/boggle/frontend.md),
[dictionary editor](../../engines/boggle/dictionary-editor.md),
[definitions](../../engines/boggle/definitions.md) and
[presentation comparisons](../../engines/boggle/presentation-comparison.md).
The original mode hover/sound control flow is now independently traced and implemented;
the optional VM reply supplies additional corroboration. Dictionary
labels/defaults and immediate behavior are now independently corroborated by
resources/control flow; an optional VM reply remains additional evidence. This tracker supersedes historical “pending Windows test” wording
for stages the owner has subsequently confirmed. The preceding frontend,
editor and presentation batch passed the owner’s Windows test.
The owner also confirmed the initial definition viewer works. Later lookup
extensions have native checks; detailed Windows coverage has not been separately
reported. The completed dictionary milestone still needs the owner’s Windows regression. Main, other branches and original files remain
untouched. All original bytes, screenshots and research artifacts stay outside
Git; implementations and synthetic fixtures are independently written.
