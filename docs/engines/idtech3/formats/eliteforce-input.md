# Elite Force: joystick input

How both of Elite Force's engines (`stvoy.exe`, single player, and
`stvoyHM.exe`, Holomatch) read a joystick, from `stvoyHM.exe`'s code
(addresses below are its). Both carry the same strings and settings;
single player adds force feedback (`ff_usejoy`), which Reluva doesn't do.
The engines read joystick 1 through Windows' `joyGetPosEx` once a frame.

## Settings

Registered at input start-up (0x43ec7e):

| Cvar | Default | Flags | |
|---|---|---|---|
| `in_joystick` | `0` | archive, latch | the joystick read at all; the Controls menu's Joystick line |
| `joy_threshold` | `0.15` | archive | dead zone, of the axis's full travel |
| `joy_xbutton` | `1` | archive | the x axis taken as keys rather than analog |
| `joy_ybutton` | `0` | archive | the y axis likewise |
| `in_joyBallScale` | `0.02` | archive | |
| `in_debugjoystick` | `0` | temp | prints each reading |

Patch 1.2's menus (Mouse/Joystick) set `joy_xbutton` and `joy_ybutton`
with the "X AXIS" and "Y AXIS" buttons ("ANALOG JOYSTICK X AXIS"); the
readme: "As a default, the X-Axis and Y-Axis operate in the analog mode.
... By turning X-Axis and/or Y-Axis Off, they will act like buttons".

## Each frame (0x43f580 on)

- **Buttons:** button *i* (from 0) pressed or released since the last
  frame is a key event for key 185 + *i*, `JOY1` to `JOY32` (the key
  name table: `JOY1` 185 ... `JOY32` 216, `AUX1` 217 ... `AUX16` 232).
- **Axes**, the first four only: the position less 0x8000, times
  1/32768, held to -1..1 (*v*).
  - x (axis 0) when `joy_xbutton` is 0, and y (axis 1) when
    `joy_ybutton` is 0: an analog event for that axis (`SE_JOYSTICK_AXIS`)
    with 0 inside ±`joy_threshold`, else `-(v * 127)` truncated: x pushed right
    gives a negative value, y pushed forward a positive one.
  - Otherwise each axis has two bits: *v* below -threshold sets bit
    2*i*, above +threshold bit 2*i*+1.
- **Hat**, when the device has one: exactly 0 sets bit 12, 18000 bit
  13, 9000 bit 14, 27000 bit 15 (diagonals set none).
- The 16 bits that changed are key events, from the table at 0x4ce698:
  134 135 132 133 (x: `LEFTARROW` `RIGHTARROW`, y: `UPARROW`
  `DOWNARROW`), 200 201 202 203 (third and fourth axes: `JOY16` to
  `JOY19`), 204–207 (never set), 208 209 210 211 (hat up, down, right,
  left: `JOY24` to `JOY27`).

So by default (`joy_xbutton 1`) the stick's x presses the arrow keys,
which `default.cfg` binds to turning (`LEFTARROW +left`), and its y moves
the player by how far it's pushed. No joystick key is bound by default.

## The client's use of the analog axes (CL_JoystickMove, 0x409f10)

`CL_JoystickEvent(axis, value)` (0x409ed0) keeps the value for axes 0–5.
With *s* the frame's seconds (times `cl_anglespeedkey` while `+speed` is
held):

- x: unless `+strafe` is held, yaw += *s* × `cl_yawspeed` × 0.01 × x;
  with it, `rightmove` = clamp(`rightmove` + x).
- y: with `+mlook` held, pitch += *s* × `cl_pitchspeed` × 0.01 × y;
  otherwise `forwardmove` = clamp(`forwardmove` + y).
- the third axis: `upmove` = clamp(`upmove` + it) (never sent: only x and
  y are analog).
- the walking button bit set unless running, as for the keys.

## Reluva

`client/joystick_input.cpp` reads the joystick SDL reports first (its
raw buttons, axes and first hat, in the device's order as Windows' API
gave them) and follows the above each frame; its keys run their bindings
(or go to the menus, so the Controls menu can bind `JOY1`...), its
analog axes go to `PlayerInput`. `in_joystick` takes effect at once
rather than on `in_restart`.
