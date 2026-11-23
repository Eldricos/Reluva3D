# Star Trek: Armada: key and mouse bindings (`Input.map`)

> **Status:** decoded · **Code:** `src/engines/storm3d/formats/input_map.cpp` · **Checked on:** the install's `Input.map`, and the CD's French and German ones · **Confidence:** layout confirmed; meanings inferred

## Overview

`Input.map`, in the install's folder, names each of the game's actions
and the keys and mouse buttons that trigger it: 198 actions, from
`left_click` and `cursor_x` to `attack` and `zoom_factor_plus`. The CD has
two more, `setup/Data_f/Input.map` and `setup/Data_g/Input.map`, for
French and German keyboards *(inferred)*.

`Gamekey.map` beside it is older: its own comment calls it "Old I'76
style key mappings" and says to use `Input.map` instead. It isn't read
yet.

## Layout

Text, with `#` comments to the line's end. Each action is a block:

```text
attack {
	+ keyboard	A
	- keyboard 	Control
	- keyboard	Shift
	- keyboard	Alt
}
```

| Line | Meaning |
|---|---|
| `<action> {` | The action's name, as the game knows it |
| `+ <device> <control>` | The control must be down *(inferred)* |
| `- <device> <control>` | The control must not be down *(inferred)*: so `attack` is A alone, and Ctrl+A something else |
| `}` | The block's end |

The devices are `keyboard` and `mouse`. The mouse's controls are its
buttons (`LeftBtn`, `LeftBtnDblClk`...) and its position (`HorizPos`,
`VertPos`). The keyboard's are named after keys: letters, `One` to
`Zero`, `F1` to `F12`, `Control`, `Shift`, `Alt`, and the keypad's and
arrow keys as `Grey...` (`GreyPlus`, `GreyUpArrow`).

### The French and German maps

They differ from the English one only in which keys: the French in 21
actions (`attack` is Q, not A), the German in 8 (`yellow` is Z, not Y).
Those are the letters' places on French and German keyboards, so a
control names a key's place on a US keyboard, not the letter on it
*(inferred)*.

## In Reluva

`InputMap` (`src/engines/storm3d/formats/input_map.h`) reads the blocks
into bindings, each with its conditions in order; a line of no known form
is kept as a problem.

- `reluva-tool storm3d input-map <Input.map>`: each action with its
  conditions.

## Sources and checks

- **Where the facts come from:** the three files; the meanings of `+`
  and `-` from the blocks themselves: the letter shortcuts each have a
  `+` for their letter and a `-` for each of Control, Shift and Alt, and
  `yellow` has `+` for Shift.
- **How it was checked:** all three read whole, 198 actions each, with no
  line left over; the unit tests (`storm3d.unit.input_map`) cover the
  forms, a stray line and a block left open.

## Open questions

- What the game does when two actions have the same conditions
  (`left_click` and `left_hold` are both `+ mouse LeftBtn`): it may tell
  them apart by how long the button is held *(guessed)*.
- `Gamekey.map`'s actions and whether the game still reads it.
