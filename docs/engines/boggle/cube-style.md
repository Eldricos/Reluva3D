# Classic cube-style selection

The launcher now offers **Cube style** for both Classic variants:

| Choice | Stored value | Behavior with installed artwork |
| --- | --- | --- |
| Automatic (default) | `auto` | Rounded when available, then angular; flat fallback if neither exists |
| Rounded | `rounded` | Use the rounded mesh and its measured pose/texture mapping |
| Angular | `angular` | Use the angular mesh and its measured pose/texture mapping |

An absent setting means Automatic, preserving existing launches and older
imports. Explicit choices load only the selected script; an unused alternative
can be absent or malformed. If the selected script is missing while installed
artwork is active, the error explains how to re-import or choose Automatic.
Malformed selected scripts still fail validation. Automatic also retains the
existing error for a malformed preferred mesh rather than hiding that error by
trying another one.

The choice is saved per target by the launcher's existing settings UI and takes
effect on the next launch. It applies to gameplay, settled lid overlays and
round-end review through the same renderer, including side letters, lighting,
selection and computer cursor destinations. Auto-orient remains independent.
Dice generation, seeded turns, word eligibility, timing and scores are unchanged.

With installed artwork disabled, the prototype ignores mesh selection. Older
imports without an artwork bundle also retain their existing prototype behavior.
The existing optional `Scripts/Rounded.x` and `Scripts/Die.x` imports are enough;
no new assets or dependencies are added. Re-import if your library predates the
[complete cube preparation](cube-geometry.md) and you want to choose a mesh.

## Original evidence and native policy

The executable identity is recorded in [die orientation](die-orientation.md).
Read-only analysis at `0x41d96d..0x41d99a` queries the mode's `CubeQuality` profile
value and selects `Scripts/Die.x` for zero, `Scripts/Rounded.x` for nonzero.
An external isolated probe supplies invented profile and resource services;
values 0, 1 and 2 produce the expected three resource-path results. The inspected
branch supplies profile default 1; another branch supplies 0. This alone does
not establish the original Classic UI's default, label or available controls.

The native selector uses descriptive choices and adds Automatic for compatibility.
It does not read or modify the original registry/profile. Exact VM controls are
still awaiting owner confirmation. This feature does not implement or infer the
original full/one-sided visual-switching behavior. All original files, raw
research and reference probes remain outside every Git worktree.

## Verification

Synthetic tests cover advertised choices and defaults, strict invalid-setting
handling, both styles/variants with auto-orient off, every cell's projected
center, mouse word entry and scoring, missing selected meshes, ignored malformed
alternatives, Automatic's preference/error behavior and the no-mesh/prototype
fallbacks. The full existing installed-data regression run checks that default
artwork and import digests stay unchanged. Focused sanitizer checks exercise the
new selection and renderer integration. External SDL runs load the owner's
actual resources for both styles and variants without copying them into Git.
