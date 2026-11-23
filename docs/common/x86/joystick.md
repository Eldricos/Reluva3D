# Joysticks

## What it is

`src/emulation/x86/joystick.cpp`: WINMM's joystick functions, for
programs run in the [x86 emulator](README.md) that read their joysticks
the way most Windows 95 games did, before DirectInput's: up to two
joysticks, whose state the engine sets from the players' controllers
(`System::readGamepad(index)`: the first controller, and the next pad
connected or else a racing wheel).

## What Reluva supports

- `joyGetNumDevs` (16, as Windows 95's driver says), `joyGetDevCapsA`,
  `joyGetPos` and `joyGetPosEx`. A joystick not plugged in answers
  `JOYERR_UNPLUGGED`; one past the sixteen, `JOYERR_PARMS`.
- **Axes** X, Y, Z, R, U and V, each 0 to 65535 with 32767 in the middle,
  X increasing to the right and Y down; the caps say which a joystick
  has (`JOYCAPS_HASZ` and on). **Buttons:** up to 32, as bits, and how
  many are held. **The hat:** in hundredths of a degree clockwise from
  ahead (`JOYCAPS_HASPOV` and `JOYCAPS_POVCTS`), `0xffff` when it isn't
  pushed.
- **A game controller as a joystick** (`Joysticks::fromGamepad`, `stateOf`),
  Reluva's choice, as an Xbox-style pad is usually laid out for an old
  game:

  | WINMM | The controller |
  |---|---|
  | X, Y | The left stick |
  | Z | The triggers: the right less the left |
  | R, U | The right stick |
  | Buttons 1 to 10 | A, B, X, Y, left and right shoulder, Back, Start, the left and right stick's click |
  | The hat | The D-pad, eight ways |

  A racing wheel comes as a pad does (`GamepadState`): the wheel as X,
  the pedals as Z (the accelerator less the brake), its buttons as the
  pad's in order, its hat as the D-pad. Its pedals are found as they rest;
  an engine names them where they aren't
  (`System::setWheelPedals(index, accelerator, brake)`, for either
  controller).

- **No forces:** WINMM's joysticks have none, so a program reading them
  gives the controllers none. Each controller can be given forces apart
  (`System::setGamepadForce(index, ...)`: a wheel's motor through SDL's
  haptics, a pad's rumble), for an engine with force feedback of its own.
- **Not yet:** a joystick's own registry settings (calibration, names),
  `joySetCapture` and its messages, `joyGetThreshold` and
  `joySetThreshold`, the Unicode `joyGetDevCapsW`.

## Sources and checks

- **Where the facts come from:** Microsoft's documentation of the WINMM
  joystick functions, and `mmsystem.h`'s structures (`JOYCAPSA`,
  `JOYINFO`, `JOYINFOEX`), flags and error codes. Jedi Knight's code
  confirms what it uses: `JOYCAPSA`'s ranges, `wMaxButtons` and `wCaps`,
  `JOYINFOEX`'s positions and `JOY_RETURNPOVCTS`
  ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
- `testJoysticks` (`common.unit.windows`): none plugged in, then a
  controller as the first: its caps, a stick, a trigger, a button and the
  D-pad as `joyGetPosEx` and `joyGetPos` give them; a second, read apart
from the first; a third not there; and the first unplugged again.
- Jedi Knight turned and walked with a controller scripted through
  `reluva-tool sith host -pad`, and turned with a second one bound in its
  *Setup*
  ([TESTING.md](../../engines/sith/games/jk/TESTING.md)).
- **Confidence:** published for the API; Reluva's own for the controller's
  layout.

## Used by

- **`sith`**: Jedi Knight's joysticks
  ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
