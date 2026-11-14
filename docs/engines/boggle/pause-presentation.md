# Classic manual-pause presentation

With installed artwork active, **Pause** now covers the dice with the installed
closed-lid scene and shows the native Resume panel over it. Resume returns to
the existing board, selected path and orientations. This works with either
mesh style and with the flat installed-artwork fallback. The word list and
player labels remain visible; dice and the animated hourglass are not rendered
behind the closed scene. Gameplay entry hints and action buttons are hidden
until Resume; unfinished input is retained.

Window inactivity still uses the existing full-screen cover. Returning to the
window preserves a manual pause until Resume is clicked. The prototype view
also retains its full-screen pause cover. No shortcut is added. Human hand
artwork yields to the system pointer for Pause/Resume, as before.

## Observed contract and implementation boundary

Executable identity is recorded in [die orientation](die-orientation.md).
Classic's pause wrapper `0x429fa0` calls the base pause service, then lowers the
lid through `0x42a7c0` only when its explicit-pause argument is nonzero and the
lid-open state is set. Resume `0x429fd0` raises the lid through `0x42a010` for
an explicit pause when the lid is closed and the pause nesting is one, before
calling the base resume service. The focus handler at `0x4144b0` passes zero;
this does not request those explicit-pause lid actions.

An external isolated probe captures those lid calls with invented state and
intercepted base-pause/resume services. Both argument values, open/closed lids
and three nesting values give 24 passing wrapper checks. These establish
wrapper call conditions, not complete animation, dialog or audio behavior.
Original bytes, raw analysis and probe code stay outside every Git worktree.

The native pass uses the settled closed frame already loaded from the supplied
lid AVI (frame 54). It switches immediately to that still view and restores the
playing view on Resume. It does not add pause-specific closing/opening animation
or claim matching original modal-window styling. The renderer receives a display
flag; it never changes the round phase, board, selected path or player clocks.
Existing focus/manual/notice clock nesting and event guards are unchanged.
Music and effects retain their existing behavior pending the separate audio
comparison. Round-end review still has its independently documented lid policy.
No imports, dependencies, save formats or game files change.

## Verification

Synthetic cases compare the covered scene with the installed Ready closed view
for Original/Master and both meshes plus the flat fallback. Restoring the playing
view is byte-identical, including the selected cube. Scripted game input checks
the closed scene during Pause, the same scene after Resume and a successful
submission of input entered before pausing. Existing manual
pause tests verify clocks, foreground/background computer turns, focus nesting,
ignored keyboard shortcuts and discarded event batches.

The full regression run retains its existing default artwork and import records.
Focused ASan/UBSan checks exercise the renderer and Traditional/Chess pause flows.
External SDL software-backend runs inspect Pause, focus loss/regain, Resume and
cursor cleanup with the supplied resources in both styles and variants. Assets
and screenshots stay outside the repository. Windows comparison of this new
settled-pause view remains pending.
