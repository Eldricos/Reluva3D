# Classic completion work

The owner requested frontend appearance, dictionary features and presentation
comparisons together after confirming the version-panel background. This batch
makes progress in all three areas; it does not close every original-fidelity gap.

| Area | Implemented or verified in this batch | Still open |
| --- | --- | --- |
| Frontend appearance | Original/Master roster PN backgrounds, NP turn-handoff backgrounds, High1 ranking background; optional Nelson title rasterization; fallback and import coverage | Original button skins, exact font roles/metrics, remaining dialog styling; mode hover/audio triggers awaiting reference checks |
| Dictionary features | Transactional personal editor; deferred/immediate/auto-add/reject policies; immediate-prompt clock/focus guards; bounded definition decoder and post-round exact-headword viewer | Original definition morphology, pronunciation typography and rich layout; detailed original editor/options and immediate-review edge comparison |
| Presentation comparisons | Region-based frame comparison tool; all 814 frames and intro PCM match FFmpeg; rational frame-boundary checks | Original lighting/rasterization, idle cursor wandering and real application/device latency |

See [frontend](../../engines/boggle/frontend.md),
[dictionary editor](../../engines/boggle/dictionary-editor.md),
[definitions](../../engines/boggle/definitions.md) and
[presentation comparisons](../../engines/boggle/presentation-comparison.md).
The original VM questions about mode hover/sounds and dictionary labels/defaults
are pending. This tracker supersedes historical “pending Windows test” wording
for stages the owner has subsequently confirmed. The preceding frontend,
editor and presentation batch passed the owner’s Windows test.
The new definition viewer still needs Windows validation. Main, other branches and original files remain
untouched. All original bytes, screenshots and research artifacts stay outside
Git; implementations and synthetic fixtures are independently written.
