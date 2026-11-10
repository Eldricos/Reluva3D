# Need for Speed III: car handling (`carp.txt` and the car tasks)

How `nfs3.exe` turns a car's `carp.txt` into driving: where the file's
fields go, which per-step tasks use them, and the race settings that
change them. Worked out in October 2026 from the retail `nfs3.exe` (its
disassembly) and runs in `nfs-host` into a Hometown race. The step
tasks themselves are listed in
[nfs-threads.md](../../engines/nfs-threads.md); this is what they do.

Status: **read from the code**, with the steering, tyres, hand brake,
landing, the computer cars' controllers and the police chase's start
checked at run time (below). Not
implemented in Reluva.

## In short

- `carp.txt` is read into a 0x4AC-byte record (P below). The car record
  keeps a pointer to it at **+0x528**. The race's set-up then changes P
  in place: the car set-up sliders, the automatic gearbox, the assists
  and codes (`0x4a78d6`).
- The **human's car** runs a four-wheel model every 32 Hz tick
  (`0x4ad7a3`, from the +0x608 task): engine and gears, brakes, four
  tyres with their loads, then the velocity and spin. The computer's
  cars use a simpler model (`0x4088f0`).
- Many `carp.txt` fields do nothing in this build: the gas curves,
  wheel base, tyre specs, tyre wear and the slide and turn fields
  (38-44). The tyres use the load, the lateral grip multiplier, the
  minimum steering acceleration and the set-up's Tire Factor.
- **Car dynamics** (`0x4a2830`, every car): gravity, downforce, the
  height over the road, and whether the car is on the road, landing or
  in the air.
- Movement is separate: position += velocity / 64 at 64 Hz (`0x4a2c60`).
- Units are SI: metres, m/s, kg, rpm. One tick is 1/32 s, so velocity
  changes are acceleration × 1/32 (`0x53dea0`), gravity is 9.81/32 =
  0.306563 a tick (`0x53c8dc`).

## The clock, the tick and the units of spin

Read from the code (November 2026: `asm`, `xref`, `callers`, capstone
over the exe; the constants read from the exe). This settles two
things that had been deduced from runs: the computer controllers'
yaw rates are in **turns a second**, and a `-watch` or `-watchmem`
sample taken once per call of a 32 Hz car task comes **32 times a
second** of game time.

- **The timer.** Start-up calls `0x48eff0(128)` (`0x4b6160`): a
  128 Hz multimedia timer ([nfs-threads.md](../../engines/nfs-threads.md)).
  The controls thread (`0x42dc20`) is woken on every 4th tick, so it
  makes 32 control frames a second (`0x48f675`-`0x48f680`: `0x79c4ac`
  + 1 per frame).
- **The step.** The race loop (`0x4c4e40`) runs two world steps per
  control frame (the target is 2 × `0x560104`), so `g_step`
  (`0x7d3684`, `inc` at `0x4c4a9c`) counts **64 a second**: the race
  clock is (step − 512) / 64, a cop's turn every 8 steps is 8 a second,
  the stop's 24 steps are 0.375 s.
- **The two task lists.** `0x4c48d0` runs Sc64 (`0x7d368c`, at
  `0x4c4a97`) every step, and Sc32 (`0x7d3690`, at `0x4c4a7f`) only when
  the step is even (`test byte [0x7d3684], 1` at `0x4c4a6a`): 32 a
  second. Every car task in the tables below that is said to run "each
  tick" is in Sc32 (the human's `0x4ad7a3`, the computer's `0x4088f0`,
  the dynamics `0x4a2830`, the drivers, the collisions); the position
  (`0x4a2c60`, × `0x53c8ec` = 1/64) and the human's orientation are in
  Sc64.
- **The tick's length in the code.** The human's tick uses `0x53dea0`
  (0.03125); the computer's model stores `0x3d000000` (1/32) into
  `0x56ec58` at `0x408921` on each call, and `ai_integrate` (`0x4071a0`)
  adds the forces × that to the velocity and the torques (+0x2F4,
  +0x2F8, +0x2FC, put into the world by the car's rows) × that to the
  spin +0xE4, +0xE8, +0xEC (`0x407328`-`0x40736e`). The landing's own
  1/32 is `0x53c8f4`.
- **The spin's unit.** The orientation task divides the spin by the
  number in edx (`fild`, `fld1`, `fdivrp` at `0x4a2e6a`-`0x4a2e6f`)
  and turns the body by the result about each axis with `0x4dfd80`,
  `0x4dfde0`, `0x4dfe40` (from `0x4a2cc0`), which take turns (× 2π,
  `0x54b6b0` = 6.283185). For a computer-driven car it is
  `0x4a2f10` (`mov edx, 0x20`, in Sc32: 32 calls of spin/32 a second),
  for a human `0x4a2f30` (`mov edx, 0x40`, in Sc64: 64 of spin/64).
  Either way a spin of 1 turns the car one turn a second.
- **The controllers' yaw.** `0x4088f0` puts the spin into the car's
  frame (+0x4FC is the yaw part, stored at `0x408a4b`); `0x4076b0`
  reads it at `0x40789d` and compares it with its wanted rate (−2.5 ×
  the heading error, which is in turns: `0x4070c0`, ±0.5), and the
  three controllers write the torque to +0x2F8 (`0x407c01` for
  `0x407c30`), which `0x4071a0` adds × 1/32 to +0xE8. So the wanted
  yaw rates (±0.8, ±1.6, ±3) are turns a second: 0.8 is 288°/s.
- **`d7-watch2`'s rate.** Its 1,308 samples (one per `0x4088f0` call
  for car 1) over 41 s are 31.9 a second, as the Sc32 list gives;
  the `gh1-ai` counts (about 1,300 controller calls per car by 90 s,
  from the start at 48.9 s) are the same rate.

## The race settings named

From the front end's option table (`0x5563fc`, 20-byte rows: name,
two lists of text numbers, a third, the variable) and `text.eng`:

| Variable | Option | Values |
|---|---|---|
| `0x6fd3b8` | `racetype` | 0 Single Race (text 213), 1 Tournament (214), 2 Knockout (215), 3 Hot Pursuit (220) |
| `0x5556d8` | `multracetype` (row `0x5563e8`) | Single Race, Hot Pursuit, Knockout (Beginner), Knockout (Expert), Tournament (Beginner), Tournament (Expert) (texts 213, 220, 218, 219, 216, 217) |
| `0x6fd470` | `gamelevel` | 0 Beginner (400), 1 Expert (401): the tournament's and knockout's level |
| `0x6fd4f0` | `ai` | 0 Normal (175), 1 Aggressive (143) |
| `0x6fd474`-`0x6fd488` | `braking`, `steering`, `bestline`, `navigator`, `collision`, `traction` | on, off; copied at a race's set-up to `0x6fd490`-`0x6fd4a4` (`0x473c36`) |

`0x6fd3b0`, the kind of game, is set from the link the player picked
(`0x440761`-`0x4407f7`, strings at `0x5378f8`-`0x537910`): 0 one
player, 1 two on one machine, **2 modem, 3 serial, 4 IPX, TCP or
`racenet`**.

`0x6fd520` is **the number of computer racers** (list `0x5efb88`). At
set-up (`0x473cd9`): with `oppnumber` (`0x6fd4d0`) Full Grid it is 8 −
the humans (`0x6fd518`), with One 1, with None 0. The loop at
`0x4c7c49` sends each one's u16 +8; the music's lap tables
([mus.md](mus.md)) are the one-opponent case.

Tournament and Knockout (and network games) skip the set-up sliders
and the assists below: everyone drives the car as its `carp.txt` says.

The cheat codes are bits of `0x6fd4fc`, set by `0x4525ed`-`0x452861`
from the names at `0x5594e4` (each letter + 9): 1 `GOFAST`, 2 `TDRIVE`,
4 `MONKEY`, 8 `RUSHHOUR`, 0x10 `BULLHORN`, 0x20 `MADLAND`. `MADLAND`
makes the tyres slide more in rain and snow (`0x4abde0`, below).

## carp.txt in memory (P, at car +0x528)

The writer `0x4b2340` (the debug dump of a car's values) walks the
label table at `0x55eddc` (80 names; the "(n)" numbers come from
`0x4af3b2`) and shows where each field lives. Arrays of 8 are by gear:
0 reverse, 1 neutral, 2 first, and so on, the same index as the gear
byte +0x50B.

| P | Field | Used by |
|---|---|---|
| +0x0 | Car Classification | |
| +0x4 | mass | the acceleration factor (`0x4a8179`); collisions (`0x42bd3f`) |
| +0x8 | number of gears | gear changes, drag set-up |
| +0x10 | gear shift delay (ticks) | gear changes (`0x4a9423`, `0x4aa0b4`) |
| +0x14, +0x34 | shift blip, brake blip [8] (int rpm) | rpm while a shift is under way (`0x4ab254`, `0x4ab276`) |
| +0x54 | gear ratios [8] | not read in the race (only copied) |
| +0x94 | velocity to rpm ratio [8] | wheel rpm (`0x4ab43e`), the factors below |
| +0xD4 | gear efficiency [8] | the acceleration factor (`0x4a81ab`) |
| +0x114 | torque curve [41], 256 rpm a step | the engine (`0x4a8a99`) |
| +0x1B8 | final gear | only copied |
| +0x1C0, +0x1C4 | engine minimum rpm, redline (int) | the engine, gears |
| +0x1C8 | maximum velocity | the drag set-up, a speed cap |
| +0x1CC | top speed cap | race end and the AI (`0x4ce2e9`, `0x4093ab`) |
| +0x1D0 | front drive ratio | drive split front/rear (`0x4adc18`) |
| +0x1D4 | ABS (byte) | the tyres (`0x4acd99`) |
| +0x1D8 | maximum braking deceleration | the brakes (`0x4add0b`) |
| +0x1DC | front bias brake ratio | the brakes (`0x4addae`) |
| +0x1E0, +0x1E8 | gas increasing, decreasing curve [8] (bytes) | not read in the race (below) |
| +0x1F0, +0x210 | brake increasing, decreasing curve [8] (floats) | the brake pedal's ramp (`0x4a9e86`, `0x4a9f2c`) |
| +0x230 | wheel base | not read in the race |
| +0x234 | front grip bias | load split front/rear (`0x4adea1`) |
| +0x238 | power steering | force feedback (`0x4718d1`) |
| +0x23C | minimum steering acceleration | the wheels' angle (`0x4ad613`), the tyres' lateral cap (`0x4acf3a`) |
| +0x240, +0x244 | turn in ramp, turn out ramp | the steering's ramp (`0x4aa354`, `0x4aa2c2`) |
| +0x248 | lateral acceleration grip multiplier | tyre loads (`0x4ade33`), tyres (`0x4acbec`), `0x4ae6ae` |
| +0x24C | aerodynamic downforce multiplier | `0x4a22b9`, `0x4aad74` |
| +0x250 | gas off factor | engine braking (`0x4ab6f7`) |
| +0x254 | g transfer factor | weight transfer (`0x4ae10a`) |
| +0x258 | turning circle radius | steering (`0x4aec27`) |
| +0x25C, +0x268 | tyre specs front, rear (3 ints) | not read in the race |
| +0x274-+0x290 | tyre wear, slide multiplier, spin and slide velocity caps, slide assistance (int), push factor (int), low and high turn factor | not read in the race |
| +0x294 | pitch roll factor | body pitch and roll (`0x4295b7`, `0x42a806`) |
| +0x298 | road bumpiness factor | `0x42952a` |
| +0x29C, +0x2A0 | spoiler type, speed | `0x4ab2f9` (type 8 changes the rpm fall) |
| +0x2A4-+0x2B8 | turn cutoffs (int) and speed modifiers | the computer's cars (`0x4a85e5`-`0x4a8686`) |
| +0x2BC | subdivide level | |
| +0x2C0 | camera arm (written with `0x4b08d0`) | the camera (`0x424484`) |
| +0x2C4-+0x2E8 | the damage and tuning fields (57-66: body, engine, suspension damage, then the seven set-up values) | not read in the race but +0x2E0, Suspension Stiffness: the landing's bounce (`0x4a1d7d`). The set-up sliders come from elsewhere (below) |
| +0x2EC | AI acceleration table, 8 × 14 floats | the computer's cars (`0x40828b`, `0x40657d`) |

"Not read in the race": the only code that touches these fields is
the parser (from `0x4b0bb0`, which first copies the defaults at
`0x55f874`; its stores at `0x4b0e64`-`0x4b106d`, `0x4b1850`-`0x4b1c0d`),
the debug writer `0x4b2340` (`0x4b3033`-`0x4b39e8`) and `0x4b48d0`,
which compares two P records field by field (80 flags). A search of
every read at these offsets, from any register, finds nothing else, and
P is never copied (the only 0x4AC-sized copy is the parser's
defaults). So the gas curves, wheel base, tyre specs, tyre wear and the
seven slide and turn fields (38-44) **do nothing** in this build. The
brake curves were missed before because they are read with a computed
index (P + 0x1F0 + 4 × (brake / 32)).

## The race's set-up (`0x4a78d6`)

Called once per car at set-up (`0x42c3c8`).

- **Automatic gearbox** (the player's `0x6fd548` + car × 0x6C is 1, and
  not the `MONKEY` code): the automatic fields (+0xC, +0x1BC, +0x74,
  +0xF4, +0xB4) are copied over the manual ones.
- **The set-up sliders.** The car's set-up record (car +0x220, +0x40),
  7 floats, default all 1 but brake balance 0 (`0x55d024`). For a
  computer car, and in Tournament, Knockout and network games, the
  defaults are used. Each changes P:

  | Slider | Changes |
  |---|---|
  | +0x40 Engine Tuning t | each torque value −= (t − 1) × 0.1 × value × (r − i)/(R − r), r = (min + redline)/2/256 + 3, R = redline/256 + 3 (`0x4a7f25`); max velocity × (1 + (t − 1) × 0.1) |
  | +0x44 Brake Balance b | front brake bias −= b |
  | +0x48 Steering Speed s | min steering acceleration × s; front grip bias × (1 + (s − 1) × 0.625), kept in 0.46-0.56; turning circle + (1 − s)/2 × circle |
  | +0x4C Gear Rat Factor g | velocity to rpm × g; max velocity / g |
  | +0x50 Suspension Stiffness k | g transfer × k, kept in 0.3-0.7; pitch roll / f(k) (`0x4ea920`) |
  | +0x54 Aero Factor a | downforce × (1 + (a − 1)/4); max velocity / (1 + (a − 1) × 0.05); the drag's scale (below) |
  | +0x58 Tire Factor | tyre grip (`0x4abd9c`, `0x4ae70a`) |

- **Braking assist** (`0x6fd490`): front brake bias + 0.05, g transfer
  − 0.05. The front brake bias is then kept in 0.3-0.75.
- **`GOFAST`** (`0x6fd4fc` & 1): torque × 1.75, max velocity × 1.75,
  every gear's efficiency at least 1; wheel grip × 1.41.
- **The per-gear factors**, into the car record:
  - +0x54C[g] = 1 / (velocity to rpm[g]), m/s per rpm (0.01 when 0);
  - +0x56C[g] = (velocity to rpm[g] / mass) × 0.1 × efficiency[g]:
    acceleration (m/s²) per N·m of engine torque. (0.1 is about 2π/60:
    velocity to rpm is gear × final × 60 / (2π × tyre radius); the
    Corvette's first four gears fit its 275/40R18 rear tyre exactly.)
  - +0x52C[g] = 1 / (redline × +0x54C[g]), one over the gear's top
    speed.
- **Drag** (+0x58C), so that top gear just reaches the maximum velocity
  v: A = torque(v × v2rpm[top] / 256) × +0x56C[top] / efficiency[top];
  +0x58C = 0.98 × A / ((2/3) × 0.367572 × v³) (with the Aero slider,
  (1 + a)/3 in place of 2/3).

## The engine and gears (`0x4aaed1`)

Returns the drive acceleration along the road. The car's fields:
+0x590 rpm (int), +0x50B gear, +0x5F0 throttle 0-1, +0x5EC brake 0-1,
+0x510 shift timer, +0x50D the hand-brake flag (below), +0x4F4 speed
along the road.

- **Drag**: D = +0x58C × v³ × 0.367572 × 2/3 (`0x4ab000`), v = +0x4F4;
  over the maximum velocity, + 0.01 × (|v| − max)².
- **Over-rev**: rpm above redline + redline/3 sets +0x50D; rpm is
  kept at most at the redline.
- **Neutral, shifting or in the air**: the rpm moves by itself. Up 250 a
  tick towards max(min rpm, redline × throttle); while a shift runs the
  blip is added; else it falls by 150 (or 25-75 by spoiler type and
  shift delay). No drive.
- **In gear on the road**: wheel rpm = |v × v2rpm[gear]| (`0x4ab43e`).
  The torque is the curve at the engine rpm, linearly between the 256
  rpm steps (`0x4a8a99`, rpm / 256 kept in 0-40), and acceleration =
  torque × +0x56C[gear], at most 10 m/s² in first, 8 in the others,
  −6 in reverse. Then:
  - target (redline × throttle, at least min rpm) below the wheel rpm:
    **engine braking**, −acceleration × gas off factor, against the
    motion; the rpm follows the wheels;
  - else the rpm moves up to 200 a tick towards the wheels' (at least
    min rpm, at most the target), and the acceleration is × throttle ×
    (1 + 0.6 × |+0x5B0|, at most 1.5);
  - faster than 1.15 × the gear's top speed (not in reverse or
    neutral): the acceleration is × top speed / |v|, the spin flags
    are set (+0x594, +0x50D) and +0x514 counts up by the weather
    (`0x4ab914`-`0x4aba6d`).
- The result: on the road, acceleration − D going forward,
  acceleration + 2D going backward; in the air −D/2 or D.

## Gears

The gear is +0x50B (0 reverse, 1 neutral, 2 first...), the gear asked
for +0x506, the gear before +0x50A, +0x50C 1 for a change down. A
change sets +0x510 to the shift delay (P +0x10, ticks) and plays sound
13 (`0x416d90`) for the player.

- **Manual** (`0x48f6e0`, from `0x4314c4`): control bit 0x10 asks one
  gear up (to number of gears − 1), 0x20 one down (to reverse).
  `0x4aa1b4` makes the change at once. `0x4a9769`, every tick in gear:
  in fourth or higher below first gear's top speed (redline ×
  +0x554), it drops to third.
- **Automatic** (`0x48f710` for the keys; `0x4a90a0`, every tick in
  first or higher). With R the redline, the "rpm" of a gear g is
  int(+0x5A4 × velocity to rpm[g]):
  - the top gear used is the last, or the one before it when that one
    already reaches the maximum velocity (redline × +0x54C[n − 2]);
  - **change up** when the rpm in this gear is over **R − R/8**
    (5,250 for the Corvette) and this isn't the top gear, but only
    with throttle over 0.9 or the rpm over R;
  - **change down** two gears or one with throttle over 0.9 (only one
    with less throttle), when the rpm in that gear would be under R − R/6 (under
    R/2 when that gear is first) and |+0x5B0| < 0.05 (0.1 with less
    throttle, one gear down);
  - from neutral or reverse, first is picked as the last gear whose top
    speed (redline × +0x54C[g]) is under the speed (`0x4aa137`), so
    first when stopped.
- **Both boxes** (`0x4a97cb`, from the controls): from step 572 on,
  stopped (|velocity| < 0.5) with the brake held over half and no gas,
  first or neutral changes to reverse; in reverse the gas and brake
  bytes are swapped, so the brake key drives. Stopped with no gas in
  reverse or first and the rpm under min + 50, the car goes to
  neutral, its brake is held full and its road-frame velocity zeroed
  (`0x4a9b07`-`0x4a9b52`). Before step 572 the automatic always asks
  for first (`0x4aa004`).

Checked at run time (`-watch 0x4ab42f`, the gear in dl; `-watch
0x4ab44b`, the wheel rpm in eax; the speed by `-watchmem`), the
Corvette with the throttle held from rest on Hometown's grid: the wheel
rpm was the speed × 271.8 in first and × 144.8 in second, the
**automatic** gearbox's velocity to rpm ratios (the default gearbox,
copied over the manual ones), not the manual's 256.6 and 171.7. 0 to
16.7 m/s took 2.3 s in first (7.3 m/s², under the cap of 10). With
`-watch 0x4a93ad` (the gear's rpm in eax) and `-watch 0x4a9414` (the
new gear in al), the changes up came at 5,261 rpm (to second, 55.1 s)
and 5,265 (to third, 64.9 s): the first tick over 5,250. The 5,237
seen before was the engine's rpm a tick earlier.

## The human's car each tick (`0x4ad7a3`)

All in the **road's frame**: the matrix at car +0x190 (rows right
+0x190, up +0x19C, forward +0x1A8, from the road under the car).

1. Velocity into the road's frame: +0x4EC lateral, +0x4F0 up, +0x4F4
   forward (zero before step 512, the start). Spin likewise into
   +0x4F8.
2. Surface grip +0x20C = `0x55ed30`[+0x18C]: 16 values, 1.0, 1.0, 0.75,
   0.8, 0.98, 0.8, 0.65, 0.95, 0.75, 0.6, 0.98, 0.95, 0.95, 0.8, 1.0,
   0.6.
3. Controls (`0x4a9bc4`), the front wheels' angle (`0x4ad2e1`), both
   pedals (`0x4aeffc`), the drive (`0x4aaed1`); all below. The drive is
   split: each front wheel × front drive ratio, each rear wheel × the
   rest. The front wheels' slip velocity gets + Y, the rear − Y, a yaw
   term (`0x4af254`): Y = |+0xE8| × 32 × +0xB0 × +0x100 × k, k =
   0.0125 × +0x5A4 − 0.25 kept in 0.1-1, halved in reverse. The code
   takes the absolute value of the yaw rate, so Y has one sign.
4. Each wheel (4 records of 0x30 bytes on the stack) starts with −½ of
   its own slip velocity × 32 (`0x4ad9d7`): the force that would stop
   half the sliding this tick.
5. **Brakes**: brake × max braking deceleration, at most |v| × 32, against
   the motion; front wheels × front bias (+ 0.05 when spinning), rear
   the rest.
6. **Loads** (`0x4aab41`): each wheel's grip +0x5D8-+0x5E4 = road normal
   y (kept 0.75-1) × its surface's grip, × 0.85 in rain or 0.8 in snow
   (+0x160 1 or 2), × 1.41 with the traction assist (`0x6fd4a4`) or
   `GOFAST`. Over 10 m/s, downforce: +0x5C8.. = 1 + v × downforce (rear
   × 1.5). Then wheel load = −gravity's up part × lateral grip
   multiplier × wheel grip; front × front grip bias, rear × (1 − bias).
   **Weight transfer** +0x5BC = forward acceleration (+0x4E8) × g
   transfer − gravity's forward part × 0.1 (`0x4ae116`), moved from
   front to rear (`0x4ae5c2`).
7. **Tyres** (`0x4aca4f`, each wheel; below).
8. **Integrate**: forces averaged by axle into +0x4E0 (lateral) and
   +0x4E8 (forward), into the world (+0x2E8), velocity += /32
   (`0x4ae188`). Yaw: (front − rear lateral) × +0xB0 × +0x100 / 32 into
   the spin (+0xE4-+0xEC). Then `0x4ae79b`: gas ≥ 0.5, brake ≥ 0.75,
   |steering| > 64 and under 5 m/s zeroes the velocity. Lateral damping
   (`0x4ae85b`): the lateral speed × 0.9 under 1 m/s, × 0.99 over 2,
   linearly between. With +0x50D the speed × 0.9985 or 0.9995
   (`0x4ae98b`). **In neutral** (`0x4af140`, from `0x4ae4b3`) on a road
   whose gravity along it is under 0.5 m/s² (`0x7a3a48`): velocity and
   spin × 0.99 a tick under 20 m/s or with |steering| > 32, else ×
   0.998.

In the air or tipped (+0x8A set, or +0x10C < 0.9, `0x4ae4c1`) the rpm
falls 500 a tick to the minimum and nothing drives.

## Controls (`0x4a9bc4`, each tick)

The control frame is unpacked into the car at `0x431462`-`0x431545`:
+0x504 gas (byte), +0x505 brake (byte), +0x518 steering (signed byte,
−128..127), +0x506 the gear asked for, and from the bits of
`0x79c4ab`: 0x04 → **+0x50D**, 0x08 → +0x50E, 0x80 → +0x512, 0x40 the
view. +0x50D is the **hand brake**: bit 0x04 comes from slot 6 of the
key bindings (`0x48f1f0`, `0x48f550`), **Space** by default ([net.md](net.md), the input word),
and it acts while held (the edge mask 0xF2 at `0x48f6be` leaves it
alone). Over-rev and over-speed also set +0x50D. Checked at run time
(`sw1-a`): with Space held from 64.0 to 68.0 s, `0x4314d8` (bit 0x04
set) ran 128 times, from 64.05 to 68.03 s, 32 a second, and at no
other time.

- **Gas**: +0x508 moves towards +0x504 by at most 16 a tick up and
  16 down (+ (set-up +0x48 − 1) × 60 down, single races only).
  Throttle +0x5F0 = +0x508 / 255. The gas curves are not used.
- **Brake**: with the flag at +0x2C of the car's set-up record (car
  +0x220), +0x509 moves towards +0x505 by brake increasing[+0x509 / 32] a tick up and brake
  decreasing[+0x509 / 32] down (`0x4a9e28`-`0x4a9fa6`); without it
  +0x509 = +0x505. Before the start (`0x7d3680` 0) the brake is held
  at 255.
- **Steering**: with the set-up record's flag +0x24 (on with the keys), +0x51C moves
  towards +0x518 by at most the **turn in ramp** a tick (16 for the
  Corvette: full lock in 8 ticks), or back towards 0 by the **turn out
  ramp** (32) when the two have opposite signs (`0x4aa278`-`0x4aa3c3`).
  Without it (an analogue wheel) +0x51C = +0x518.
- With the braking or steering assist on, `0x4a83d3` drives the pedals
  instead of the ramps (The assists' driver, below).
- A car with +0x200 bit 1 and +0x2C4 = 3 (out of the race) gets no
  controls, neutral, +0x50D set and its velocity and spin × 0.98 a
  tick (`0x4a9c32`-`0x4a9ccc`).
- **Both pedals** (`0x4aeffc`): gas and brake both ≥ 32, on the road,
  slow (under 4 m/s, or under 5 in reverse or neutral): velocity and
  spin × 0.8 a tick.

## The front wheels' angle (`0x4ad2e1`)

In turns (one turn = 2π, as `0x496bf0` and `0x4ea930` take angles).
s = +0x51C, v = +0x4F4:

1. In the air: 0.
2. With +0x50D, moving, steering against +0x5B0's sign: s × max(0.85 −
   0.0056 |v|, 0.55).
3. Else over 26.7 m/s with |+0x5B0| > 0.05 and steering the same way:
   s × 2|+0x5B0| when that is under 1; over 13.4 m/s the same with
   0.15.
4. Over 40 m/s: s / (0.015 |v| kept in 1-1.5): from 67 m/s up to a
   third less lock at 100.
5. angle = s / 128 × **minimum steering acceleration** × 1.5 / 360
   turns. For the Corvette (17.1) full lock is 25.4°.

The code then clamps the angle to ±1 but returns the unclamped value.

Checked at run time (`sw1-a`, the Corvette, keypad 6 held; `-watch
0x4ad663`, the angle in eax, and `-watchmem` on +0x51C): +0x51C went
16, 32 ... 112, 127 a tick and back 95, 63, 31, 0, and every angle was
+0x51C / 128 × 17.1 × 1.5 / 360 to the last digit (0.070693 turns at
127). With Space held as well the angle was × 0.6965 at first, rising
to × 0.755 as the car slowed: rule 2's 0.85 − 0.0056 |v| for 27 to 17
m/s; on some ticks it was × 1 (+0x5B0's sign the other way).
The angle turns the front wheels' slip velocity into the wheel's
frame (`0x496bf0`) before the tyre and the tyre's force back. The
yaw comes from the axles' lateral forces (step 8 above).

## Tyres (`0x4aca4f`)

Each wheel record (0x30 bytes on the stack at `0x4ad7a3`'s ebp−0x14C,
front pair first): +0 drive and brake force, +4 load, +8 downforce
factor, +0xC steer angle (front), +0x10, +0x18 the lateral and forward
slip "forces" (−16 × the wheel's slip velocity), +0x1C, +0x20, +0x24
the lateral, up and forward force out, +0x28 1 for a front wheel,
+0x2C the skid level. The forces are accelerations (m/s²).

S, the **Tire Factor** (set-up +0x58, t; 1 in Tournament, Knockout and
network games), is a loss of grip (`0x4abd9c`):

| Weather (+0x160) | S | at t = 1 | with `MADLAND` |
|---|---|---|---|
| 0 dry | 1 / max(28 − 40(t − 0.8), 16) | 0.050 | the same |
| 1 rain | 1 / max(24 − 32(t − 0.8), 6) | 0.057 | 3 / max(16 − 32(t − 0.8), 6): 0.31 |
| 2 snow | 1 / max(16 − 24(t − 0.8), 4) | 0.089 | 3 / max(12 − 24(t − 0.8), 4): 0.42 |

G = load × (1 − S) is the wheel's grip.

1. Rotate the slip forces by the steer angle.
2. When the drive force and the forward slip force have the same sign,
   the drive is limited to the slip force (braking can't reverse the
   car) unless driving with throttle over 0.25 in that direction.
3. Drive force × **lateral acceleration grip multiplier** (3.3).
4. With +0x50D, +0x514 counts up 1 a tick (3 in rain, 4 in snow) to
   384; else 0. A rear wheel then goes to `0x4ac094` (below), a front
   one too when its drive force is over its load.
5. **ABS** (P +0x1D4), when the force is over the load: clamp it to the
   load; the skid level is 3 every 4th step, else 0. Without ABS
   nothing is clamped here.
6. **Slip angle** α = atan2(lateral, |forward|) / 2π (turns,
   `0x4ea9c0`). The lateral limit L = |4 G sin(πα)| (|2 G sin(πα)| in
   reverse), at most 2 × **minimum steering acceleration** (34.2).
   Under 13.4 m/s with no gas (under 0.015, or under 0.02 at full
   lock): L / 4.
7. The lateral force out = the lateral slip force kept within ±L,
   **× 0.8 at the front, × 1.4 at the rear**. A front wheel with
   +0x50D under 0.5 m/s: 0. Up 0. Forward = the drive force.
8. **Friction circle** (`0x4ac3af`): M = max(|lat|, |fwd|) + ¼
   min(...). Only when M > G:
   - traction assist on, rear wheel, |+0x5B0| > 0.15: both forces ×
     G/M (the lateral only above first gear); skid level 2G on three
     steps of four;
   - else: skid level = (old × k + (M − G) × k), k = 0.95 − t/4 (0.7
     in Tournament...), at least 0.5; the kinetic grip G − R, R =
     min((M − G)S, GS) × 1 dry, 1.5 rain, 2 snow. Only a rear wheel in
     first on a surface of grip ≥ 0.95, throttle ≥ 0.85 and rpm ≥ 0.85
     × redline has its forces scaled to it (× (G − R)/M; the forward
     × another 0.5 without traction assist): the launch's wheelspin.
     Otherwise the forces stay over the circle.
   - A rear wheel over 1.15 × the gear's top speed (+0x594): skid level
     = M, × 4 in first and second.
   - The skid level is kept at most int(12 − 4t) (`0x4ac027`) and
     stored at +0x5C0 (front) or +0x5C4 (rear); `0x42a699` (× 7.5, at
     most 10) and the force feedback (`0x47179f`) read it.
9. Rotate the forces back by the steer angle.

Checked at run time (`sw1-c`): `-dumpmem at=0x4acf74#N` of the stack
(the frame is at the same place each call, ebp `0x17fdb5c`) and of the
car, four wheels at 61.7 s (23.7 m/s, third gear, full right lock) and
four at 62.3 s (wheels straight). For each, G = load × 0.950 (dry, t =
1), α = atan2(lateral, |forward|) / 2π, and L = 4 G sin(πα) agreed with
the stored values to the last digit: front 0.1010 turns and L 22.6 with
the lock on, rear 0.0295 and 7.75. The forward slip force was about
−16 × the car's speed: the wheels have no spin of their own.

**The hand-brake wheel** (`0x4ac094`): +0x59C counts the wheels (1 a
front, 2 a rear). G = load × downforce factor × (1 − S), M as above;
f = G/M if M > G, else 0.75. Lateral out = lateral slip force × f ×
H, forward = drive force × f; in neutral both × 0.05. H (`0x4ac9dd`)
falls with the time held, x = +0x514 / 384: H = 0.75 × (1 − 0.8 / (1 +
e^(−4π(x − 0.5)))) (1 while +0x514 is 0), from 0.75 to 0.15, half way at 6 s dry (2 s in
rain, 1.5 s in snow).

## Car dynamics, every car (`0x4a2830`)

Skipped before step 512 (the start) and for a car not in play (+0x89).

**The downforce in detail.** `0x4a2260` downforce, the first step of `car_dynamics` (`0x4a2871`): only upright (+0x10C over 0.75, `0x53c8a4`) with +0xB8 over 10; the velocity's part along the body's up (+0xCC) loses P +0x24C × the velocity along the body's forward (when that is over 0), × 2 for a car without +0x200 bit 4 (`0x53c8ac`). For a computer car on the road `car_on_road` then puts the velocity through its road rows and back, dropping the part along their up (frd.md, "The contact's frame, recorded a step at a time"): the rows are the car's own polygon's in 16.16 fixed point, not quite unit length, so the rebuild also takes about 0.002 m/s a call off the speed at 20 m/s, or, when the box's front-bottom one tick ahead is on another polygon (the track position's +0x6A), the rows turned half way to that polygon's by quaternions (`0x4b7680`). See frd.md, "The body's centre and the downforce" and the section after it.

1. `0x4a2260`: **downforce**, over 10 m/s on the road: velocity's up part
   (car frame) −= downforce (doubled for computer cars) × forward speed;
   **in the air**, a drag by axis (0.006, 0.002, 0.004; traffic 0.006,
   0.004, 0.004).
2. `0x4a00d0` the road's height under the car (+0x158; the plane through
   the road point +0x148 with up +0x130, below), `0x4a21a0` a roof over the road, `0x4a0440` the lowest
   corner's height over the road (+0x15C, from the half sizes +0x114,
   +0x118, +0x11C).
3. +0xB8 = max(|vx|, |vz|) + ¼ min(...), the cheap ground speed.
4. **Gravity**: in the air (+0x164 > 0), vy −= 0.306563. On the road,
   gravity is split along the road's three axes: the up part is always
   added; the side and forward parts only on a steep road (normal y <
   0.87); on a flatter one **the forward part is added while the car is
   upright** (+0x10C > 0.9, `0x4a2af7`).
5. Clearance < 0.6: on the road (`0x4a1610`: push out of the road,
   zero the speed along the road's up when it is under +0.5 m/s, turn
   the body towards the road at most 0.5 turn/s) or landing (`0x4a1ce0`, below). Else in
   the air: +0x164 counts, spin kept within ±2.4 turns/s (`0x4a20c0`; the spin's unit, below
   at Rolls in the air).

**Landing** (`0x4a1ce0`): when the velocity (+0xA4) goes into the road
(its dot with the road's normal is negative), `0x4a1210` finds a part
of the body under the road, and the car is upright (+0x10C > 0.9):

- e = 0.1 × P's **Suspension Stiffness** (+0x2E0) × the set-up slider
  (+0x50; 1 in Tournament, Knockout and network games), kept in 0-0.2;
- in the road's frame (`0x4e0110` of +0x124), the speed along the
  normal × (0.5 + e) (`0x4a1e68`), the spin (+0xE4) × (0.5 + e);
- an impact: strength +0x168 = 4 × |the speed into the road|, +0x16C
  0, +0x170 = the surface | 0x10000, points at 0.75 of the half
  sizes.

**The speed into the road is not reversed.** The landing scales it
and keeps its sign; nothing later turns it round. Each tick that
+0x164 is set and the clearance is under 0.6, the landing runs again
(`0x4a2b3d`); once the speed along the normal is under 0.5
(`0x5efddc`, set at `0x42c636`), every spin is under 0.6, `0x49fde0`
agrees and the clearance is 0 or less, +0x164 is cleared and
`0x4296c0` runs (`0x4a2068`). From then on the on-road code
(`0x4a1610`) takes the speed into the road out. The 0.5 in both tests
is signed: "under 0.5 m/s" means any speed into the road, however
large, and any speed away from it slower than 0.5. So a landing is a
loss, with no bounce. Checked at run time (`sw1-f`): with +0x164 poked
to 1 and the vertical velocity to −4 on Hometown's grid, the body's
contact (`0x42e8f7`) left −3.17, the landing made it −1.90 (× 0.6, e =
0.1; the spin × 0.6 too) and ended in the same tick (`0x4a2071`);
gravity took it to −2.19 and the next tick's on-road code (store
`0x4a1b10`) to −0.0009. It never went positive.

Why all 2.19 m/s went at once: in `0x4a1610`, when the clearance is
0 or less, the velocity is put into the road frame (`0x4e0110` of a
copy of +0x124, `0x4a1a3d`), its up part is compared with `0x5efddc`
(0.5, `0x4a1aa2`) and set to 0 when it is less (`0x4a1aaf`), and the
velocity is put back (`0x4a1ac4`-`0x4a1b10`). There is no "under 0.5"
limit on the size. The frame used is the car's road frame blended half
way (`0x4b7680`, weight 0.5) towards the frame at the point the car
will reach next tick, so the zeroed up is not quite the world's: the
−0.0009 left is that difference.

`0x4a1210` tests twelve points of the body box (its corners and points
at 0.75, 0.5 and 0.8 of the half sizes) against the road slices
(`0x5dd948`). There is no suspension model: the car is a box on the
road's plane. The pitch and roll seen are the body's drawing (pitch
roll factor, `0x4295b7`).

## The creep on the grid

A car standing on the grid after the start moves at 0.1-0.24 m/s with no
throttle. It is **gravity's along-the-road part** (step 4 above): the
code adds it on any road flatter than normal y 0.87, and nothing holds
a car at rest (no rolling resistance, no hand brake; the brake only
works when pressed, and the engine gives nothing at zero throttle).

Checked at run time: Hometown's grid road has forward y −0.009 (car
+0x1AC), so 0.306563 × 0.009 = 0.0028 m/s a tick; the car's speed
(+0x4F4) rose from 0 by 0.0025 a tick. It levels off (0.245 m/s, then
0.21 and 0.14 as the road under it changes, then 0.125 where forward y
is −0.004), so a loss of about 1% of the speed a tick balances it.

**The loss is `0x4af140`** (step 8 above): a car in **neutral** on a
road whose gravity along it is under 0.5 m/s² has its velocity and spin
× 0.99 every tick while under 20 m/s. 0.0025 / 0.01 = 0.25 m/s, the
speed it levels at. The car is in neutral because, stopped with no gas
from step 572, the gearbox puts it there (`0x4a9b07`, Gears above).
Checked at run time: with `-watchmem 0x5e11c4 12` (the player's world
velocity) each tick's stores were `0x4a2a91`/`0x4a2b0e` (gravity),
`0x4ae1a3` (the forces, nothing at rest), `0x4ae2c0` and `0x4e0065`
called from `0x4af221`, which took x from 0.16461 to 0.16295 (×
0.98992); `-watch 0x4e0050` showed its caller `0x4af226` 583 times for
the player's car.

## The computer's cars (`0x4088f0`)

Each tick, per car:

- skipped when `0x4be1a0` says so or +0x620 has bit 4;
- in the air or tipped (+0x8A, or +0x10C < 0.9): no grip, no skid,
  the rpm falls 500 a tick to the minimum, then `0x4aa8e7`;
- before the start (`0x7d3680` 0) velocity and spin are zeroed;
- on the road: velocity and spin into the **car's** frame (+0xC0,
  +0xCC, +0xD8 rows) at +0x4EC-+0x500, then one of three models:
  - **+0x88 set** (`0x4068a0`): no forces, below;
  - +0x620 bit 0x80: `0x408520`, which copies another car's brake
    (+0x509);
  - else `0x408600`, below.

The computer cars use no tyres and no engine torque. They are steered
and pushed by a few clamped controllers, read from the code
(`0x408600` and its callees) and checked at run time (`gh1-ai`, below).

**The world velocity.** The computer cars' velocity +0xA4 is in the
world: `ai_integrate` `0x4071a0` adds the road-frame force through the
rows at +0x124 and the body-frame force +0x4E0 through +0xC0, and
`car_move` `0x4a2c60` adds +0xA4 × 1/64 (`0x53c8ec`) to the place +0x98
every step. +0x7C0, +0x4EC and +0x4F4 are dot products of it (`0x42cdd0`,
`0x4088f0`). +0x210 is `atan2_turns` of the levelled body forward
+0x1A8 × 1024, chopped (`0x42cee9`). `car_ai_model` `0x408600` ramps the
aim before the heading error. See frd.md "The world velocity".

**Gravity and the body's tilt.** Gravity (`0x4a2a0c`) goes through the rows at +0x190, each × its own y. `0x4a0f30` (from `car_road_dots` `0x4a2690`) makes them the body's own rows (+0xC0, +0xCC, +0xD8) with the road's normal (+0x130) as the up, for a non-human car with +0x10C over 0.9 (`0x53c7f4`). The up part always applies. The forward part applies with the normal's y at least 0.87 (`0x5efdd8`) and the car upright (+0x10C over 0.9); with the normal's y under 0.87 the right and forward parts both apply. On the road, `car_on_road` sets the spins about the world's x (+0xE4 = normal.z − up.z) and z (+0xEC = up.x − normal.x), the normal being the up row of the frame it took the velocity through when the box touches (clearance 0 or less: `0x4a1a2a`'s branch replaces the +0x130 copy it was passed), each held to ±0.5 turn/s, both 0 within 0.02 while upright (`0x53c83c`). `car_integrate_turn` (`0x4a2e20`) turns the rows by (+0xE4, +0xE8, +0xEC)/32 turns about the world's axes. `ai_integrate`'s damping loop: `0x56ec68` is 0 or 1.0, so it does nothing. See frd.md for the page's port.

**The fields.** +0x7B0 the speed wanted (m/s, below). +0x7B8 the car's
side offset on the road, +0x7B4 the side offset it wants, +0x7C8 its
present aim, moved towards +0x7B4 by 0.2 a tick (`0x408870`). +0x7C0
is the speed along the road: the velocity (+0xA4) dotted with the
road's forward at +0x13C (`0x42cdd0`); +0x7C4 is its size. Both are worked out
for every car in the Sc32 task `0x42ce70` (stores `0x42cef8`,
`0x42cf25`), which also sets +0x210, the heading (atan2 of the road
frame's forward row +0x1A8 × 1024, so 1024 a turn, `0x42cee9`).
+0x7B8 (`0x42ce70` → `0x4a2bc0`) is the car's place less its slice's
point along +0x124, the road polygon's right, not the slice's. +0x1C is
the track position's slice, whose run of road polygons the car is in,
and is often already the next slice before the car reaches that
slice's point (t6-log: −2.5 to +3.2 m). The same task writes +0x21C through
`0x42cd00`: the car's span across the road, its half width, height and
length (+0x114, +0x118, +0x11C) each times |road right · body right,
up, forward|; `0x40e350` sets the lane edges at +0x7B8 ∓ +0x21C, each
taken in by 0.5. On each even step the race task runs for all cars
first, then every driver, then each car's dynamics, then the hits
(run `e9 order`); see frd.md "The tick's order and the lanes a car
spans".

**`0x408600`**, each tick:

1. Tipped (+0xD0 < 0.3): `0x4aa8e7` only. Out of the race (+0x2C4 3 or
   4): `0x407380`, then `0x4061b0`.
2. `0x408870(0.2)` (the aim, above).
3. **Gear and rpm** (`0x4061b0`). With T the track section's speed
   (`0x40ed40`: hard-coded slice ranges per track, 13.41 to 31.29
   m/s): if |+0x7B0| is over T + 10, the rpm aimed at is 0.8 × redline
   (redline − 200 when the car is 20 m/s or more under the speed
   wanted), else 0.5 × redline. The gear picked is the one (from first
   up) whose rpm at this speed (+0x7C4 × velocity to rpm) is nearest
   that aim; reverse while reversing (+0x7D4). A change sets +0x64C to
   the shift delay, and while it counts down the rpm aims at 0.4 or
   0.2 × redline. The rpm moves 1/8 of the way each tick. This is only
   for the sound: no torque is read.
4. **Reversing** (`0x4060d0`, `0x405fd0`): +0x7D4 1 while backing up.
   A hit under 10 m/s starts or ends it for 128 steps (`0x406120`, from
   the impacts task `0x42b9a8`). Each start adds 1 to +0x658; over 4
   the car changes lane, over 8 it is put back on the road (`0x429100`,
   from `0x406e50`).
5. **The heading error** (`0x4070c0`): `0x406e50` picks the point to
   steer at: the target car +0x624 (a chase, or a grudge, below), or
   the road ahead (`0x406d40`, `0x406c00`; a car slow for 1000 ticks,
   counted in +0x7D8, is put back on the road). `0x56ec60` = the car's
   heading (+0x210 / 1024) − the direction to that point, in turns,
   kept in ±0.5.
6. The **speed wanted** is set to 0 before +0x65C's step for a car with
   +0x200 bit 0x10 (traffic).
7. One of three controllers, then `0x4071a0`:
   - reversing, or +0x4F4 < 0: **`0x407c30`**;
   - else, unless +0x200 has 0x200: if the car is outside the road
     (+0x7B8 beyond the slice's lanes: byte 0x13's nibbles × bytes
     0x17 and 0x1B × 0x8000, a 16.16 edge loaded with `fild` as an
     integer against the metres, so only a side with no lanes counts;
     frd.md, The controllers recorded per call), or |heading error| > 0.04 × the surface
     grip (`0x5523d8`[surface × 3 + weather]: 16 surfaces × dry, rain,
     snow, 0.6-1.0), or +0x7C4 < 0.5, or before +0x65C's step, or
     sliding (+0xB8 > 2.5 and |+0x4EC| > 0.5 × +0x4F4): **`0x407fa0`**;
   - else under 13.41 m/s `0x407c30`, over it **`0x4076b0`**.

The gains are constants set once by `0x405e60` (from the race tasks'
set-up, `0x4c46e1`) at `0x56ebd0`-`0x56ec54`.

**The point steered at** (`0x406e50`, step 5). With no target car:

- +0x620 bit 0x800: `0x406c90`, the car's own slice point, and the
  speed wanted = its distance × 32: drive to a point and stop there.
- On the road's lanes (`0x40f110` 0: the car's lane +0x638 or one next
  to it is within the slice's lanes): `0x406d40`, unless the car has
  been slow (+0x7C4 ≤ 10 m/s) and reversed more than 4 times (+0x658)
  while outside the lanes, then `0x406c00`; more than 8 times puts it
  back on the road (`0x429100`).
- Off the lanes: +0x7D8 counts the ticks under 3 m/s; `0x406d40` while
  bit 0x40 of the count is clear, `0x406c00` while it is set (64 ticks
  each), and over 1000 the car is put back on the road.

With a target (+0x624): a cop (+0x200 bit 0x20) with +0x711 bit 2
within 4 slices of it uses `0x406b60`; with +0x620 bit 0x2000 or 0x8000,
`0x406b00`; else `0x406d40` (the road, with the target only for the
speed).

- **`0x406d40`, the road ahead**: the slice from `0x406980` and the
  point there moved sideways along the car's road right (+0x124) by
  the aim +0x7C8 (10 m more to the negative side on Hometown's slices 0x205-
  0x210 when the aim is negative). `0x406980` starts 4 slices on (the
  way the car runs: +0x621 bit 0x10 xor `0x6fd4c0`) and steps on to the
  look-ahead slice `0x40e1b0(car, 1.7, 4, 20)`, +0x7C4 × 1.7 / 6
  slices, 4 to 20 (backward when reversing), but stops at the first
  slice whose forward, in x and z, has a dot of 0.7 or less with the
  car's slice's (`0x40ef70`): about 1.7 s ahead, or nearer where the
  road turns by more than 45°. Hometown's slices 0x225-0x235 use the
  look-ahead slice as it is.
- **`0x406c00`**: the car's own slice point: back to the road's middle.
- **`0x406b00`**: the target's position; the aim +0x7C8 is set to the
  target's side offset (+0x7B8).
- **`0x406b60`**: the target's position + its velocity × 0.4 (where it
  will be in 0.4 s); the aim likewise.

All return the vector from the car (+0x98) to the point.

**`0x407380`, a car out of the race** (+0x2C4 3, finished, or 4,
busted; step 1 of `0x408600`). Only on the road and upright (+0x164 0,
+0x10C > 0.9). No controls (steering, gas, hand brake 0, brake 255).
In the car's frame, the forward speed × 0.98 a tick (× 0.95 when the
ground speed +0xB8 is under 22.35 m/s); between −10 and 5 m/s × 0.5
more, else the rear skid level is 0.1 × |speed|; the side speed ×
0.85, the yaw spin (+0x4FC) × 0.5 (over 0.2 it adds to the rear skid);
when the side speed is bigger than the forward, both skid levels are
|side| and the side speed × 0.2. Then back to the world. The car coasts
to a stop in a few seconds and its tyres sound. Not hit in `gh1-ai` (no
car finished). Wired in November 2026 (frd.md, "Out of the race, tipped,
the grid and the rows"): the velocity and spins go into the body's rows
(+0x4EC-+0x4F4, +0x4F8-+0x500) and back through them, and ai_integrate
isn't called, so nothing else pushes the car; the driver's state 4 sets
only the speed wanted (`0x408c50`, 11 or 0, for the gears' sound).

**The drivers** (`0x55226c`, 15 rows of 0x18 bytes, indexed by the
set-up record's +0x30): the name (Ace, Lead Foot, Gotcha, Ram Rod,
Dead Beat, Mulligan, Wild One, then the cops Shield, Smooth, Trigger,
Blue, Moose, Blender, Fancy, Sterno), +4 1 for a cop, +8 the **skill**
0-2, +0xC 0 for a driver who surges, +0x10 a personality 0-5, +0x14 a
colour (`0x404810`, the front end). **`0x404610` returns the skill**,
at least 1 in Tournament and Knockout. Ace and Lead Foot have skill 2,
Mulligan and Wild One 0. It is used:

- the slide on a slippery surface (`0x40841e`, in `0x407fa0`): skill 0
  every tick that `0x408459`'s test passes, skill 1 only when step bit
  0x20 is set, skill 2 only when bits 0x60 are clear: worse drivers
  slide more;
- the lane vote (`0x40974d`): skill 0 votes 6 for the best line, else
  10, or 12 with personality over 2;
- the speed factor `0x408dc0`, below; the braking (no effect, above).

`0x404690` (from `0x40cb70`) sets the grudge limits per slot
(`0x56ea70` + slot × 20) by personality: hits 10 to 110,
times 0x40-0xC0, a count 1-5 (less one with Aggressive).

**The speed scale +0x7BC** (`0x409330`) is the product of
`0x408dc0` × `0x4090a0` × `0x408f10` × `0x40ee80` (two more return 1:
`0x408f00`, `0x409090`), **kept at most 1** (`0x409410`-`0x409449`),
or 1 with `MADLAND`; the speed wanted is × it.

- `0x40ee80`: a per-track factor (`0x56ef94` + track × 32 with `ai`
  Normal outside Tournament and Knockout, else `0x56ef98` + track × 32).
  The rows (`0x56ef80`, 32 bytes a track, tracks 0-8) are filled at
  each race's start by `0x40e410` through `0x40e3c0`, by the race type.
  Single race, Normal then Aggressive: 0.82/0.94 on tracks 0, 3, 5 and
  7, 0.84/0.96 on 1, 2, 4, 6 and 8. Hot Pursuit: 0.78/0.88, 0.82/0.90,
  then 0.80/0.85, and 0.80/0.83 on track 8. Read from memory in a Hot Pursuit
  (`cq-reset1`, at 62 s), the rows were these. Tournament and Knockout use
  only +0x18: Beginner 0.92 or 0.94 by track; Expert 0.98, 0.99 or 1.0
  less 0.005 for each of night, weather, mirrored and backward. Hot
  Pursuit racers are the slowest computer racers. The same rows hold
  the traffic: +0 the most traffic cars in play (`0x40ef10`, read by
  `0x40c210`: 8, 6, 9, 5, 7, 6, 5, 5 and 12 by track in a single race
  or Hot Pursuit, 0 in Tournament and Knockout, 15 with cheat bit 8),
  +4 how long a parked traffic car waits (`0x40eed0`: rand × +4 + 1,
  `0x405d40`), and +0x1C 13.41 m/s. A flag (`0x55eb2e` bit 0x20)
  sets track 3's row to 1, 100, 0.9, 1.0.
- `0x408dc0`, the driver: skill 0 × 0.94, 1 × 0.97, 2 × 1; a surging
  driver (+0xC 0) × 1.05 or × 0.95 in alternate 128-step quarters of
  512 steps; personality 0 × 0.95, 1 × 0.97, else 1 (with +0x620 bit
  0x20, 0.95 + 0.05 × personality instead). 1 in a one-machine Knockout.
- `0x4090a0`, the **catch-up**. `0x404c80` (each step, from the human's
  states) keeps the human furthest on (`0x56ebb8`, race distance +8; a
  cop counts as 1) and `0x56ebb4`: 0 when no computer racer is ahead
  of him, 1 when some are, 2 when all are. With 0: a car more than 15
  slices behind him (10 Aggressive, 5 in an Expert Tournament or
  Knockout) gets × 1.1 (1.15 in Tournament and Knockout). With 1: a car
  more than 50 slices ahead (70, 100) × 0.85. With 2: × 0.95
  Aggressive, × 0.85 Hot Pursuit, else × 0.9 in places 1-5 or a
  network game, × 0.8 behind. Never under 1 in Knockout on track 8,
  never over 1 in a network game. As the product is kept at most 1,
  the catch-up's × 1.1 only undoes the other factors.
  Checked against three Hometown dumps (+0x7BC, +8, +0x2BC; November
  2026): the human on the grid behind all seven (`t4-town`), places 2-5
  at 0.82 × the driver × 0.9 and 6-8 × 0.8; the human second with one
  racer 6 slices ahead (`t5-fog`), no factor; none ahead, the racers
  140-156 slices behind (`t5-hd`), 0.82 × 1.1 = 0.902. The place
  (+0x2BC) counts from 1. The Tracks page's stand-in racers take the
  catch-up with the camera's car as the human (`aiCatchUpFactor`,
  `aiRacersAhead`; [frd.md](frd.md)).
- `0x408f10`: in a network game with positional updates, a factor
  by the distance to the local player (1 + (2 − slices) × `0x53640c`).

**Seen in footage** (the 17 race clips, 8 cars, 2 laps). The racer
list at the bottom left is in place order, each name in its driver's
colour (`0x404810`: Lead Foot green, Ram Rod and Dead Beat blue,
Mulligan red, Wild One yellow, Gotcha and Ace white), with the gap to
the player: red "−" ahead, green "+" behind, dashes for the player
(gap from `0x485a60`). In `Hometown_Day` the player leads from early
in lap 2 and the gap to second grows steadily: Lead Foot +0:00.71,
1.23, 1.64, 1.81, 2.54, 3.85, 4.73, 5.50 every 8 s, the last car from
7.9 to 17 s. Nobody is pulled back towards the player: with no
computer racer ahead the catch-up's × 1.1 only cancels other factors
(the product is kept at most 1), as read. The gap on the HUD at the
line (Ram Rod +0:05.07) is not the finish difference (4.56 s on the
results): it is an estimate. Traffic is seen in both directions; no
cop car was seen in the race clips (one frame every 15 s of four
clips looked at). Lane changes and the AI's recovery weren't measured.

**Checked at run time** (`gh1-ai`: Hometown, single race, seven
computer racers, keypad 8 held from 52 s; `-watch` on the three
controllers' first instructions, eax the car, and `-watchmem` on car 1
+0x7B0-+0x7CC). Every car ran a controller every tick from the start
(48.9 s), about 1,300 calls each by 90 s. For car 1 (`0x5e1acc`): all
63 calls of `0x407c30` came with +0x7C4 under 13.41 m/s, all 1,166 of
`0x4076b0` over it; it changed from one to the other at 50.98 s, at
13.50 m/s. `0x407fa0` ran 65 times: the first tick (+0x7C4 0, under
0.5) and in short bursts while the aim +0x7C8 moved between lanes
(4 → 12 and back) or after a bump (71-77 s). The aim moved 0.2 a tick
towards +0x7B4, which took the values 4.0, 12.0 and 17.1 (lanes) and
2.6-3.75 after 75 s. The speed followed the speed wanted to within
about 0.5 m/s (43.6-43.7 for 44.0; 36.9 for 36.5), from 0 to 12 m/s in
1.8 s. +0x7BC was 0.66 throughout (the human behind all of them).

- (November 2026: the yaw rates below are in turns a second, the side
  term of `0x4076b0` is a side speed wanted and its "heading term" the
  velocity along the right of the road two slices on, `0x406a80`; the
  Tracks page models all three, [frd.md](frd.md), The controllers.)
- **`0x4076b0`** (on the road, lined up, fast): yaw rate wanted = −2.5
  × heading error, in ±0.8; side term = −5 × (+0x7B8 − +0x7C8), in ±32
  and ±0.5 × +0xB8; under 22.35 m/s these are kept within 0.129 and
  0.012 × +0x7C4. Yaw force (into +0x2F8) = −5 × (yaw rate − wanted),
  in ±0.8. Side force = (heading term − side term) × 4 × (1 + 0.0015 ×
  speed), at most 38 × (1 + 0.0015 × speed); both × +0x7BC when it is
  over 1, which never happens: `0x409330` keeps +0x7BC at most 1. Forward force: the acceleration below when under the speed
  wanted, else minus the braking. Over 44.7 m/s a racer's drive loses
  3.45 / P's lateral grip multiplier (others: |side force| / (speed /
  2)). Skid marks when |heading term| > 7.
- **`0x407fa0`** (sliding or off the line): the same kind of
  controller on the side *velocity* (+0x4EC) rather than the offset,
  with its own gains (`0x56ec30`-`0x56ec54`: 32, 40, 6, 3, 3, 1, 10,
  0.2, 0.04).
  On a surface with grip under 1, an AI racer (+0x200 & 8) every few
  steps gets its forces × 1.1 (Aggressive) or the yaw × 50 and side ×
  10 (Normal), by `0x404610` and the step's bits (`0x408459`): it
  slides. In the first 20 of race distance, an AI racer accelerating at
  its table's first value draws the race's random number (`0x554f10` ×
  `0x554f18`, low 16 bits kept; pick ((s & 0xFFFF00) >> 8) × 14 >> 16).
  When the pick is its slot (+0x1F0), it gets 0.5 (`0x53638c`) more
  side force, with yaw from it, and rear skid level 10 (`0x4084bc`).
  The push is the side force (+0x4E0) + 0.5 and the yaw force (+0x2F8)
  + 0.5 × that pushed side force (`0x4082dc`-`0x4082f8`; the × 1.0 after
  it is `0x53638c` not over 1), before the slippery surface's factors.
  The random is seeded by `0x4371d0` from `0x6fd3b4`. In Reluva:
  `aiStartSpin` and `AiStartRandom`, the push in `aiMove`; the Track
  page's racers start from the grid with Ctrl+J (frd.md, "Out of the
  race, tipped, the grid and the rows").
- **`0x407c30`** (slow or reversing): the speed wanted is kept under
  16.41 m/s; yaw from the heading error (× 2.5, in ±1.6, and ±0.02 ×
  +0xB8); the side speed damped (× 3, in ±38).
  The side speed wanted is sin(2π e) × +0x4F4 for e ≥ 0 and
  −sin((1 + e) 2π) × it for e < 0 (`0x407d09`-`0x407d35`): the same
  size and sign, so the slow controller never asks for the left.
- **`0x4071a0`**: the road-frame force (`0x56ec6c`-`0x56ec74`) and the
  car-frame force (+0x4E0) go into the world (+0x2E8), velocity +=
  force × the tick; spin += +0x2F4 × the tick; `0x56ec68`, when set,
  damps the velocity.

**The acceleration** (`0x4064f0`): P's AI acceleration table (+0x2EC,
112 floats) at int(|+0x7C4| − 0.5), so **one value per m/s**, 0-111.
It is × 0.9 with `ai` Normal after race distance 100, × 0.9 in rain or
snow under 45 m/s, × 1.3 while +0x6E4 > 0 (the grudge, below). In a
network game with positional updates (`0x6fd50c`), a car other than
the local player's that is 3 or more slices behind it or 15 or more
ahead gets × 2 (within −8..30 slices) or × 3. Traffic under 2 m/s: at most 12. A cop
over 20 m/s with `0x55eb2e` & 0x20: × 0.5. A racer is held under P's
top speed cap × +0x7BC. **The braking** (`0x4063e0`): brake 255 and
P's maximum braking deceleration, × 2 for a cop, × 2 going backward, ×
1.25 for an AI racer. `0x404610` is called there (`0x40648a`) but
every branch after it goes to the same `0x40649f`: the skill changes
nothing.

**The grudge** (`0x404a00`, from the impacts task `0x42b9af`): with
`ai` Aggressive, an AI racer hit by a human's car (not a cop's) takes
it as its target (+0x624) and counts the hits at +0x6E8; past a limit
for its slot (`0x56ea78` + slot × 20) +0x6E4 gets a time (`0x56ea7c`),
and the car drives at the human with × 1.3 acceleration.

**`0x4068a0`** (+0x88 set, at `0x46681e`): no forces. `0x4066c0` moves
the speed along the road +0x7C0 towards the speed wanted by the
acceleration (or braking) × the tick, signed −1 for a car running the
track the other way (+0x621 & 0x10 xor `0x6fd4c0`), not past it; +0x7C4
its size. Then velocity = the car's forward row (+0xD8) × +0x7C4 + the
vector at +0x124 × 8 × (+0x7B4 − +0x7B8) (`0x406880`), the spin zeroed,
+0x4F4 = +0x7C0, and the car put on the road (`0x4a0980`).

- The **speed wanted** (`0x409330`): +0x7B0 = the speed table's byte
  for this slice pair (m/s). Over 67.05 m/s and with +0x700 over 1 it becomes
  67.05 + (speed − 67.05) × +0x700 (`0x40936d`-`0x409392`: `fld1`,
  `fcomp` with +0x700, `jae` past it when 1 ≥ +0x700). It is kept under P's top speed cap
  × `0x40ee80`'s factor.
- **speedsF.bin / speedsR.bin** (`0x40e000`; R when `0x6fd4c0`, the
  track run backward): after a 16-byte header, one byte per two road
  slices, the speed (`0x40e080`; 64 without the file); then one byte
  per slice whose high 4 bits `0x40e0c0` returns (7 without the file,
  `0x56f22c` set).
- **The slice nibble is the best line's lane.** Lanes are numbered as
  +0x638 (the car's lane, from `0x40e2b0`), 7 the middle.
  `0x4096b0` (from the cop states `0x4057db`, the AI driver `0x409ca6`
  and `0x40bfbd`) takes the nibble 4 slices ahead and adds a vote for
  it (2, or 6, 10 or 12 by the driver) to the lane choice's counts for
  left, same and right of the car's lane (`0x56ef14`, `0x56ef18`,
  `0x56ef1c`). `0x493a30` counts each step whether the car is on that
  lane (its middle or either side, half width +0x114): +0x2DC on (and
  every step before 1152), +0x2E0 off: the race statistics.

**The assists' driver** (`0x4a83d3`, from `0x4a9d1d` in the controls
with `0x6fd490` braking or `0x6fd494` steering on, not out of the
race). It drives only the pedals:

1. The player's pedal p = gas − brake + 256 (0-511), moved at most 32
   a tick (+0x218).
2. The bend: the road's heading 10 slices on (10 back when the car
   faces more than a quarter turn from the road; `0x49fee0`) − the
   heading at the car's slice (+0x154), in ±512, smoothed: +0x214 =
   (7 × +0x214 + bend) / 8.
3. Its size against P's turn cutoffs (+0x2A4, +0x2A8, +0x2AC) picks a
   speed modifier (+0x2B0, +0x2B4, +0x2B8; 1 under the first).
4. Target = the speed table 7 slices ahead (−7 backward) × the modifier.
   With e = target − speed, +0x520 (256 = neither pedal): e > 1 adds
   min(int(0.5 e) + 4, 16); e < 0 adds max(int(0.67 e) − 8, −24);
   else it is set to 256 + 256 × (wheel rpm − min) / (redline − min).
   It is kept at most p and at least 0.
5. Under 255: brake 255 − +0x520, no gas; else gas +0x520 − 256.

So the assist brakes before bends and lifts off, but never gives more
than the player's own pedal. It does not steer: with the steering
assist on, the police states (`0x405740`, state 2) run the computer's
lane functions (`0x40dd60`, `0x4096b0`, `0x40ddb0`, `0x409660`,
`0x40de30`) for the human's car. **For a human they change nothing
that drives.** They set the car's lanes (+0x638, and +0x63C, +0x640
for its two sides, `0x40e350`), fill the lane votes (`0x56ee90`-
`0x56ef78`, cleared each call by `0x40dd60`), pick a lane
(`0x40da30`, into `0x56ef74`) and write the side offset wanted +0x7B4
(`0x40daf0`). The only readers of +0x7B4 are the computer's code
(`0x408870`, `0x406880`) and the warp `0x40cd80`; the human's tick,
controls and the assists' driver read neither +0x7B4 nor `0x56ef74`
(every access listed with `field car 0x7b4` and `xref 0x56ef74`).
+0x638 is read anyway by the best-line count `0x493a30`, but
`0x40e350` sets it in the other states too. In the wheels' angle,
`0x4ad320` compares `0x6fd494` and then adds a term that is always 0
(`[ebp−4]`, `0x4ad330`): a steering assist that was taken out. The
steering assist is the pedal driver and nothing more. Hot Pursuit's
set-up (`0x4746ab`) turns it off.

## The Hot Pursuit states (`0x405740`, car +0x6E0)

Run by the human's driver (`0x431455`) when there are computer cars or
cops, or the steering assist is on. A human's car is a cop when its
set-up record's role (+0x18 of the 0x6C-byte record at `0x6fd52c`) has
8: `0x474732` sets it in the Hot Pursuit set-up (`0x474640`) when
player 1's car (`0x6fd2bc`) is a police car (`0x4396b0`); `0x428700`
then gives the car +0x200 bit 0x20.

- **State 0** (set at `0x40cbe5`): a cop goes to 3 with no target, any
  other car to 2.
- **State 2**, a human racer: when the record at +0x628 has +0x1C = 2
  (`0x401500`), the car goes to **6**, becomes computer-driven (+0x200
  | 2) for 320 steps and pulls over; then **7**, stopped; then back to
  2 and reset (`0x429100`). This is the speeder being busted.
- A **computer racer** has its own states in `0x409c00` (same field):
  1 or 2 racing; **5** when its record has +0x1C = 2 (pulling over,
  until it is under 3 m/s or 320 steps pass, 640 with +0x620 bit
  0x4000; not while tipped, +0x10C ≤ 0.9); **6** stopped (speed wanted
  0, `0x40ce70`); back to 1 when the record's +0x1C clears (the
  ticket); and **4** once out of the race (+0x2C4 3 or 4), where each
  step `0x408600` calls **`0x407380`**: no controls, brake 255, speed
  × 0.98 (× 0.95 under 22.35 m/s), so it stays where it stopped. In a
  state 5 with no cops left on the record (+0x2C 0) it goes back to 1
  (`0x409b40`). Seen in `cp-stop7b` (car 1): 2 → 5 at 64.05 s, 5 → 6
  at 64.16 s, 6 → 1 at 67.28 s, 1 → 4 at 67.38 s; `0x407380` first ran
  for it at 67.22 s (`cp-stop8`), and it was still at the roadside, at
  0 m/s, at 70 s.
- **State 3**, a cop with no target. Each step:
  - `0x4052e0`: each racer within 50 slices gets +0x6F0 = the most it
    has been over the section's speed (`0x40ed40`);
  - `0x404ff0`: the nearest racer within 50 slices (by slice, then by
    distance) into +0x730, and the rows of the cop's racer list
    (`0x485180`);
  - `0x404f80`: the **horn key** (bit 0x08, +0x50E) toggles the siren,
    +0x710 bit 0x10 (+0x734 1 or 0), once a press;
  - `0x404e40`: the spike strip (bit 0x80);
  - **`0x4053b0`: with the siren on, no target yet, and +0x730's car
    over the speed by more than 2 m/s (+0x6F0 > 2) and still racing,
    `0x4018b0(2, ...)` makes it the target (+0x624, `0x4019a1`).**
    Then state 3 → **4**, the chase (`0x405879`).
- **State 4**: the chase. Each step the steps of state 3 but
  `0x4053b0`, then `0x405630`. The cop does nothing by itself: the
  human drives. The chase record is +0x628 (static records from
  `0x56e410`, one per chased racer: +0 and +0x14 counts, +4 the most
  over the speed, +0x1C 2 when the racer is pulled over, +0x28 a step,
  +0x2C the cops on it, +0x30 their list, +0x50 the cops stopping it).
  `0x405630`: with the siren off (+0x710 bit 0x10) or the target over
  **300 m** away, or a racer that has finished, the chase is lost
  (`0x401a80`: the record's counts + 1, +0x624, +0x628, +0x734
  cleared, siren off) and the next check (`0x4058db`) goes back to 3.
  Else `0x405430`: when the cop is within **7 m** of the target, the
  target's +0x7C4 is at most `0x56ebb0` (**20.12 m/s**, 45 mph, or
  11.18, 25 mph; `0x404c50`), and
  the cop is on the road and upright (+0x10C > 0.5): the first such
  step calls `0x4127b0`, which is empty (a bare `ret`: the sound
  there was taken out), and sets +0x720 = step + 24 and +0x710 bit
  0x40; if it still holds 24 steps (0.375 s) later, the record's +0x1C
  = 2 and +0x28 = step + 640 (`0x40554a`). Failing a test clears bit
  0x40. Once the target has stopped for it (an AI racer in its state
  6, a human in 7, see state 2), `0x405430` returns 1 and the cop goes
  to **5** (`0x4058bf`) through `0x401e40`: of the record's cops the
  nearest to the target does the ticket (+0x710 | 0x80, siren off,
  record +0x50 + 1); on the first, +0x28 = step + 192 for a human cop
  (3 s), or `0x5521b8`[the target's tickets × 16] for a computer one
  (384 steps, 6 s, for 0-4 tickets; 448 from 5), and the two cars
  point at each other (+0x9A4). Also on the first: for a human target,
  `0x401580` plays police speech at the target (`0x412880` →
  `0x4c8b30`, bank entry 8, `copspch`; volume by distance) chosen by
  the tickets left, `0x6fd4bc` − its +0x2C8: 1 left slot 0x20-0x22
  (random, not the last one played), 2 slot 0x13, 3 slot 0x11-0x12,
  else 0x10-0x11. Summit_Night's two scenes (out at 3) carry slots 17
  and 19 (bnk.md): 3 left, then 2, as this gives. Then `0x401c20`
  (always) places **the officer on foot**: +0x910 of the target = its
  position 2 m along its road right, to the side picked by which way
  it faces on its slice (`0x401b50`), +0x8EC his frame from the
  heading +0x210, and +0x8E4 = +0x8E8 = 1. While both are set the car
  drawing (`0x4bbba0`) draws him (`0x4b9bc0`, within 100 m). After the
  ticket `0x401e20` clears both and he goes. The spot is lowered by
  the target's half height (+0x118) and handed to `0x4205a0` with the
  slice. Run (`r1-ram4`, below): he stood 1.99 m across and
  0.94 m below the target's centre.
  - **Which side** (read, November 2026). `0x401c20` takes side 1
    (−2 m along the car's +0x190 row) when the car is right of its
    slice's road point (the offset from the point dotted with the
    slice's right bytes is over 0), else side 0 (+2 m), and flips that
    when the car faces against the road (+0x1A8 dotted with the
    slice's forward bytes under 0): the side towards the road's
    middle. `0x4205a0` (through `0x420650`) builds a track position
    for the spot (`0x41e940`, `0x41f6e0`) and puts the spot's height
    on the road there (`0x4969c0`): hence the 0.94 m. It gives back
    the 0x78-byte track position, and `0x401d78` keeps the side only
    when its +0x6C is 0 (the spot is not beyond either outer edge of
    the slice's road polygons: Other car fields, +0xC), its polygon
    (+0x14) is set, and the surface of
    the polygon's 8-byte record (+0x18, byte 1 & 0x3F, the same code
    `0x4207e0` gives the car's +0x18C) is neither 0 nor 14. Else he
    goes to the other side, unchecked (`0x401dc4`: the side flipped,
    `0x401b50` and `0x4205a0` again; `-count 0x401dc4` counts the
    failures).
  - **Where it can fail** (November 2026, from the files: each slice's
    run of list-4 polygons in the `.frd` with its road record's
    surface, measured along the slice's `.col` right from the `.col`
    point, by a script outside the repo). The spot is always 2 m
    towards the slice's road point, so it fails only where a surface
    0 or 14 polygon (or no polygon) lies between a drivable one and
    the road point, within 2 m of the car. At the runs' outer ends
    (walls, banks) that never happens: the spot points inwards, away
    from them. Inside the runs it does on all nine tracks (Hometown 21
    slices, Redrock Ridge 90, Atlantica 202, Rocky Pass 13, Country
    Woods 75, Lost Canyons 134, Aquatica 227, The Summit 9, Empire City
    79, with a 0 or 14 polygon between drivable ones), but only some of
    those lie between the car and the point. Places where it does:
    - **Hometown slices 510-519** (the town centre): a surface-0
      polygon 13.2 m wide, from −13.2 to 0 m, between the two
      carriageways, with the road point on its right edge. A car on
      the right carriageway (0 to +9.4 m) less than 2 m from that edge,
      or on the left one (−23 to −13.2 m) less than 2 m from it, has
      its spot on the 0 polygon.
    - **Empire City slices 207-213**: a surface-0 strip from 0 to +5.5
      m in the road's middle, the road point on its left edge: a car
      within 2 m of it on either side.
    - **Hometown slices 172-178** (the first covered bridge): a 3 m
      surface-0 strip from −13.1 to −10 m between the road (−7.8 to
      +7.8, with a 2.2 m edge) and a strip of surface 11, 2 or 3 to
      its left (−22.6 to −13.1); a car on that left strip within 2 m of
      the 0 strip (centre −15.1 to −13.1 m, so pressed against it).
    - **Redrock Ridge slices 3-6, 12 and 21**, beside the grid: on the
      wide area left of the road, a surface-0 polygon (−35.3 to −22.9
      m at slices 3-6) with a surface-10 one beyond it; a car on the
      far one within 2 m of it.
    A car's own centre on surface 0 or 14 starts the reset timer
    (`0x429452`), so the car must stand beside such a polygon, not on
    it. The stops in the earlier runs were all in the open road (the
    player's at Hometown slice 225, 1.8 m left of the point, in
    `d1-r1`: its spot 0.2 m right of it, on the road).
  - **Tried at the covered bridge** (`e2-h1`-`h15`, November 2026:
    Hometown, the racer's pursuit, the set-up pokes only; the player
    `-drive`n at car 1 from 38 s as in `d3-e1`, then keys: a left
    pulse with keypad 4 from 81.5-81.8 s, keypad 6 to straighten,
    keypad 2; cars 0-7 dumped every 0.25 s; `-count` and `-watch` on
    `0x401c20`, `0x401dc4`, `0x401e40`, `0x401ff0`). Cop 3, in state 5
    on the player since about 78 s, stopped it every time (its stop
    `0x401e40` at 84.9-93.5 s and the ticket 384 steps on). The
    player came to rest on the bridge's left strip in `h2`, `h3`
    (slices 169-170, 19-24 m left), `h7`-`h15` (slices 173-177, 16.4
    to 21.3 m left; the 0 strip's left edge is at 13.1 m), inside the
    bridge on the road in `h4`-`h6` (4-7 m left), and in `h1` (a
    hand-brake turn, the car facing backwards at slice 205) on the
    road. `0x401c20` ran once in each and `0x401dc4` never: the side
    test passed fifteen times. The nearest, `h15`, stood 3.3 m from
    the strip's edge, nose-in (the car's forward along the road's
    right: a half length of 2.41 m in the way), so its spot was 1.3 m
    short of the strip. The strip is a wall to the car (`0x430350`:
    a corner on surface 0 is off the road), so only a car standing
    along it, its side within 0.85 m of it (centre 14.3-15.1 m left),
    has its spot on it. The human's pull-over in state 6 also moved
    the car back 0.6 m (`h15`: 16.0 → 16.6 m). Not reached.
  - **Failed at the covered bridge** (`e4-q1`, `e4-q1v`, November
    2026: the same set-up, the set-up pokes only, no car-state poke;
    `-drive` at car 1 from 38 to 81.5 s, then keys; cars 0-7 dumped
    every 0.125 s, `-count` and `-watch` as above). Probe runs with
    dumps (`e4-p0`, `p15`, `s1`-`s3`, `t1`-`t3`) showed why the earlier
    tries ended nose-in: at 33 m/s a left pulse slides the car 20 m
    across in a second, and a car at under 15.3 m left at slice 172
    runs head-on into the strip's leading end (`s1`-`s3`: 29 to 3 m/s
    at 83.2 s). The keys that worked: keypad 4 81.5-82.2 s, keypad 6
    82.2-82.5 s, keypad 2 82.5-83.4 s (past slice 172 at 22 m left,
    standing at slice 174, 21.6 m left, by 85 s: `t2`), then keypad 8
    with keypad 6 84.0-84.5 s, keypad 4 85.6-86.0 s, the gas let go
    and keypad 2 86.0-86.8 s. The car came within 14.6 m left at
    slice 176 (86.0 s), ran on to slice 178 (17.4 m left, 88 s), then
    ran backwards at up to 4 m/s, drifting right, and came to rest at
    slice 176, 14.7 m left, at 92.5 s, its nose 6° left of the road:
    its right side 0.4 m from the
    surface-0 strip (−13.2 to −10 m there), the bridge's wall face.
    Cop 3 (state 5 on it since about 78 s) began its stop at 92.342 s
    (`0x401e40`); `0x401c20` and **`0x401dc4`** ran in the next step
    (92.343 s): the side test failed, the only time `0x401dc4` has run
    in any run. In `q1v` (to 100 s, the player dumped every 0.5 s,
    frames kept outside the repo) the officer's spot (+0x910) was
    1.82 m along the car's left (its +0x190 row), on the strip the car
    stood on, away from the wall, and stayed there until the ticket
    (`0x401ff0`, 98.516 s), when +0x8E4/+0x8E8 went back to 0; the
    frames show him there. Two other endings (`q2`, `q3`: the second
    pulses 0.1-0.3 s apart) stopped farther from the strip and passed.
    So the fallback is real road behaviour: a car stopped along a
    surface-0 polygon, side within 0.85 m, gets its officer on its
    other side.
  - **The 7 m** and the 300 m are not true distances: `0x4965b0` takes
    the largest of |dx|, |dy|, |dz| plus a quarter of each of the
    others (`0x53bedc` 0.25), so straight ahead it is 7 m and on a
    diagonal about 6 m.
  - **Which limit.** `0x404c50` compares the human racers
    (`0x6fd518`; a human cop is counted in `0x6fd51c`, not there) with
    the computer racers (`0x6fd520`): 20.12 m/s when there are fewer
    human racers, else 11.18. From `0x474640`: one player as a cop
    (0 against 6) and two cops (0 against 6) get 20.12; one player as
    a racer (1 against 1), two players with a racer, and network
    games with a human racer (no computer racers then) get 11.18. So
    the limit is 20.12 when every human is a cop, and 11.18 when a
    human is being chased. Read from memory at run time: 20.12 in a
    cop game (`cp-stop1`: `0x6fd518` 0, `0x6fd51c` 1, `0x6fd520` 6),
    11.18 in a racer's pursuit (`cp-racer1`: 1, 0, 1, and 6 computer
    cops at `0x6fd524`).
  - **A strip's stop needs no 7 m.** A racer that hits a spike strip
    has its record's +0x1C set to 2 at once (`0x401650`), and
    `0x405430` returns 1 as soon as the target is in its state 6,
    wherever the cop is (`cp-spike1`, below).
- **State 5**: the stop. The speed wanted is 0 and `0x40ce70` halves
  the cop's velocity and spin each step with the brake at 255 (on the
  road, upright). `0x401fb0` ends it when the step passes the record's
  +0x28 (or the target is gone), with **the ticket** `0x401ff0`, and
  the cop goes back to 3 (`0x405911`):
  - the record: +0 + 1, +4 = the most of int(target +0x6F0), +0x14 0;
  - the cop: +0x2CC tickets given + 1 (reaching `0x6fd4b8`, +0x2D4 =
    step − 512, the race clock when the cop made its quota: the clock
    starts at step 512), +0x2D0 += int(the target's +0x6F0, the most it was
    over the speed);
  - the target: +0x2C8 tickets + 1, +0x6F0 0, and at `0x6fd4bc`
    tickets +0x2C4 = 4, **out of the race** (it then coasts,
    `0x407380`); +0x634 = step + `0x5521b4`[tickets × 16] (960, 896,
    768, 640, 576: 15 down to 9 s), before which no cop picks it again
    (`0x402e42`); +0x8A 0, `0x401e20` (the officer gone); record +0x2C, +0x50, +0x1C 0,
    which lets the racer's state 7 go back to 2 and onto the road.
  - then `0x4040e0`: the record's cops lose it; a human cop's strip is
    cleared (`0x4c9f50`) and the cop is put back on the road by
    `0x405220`, the same as its reset key: in a one-player game with
    the cop assist on (`0x6fd4a8`, copied from the option `0x6fd48c`
    by `0x474640`) and after step 0x480, at **one slice behind the
    leading racer still in** (lowest +0x2BC), then `0x429100`. In
    `cp-stop7b` the cop was at slice 0 beside the ticketed car and
    was at slice 121 a step later, behind the leader (slice 117 at
    the ticket).
  - `0x474640` sets the limits. One player as a cop: `0x6fd4b8` 6
    (all six racers), `0x6fd4bc` 1 (one ticket and a racer is out),
    six computer racers. One player as a racer: `0x6fd4bc` laps + 1,
    `0x6fd4b8` 100, **one** computer racer (`0x6fd520` 1). Two players:
    both cops, quota 3 and out at 1; else out at laps + 1 (quota the
    same, or 100 with two racers). So the count comes from the race
    length, not from a setting or the number of opponents. It also
    turns every assist off (`0x4746ab`).
  - `0x5521ac`, 16 bytes by the target's tickets (0-7): +0 50, 50, 70,
    70, 90, 90, 90, 90; +4 65, 65, 75, 85, 105, 105, 115, 115; +8 the
    steps before a cop may pick it again (960, 960, 896, 768, 640, 576,
    512, 512); +0xC the stop (384 × 5, then 448). +0 is a slice range:
    `0x4043a0` (from a chase) sends free computer cops on the other
    side of the target within +0 × 2 slices, and those with +0x711
    bit 1 within +0 × 8, after it (`0x404340`); +4 (+0 without bit 1)
    is how far, in slices, the computer cop's driver `0x4025c0` keeps
    up the chase. Read, not run: a racer with more tickets draws cops
    from further away.
  - The HUD (`0x482b00`): "TICKETED" (text 422) while the target has
    fewer than `0x6fd4bc` − 1 tickets, else "ARRESTED" (423), while
    +0x9A4 is set; "tickets: n/m" (text 333, `0x489a50`, "%s: %d/%d")
    in a Hot Pursuit, for a cop as well; and for a racer the radar
    detector `0x48c0e0` with level 4 − (d − 15) / 10 in 0-4, d the
    slices to the nearest cop (`0x4013b0`), and a beep (`0x418990`).

  Checked at run time only up to the chase (`gh1-cop`: the cop with H
  at 47.0 s and keypad 8 from 48 s): the siren at 47.1 s
  (`0x404fce`), car 1 targeted at 52.76 s (`0x4019a1`, record
  `0x56e410`) and state 4 (`0x405883`) to the end at 80 s. The cop,
  driving straight, never got within 7 m of a slow enough target, so
  `0x40554a`, state 5 and the ticket were not reached; the chase
  wasn't lost either (within 300 m).

  **The whole stop, run** (`cp-stop7b`, November 2026, nfs-host with
  the cop pokes). Car 1's +0x6F0 was poked to 5.0 at 41 s so that it
  could be chased on the grid, and its speed wanted (+0x7B0) was
  poked to 0 at each `0x408600` so that it stayed near the grid (it
  crept backwards at under 2 m/s). Keys: H at 41.5 s, then gas
  (keypad 8; the cop starts in neutral, A for first gear), left, brake.
  The race clock started at 48.86 s.
  - 41.57 s, step 58 (before the start): the chase (`0x405879`).
  - 63.61 s, step 1466: the first step of the stop test (`0x40559b`).
    Its inputs: the cop 6.99 m away by `0x4965b0` (6.82 m true: dx
    6.28, dz 2.66), the target's +0x7C4 1.68 m/s against `0x56ebb0`
    20.12, the cop on the ground, +0x10C 1.0. +0x720 = 1490.
  - 64.00 s (24 steps on): record +0x1C = 2, +0x28 = step + 640
    (`0x40554a`), and again each check until the target stops.
  - 64.05 s the target 2 → 5, 64.16 s 5 → 6 (it was under 3 m/s).
  - 64.17 s: state 5 (`0x4058bf`, `0x401e40`). The cop 6.48 m from the
    target, which was at 1.33 m/s. +0x710 went 0x10 (siren) → 0x50
    (stop test) → 0xC0 (ticket, siren off); +0x9A4 each the other.
  - 67.21 s (192 steps, 3.04 s): the ticket (`0x401ff0`), `0x4040e0`,
    the strip cleared (`0x4c9f50`, none was out), the cop back to 3
    (`0x405911`). The target: tickets 1, +0x2C4 4 (out), +0x6F0 0.
  - Frames (`fr-cp-stop7b`): 2600 (63.7 s, the target boxed beside
    the cop), 2608 (64.2 s, the scene: no HUD, an officer at the
    racer's door, the cop car behind), 2672 (67.1 s, the scene), 2680
    (67.4 s, the HUD back with "tickets: 1/6", the cop on an empty road
    ahead, by the leaders).

  **A natural stop, run** (`cq-ram4`, November 2026, nfs-host with
  the cop pokes and nothing else poked). The cop blocked a racer at
  full speed. Keys: R at 61.0 s (race clock 12.1 s), which put the
  cop at slice 80, one behind the leader, standing (`0x405220`); A
  (first gear) at 61.1 s; keypad 8 and 6 (gas, right) from 61.15 s;
  6 let go at 62.8 s, 8 at 63.0 s; keypad 2 (brake) 63.01-63.9 s; H at
  64.4 s. The cop drove about 5 m to its right, into the line three
  racers took (x 750 to 745), and stopped there. The last racer, car 1
  (`0x5e1acc`), at 40 m/s, ran into it at 64.35 s: 40.6 m/s, then 10.4
  m/s a tenth of a second later, and at rest 4 m from the cop by 65.1
  s. In a run with the same keys but no H (`cq-ram2`) it stopped there
  too and then backed off: the crash slowed it, not the stop.
  - 64.48 s: the chase (`0x405879`): H turned the siren on, and car 1,
    the nearest racer (+0x6F0 18.4), became the target.
  - 64.48 s, the first step of the stop test (`0x40559b`): the cop 3.99
    m away by `0x4965b0` (4.14 m true), the target's +0x7C4 9.21 m/s
    against 20.12, the cop on the ground (+0x164 0), +0x10C 1.0, at
    0.76 m/s.
  - 64.91 s: record +0x1C = 2 (`0x40554a`); the target 2 → 5, then
    5 → 6 at 65.03 s (`0x409f71`), 4.6 m away at 0.59 m/s.
  - 65.07 s: state 5 (`0x4058bf`, `0x401e40`, `0x401c20`); +0x710 0x10
    → 0x50 → 0xC0 as in `cp-stop7b`.
  - 68.11 s (3.03 s on): the ticket (`0x401ff0`, `0x4040e0`, back to 3
    at `0x405911`). Just before it the target's +0x8E4 and +0x8E8
    were 1 (the officer); after it 0. The target: tickets 1, +0x2C4 4,
    +0x6F0 0; the cop: +0x2CC 1, put back at slice 127 behind the
    leaders.
  - **The officer and the speech, rerun** (`r1-ram4`, the same keys,
    November 2026; the same steps to the millisecond). At 65.074 s
    `0x401c20` put +0x910 at (742.79, 22.14, −266.21), the target at
    (741.09, 23.08, −267.25): 1.99 m across, 0.94 m down; +0x8E4 =
    +0x8E8 = 1. The last race view is frame 2729 (65.19 s); the scene
    starts at frame 2730 (65.25 s, camera fixed by the white car), the
    officer standing at its right front, and he is in every scene
    frame through 2788 (68.19 s). `0x401e20` ran at 68.105 s; the next
    frame, 2790 (68.26 s), is the race view again with "tickets: 1/6"
    and the cop moved, no officer. `0x401580` didn't run: the target
    was a computer racer (`0x401f78` tests its +0x200 bit 4), and the
    sound log has no speech. With that test poked out (`r1-ram4s`,
    `r1-slot`, `0x401f7f` = 90 90) it ran at 65.074 s: tickets left
    1 (`0x6fd4bc` 1 − 0), `0x4c8b30` picked **slot 0x21** (0x20 +
    random of 3) at d² 100 and started it (`0x4c8c6a`); the mix was
    louder from 65.0 to 67.0 s. A human target is in "The player's
    ticket" below (`c3-spch`).
  Earlier tries: `cq-ram1` (steering right until 62.4 s, braking from
  63.2 s) and `cq-ram3` (no brake) missed: car 1 went by 4-9 m away at 40 m/s. Their H came
  late: the host then took keys from a queue in the order written, so an
  H written after a later key waited for it. It now queues each key at
  its time (November 2026), so the order written doesn't matter.

  **A stop after a bump, run** (`c3-bumpA`, November 2026, the cop
  pokes, nothing else poked). The cop's car was driven by
  `nfs-host -drive` (below) at car 1, full gas: R at 61.0 s, A at 61.1
  s, keypad 8 from 61.15 s, steered from 64 s. It caught car 1 at 85
  s and sat 5 m behind it at 40 m/s; H at 85.5 s made car 1 the target
  (`0x4019a1`, `0x405879`). At 88.2 s the cop ran into its back: car 1
  went from 37.7 to 4.8 m/s and drove on, 13 to 19 m/s, with the cop 5
  m behind. The stop test began at 93.23 s (`0x40559b`) and the record
  +0x1C = 2 at 93.78 s (`0x40554a`, then again at each check, 165
  times). Car 1, still driving, only went to 6 at 98.92 s: 320 steps
  later, never under 3 m/s with the cop pushing (its state 5 rule
  above). The stop began the same step (`0x4058bf`, `0x401e40`,
  `0x401c20`) and the ticket came at 102.00 s (3.08 s). So a bump that
  slows a racer under 20.12 m/s is enough, and the racer need not stop
  first. In `c3-bumpC` (the same, but braking to stay 4 m behind from
  88 s) car 1 almost stopped after the bump, crept on at up to 8 m/s,
  and the stop test began at 92.01 s; 6 at 92.64 s, the ticket at
  95.70 s.

  **A whole chase by keys** (`c5-b1`, November 2026: the two cop pokes
  at `0x474ca0` and nothing else poked, no `-drive`). Keys: R at 61.0
  s (the cop to slice 80, one behind the leader, standing), A at 61.1,
  keypad 8 and 6 from 61.15 s (6 off at 62.8, 8 off at 63.0), keypad 2
  63.01-63.9 s, H at 63.8 s. The cop pulled about 5 m right into the
  racers' line and stopped, with the siren on before car 1, the last
  racer, got there:
  - 63.885 s: the siren (`0x404fce`) and in the same step car 1, the
    nearest racer, at 40 m/s and over the limit, the target
    (`0x4019a1`, `0x405879`, state 4).
  - About 64.3 s car 1 ran into the cop (41 m/s, then 7.5 m/s 0.25 s
    later). 64.327 s: the first step of the stop test (`0x40559b`);
    64.702 s (0.375 s, 24 steps on): record +0x1C = 2 (`0x40554a`,
    then again at each check until it stopped).
  - 64.925 s: car 1 5 → 6 (`0x409f71`, under 3 m/s); 64.929 s: state 5
    (`0x4058bf`).
  - 67.942 s (3.01 s, 192 steps): the ticket (`0x401ff0`), back to 3
    (`0x405911`); the cop was put at slice 126 (`0x405220`), car 1
    left at the roadside in its state 4.
  So the rules hold with nothing poked: the target taken at H as the
  nearest racer over by 2 m/s, the 24-step hold, the stop when the
  target is stopped, 192 steps to the ticket. It is the `cq-ram4`
  block with H before the crash instead of after.

  **A chase from behind by keys wasn't finished.** The Pursuit car
  (`0x6fd2bc` 12), full gas from rest on the automatic box, took about
  10 s to 37 m/s (`c5-s1`, `c5-i1`), while the racers cruise at 36-43
  m/s, so a target taken as it passes (car 1 in `c5-s1`, car 4 in
  `c5-i2`) was 100 m ahead by 69 s and the chase would need 20 s or
  more of steering through Hometown's bends before the cop could close
  in. Driven straight it hit a wall at 73 s and lost the target at
  300 m (`0x401a80` at 81.8 s and 83.1 s). Keys steered by a script
  that re-ran the game (deterministic) and kept the keys that agreed
  with the next run's positions (`c5-i3`-`c5-i7`) got about 1.3 s
  further a run, to 69.3 s, and were stopped there.

  **The slow start is the held gas** (`c6-g1` to `c6-g4`, November
  2026, the cop pokes only). The cop starts at slice 957, 26 m behind
  and left of car 1, the last of the grid, in neutral. With A and
  keypad 8 held from before the start (`c6-g1`, from 46 s) the engine
  sat at 6,000 rpm and the wheels spun: 11.6 m/s 3.1 s after the
  start, against car 1's 21.1. With keypad 8 pressed at the start
  (48.9 s, `c6-g4`) the rpm rose from rest and the cop made 18.7 m/s
  in the same time, as fast as the racers off the line. Gas pulsed
  every 0.1 s (`c6-g3`) was slower still, and a second A in neutral
  (`c6-g2`) changed nothing. Even so the racers cruise faster: at 57 s
  the cop had 33.8 m/s, car 1 40.1 and 45 m ahead. Driven straight
  the cop hits the wall at slice 29 (57.8 s); with steering searched
  over re-runs (`c6-it0`-`c6-it2`, keypad 4 and 6 in 0.1 s steps
  towards the racers' line) it got to slice 63 by 62.6 s, but
  scraping the walls at 24-36 m/s, with car 1 118 m ahead. With H at
  49.5 s car 1 was its target from 52.75 s, not lost by 75 s
  (`c6-g1`). An
  R after step 0x480 places the cop standing, so a racer behind it
  goes by at speed. So a chase from behind ending in a ram by keys is
  still not done: it needs the cop driven cleanly through Hometown's
  bends for 20 s or more.

  **A chase from behind by keys, to the ticket** (`c7-at0` to
  `c7-q1`, November 2026: the two cop pokes and nothing else poked, no
  `-drive`). The race was picked by clicks: Location... (65, 217),
  the track's bar, Atlantica; Opponent Car... (65, 252), Class C.
  - **Why there.** Atlantica's virtual road (`tr02.col`) runs straight
    for 100 m, bends 34° left over slices 16-48, crosses a bridge 16 m
    wide from slice 39 to 63, then runs straight and 21 m wide from
    slice 64 to past 160. Class B racers cruise there at 42-44 m/s
    (`c7-at1`), class C ones at 36-40 (`c7-at3`). The cop, with gas
    from the start (69.5 s), made 37.4 m/s at 79 s, as fast as car 1,
    and 43-44 m/s on the bridge, where car 1 held 39-41.
  - **The keys.** A at 66 s, keypad 8 from 69.5 s (off at 89.4 s), H
    at 72 s. Steering and brake in 0.1 s steps from 74.0 s to 93.4 s,
    106 of them. The steering was planned from a run's logged state
    with a rough model (the steer moving 32 a step of 0.05 s; 0.84
    rad/s of turn at full lock at 35 m/s, lagging 0.2-0.35 s), then the
    game was run again from the start, and the keys were kept as far
    as the run followed the plan. Thirteen rounds of three runs
    (`c7-k0c0` to `c7-k12c2`) took the cop through the bend and over
    the bridge; the last taps were added by hand (`c7-m1` to `c7-q3`).
  - **The run** (`c7-q1`). The siren came on at 72.1 s, and car 1 was
    the target from 74.1 s (`0x4019a1`). The cop was 23-26 m behind
    from 72 to 83 s, 15 m at 84.5 s on the bridge, 9 m at 86 s, and
    beside car 1 at 87.5 s (the same slice, 7 m across). Keypad 6 at
    87.6-88.0 s turned it into car 1. Side by side, 3 m apart, it
    pushed car 1 from 6.6 m to 11.7 m right of the road's points,
    against the right wall at 13 m: car 1 37 → 29 m/s by 89.4 s. With
    the gas off and the brake on (89.4-90.0 s and 90.5-93.5 s) the cop
    stayed a slice ahead of car 1, which was boxed in against the wall
    and slowed: 21.5 m/s at 91.1 s.
  - 91.23 s: the stop test (`0x40559b`). 91.67 s: record +0x1C = 2
    (`0x40554a`), and car 1 pulled over (its state 5). 93.20 s: car 1
    5 → 6 (`0x409f71`). 93.25 s: the cop to 5 (`0x4058bf`), 4-5 m
    from it. 96.27 s: the ticket (`0x401ff0`, step 2234), the cop back
    to 3.
  - With the brake let off at 91.8 s or 92.0 s (`c7-p1`, `c7-p3`) car
    1 still pulled over and stopped, but the cop rolled on to 10-15 m
    and no stop came: the cop must be within 7 m when the target
    stops.

  **`-drive`**: `nfs-host -drive CAR:TARGET:FROMMS[:TOMS[:GAP]]`
  writes a human car's steering (+0x518) and pedals (+0x504, +0x505)
  at `0x4314c9` each tick, after the game unpacks the controls: the
  steering towards TARGET (the angle off the car's +0x1A8 row, full
  lock at 0.35 rad), full gas, or with GAP the target's speed plus half
  the distance over GAP metres. In neutral it asks for first gear. It
  steers either player's car in split screen.

  **Two cops** (`c3-two1` to `c3-two3`, November 2026). The single
  race's clicks with the cop pokes and `0x6fd3b0` = 1 and `0x6fd328` =
  13 poked at `0x474ca0` give a split-screen pursuit: two human cops
  (`0x6fd51c` 2, `0x6fd518` 0) against 6 computer racers, no computer
  cops, "tickets: 0/3" on both halves; both cops start in state 3.
  Both may chase one racer: in `c3-two2` and `c3-two3` the sirens
  (+0x710 0x10, poked) made car 2 the target of both in the same step,
  one record (`0x56e410`) with two cops. A cop more than 300 m away
  loses it alone (`0x401a80` for cop 0 at 108.58 s in `c3-two2`; the
  other kept it). The stop is shared through the record: once the
  record's +0x1C is 2 and the target is stopped, `0x405430` returns 1
  for every cop on it, whoever made the 7 m test. `0x401e40` then
  lets only one do it: for a human cop with other cops on the record,
  if one is already stopping (+0x50 > 0), or another cop on the record
  is nearer the target (`0x4965b0`), it drops the target (`0x4042b0`:
  off the record through `0x401a20`, +0x624, +0x628, +0x730, +0x734
  cleared, siren and stop bits off) and goes back to 3. A computer cop
  always does its own stop. Run (`c3-two3`, car 2 held near the grid by
  poking its +0x7B0 to 0 at each `0x408600`, both cops driven at it by
  `-drive` with a 3 m gap): player 2 began the test at 51.38 s, 6 m
  away; car 2 went to 6 at 51.91 s; at 51.91 s player 1, about 8.5 m
  away, entered 5 and dropped it (`0x4042b0`, `0x401a20`, `0x4058db`),
  and player 2 did the stop (`0x401f24`) and the ticket at 54.92 s
  (3.0 s). In split screen there is no scene: both halves stay in the
  race view, the officer at car 2 in both, and only the ticketing
  cop's half loses its HUD (frame 2320, 53.1 s).

  **Two cops over the network** (`c4-net1`, November 2026). Two
  copies over TCP (127.0.0.12 hosting, 127.0.0.13 joining, Controller
  Updates, so lockstep), the clicks of the network test in
  [nfs.md](../../engines/nfs.md); `0x6fd2bc` = 12 held in both copies
  from 40 s and `0x6fd3b8` = 3 held in the host from 60 s, so the
  lobby's 02 carried race type 3 and each 03 car 12 (net.md). The set-up
  is the network branch of `0x474640` (link kinds 2-4): a role of 9
  (1 | 8) for each player in a police car, and with no human racer,
  computer racers = 8 − the traffic (1 with traffic on and under 6
  cops) − the cops, at most 6, and quota = out = laps + 1. The 05 had
  0 human racers, 2 cops, 5 computer racers (cars 2-6), no computer
  cops, one traffic car (7), quota 3 and out at 3: not split screen's
  3 and 1. The HUD read "tickets: 0/3". Car 2 was held near the grid
  (+0x7B0 0 at each `0x408600`, +0x6F0 5 at its first), both cops were
  driven at it by `-drive` with a 3 m gap, run in both copies for both
  cars (the steering comes from the game's state only, so the copies
  stay in step), and both sirens were poked at the 400th `0x405740`.
  The same steps in both copies: both cops took car 2 at step 514
  (`0x4019a1`, `0x405879`), the joiner's car began the stop test at
  step 692 (`0x40559b`), at step 734 the host's car (car 0) entered 5
  and dropped it (`0x4042b0`) and the joiner's (car 1) did the stop,
  and the ticket came at step 928 (`0x401ff0`); only the game's
  milliseconds differed (the stop at 122.13 s in A, 125.12 s in B).
  Dumps of the eight cars at the ticket (`0x405911`) agree in place,
  speed, states and tickets (car 1 +0x2CC 1, +0x2D0 5; car 2 +0x2C8 1;
  record +0 1, +4 5, the host's car back in 3); what differs is
  pointers into each copy's heap (+0x20, +0x60, +0x314-+0x4C8) and
  +0x738-+0x797, +0x8B4, +0x934. Nothing about the chase, stop or
  ticket goes over the network: in the race only 06 went (the host
  sent 3,574 frames and took 5,374 words, 255 and 361 of them from
  the chase to the ticket); each copy works the stop out from the same
  controls.

  **The results with two cops** (`c4-res1` to `c4-res3`, November
  2026). The split-screen run above with `0x6fd4b8` (the quota) poked
  to 1 at 41 s and car 3 held as car 2 (+0x6F0 5). In `c4-res2` only
  player 1's siren was on at 45 s; driven at car 2, it ticketed it at
  55.89 s (step 1000, +0x2D4 488); then player 2's siren (56 s) and
  `-drive` at car 3 gave the second ticket at 76.75 s (+0x2D4 1822).
  A human cop whose tickets given reach the quota is finished
  (`0x4ce4e9` in `0x4ce280`: +0x2C4 = 3, +0x238 = the step), so with
  both finished the race ended and the Pursuit Summary came at about
  88 s. Its draw (`0x46f510`) sorts the cops' list (list 1, cars at
  `0x66e270`) each frame by `0x455c10` with the key of the column
  chosen: Player 8 (the names, set-up +0x34), Busts 5 reversed, Quota
  Time 7; ties go by +0x238. It opens on Quota Time (`0x455c10(1, 7,
  0)` from the first frame): "Player1, Pursuit Corvette, 1/1 (Met
  Quota), 0:07.62" above "Pursuit Diablo SV" (player 2, no name),
  "0:28.46", the time being +0x2D4 / 64 cut to hundredths. Clicking a
  column's title (`c4-res3`: at 95.7, 102.7 and 109.7 s) moved the
  "Sort By" arrow over it and the key to 8, then 5 (reverse 1), then
  7: by Player the nameless player 2 came first, by Busts (tied, so
  by +0x238) and by Quota Time player 1 (frames 3400, 3700, 4200). In
  `c4-res1`, where player 2 made the quota first (0:06.50 against
  0:13.71), it was first under Quota Time.

  **The network game to its results, sirens by H** (`c6-net5`,
  November 2026). `c4-net1`'s two copies with nothing poked in the
  chase: the quota (`0x6fd4b8`) poked to 1 at the 300th `0x405740` in
  both copies, cars 2 and 3 held near the grid (+0x7B0 0 at each
  `0x408600`, +0x6F0 5 at the first), the host's cop driven by
  `-drive` at car 3 and the joiner's at car 2 (3 m gap, from before
  the start, in both copies). Each siren came from the H key, held 1 s:
  the joiner's at its 125.0 s went in its 06 as `00000008` and in 11
  of the host's frames as car 1's word; the host's at its 205.2 s in
  10 frames as car 0's. Both copies made the same steps: the joiner's
  siren (`0x404fce`) and car 2 its target at step 786 (`0x4019a1`),
  its stop at 830 and ticket at 1024; the host's siren and car 3 at
  step 3532 (a second record, `0x56e470`), the ticket at 3822. With
  both cops at the quota the race ended and both copies showed the
  Pursuit Summary, sorted by Quota Time: "Player1, Pursuit Corvette,
  1/1 (Met Quota), 0:08.00" (the joiner: (1024 − 512) / 64) above
  "Player1 ... 0:51.71" (the host: 3310 / 64 = 51.72, cut). Both
  copies' players have the default name, so only the times tell them
  apart; the rows were in the same order on both screens. The host's
  screen offers Start Race and Main Menu, the joiner's Ready to Race
  and Game Setup. In `c6-net3` and `c6-net4` both sirens took car 2,
  the nearest: the farther cop dropped it at the stop (`0x4042b0`)
  and only one ticket came.

  **A network pursuit with nothing forced** (`c7-lob1` to `c7-net10`,
  November 2026). The same two copies over TCP, with no pokes and no
  `-drive`. The set-up is all clicks and keys:
  - **The lobby** (Multiplayer, Single Race) has the race type in the
    bar at its top (click 240, 77): Single Race, Hot Pursuit, Knockout
    (Beginner), Knockout (Expert), Tournament (Beginner), Tournament
    (Expert). The host clicked Hot Pursuit (205, 110). Its Opponent
    Car then read None.
  - **The car.** Each player clicked Player Car... (65, 217), then the
    car's bar (240, 77). The list shows nine rows and doesn't scroll
    by a click on its arrow (the click picks the row under it). Down
    pressed 11 times from Chevrolet Corvette and Enter picked Pursuit
    Corvette (after Mercedes SL600, before a "(Bonus Pursuit)" row).
    Back (60, 465). The joiner's lobby then listed two Pursuit
    Corvettes, and Opponent Car read Class A.
  - **The start.** Ready to Race on the joiner (354, 444), Start Race on
    the host (366, 444). Both cops went 0 → 3 (`0x405782`) at about
    101 s.
  - **Without a stop** (`c7-net3`) the race ran until the racers had
    done their two laps. Both copies then showed the Pursuit Summary,
    two rows of "Player1, Pursuit Corvette, 0/3, N/A". So the race ends
    without the quota.
  - **A block by keys failed.** The host's cop rolls from the grid in
    neutral and stops against a sign post at slice 2, 4.5 m right of
    the racers' lap-2 line (the road frame's 16.9 m against their 12 m;
    `c7-net1`, `c7-net2`). Full gas there left it at 900 rpm and
    standing (`c7-net4`, `c7-net8`). Reset (R) put it in the road's
    middle, 12 m off the line (`c7-net6`). Moved onto the line early,
    it was steered round by every racer (`c7-net9`; in one player,
    `c7-mi`, the same).
  - **The stop by a strip** (`c7-net10`). The host pressed S at its
    106.0 s, before the start. `0x4c9dd0` laid the strip in both
    copies (106.08 s in the host, 106.06 s in the joiner). On lap 2
    car 4 (`0x5e37d0`) ran over it: at 244.44 s in the host and 244.43
    s in the joiner, `0x4c9f50`, `0x401650` and `0x4019a1` (the target,
    siren on); state 4 at 244.47 s, state 5 at 247.96 s (`0x4058bf`),
    and the ticket at step 0x258C (9612) in both (`0x401ff0`, 250.99 s
    and 250.98 s), the cop back to 3.
  - **The results.** At the race's end both copies showed the Pursuit
    Summary: "Player1, Pursuit Corvette, 1/3, N/A" (the host) above
    "Player1, Pursuit Corvette, 0/3, N/A". The host's screen offered
    Start Race and Main Menu; the joiner's Race Again and Game Setup.
  - **Not deterministic.** The copies agree with each other, but not
    run to run: the racers reached the line on lap 2 at 242-255 s.
    With other runs loading the machine, three runs (`c7-net5`,
    `c7-net7`, a first `c7-net10`) lost the menu clicks and never left
    Game Setup. The cause is the seed (`0x6fd3b4`, the host's
    `GetTickCount` at start-up, `0x47242c`, sent in 02 +0x20): 0x1603
    in `c7-net10`, 0x1611 in `c7-net3`. With `nfs-host -deterministic
    50000` in both copies (`d1-n1`, `d1-n2`, November 2026, the same
    lobby clicks) it was 0x1768 in both runs, and the race repeated
    step for step: at 135 s the five racers' +0x6F0 were the same
    to 0.1 and their places one slice apart, because the race started
    0.18 s later in the second run (101.86 and 102.04 s; the copies'
    own clocks differ by up to 1.5 s, as the waits for the other copy
    are counted in instructions).

  **A network stop by a ram** (`d1-n3`, `d1-n4`, November 2026). The
  same two copies with `-deterministic 50000`, the lobby by clicks,
  no pokes and no `-drive`: only the host's keys. The racers' lap-1
  ends were taken from `d1-n3` (dumps every 0.25 s): car 2 and car 3
  passed 6.6 m from the host's grid spot (810, 134) at 247.0 and
  249.0 s, car 4 at 250.3 s, car 5, the last, about 253.4 s, about
  41 m/s. In `d1-n4` the host held Space (handbrake, word `00000004`)
  from 100 s, so its cop stayed on the grid (slice 957, 0 m/s) instead
  of rolling to the sign post; after car 4 had gone by, Space was let
  go and A pressed at 250.6 s, keypad 8 and 4 (`80ff0000`) at
  250.7-252.1 s, keypad 8 alone to 252.3 s, keypad 2 252.31-253.2 s,
  H at 252.9 s (`0000ff08`). The cop moved about 12 m (to (822, 137)
  by 253.0 s) at up to 10.4 m/s, across car 5's line. In both copies,
  by each copy's clock (host / joiner):
  - 252.994 / 251.377 s: the siren (`0x404fce`) and in the same step
    car 5 (`0x5e417c`), the nearest racer, the target (`0x4019a1`,
    record `0x56e530`), state 4 (`0x405879`). At 253.0 s the cop was
    4.4 m from car 5, at 6.8 m/s.
  - About 253.1 s car 5 ran into it: 40.9 m/s, then 16.2 m/s 0.25 s
    later. 253.068 / 251.431 s: the first step of the stop test
    (`0x40559b`); 253.438 / 251.799 s (24 steps): record +0x1C = 2
    (`0x40554a`, then at each check, 30 times).
  - Car 5 in its state 5 crept on at about 10 m/s, the cop beside it,
    and went to 6 only at 258.668 / 257.089 s (`0x409f71`, 320 steps
    on, as in `c3-bumpA`); the cop to state 5 the same step
    (`0x4058bf`).
  - 261.743 / 260.216 s: the ticket (`0x401ff0`, step 0x27FA = 10234
    in both), the cop back to 3 (`0x405911`), car 5 back to its state
    2 (racing) by 262 s.
  In the race only 06 went (and two 04 barriers at the start): 5,124
  frames from the host, 5,058 from the joiner to 262 s; the keys are
  in car 0's word of the host's frames from 250.0 s, in the order
  above. So a stop by a ram over the network needs nothing but the
  keys, once the race repeats: the plan was made on one run and worked
  on the next, the 0.2 s shift of the start being under the 2.4 s
  gap between car 4 and car 5. It is a block, as `cq-ram4` and
  `c5-b1` in one player: the cop drove into the racer's line and the
  racer hit it; a chase from behind ending in a pin (`c7-q1`) was not
  done over the network.

Checked at run time (`sw1-cop3`): the Hot Pursuit pokes put at
`0x474ca0`, before the set-up picks its function by the race type
(`0x474cd9`), not at `0x473bf0` (by then the single race's set-up has
been picked and the player is a racer, in state 2). Then `0x474732`
ran (37.4 s), the player went 0 → 3 at `0x405782` (40.8 s), and with
H pressed at 54.0 s the siren went on (`0x404fce`) and in the same
step `0x4019a1` targeted car 1 (`0x5e1acc`) and `0x405879` went to
state 4. Without H (`sw1-cop2`) the player stayed in state 3.

### The computer cops' driver (`0x4032e0`)

The Tracks page's stand-in police car drives by this driver's states 1
to 11, with the reversing, strips, roadblocks, stops and tickets below
(`aiCopStep`; [frd.md](frd.md), The police car's driving, and its
orders, strips, roadblocks, reversing and stops).

Read (November 2026), then run (below). A computer cop's state is
+0x70C; its orders come in an 8-byte record at +0x7AC (+0 the kind, +1
set once taken, +2 and +4 a slice range, +6 the way to face), written
by `0x403830` for computer cops only (`0x4040e0` gives each kind 2 when
a racer is ticketed). **The driver acts one call in four**: it is a
32 Hz task, but `0x40df10` → `0x401240` lets it through only when
(step / 2 + car +0) mod 4 is 0 (`0x4be1a0(8, ...)`), so each cop acts
every 8 steps, 8 times a second, each on its own phase (in `r2-cop4`
cop 2 on steps 3532, 3540, ...; cop 5 on 3942, 3950, ...). Every count
below is in steps but tested at that rate. On a new order (+1 still 0,
`0x404560`), and in state 1 at each call without a target,
`0x403030` applies it: kind 0 goes to state 3 when the car is inside
the slice range (`0x402fd0`), else turns towards it (`0x403000`); 1 is
state 2; 2 state 1; 3 state 7 and 4 state 8 (or 10 when already in
range), both with the siren on (+0x710 bit 0x10). Then by state:

- **1**, patrol: speed wanted = the section's speed × 1.19
  (`0x401430`, kept under `0x401370`: the car's top speed, +0x1C8 of
  its car data, × 1.1 for a cop with +0x711 bit 1), the racers' lane votes, and
  `0x402e10` looks for a target. **2**: the same, driving to the
  order's slices.
- **3**, to the roadside: votes 100 to the nearer road edge
  (`0x402d90`), speed 6.5 m/s. Off the slice's lanes (`0x402f70`) it
  goes to **4**, stopped (`0x40ce70` each step), and with +0x711 bit 8
  lays a spike strip (`0x403d30`): at lane 7 for an even cop index
  (+0x714), lane 6 for an odd one; on a track run backwards
  (`0x6fd4c0`) 14 − the lane. `0x403d7d` is not another routine, only
  the odd branch's label (Ghidra's decompile shows it as a call).
  Lanes under 7 take the left half's width (slice byte 0x17), 7 and up
  the right half's (0x1B), so even cops' strips lie right of the
  middle and odd cops' left. The same bits pick its radio line
  (`0x40b9c0` variant: 2 even, 0 odd, 1 when the index & 2;
  2 − that backwards). A cop with a strip out radios
  once as its racer is past it by less than 30 slices (`0x403df0`:
  strip slice − racer slice from −1 to −29, `0x40bca0`, kind 0x1D, the
  officer's "missed the strip"), and clears it when the racer is 50
  slices past (November 2026: the test's sign read again; it was written
  up as the racer coming within 30).
- **7**, **8**: the siren on, the section's speed × 2. 7 (a call for
  help, below) goes back to 1 with the siren off once in the order's
  slices. 8 goes to **9** there (6 m/s) and, at 6.5 m/s or less, **10**
  (radio `0x40bac0`): +0x621 bit 8 and `0x403180`, which waits while
  any racer is within 240 m (`0x4965b0`) and otherwise turns the car a
  quarter turn (`0x4dfde0`, 0.75 turn about y) on its slice
  (`0x4a0980`), speed wanted 0, bit 8 off: then **11**, stopped hard
  across the road, a roadblock. +0x711 bit 4 ends it when the racer is
  5 slices past (`0x403ed0`: back to patrol, an order of kind 2).
- **5**, the chase: `0x4025c0` drives at the target and gives up past
  the table's slices (above). Its speed wanted (`0x402420`, with
  `0x401780`: `0x56ea60` the slices to the target, `0x56ea68` set when
  the cop is past it) is the top speed (`0x401370`) when more than 8
  slices behind or when the target is faster, else (top − the
  target's +0x7C4) × slices × 0.125 + the target's speed + 5.81 m/s;
  past the target, its speed × 0.2 (the car at `0x56ef54`) or × 0.5, or
  reverse (−9.83 m/s) when nearly stopped and over 7 slices off.
  `0x4025c0` itself asks for the top speed at `0x40269d`. **`0x401370`**
  (read, November 2026) gives +0x1C8 of the car's data (`carp`, +0x528),
  × 1.1 (`0x536044`) when the car's +0x711 has bit 1 (cops 2 and 6 by
  default, the bit that also widens the calls for help). Every 320 steps (the record's +0x24),
  with no human cop (`0x6fd51c` 0), it asks for more: the cop that
  radioed last time (+0x5C) only clears it; others try a strip
  (`0x4039c0`), then a roadblock (`0x403b20`), and failing both call
  free cops (`0x4043a0`: orders of kind 3, a run with the siren to the
  target's slices, radio `0x40b7f0`). `0x402110` is its stop test; on 1
  it goes to **6** (`0x401e40`), the stop, and from there back to 1
  (`0x401fb0`).

**Strips and roadblocks are ordered by heat**, h = the record's +0x14
(chases lost) + its +0x2C (cops on it) + the target's tickets + (step
− the record's +0x20, the first chase's step) / 2048:
- `0x4039c0`, **a strip**, when h > 6 and none is ordered yet (record
  +0x58 0). The first time it only radios (`0x40b910`; record +0x54 |=
  2, +0x5C = this cop); the next time it takes the nearest free
  computer cop (no target, no +0x711 bit 8) 51 slices to ¾ of the track
  ahead of the target, orders it kind 0 from its slice − 6, and sets
  its +0x711 bit 8 and the record's +0x58. That cop drives to the
  roadside (3), stops (4) and lays its strip (`0x403d30`,
  `0x4c9dd0(index, slice, 7, step)`).
- `0x403b20`, **a roadblock**, for a human target only (and not with
  command-line flag 0x20), when h > 3. The first time it radios
  (`0x40b860`; +0x54 |= 4, +0x5C) and in the same call takes the two
  nearest free cops 51 slices to ¾ of the track ahead, within 400
  slices of each other, and sends both (kind 4, siren, +0x710 |=
  0x410) to the slice halfway between them.

**The radio** (read, then run in `c3-radio`, November 2026). The
radio calls queue speech; they change nothing in the chase. Each line
is a 9-dword record (slot, volume, pitch 0x40 or 0x50, pan, bank,
channel, kind 5-7, the speaking car) handed to `0x4c85a0`, which
starts it (`0x4c85f4`) if channel 0 is free, else queues it (up to
16, `0x7dd398`, count `0x7dd378`); the sound step `0x412d60` starts
the next as each ends (`0x4c86d0`). Nothing plays in a replay
(`0x6fd3a0` set), nor while `copspch` holds `0x560118`. A call first
checks there is room (`0x40a910(n)`: n + queued ≤ 11). A call is an
exchange:
- the officer's call number (`0x40aa20`: slot 0x10 + the cop's index,
  unless he spoke last, `0x5524ec`), from his bank: the cop index mod
  8 picks `off..A` (0, 3, 6), `off..B` (2, 5, 7) or `off..C` (1, 4),
  pitch 0x50 for 3, 4 and 5 (`0x40a410`, table `0x40a3f0`), volume 127,
  pan 0x3FFF;
- often the dispatcher's acknowledgement (`0x40a970`);
- the message (`0x40ab20(1, cop, kind)`), and for some kinds where
  (`0x40aeb0`: a place line from the track's slice table
  `0x560aa0`, read by `0x4c8550`, for the slice the speaking cop will
  reach: its own slice, +0x1C, plus its speed (+0x7C0) / 6 × the lines
  queued × 2, `0x40af3c`-`0x40af89`)
  or how fast (`0x40b190`: lines 0x3C-0x3F above 44.7, 53.6, 62.6 and
  80.5 m/s);
- a click from the siren bank (`0x40a380`: entry 5, slot 0x20-0x28);
- the dispatcher's reply from `disp..` (`0x40ab20(0, ...)`, volume
  110, pan 0xC000), some kinds only when the last reply was over 1,000
  sound steps ago (`0x5524c8`).
The suspect's car is named from a bank of its own: in a pursuit
`0x40a010` loads, for each racer, `IP<colour>spch.bnk` from its car's
`IPspch<language>.viv` (`0x413750`; `IPNCspch.bnk`, no colour, one
time in six or for some cars) into entries 15 on (`0x56ee28`[car]);
lines 0x00 (dispatcher) and 0x10/0x20/0x30 (by the officer's voice).
The calls and when they come:

| Call | From | Kind (officer / dispatcher) |
|---|---|---|
| `0x40b220` | a cop takes a target, the only one on it (`0x4018b0` → `0x404070`) | 8, the car |
| `0x40b330` | a cop joins a chase | 10 or 9 |
| `0x40b530` | a chase taken again of a human target (record +0x14) | 0xB |
| `0x40b480` | a chase of a human target lost (`0x401a80` → `0x4040b0`) | 0xC |
| `0x40b670` | a computer racer ticketed (`0x4040e0`) | 0x13, or 0x14 arrested |
| `0x40b720` | the call for help (`0x4043a0`) | 0xF / 0x10 |
| `0x40b7f0` | each cop sent by the call for help | none: a bare `ret` |
| `0x40b910` | a strip asked for (`0x4039c0`) | 0x1B |
| `0x40b860` | a roadblock asked for (`0x403b20`) | 0x1C |
| `0x40b9c0` | a strip laid (`0x403d30`, and a human cop's S, `0x404e40`) | 0x1F-0x21 |
| `0x40bac0` | a roadblock in place (state 10) | 0x22 |
| `0x40bca0` | the strip's racer past it, under 30 slices (`0x403df0`) | 0x1D |
| `0x40bd00` | the roadblock's racer 5-19 slices past it, from state 10 (`0x403ed0`) | 0x1E |
| `0x40bba0` | once a cop (+0x711 bit 0x10, `0x403950`) | 0x19, 0x1A |
| `0x40bc30` | once a record (+0x54 bit 8, `0x403910`) | 0x23 |

`0x40b7f9` (kind 0x18) has no caller. Run (`c3-radio`, the `r2`
racer's pursuit to 130 s, `0x4c85f4` watched with its record): 54
lines. At 71.10 s cop 1 took car 1 (`0x40b220`): `off00C` slot 0x11
(its number), car 1's bank slot 0x30, `off00C` 0x43 and 0x3C, a click
(siren bank 0x27), then `disp00` 0x10, 0x15, the car (slot 0x00),
0x43, a click, each starting as the last ended (about 1.1-1.3 s
apart). Cop 3's lines came at pitch 0x50 from `off00A`, cop 5's from
`off00B`, as the table gives. The ticket of car 1 (84.47 s,
`0x40b670`) and the calls for help (104.25 s, 120.04 s) spoke too;
`0x40b7f0` ran three times, silent. In the recording `off00C` slot 17
matches best from 71.20 s and `disp00` slot 16 from 76.64 s (0.072 and
0.088, others 0.04-0.05). The voice is not placed: every line has the
same volume.

**What each kind says** (read from `0x40ab20`, `0x40a970`,
`0x40aeb0` and the calls; the lines transcribed, November 2026, in
[bnk.md](bnk.md), What the radio lines say). `0x40ab20(who, cop,
kind)` picks the slot, the same numbers in the officer's and the
dispatcher's banks where both speak: kinds 8, 9 and 10 the car's bank
(officer 0x10, 0x11, 0x12 + the voice's 0/0x10/0x20, `0x40a580`;
dispatcher 0x00); 0xB the officer's "got him again" (0x24-0x27, skipped
right after a strip or roadblock line, `0x5524f4`) then the car's
0x11; 0xC and 0xD lost him (0x20-0x23); 0xF backup (0x28-0x2B); 0x10
the dispatcher's "unit asks for backup" (0x30-0x33); 0x13 0x2C-0x2E
and 0x14 0x2C-0x2F, the suspect caught; 0x19 disabled (0x1E-0x1F);
0x1A a wrecker (0x1D); 0x1B a strip asked for (officer 0x37-0x38,
dispatcher's go-ahead 0x34-0x35); 0x1C a roadblock (0x33-0x34;
0x3A-0x3B); 0x1D missed the strip (0x31), 0x1E ran the roadblock
(0x32), both setting `0x5524f4`; 0x1F, 0x20, 0x21 the strip laid
(officer 0x39, 0x3A on the left, 0x3B on the right; dispatcher 0x37);
0x22 the roadblock set (0x35-0x36; 0x3C); 0x23 crashed (0x1A-0x1B);
0x18 0x0C-0x0D, empty slots. `0x40a970(cop, form)` is the dispatcher's
answer when the cop didn't speak last: form 0, "go ahead, unit n"
(0x24 + index) or, half the time, "unit n" (0x14 + index, half of
those) and "go ahead" (0x22); another form, perhaps "unit n" and
"ten-four" (0x20). `0x40aa70` is the dispatcher's opener: 0x10 "all
units be advised" (a new chase; a strip or roadblock set when he last
spoke over 1,000 sound steps ago), 0x11 "all units" (a cop joining, a chase taken
again; bnk.md), 0x12 "available units" (the call for help). `0x40aeb0` gives
the place: the row of the track's table (`0x560aa0`, rows of first
slice, end, kind, slot) for the speaking cop's slice a little ahead, or the
nearest row, said as "at/by" (the slot, 0x40-0x5F) or "near/from"
(+0x20), with "a quarter mile" (0x7E, 50 slices off) or "a half mile"
(0x7F, 100) first; the dispatcher repeats the officer's last place
(`0x5524f8`, `0x552520`). `0x40b190` says the speed (0x3C-0x3F: over
100, 120, 140, 180 mph). So `c3-radio`'s first call is unit 46's call
sign, in pursuit of the car, by the covered bridges (Hometown slices
171-224), over 100 mph; the dispatcher's all units, unit 46, in pursuit
of the car, by the covered bridges.

**Radio runs** (`s6-r1`-`r3`, November 2026: the racer-side pursuit
of `c3-radio`, Hometown, the computer driving the player's car, the
lines watched at `0x4c85f4`, the place look-ups at `0x40af89`). A
disabled cop and a crash need a car on its roof (`0x402c80`: upright
+0x10C below 0, not in the air), which the computer's chase never
gave, so the upright was poked just before the test (`-pokeevery`,
cop 0 at `0x40330f` from its 2,950th time, 102.4 s; the target at
`0x403774` from its 212th, 102.3 s), only into the read: the cars
weren't turned over.
- **Disabled** (`s6-r3`, cop 0, voice A, `0x403950` at 102.37 s): its
  call sign (0x10), the dispatcher's "go ahead, unit 20" (0x24), "car
  disabled" (0x1F), the wrecker (0x1D, the random half), the place
  (0x48, the slice ahead 412, in Hometown's row 409-418), a click. No
  reply follows. The bit (+0x711 0x10) clears when the cop is upright
  again, so with the poke held it spoke again (0x1F or 0x1E, mostly
  with 0x1D) every 3 to 12 s.
- **Crashed** (`s6-r2`, car 0 chased by cop 0, `0x403910` at
  102.25 s): the call sign (0x10), the dispatcher's "unit 20" (0x14)
  and "go ahead" (0x22), "suspect has crashed" (0x1B), the place
  (0x48, slice 411), a click; no reply. The record's bit (+0x54 8) is
  set even when the radio has no room: in `s6-r1` the crash came at
  109.1 s, with a call for backup queued, and was never said.
- **The places** (all three runs, the speaking cop's slice ahead and
  the line): 167 → 0x43 (4 slices before row 171-224, kind 1: the
  "at" form); 323 → 0x40 (in row 322-341); 411-412 → 0x48 (in row
  409-418, kind 0, the "at" form inside); 390-425, off the rows →
  0x68 (the nearest, 409-418, kind 0: "near/from"); 769 → 0x69 (row
  667-700, kind 0, 69 slices off: "near", no distance, as kind 0
  gives none); 453 → 0x41 (row 469-491, kind 1, 16 off: "at"). Each
  dispatcher's reply repeated the officer's place. So the table works
  as read, by the cop's slice.

**Radio runs on other tracks** (`s7-t2`-`t4`, 200 s; `s7-L3`, `L4`,
`L7`, 400 s, `s7-L5`, 300 s; November 2026). The same pursuit, with
the track `0x6fd4ac` poked at `0x474ca0` (2 Atlantica, 3 Rocky Pass,
4 Country Woods, 5 Lost Canyons, 7 The Summit). Chases began at 78 to
90 s. Every place said fits the track's table, by the cop's slice a
little ahead (bnk.md, The places). Rocky Pass and The Summit, which
have no row between slices 42 and 591, gave the half mile (0x7F) nine
times and the quarter mile (0x7E) once (`s7-L7`, slice 496, 95 slices
before row 591-651), each with the "from" place, and the dispatcher
repeated both.

**Why `s7-x1` lost the "from" place** (`c8-q1`-`q3`, November 2026:
`s7-x1` run again, `ram.sh x1 200 0 -drive 0x5e1120:0x5e2e24:50000`,
with `0x4c8653` (a line queued; edi the count before), `0x4c85ce` (a
line handed over with channel 0 free; ecx `0x560118`), `0x4c8b30`,
`0x4c8aeb`, `0x4c8ad7` and `0x401e40` watched). The run repeats `s7-x1`
line for line. **The second line was queued; a stop of the player
then flushed the queue.**
- At 140.17 s cop 6 (index 4, voice C) joined (`0x40b330`, room 4 of
  11 with 7 queued) and queued its call sign 0x14, the car line, 0x7F,
  **0x63** (queue places 7-10), a click, the dispatcher's 0x18 and 0x20
  and a click: 15 queued. The room test asked for 4 of the 8 lines,
  but the 16 places held them all (`0x4c8653`: none at 16); nothing
  was refused. The dispatcher's own call (`0x40a910(3)`, 11 queued)
  had no room, so he gave only the short reply.
- 151.05 s: 0x7F started (`off00C` slot 127, 0.84 s). 151.10 s: cop 7
  queued a new call (9 lines, places 5-13).
- 151.495 s: cop 3's stop of the player began (`0x401e40`; cop 6's at
  151.503 s). `0x4c8b30` (player 0, 3 tickets left) muted channel 0
  (`SNDvol` 0: the 0x7F was cut 0.45 s in) and set `0x560118`.
- 151.90-152.27 s: as each muted or skipped line "ended", `0x4c86d0`
  handed the next to `0x4c85a0` and took it off the queue. With the
  channel free and `0x560118` set, `0x4c85a0` neither starts nor keeps
  it (`0x4c85d0`). All 14 went, 0x63 first, about two each 0.06 s.
- 157.59 s: the `copspch` line over, `0x4c8ac0` cleared `0x560118`
  (`0x4c8aeb`). The next line was a new call at 166.26 s.
- The second time is the same: 0x7F at 187.59 s, cop 4's stop of the
  player at 188.08 s (2 tickets left), 10 queued lines dropped, 0x63
  among them; `copspch` over at 194.19 s.

So any line queued when a computer cop's stop of a human racer starts
is lost, and the line playing is muted. `off00C` has slot 0x63 (`bnk-info`:
present, 1.00 s); no priority, room or timer is involved.

**No roll without a poke** (`s7-x1`-`x4`, 200 s; `s7-y1`-`y4`, 300 s;
November 2026). The player's car, with no computer driver, was steered
by `-drive` at full gas from 50 s: at cop cars 3 and 2 on Hometown
(`x1`, `x2`), at cop car 3 on Rocky Pass (`x4`), and at car 1, the
other racer, on Hometown, Redrock Ridge, Rocky Pass and Lost Canyons
(`x3`, `y4`, `y2`, `y3`, `y1`). It reached 54 m/s and hit cars (within
3 m at over 15 m/s in `y4`, 0.4 m in `x3`), but mostly stuck on walls,
as it steers straight at its target. `-count` showed `0x403950`,
`0x403910`, `0x40bba0` and `0x40bc30` never ran: no car went onto its
roof, so no disabled cop and no crash was said. The strips and
roadblocks these runs gave (dispatcher 0x31-0x33) didn't turn anyone
over either. None of the earlier runs logged these routines.

**What a disabled cop or a crash needs, from the code** (read,
November 2026: `callers`, `gxref`, `gdecomp`, the disassembly).
- **One way in each.** `0x403950` is called only at `0x40331c` and
  `0x403910` only at `0x403781`, both in the computer cop's turn
  `0x4032e5`; `0x40bba0` is called only from `0x403950` (`0x403961`)
  and `0x40bc30` only from `0x403910` (`0x40392a`). Both turn on
  `0x402c80` (+0x10C below 0 and +0x164, the air ticks, 0). No damage,
  wall, hit or timer reaches them, and nothing else says 0x1B or
  0x1E-0x1F.
- **The disabled cop**: at every turn of every computer cop, in any
  state, before the state switch (`0x40330f`); the turn comes every 8
  steps (`0x40df10` → `0x401240`), so the cop need lie on its roof only
  up to 1/8 s to be seen. Said once (+0x711 bit 0x10, set at
  `0x403966`; cleared at `0x403323` at a turn the cop is not on its
  roof), with room for 4 lines (`0x40bba0`: `0x40a910(4)`, so at most 7
  queued). The player is never such a cop: `0x4032e5` drives only
  computer cops, and with one player as the cop `0x474640` makes none
  (`g_num_ai_cops` 0; 2 only with the `-msdemo` command-line flag,
  `0x55eb2c` bit 21).
- **The crash**: only in state 5, after the stop test (`0x402110` at
  `0x403745`) has returned 0, when the target (+0x624) is on its roof
  (`0x403776`); then the rest of the turn is skipped. Once a record
  (+0x54 bit 8, tested at `0x403922`, set at `0x40393b` even when
  `0x40bc30` found no room for its 3 lines).
- **What +0x10C is.** Only `0x4a2690` writes it (`0x4a26ef`, and 1.0
  at `0x4a0e5b` when a car is put on a slice): the car's up row · the
  road's up (+0x130). So "on its roof" is against the road, not the
  world.
- **Only flat on the roof counts.** `0x4a2830` sets +0x164 to 1 (air
  mode) whenever `0x49fde0` says |+0x10C| ≤ 0.9; in a pursuit
  (`0x6fd4a0` 0, `0x4746bd`) that is its only test. The landing
  (`0x4a1ce0`) clears +0x164 only when |+0x10C| > 0.9, every spin under
  0.6, the speed into the road under 0.5 and the clearance 0 or less.
  So a car is on the ground only within 25° of flat on its wheels or
  on its roof, and `0x402c80` is true only for +0x10C under −0.9. A
  car on its side stays in air mode and never counts.
- **No roll on the road.** On the ground `0x4a1610` writes the roll and
  pitch rates (+0xE4, +0xEC) every tick: towards the road plane, at
  most 0.5 turn/s (180°/s; the unit is turns, see Rolls in the air), on the side the car is on (the sign of +0x10C), or 0
  once within 0.02 (`0x4a1b18`-`0x4a1cc6`). Tyre torque (`0x4ae391`)
  and hits are written over. A car turns over only in air mode: in the
  air (clearance over 0.6, spins held within ±2.4 turns/s, `0x4a20c0`)
  or tipped past 25°, where the box's corners are pushed out of the
  road (`0x4a1210` → `0x42ee10`). To be seen, it must turn 155° or more
  from flat before it comes to rest.
- **The reset gives time.** `0x429260` (from `0x42b900`, 32 times a
  second) starts the timer +0x8A0 at 700 (`0x5efdc0`, set at
  `0x42c5f4`; 175 with `0x6fd4a0`, not in a pursuit) when the car is
  tipped (its up · the road's under 0.75, 41°), on surface 0 or 0xE,
  or +0x184 is set (`0x429452`). Each tick takes 1 (a computer car, or
  +0x10C under 0.1), 2 more (+0x20 or +0x24 0, or those surfaces), 8
  more (on the ground under 1 m/s), 1 more (a human tipped with the
  gas over 200). At 0, `0x428f90` puts the car 5 slices on
  (`0x5efdcc`), 1.5 m up and still. A car at rest on its roof is
  moved within about 78 ticks (2.4 s), long enough for a cop's turn;
  one on its side, in air mode, within about 7 s. While the timer
  runs, a car on the ground (+0x164 0, clearance under 5, +0x184 0)
  tipped past 60° (+0x10C under 0.5) also loses 1/64 of each spin a
  tick (`0x4292fb`-`0x429341`, `0x5371d8`; seen in `e10-h6`, Checked
  in runs).

**Rolls by keys** (`c8-f1`, `c8-f2`, `c8-r1`, `c8-r2`, November 2026:
the racer's pursuit, only the race type and the track poked at
`0x474ca0`, the player's car by fixed keys; `-count` on `0x403950`,
`0x403910`, `0x40bba0`, `0x40bc30`, `0x428f90` and `0x429452`).
- Hometown (`c8-f1`, `c6-h2`'s keys, then keypad 6 and 4 pulses with
  the gas held): cop 3 took the player at 138.48 s and cop 6 joined
  (146.15 s), as in `c6-h2`; their stops began at 150.23 and 150.29 s
  and cop 6 gave the ticket at 156.26 s. `c8-f2`: the gas from the
  start (50 s).
- Redrock Ridge (`c8-r1`, `c8-r2`): the gas from the start, two steering
  pulses. The road's points (`tr01.col`) have a crest at slice 78, 467
  m from the line, where the road drops to −7.7°: a car leaves the
  ground there above about 43 m/s (g over the crest's curvature). Both
  went over it at 39-41 m/s and on to 47-49 m/s, then hit a wall at
  slice 104 (`c8-r2`: 49.3 m/s to 0.5 m/s in a second) or 123 (`c8-r1`).
  The car dumps (each second) show +0x10C 1.00 throughout.
- In all four `0x429452` never ran: no car of the eight tipped even 41°
  against the road, and no reset came (`0x428f90` 0). None of the
  four routines ran. The player was never chased in `c8-r1`/`r2`.
- So a disabled cop or a crash isn't reached this way: wall and car
  hits at up to 49 m/s leave the car flat (the on-road code takes out
  any roll), and a crest gives seconds of flight at best with no roll
  rate to turn it over. Other long drops: Hometown slice 199 (in the
  covered bridges, the road drops 16°; off at about 29 m/s), Rocky Pass
  and The Summit slice 311 (17.6°, 30 m/s), Lost Canyons and Redrock
  360 (35 m/s); the `s6-`/`s7-` chases ran through Hometown's covered
  bridges with no roll. Not tried then: a car that leaves the ground
  while hit from the side, or a car landing on another (since: below,
  Rolls from car hits). A bank at the road's edge turned out to be the
  way (below, A roll by keys).

**Rolls in the air, from the code** (read November 2026: `gdecomp`
of the cached export, `asm`, `field car 0xe4`, the constants read
from the exe).
- **The spin is in turns a second**, not radians. `0x4a2f11` calls
  `0x4a2e20` with 32 (edx), which turns the body each tick by +0xE4,
  +0xE8, +0xEC / 32 through `0x4a2cc0`; its rotations (`0x4dfd80`,
  `0x4dfde0`, `0x4dfe40`) take turns (`0x4ea930`: × 2π, `0x54b6b0`).
  The contact code takes the point's velocity as v + (spin × 6.2831845)
  × r (`0x42ee10`, `0x42f280`). So the air clamp (`0x4a20c0`, ±2.4,
  `0x4019999a`) is 864°/s, the on-road pull (`0x4a1610`, 0.5) 180°/s
  and the landing's 0.6 (`0x53c88c`) 216°/s. 155° is 0.43 turn: 0.18 s
  at the air clamp, 0.24 s at the 1.8 of the contact clamp below. **The
  clamps don't make a roll over impossible.**
- **What an impulse gives** (`0x42e820`, from `0x42ee10` and
  `0x42f280`): J is a momentum, × 0.75 when over 5 m/s × the mass
  (`0x5373bc`, `0x5537ac`); the velocity gains J's part along the
  contact's r̂ / mass, and the spin f × (r̂ × J) / (mass × +0x120 ×
  2π) (`0x5373c4` = 1/2π), then every spin is held within ±1.8
  (`0x42e720`, `0x5537b0`). +0x120 is the box's half diagonal and
  +0xB4 1 / mass (the player's car in `c8-r1`: mass +0xB0 1472, half
  sizes 1.18, 0.67, 2.41, +0x120 2.76): 0.058 turn/s per m/s of J /
  mass across r̂, 0.043 over 5 m/s. Against a wall or the road
  (`0x42ee10`) J is the point's momentum into the surface and f 1; car
  against car (`0x42f280`, after the box test `0x42fee0` in
  `0x430c80`) J is the whole relative velocity of the contact point ×
  the other car's mass and f = mass / (both masses), and a strength
  over 5 (`0x5373f4`) adds 1 to +0x164: **a hard car hit puts both
  cars in air mode**.
- **Where roll can come from.** On the ground `0x4304b0` tests the
  wall at 4 corners at the centre's height, the wall's normal flat
  (its y 0), so r̂ × n is upright: walls give a grounded car yaw
  only. In air mode it takes 8 corners at ± the half height: a
  corner's lever for roll is 0.67 / 2.76 = 0.24, about 0.010 turn/s
  per m/s into the wall, and a level car's top and bottom corners on
  the same wall cancel. A car hit at the same height as the hitter
  gets the same: only a contact off the centre's height rolls it.
- **What takes roll out.** On the ground, `0x4a1610` sets it every
  tick (above). In air mode on the road (clearance under 0.6) the
  landing multiplies every spin by 0.5 + e (0.6 at e = 0.1) each tick,
  but only while +0x10C > 0.9 with the car moving into the road and
  its body under it; a hit's roll then adds up to s / 32 / (1 − 0.6),
  0.078 × s turn, so one hit tips a car past 25° only with s ≥ 0.9
  turn/s (J / mass ≥ 15 m/s at the full lever, 21 m/s over the
  0.75 cut). Past 25° nothing damps: the spin stays until a contact
  changes it (`d2-b2`: +0xE4-+0xEC −0.01, −0.38, 0.08 for 1.25 s).
  In the air (clearance over 0.6) only the clamp acts.
- So no number in the code forbids it: what is needed is a car tipped
  past 25° (so out of the landing's damping) with some roll left,
  which a slope under one side gives better than any hit.

**A roll by keys** (`d2-b1`-`b10` but `b4`, November 2026: Hometown,
the racer's pursuit, only the race type and track poked at
`0x474ca0`; the gas from 40 s and one left-key pulse; car 0 dumped
every 0.125 s; `-count` on `0x429452`, `0x403950`, `0x403910`,
`0x40bba0`, `0x40bc30`, `0x428f90`). Hometown's road run has a bank
at its left edge from the grid to about slice 25, 23-27 m left of
the road points, rising 1.7-3.1 m (21-35°; the run's polygons
against the `.col` points, a script outside the repo). Without
steering (`d2-h0`) the car ran onto it at 30 m/s at 57 s and tipped
15° (+0x10C 0.965, air mode).
- A pulse from 54.0 to 55.0 s (`d2-b8`): the car reached the bank at
  slice 13 at 25 m/s (55.6 s), slowed to 7-9 m/s on it in air mode
  (+0x10C under 0.9 from 56.4 s), tipped past 41° at 56.69 s
  (`0x429452`, the reset timer started), rolled on over the bank's
  edge (+0x10C 0.29 at 58.0 s, −0.52 at 59.0 s) and came to rest on
  its roof at slice 16, out of air mode (+0x164 0, +0x10C −0.95) at
  59.5 s. At 62.0 s `0x428f90` put it back upright 5 slices on.
- The same from 54.5-55.5 s (`d2-b6`: past 41° at 56.95 s, on its roof
  on the ground at 59.25 s, reset by 61.9 s) and 53.8-55.3 s (`d2-b10`:
  56.06 s, 58.5 s, reset by 61.25 s). 54.2-55.7 s (`d2-b9`) went to
  +0x10C 0.075 (86°) and fell back; 54.0-55.5 s (`d2-b2`), 54.0-56.0 s
  (`b5`) stopped at 0.77-0.78 (39-40°, short of the 0.75 test); 53.0-54.0, 55.0-55.6, 53.5-
  55.5 s (`b1`, `b3`, `b7`) stayed under 15°.
- So the player's own car can lie flat on its roof by keys alone:
  `0x402c80` is true for it for 2.5-2.8 s, the time the reset takes.
  No line came: `0x403910` needs a computer cop in state 5 with this
  car its target at one of its turns (every 8 steps) in that time,
  and at 59 s on Hometown no cop is chasing yet (`c8-f1`: cop 3 took
  the player at 138.48 s). `0x403950` needs a computer cop on its roof,
  which keys can't steer onto a bank.
- **The crash line, heard** (`d2-c4`, `d2-c4v`, November 2026: the
  same set-up, `c6-h2`'s keys, A at 133.0 s and keypad 8 held from
  133.5 s, then the left key 139.2-140.2 s; cars 0-7 dumped every
  0.25 s, cop state +0x70C, target +0x624). The player stays on the
  grid until 133 s, so cop 3 meets it at the start: it took the
  player at 138.475 s at slice 14 (`0x4019a1`, as in `c6-h2`) and was
  in state 5 on it from then on, slices behind. The left pulse put the
  car on the grid's bank at slice 21 (140.25 s, air mode), past 41° at
  140.65 s (`0x429452`), over (+0x10C −0.10 at 141.75 s) and onto its
  roof on the ground at slice 26 at 142.25 s (+0x164 0, +0x10C −0.97),
  where it lay until the reset (`0x428f90`, slice 32, by 145.5 s).
  Cop 3's turn reached `0x403910` at 142.221 s, and `0x40bc30` ran
  once (cop 3, `0x5e2e24`) and queued three lines (`0x4c85a0`);
  `0x403910` ran 26 times in all (every turn while the car lay
  there; the record's +0x54 bit 8 kept it to one call of
  `0x40bc30`). The radio started officer bank entry 0xC's slot 0x1A,
  "the suspect has crashed" (bnk.md), at 148.869 s (`0x4c85f4`), after
  the lines already queued. Nothing of the cars was poked. The other
  pulses (`d2-c1` 138.6-139.6 s, `c2` and `c6` 139.0-140.0/140.6 s,
  `c3` 139.0-140.3 s, `c5` 138.8-139.8 s) tipped the car to 67° at
  most, or not at all, and fell back.

**Rolls from car hits, from the code** (read November 2026: `gdecomp`
and `asm` of `0x4310c0`, `0x430c80`, `0x42fee0`, `0x42f550`,
`0x42dd60`, `0x42e010`, `0x42e2c0`, `0x42e4f0`, `0x42f8c0`, `0x42fe10`,
`0x42f280`, `0x42e820`, `0x42e720`; the constants read from the exe).
- **Cars are boxes in 3D, not 2D.** `car_collisions_task` (`0x4311d0`)
  hands each car in play to `0x4310c0`, which tries it against each
  car handed in before it that step (list `0x5efe10`) when |dx|, |dy|
  and |dz| between the centres are each under the sum of their +0x120
  (half diagonals): the height counts like the rest. `0x430c80` then
  calls the box test `0x42fee0` while it finds a point inside, at most
  33 times, and the impulse `0x42f280` once at the last point found
  (`0x5efe84`). `0x42fee0` puts the other car in this car's frame (rows
  +0xC0, +0xCC, +0xD8, half sizes +0x114, +0x118, +0x11C) and
  `0x42f550` tries 28 points in order: the other's 6 face centres
  inside this box (`0x42dd60`), this car's 6 inside the other's
  (`0x42e010`), the other's 8 corners (`0x42e2c0`), then this one's 8
  (`0x42e4f0`); the first inside is the contact. The top and bottom
  faces' centres are among them, so a car above another is met. Two
  boxes crossing edge to edge with no face centre or corner inside are
  missed. After each point found, `0x42fe10` moves both cars apart
  along the line between their centres (not along a face), 0.25 m
  (`0x537404`) in all, each by the other's share of the two masses
  (+0xB0); 4 such rounds a call.
- **No contact normal is used.** `0x42f8c0` works out a face normal
  (`0x5efe78`; from the velocity difference when that is over 15 m/s,
  `0x5373fc`), but nothing outside it reads `0x5efe78`. In `0x42f280`
  a car's J is (the other's point velocity − its own, each v + 2π
  spin × r) × the other's mass, used only while closing (centre line
  · J < 0), with f = its mass / both masses. `0x42e820` cuts J × 0.75
  when |J| > 5 × its mass (`0x5373bc`, float 5; `0x5537ac`), adds
  −(J · r̂) f / mass along **J's own direction** (so the hit takes the
  sideways slip along too) and f (r̂ × J) / (mass × +0x120 × 2π) to
  the spin, then holds each spin within ±1.8 (`0x42e720`). r̂ is the
  unit vector from the car's centre to the point: the lever is the
  angle between r̂ and J, not the distance. The strength it returns
  is that velocity part + f |r̂ × J| / mass; over 5 (`0x5373f4`,
  double) adds 1 to +0x164.
- **The numbers** (two cars of 1472, +0x120 2.76, half sizes 1.18,
  0.67, 2.41, as the player's in `c8-r1`): at a closing speed u of the
  two points, J f / mass = u / 2, × 0.75 above 5 m/s. The strength is
  at least 0.375 u, over 5 from about 13 m/s: any real hit sets air
  mode. The spin is 0.75 × ½ × u sin θ / (2.76 × 2π) = **0.0216 u sin
  θ turn/s** (θ between r̂ and u); for a side-on u the roll (about the
  car's forward axis) is 0.0216 u × r̂'s up part in the car's frame.
  - Level and at one height (side-on or T-bone): a face centre lies at
    its car's centre height, so a face-centre contact has r̂ flat for
    both cars: yaw, no roll. A corner (only when no face centre is
    inside) has r̂'s up part 0.67 / 2.76 = 0.24 for the car whose
    corner it is: 0.0052 u, 0.10 turn/s at 20 m/s, 0.26 at 50.
  - The car hit lying lower than the hitter by h (up to its half
    height 0.67, the hitter's face centre inside its box): r̂'s up part
    is h / √(h² + d²), d the distance across (at most 1.18); at h 0.6,
    d 1.0 about 0.5, so 0.011 u: **0.22 turn/s at 20 m/s side-on, 0.33
    at 30, 0.44 at 40, 0.55 at 50.** The hitter's r̂ goes to its own
    face centre, flat: no roll for it. Higher than 0.67 above, only the
    hitter's lower corners can be inside, with about the same lever.
  - The clamps don't bind: one hit gives at most about 0.6 turn/s, under
    both the contact clamp 1.8 and the air clamp 2.4. 155° (0.43 turn)
    at 0.55 turn/s takes 0.78 s of spin left alone. On the road (+0x10C
    > 0.9) the landing's damping lets a spin s tip the car 0.078 × s
    turn at most (Rolls in the air): 15° at 0.55; 25° needs s ≥ 0.9,
    that is about 82 m/s side-on at h 0.6. In the air (clearance over
    0.6) nothing takes it out, so a car flying for 0.8-1 s more after a
    40-50 m/s side-on hit at that height would roll past 155°, or land
    past 25° still turning. The hit repeats each step while the points
    still close, so a second contact can add.
- **One car landing on another.** The lower car's top face centre (0,
  +0.67, 0) inside the upper's box, or the upper's bottom face centre
  inside the lower's: r̂ is that car's own up (or down), the full
  lever. The speed of the fall lies along r̂ and gives no spin, only
  the push apart (the upper up, the lower down into the road); a slide
  across gives roll, **0.0216 × u turn/s** (0.22 at 10 m/s across, 0.43
  at 20), a slide along gives pitch. The car on top is over 0.6 m
  above the road (+0x15C), so the road code treats it as in the air:
  its spin is kept, gravity (0.3066 m/s a tick) brings it down again
  and each step's contact adds more. Nothing in the code stops a car
  resting on another.

**The hit's arithmetic, re-read for the Track page** (November 2026,
`asm` with capstone of `0x4310c0`, `0x430c80`, `0x42fee0`, `0x42f550`,
`0x42dd60`, `0x42e010`, `0x42e2c0`, `0x42e4f0`, `0x42fe10`, `0x42f280`,
`0x42e820`, `0x42e720`, `0x4e0000`, `0x4e0050`, `0x4e0160`, `0x4dffb0`;
`gdecomp` of the four point tests). What the page's `carBoxTouch`,
`carBoxHit`, `carBoxCollide` and `carBoxCollisions` do:
- `0x42fee0(a, b)` works out the nine dots of a's rows with b's once
  (`0x5efebc`-`0x5efedc`), then b's half axes in a's frame
  (`0x5efee0`-`0x5eff00`, row i: a's axis i · b's axis j × b's half
  size j) and a's in b's (`0x5efe90`-`0x5efeb0`). Each round takes d =
  b's centre − a's (`0x4e0000`), d in a's frame (`0x5efe60`) and in
  b's (`0x5efe6c`), and asks `0x42f550` for a point: b's face centres
  (s = ±1 on one axis, in the order +x, +y, +z, −x, −y, −z: `ebx`,
  `ecx`, the pushed argument) at d_a + Σ s_j h_bj (a_i · b_j), inside
  when |each| ≤ a's half size (`<=` both ways); then a's face centres at
  Σ s_i h_ai (a_i · b_j) − d_b inside b's box; then b's corners and a's,
  (+,+,+), (+,+,−), (+,−,+), ... (−,−,−). The point (`0x5efe84`) is that
  centre or corner in the world. `0x42f8c0` (codes 3 and 4) only writes
  `0x5efe78`-`0x5efe83`. `0x42fe10` normalises d (`0x4972f0`) and moves a
  by −0.25 × m_b / (m_a + m_b) along it and b by +0.25 × m_a / (m_a +
  m_b). Four rounds at most; it returns 1 if any found a point.
- `0x430c80` calls it while it returns 1, with `ebx` from 32 down to 0
  (33 calls), and then `0x42f280(a, b, 0x5efe84)` once: the point is the
  last one found, from before that round's push.
- `0x42f280`: r_a, r_b from each centre (after the pushes) to the point;
  each point's velocity v + (spin × 6.2831845, `0x40c90fd9`) × r
  (`0x4e0160`, the usual cross product); J_a = (v_b − v_a) m_b, J_b =
  (v_a − v_b) m_a; a takes its share when (b − a) · J_a < 0, b when
  (b − a) · J_b > 0, each with f = its own mass / both. `0x42e820` cuts J
  to 0.75 when |J| > 5 × its mass, adds s = −(J · r̂) f / m (when over 0)
  along J / |J|, and (r̂ × J) f / (m × |half sizes| × 2π) to the spin
  (`0x5373c4`, 1/2π as a double), holds each spin within ±1.8
  (`0x42e720`) and returns s + f |r̂ × J| / m, which `0x42f280` stores in
  +0x168 (with +0x16C the other car, +0x170 0x50005, +0x178 the point);
  over 5 it adds 1 to +0x164.
- `0x4310c0` is called by `car_collisions_task` (`0x4311d0`, Sc32, so 32
  times a second, after the cars' models) for each car in play without
  +0x88 and with +0x174 (a ghost count after a reset, 64) at 0; the
  list `0x5efe10` is emptied each tick (`0x430cd0`).
- Masses (`0x42bcb0` → `0x4a0d20`): a human's P mass (× 3, `0x5efdbc`, a
  cop's or in a pursuit); a computer car's P mass, over 1.2 (`0x5efdb8`,
  `0x3f99999a`) when +0x200 has bit 1, a racer: in `t6-log` the
  Corvettes 1226.7 (P's 1472), cops in `d8-t8g0` (+0x200 0x22) their P
  mass. Half sizes from the model (+0x8A4 +0x28), +0x120 their length.
- After a hit `car_impacts_task` (`0x42b900`) calls `0x406120` (under
  10 m/s, `0x5361ec`, the car reverses for 128 steps) and `0x404a00`:
  for a car with +0x200 bit 2 (driven by the computer) +0x65C = the step
  (`0x7d3684`) + round(+0x168) × 7 (× 2 when the other is a human's, × 16
  a human cop's), and while the step is under it `car_ai_model`
  (`0x408600`) takes `ai_ctl_slide` (`0x4087c3`). With +0x164 set
  `car_ai_physics` (`0x4088f0`, `0x4089d1`) skips the model, until the
  dynamics' landing clears it (every spin under 0.6, each × 0.6 a tick
  meanwhile with a computer car's e = 0.1).

The Track page's computer cars now do this (`aiCollide`, after every
car's step on the even steps): each car's box from its place on the
road (its centre the half height over it), its velocity (the speed
along its way and across the slice) and its yaw rate as the spin; the
pushes, speeds and yaw are taken back, the slice moved where a push
takes the car past a slice's end. The page keeps the hit's spin about
the car's forward and right as roll and pitch rates and brings them
back as the dynamics does: in the air each spin × 0.6 a tick until all
three are under 0.6 (`0x4a1ce0`); on the road each rate is the
up-vector difference held to ±0.5 turn/s, 0 within 0.02 (`0x4a1610`).
A car hit under 10 m/s also starts or ends its reversing (`0x406120`,
from `car_impacts_task` `0x42b9a8`: `0x405fd0` with 2 and 0x80).

**Rolls from car hits, run** (`d3-` runs, November 2026: the racer's
pursuit, only the race type and track poked at `0x474ca0`; the
player's car by `-drive` at car 1 or 2 with full gas or a gap, and
keypad 2; cars 0-7 or 0-15 dumped every 0.125 s; `-count` and
`-watch` on the contacts `0x430cbd`, the hard hits `0x42f4a2` and
`0x42f53b` (+0x164 + 1), and `0x429452`, `0x403950`, `0x403910`,
`0x40bba0`, `0x40bc30`, `0x428f90`). The race starts at about 50 s.
- **Hard hits on the road leave the cars flat.** Hometown (`d3-e1`,
  `e3`: the player at car 1 or 2 from 38 s; `g1`, `g2`: at car 1 with
  a 3 m or 1 m gap from 50 s): cop 3, driving the wrong way from slice
  239, met car 1 nearly head-on at slice 150 at 73.60 s (car 1 at 42.4
  m/s, the cop at 12.9): both hard hits, car 1 down to 15.7 m/s, air
  mode at the 73.625 s dump and out by 73.75 s, spins at most 0.19
  turn/s. The player then ran into cop 3 at 74.29 s (42.9 to 27.9 m/s)
  and car 1 at 74.75 s. 49-78 contacts and 1-8 hard hits a car a run;
  +0x10C stayed at 0.984 or more for all twelve cars in play, and
  `0x429452` never ran. Redrock (`d3-e2`, `g6`): the player into car
  1's back at 82.69 s, 47 against 39 m/s: both 0.999. (`0x429452`
  ran for traffic car 10 at 85.02 s with +0x10C 1.00 in every dump,
  so for its surface or +0x184, not a tip.)
- **A flying car into a standing one** (`d3-h84`, `h85`, `h86`:
  Hometown as `e1`, the `-drive` ended and keypad 2 held from 84, 85
  or 86 s, so that the player stood just past the covered bridges'
  drop at slice 199 with cop 3, in state 5 on it, behind). `h85`: the
  player stood at slice 205; cop 3 came off the drop in the air (22
  air ticks, clearance 0.59, its centre 1.0 m above the player's, 37.9
  m/s and falling at 11.6 m/s) and hit it from behind at 90.129 s (hard
  for both): the player shoved from 0.2 to 17.7 m/s, cop 3 down to
  20.1, then spins at most 0.18 turn/s (yaw), the roll part 0.04, and
  +0x10C 0.984 or more for both. A hit along the cars' length gives
  pitch and yaw, not roll (above). `h84` (88.66 s) and `h86` (91.70 s)
  met on the road at 17-21 m/s: the same, less.
- **Alone in the air** (`d3-g3`: Redrock, the player at car 1 with a 3
  m gap from 50 s, never caught up): the player left the road at the
  crest (slices 78-84) at 64.9-65.6 s at 50 m/s, clearance up to 0.98,
  17 air ticks, and landed flat; car 1, 95 m ahead, only hopped at 44
  m/s (clearance 0.53, not air mode). `d3-g4`, the same run on to 200
  s for the second lap's crest, had the player standing from about 121
  s, after hard hits among it, car 1 and cops at 111-117 s (32 in the
  run; `0x429452` for it at 100.779 s and the reset at 123.132 s, not
  dumped), so it never got there.
- **A roll with no car** (`d3-g5`: `e1` run on to 110 s). Steering
  back at car 1 after passing it at 56 m/s, the player ran off
  Hometown's road at slice 320 at 40 m/s, up the ground beside it from
  9 to 21.8 m, and flew for 2.8 s (+0x164 to 91) with spins −0.19,
  −0.20, −0.24 unchanged all the way (nothing damps in the air):
  +0x10C −0.60 at 102.5 s, 0.97 as it touched at 104.0 s, then over
  again onto its roof on the ground at slice 322 at 105.5 s (+0x10C
  −0.98, +0x164 0) until the reset (`0x428f90`, 107.574 s); `0x429452`
  at 101.379 s. Cop 3 was in state 5 on it at slice 323: `0x403910`
  ran 17 times and `0x40bc30` once, the crash line queued again
  (`d2-c4`), with only `-drive` and the set-up pokes. No hard hit came
  between 97.7 and 109.6 s.
- In none of these runs did a computer cop tip 41°: `0x403950` and
  `0x40bba0` never ran. No run had a side-on hit with a car in the air
  or one car landing across another: cars in the air (the crests and
  drops above, a few tenths of a second) were within 6 m of another
  car only in `h85`.

**A cop drawn onto the bank** (`e2-a1`, `e2-a2`, November 2026:
Hometown, the racer's pursuit, only the race type and track poked at
`0x474ca0`; `d2-c4`'s keys, A at 133.0 s and keypad 8 from 133.5 s,
then one left pulse with the gas let go at its end: 138.6-139.6 s in
`a1`, 139.0-140.0 s in `a2`; cars 0-7 dumped every 0.25 s from 138 s;
`-count` and `-watch` on `0x403950`, `0x40bba0`, `0x403910`,
`0x429452`, `0x428f90`, `0x401e40`, `0x401c20`, `0x401dc4`,
`0x401ff0`). The idea, from the code: a chasing computer cop sets
+0x711 bit 2 at each turn while its target is under 15.65 m/s
(`0x402750`, `0x5360cc`; the bit is cleared at the start of each
turn, `0x403308`), and with that bit and within 4 slices it steers
at the target's place 0.4 s ahead (`0x406e50` → `0x406b60`), not at
the road. So a target standing on the bank should bring the cop onto
it. It did: in `a2` the player came to rest upright on the bank at
slice 25-26 (x 922-925, 3 m above the road; +0x18C surface 2), cop 3
(state 5 on it since 138.475 s, as in `d2-c4`) came at 29 m/s with
bit 2 set (dumps 145-148 s), left the road (surface 1 → 11 → 2),
slowed to 4 m/s and stopped 3.6 m from the player at 149 s on the
bank, +0x10C 1.000 in every dump; its stop began at 148.123 s and
cop 6 (0x5e4b28), which joined, gave the ticket at 154.139 s. In `a1`
the player stopped at the bank's foot (slice 18-19, x 907-911) and
cop 3 stopped beside it (stop 146.362 s, cop 6's ticket 152.381 s).
No cop tipped: `0x403950` and `0x40bba0` never ran. The cop arrives
slowly (the chase's speed wanted near a target is its speed + 5.81
m/s, `0x402420`) and where the player could stand, the bank is gentle.
A cop led across the steep part (slices 13-16 and 21-24, where the
player rolled in `d2-b8` and `d2-c4`) is the next thing to try; the
player can't stand there itself without rolling.

**A cop led up the bank's steep part** (`e4-` runs, November 2026:
Hometown, the racer's pursuit, only the race type and track poked at
`0x474ca0`, no car-state poke, keys only; `e2-a1`'s keys to 139.6 s,
which leave the player at rest at the bank's foot, slice 18, 20.5 m
left, facing 60° up the bank, cop 3 in state 5 on it 14 slices back;
then more keys; cars 0-7 dumped every 0.125 s; `-count` and `-watch`
on `0x403950`, `0x40bba0`, `0x403910`, `0x40bc30`, `0x429452`,
`0x428f90` and the stop's routines). The bank's cross-section, from
the `.frd` against the `.col` points (a script outside the repo): the
road's edge strip (surface 11) to 18.7 m left, then surface 2 rising
0.6-1.0 m to 22-23 m and 2.3-3.3 m at 24-27.5 m (23-35°, 42-60° at
slices 25-27), then surface 14 beyond. No car got past 26.5 m left.
- **Gas and left from rest** (`c143000`, `c143600`, `c144200`: keypad
  8 and 4 at 143.0, 143.6 or 144.2 s, 4 for 0.8 s, 8 for 1.5 s):
  the player crept up the bank, in `c143000` to stand upright on the
  steep part (slice 19, 23.7 m left, 1.8 m up; +0x10C 1.00, as the
  road's up there is the bank's). Cop 3 came along the road 13-17 m
  left at 24-27 m/s, slowed to 10 m/s within the last slice or two
  and stopped 3 m below the player (stops 146.5-147.1 s).
- **Up the bank before the cop comes** (`u1`-`u3`, `w1`-`w3`,
  `x1`-`x3`, `y1`-`y3`, `x2l`, `z144700`-`z145400`: keypad 8 from 140 or 141 s, with left
  pulses and the gas let go at various times between 142.5 and 146.2
  s). The player climbed diagonally to 24-26 m left, 2.5-3.5 m up, at
  slices 20-24 by 143-145 s, at 6-15 m/s. Where it stayed under
  15.65 m/s cop 3 (bit 2 set) turned off the road at it when 2-4
  slices back: in `u3`, `w3`, `x2` and `y1` at 30-81° to the road at
  20-30 m/s, but braking at once (the speed wanted near a target is
  its speed + 5.81 m/s), so it met the bank's foot at 11-18 m/s. The
  farthest: `w3`, cop 3 up the steep part at slice 31-32 to 24.8 m
  left, 3.3 m up, at 12.5-17.8 m/s, turned to 74-127° from the road
  and in air mode for 7-17 ticks (149.75-150.25 s), +0x10C 0.99, then
  back to rest upright; `x2`, to 25.5 m left at 17.7 m/s at slice 32.
  Where the player came down at over 15.65 m/s (`u1`, `u2`, `w1`,
  `w2`, `x3`), bit 2 cleared and the cop stayed on the road.
- **The player rolled instead** (`x2l`, `y2`, `z145200`, `z145400`): driving along the
  steep part at 13-19 m/s and turning up it, the player went past
  41° (`0x429452`, 147.1 s in `y2`, 149.6 s in `x2`), over, and onto
  its roof on the ground at slice 30-32 (+0x10C −1.00), with cop 3
  standing upright on the bank 1-2 m away. `0x403910` ran from
  149.878 s (`y2`, 50 times), 152.128 s (`x2l`, 42 times), 150.475 s
  (`z145200`) and 148.718 s (`z145400`; these two counted only
  `0x403950` and `0x429452`), and `0x40bc30` once in each counted run:
  the crash line queued, as in `d2-c4`. The
  reset (`0x428f90`) put the player 5 slices on.
- In all nineteen runs cop 3's +0x10C stayed at 0.98 or more and
  `0x403950`/`0x40bba0` never ran. The cop's chase brakes it to the
  target's speed + 5.81 m/s within a slice or two of a slow target,
  and a target over 15.65 m/s isn't steered at (`0x402750`), so the cop
  reaches the bank at under about 21 m/s; the player rolled there only
  when it turned up the slope at 13-25 m/s itself. A cop rolling needs
  it to cross the steep part fast, which this steering doesn't give;
  still open.

**Picking a target** (`0x402e10`, in every state but 3, 5 and 6):
racers still in, free (+0x634 passed) and within 16 slices, scored by
`0x403f80`. A racer scores only when it is already chased (record
+0x14), or 6.71 m/s over the section's speed now, or +0x6F0 over 2
(`0x4014b0` keeps +0x6F0 for each one looked at: the most it has been over the section's speed, not its speed; read again November 2026). The record's +0x14 counts chases lost (`cop_lose_target` `0x401a80` adds 1 when the last cop lets the racer go and no strip caught it; a human cop's `0x4052e0` sets it to 1 once +0x6F0 is over 2; the ticket clears it). A computer racer
scores the cops / 3 (at least 1), a human the cops − 1, less the cops
already on it; +0x730's car scores the most. A cop with a strip out
or a roadblock waits up to 960 steps for its own racer within 4
slices, 256 steps, before it takes another.

**The computer cop's stop** (`0x402110`) is wider than the human's:
within **20 m** (`0x4965b0`; 40 m once the record's +0x50 is 3, that
is once three cops have begun their stops on it in `0x401e40`, so it
only widens the hold for a fourth), the
target at most `0x56ea50`, **11.18 m/s** whatever the players (set
once at `0x40130a`), the cop on the ground and upright (+0x10C > 0.5).
The hold is 192 steps (3 s) for a human target, for a computer one
160 when it is more than 60 slices ahead of the leading human, 224
when 60 behind, else 192. While it holds, a human target off the gas
(+0x508 at most 11) takes 19 steps off it at each of the cop's calls
(every 8 steps), so 192 lasts 64 steps. Then the record
+0x1C = 2 and +0x28 = step + 320. Once the target has stopped for it
(a computer racer in its state 6, a human in 7: state 6 of a human
is not enough), the stop starts within 16 m, or at any distance once
the step has passed the record's +0x28. Failing a test clears
+0x710 bit 0x40. With cheat bit 0x10, `0x401520` adds speech
(`0x4127c0`) every 384 steps at a target over 8.94 m/s.

**A fourth cop, run** (`c4-four6`, `c4-four7`, November 2026). The
racer's pursuit below with the player's car driven by the computer
held to 15 m/s (+0x7B0 at each `0x408600`), +0x711 bit 1 poked on
cops 3, 4, 5 and 7 (2 and 6 have it), car 1's +0x6F0 = 5, and
`0x56ea50` poked to −1e6 from 45 s so that no cop could stop anyone
(at −1 a racer creeping backwards still passed). The calls for help
then piled cops on car 1, which raced on at 50 m/s: 3 on its record
at 96 s (cops 2, 3, 5), 4 at 110 s, 5 at 116 s. A computer racer
scores only the cops / 3 less those on it (`0x403f80`), so the
joiners came by the orders of kind 3, not by picking. At 116.0 s
three pokes: the record (`0x56e470`) +0x1C = 2, as a strip does
(`0x401650`), +0x28 = 0x7fffffff, and `0x56ea50` back to 11.176;
in `c4-four7` also car 1's tickets (+0x2C8) = 5, which makes the stop
448 steps. Car 1 pulled over (5 → 6 at 119.91 s). Cop 3 began a hold
at 13.57 m (limit 20, 160 steps; car 1 was in 5 still) and the stop
at 119.98 s, step 5066, within 16 m (`0x40240a`); cop 7 (124.99 s)
and cop 2 (125.15 s) came within 16 m and began theirs: +0x50 3.
From the next calls (125.17 s) cops 4 and 5, 90-100 m back, took the
doubled limit (`0x4021ef`). In `c4-four6` the ticket came at
126.01 s (step 5452, 5066 + 384) with them still 45-55 m away. In
`c4-four7` cop 4 began its hold at **35.53 m against 40 m** (126.19
s, `0x40237a`, 160 steps) and cop 5 at 35.44 m (126.41 s): holds
that the 20 m would have refused. Cop 4 then came within 16 m and
began the fourth stop at 126.72 s (+0x50 4); cop 5 began another
hold at 19.06 m (126.92 s), and cop 2 gave the ticket at 127.01 s
(step 5516), so neither hold ran its 160 steps.

**A fourth cop's hold to its end** (`c6-four8` to `c6-four10`,
November 2026: `c4-four7` again, the same pokes to 116 s). A held
ticket is not enough. In `c6-four8` the record's +0x28 was poked to
0x7fffffff at 121 s, after the first stop had set it, so no ticket
could come. Cops 4 and 5 began their holds at 35.53 m and 35.44 m
(126.19 s, 126.41 s) as before, but drove in under 16 m in 0.5 s and
1.1 s and began their stops there (`0x40240a`, 126.72 s, 127.53 s).
A chasing cop's speed wanted (`0x402420`) is the target's speed +
5.81 m/s plus (top speed − the target's) × slices / 8 within 8
slices, so it drives in at 6 m/s or more even to a stopped racer, and
the 16 m comes first. In `c6-four10` the 16 m (`0x536074`, 16.0) was
also poked to 0 at 126.0 s. Cop 4's hold from 35.53 m broke at step
5496 (16.7 m at the call before; one of the tests failed, which
wasn't logged) and began again at 12.77 m and 15.14 m; cop 5's from 35.44 m began again at
18.24 m. The last ones ran their 160 steps with the 40 m limit
(+0x50 3): cop 5's to step 5694 (11.83 m), cop 4's to 5696 (15.76 m).
At its end each set the record +0x1C = 2 and **+0x28 = step + 320**
(`0x4022bd`, `0x4022c4`) and returned 1 (car 1 was stopped), and its
stop began in the same step. The ticket came at step 6018 (134.84 s),
the next check after 5696 + 320. The ticket waits for the step in
+0x28 (`0x401fb0`), so **a hold that ends late puts off the ticket**.
`c6-four9` shows it without the +0x28 poke (the 16 m poked to 0 at
116 s): cop 3's 20 m hold ran to its end at step 5186 and its stop
set +0x28 = 5186 + 448; cops 7 and 2 held to 5618 and 5636, cops 5 and
4 (40 m by then) to 5710 and 5736, each moving +0x28 on, and the
ticket came at 6058, not 5634. A hold kept wholly beyond 20 m for 160
steps was not seen: cops do not stay that far.

**Why a hold beyond 20 m needs a cop past the target** (read,
November 2026; run `c7-f1`). `0x402110` runs at every turn of state 5,
after the chase (`0x403745`, after `0x4025c0`), behind the target or
past it. The hold holds while the distance (`0x4965b0`) is under 20 m
× 1 or 2, the target at most 11.18 m/s, the cop's +0x164 (time in
the air) 0 and +0x10C over 0.5. The 16 m test returns 1 only once the
record's +0x1C is 2 and the target has stopped.
- **From behind** (`0x56ea68` 0) within 8 slices, `0x402420` asks for
  the target's speed + 5.81 m/s + (top − target) × slices / 8. A slice
  is about 6 m (Hometown: 5,747 m in 959 points), so 20-40 m is 3-7
  slices. With a top near 50 m/s and the target at 11.18 m/s or less,
  that closes at 20-40 m/s. Even the bare 5.81 m/s closes 14.5 m in
  160 steps (2.5 s). So a cop behind cannot stay beyond 20 m for the
  whole hold unless something stops it driving (`c6-four8`: 35.5 m to
  under 16 m in 0.5 s).
- **Past the target** (`0x56ea68` 1, with +0x1C 2 or within 16
  slices), `0x402420` asks for the target's speed × 0.5 (× 0.2 for the
  car at `0x56ef54`): 0 for a stopped racer. Only below 3 m/s
  (`0x5360ac`) and more than 7 slices past does it reverse, at −9.83
  m/s. So a cop past a stopped racer stands where it stopped, if
  within 7 slices (about 42 m), or backs in to 7 slices and stands
  there. A fourth cop standing 20-40 m ahead passes the 40 m test at
  every call, never makes the 16 m test, and its hold runs its 160
  steps.
- **Seen** (`c7-f1`, `c6-four8` again with the cars dumped every 0.5
  s): cop 3 went past car 1 as it pulled over and stood 3 slices
  ahead of it, 16 m, at 0 m/s, from 120.0 s to the end of the run
  (136 s). Cops 2, 4 and 5, all from behind, came in under 16 m (11,
  15 and 15 m). In these runs the cops that join come from behind (the
  calls for help), so the case needs a fourth cop that is ahead when
  the racer stops; none was.
- **What it needs, and tries without pokes** (read and run, November
  2026). The 40 m comes only with the record's +0x50 at 3, and each
  cop adds to it once (`0x401e40`; a cop in its stop, state 6, doesn't
  run `0x402110` again), so four cops must be on one record. The
  first stop sets +0x28 = step + 384 for a computer cop (step + 192
  for a human one, `0x401e40`), so the other two stops and the fourth
  cop's hold must begin before that, or a hold that ends late moves it
  on. Runs with nothing poked but the race type, the player's car a
  human one: `d1-r1` and `d1-r3` (`-drive` behind car 1 at a 15 m gap,
  to 150 and 160 s), `d1-r2` (`-drive` at cop 2 from 70 s: it hit a
  wall at slice 167, 80 s) and `d1-c1` (keys only: a dash to 29 m/s,
  then full lock with the gas pulsed: into the wall at slice 32, 61 s).
  No record had more than 2 cops on it: in `d1-r1` car 1 was stopped
  by cop 3 alone, which met it head on (hold from 76.24 s, stop within
  16 m at 80.48 s, ticket 86.62 s), later by cop 5 (stop 114.43 s,
  ticket 120.55 s), and the player by cops 3 and 5, both behind it
  (stops at 118.72 and 120.78 s, +0x50 2; the player back in its
  state 2 by 126 s). `r2-cop1` (the player's car
  driven by the computer at 50 m/s for 65 s) had at most 3 (125-133
  s). Cops join only by the calls for help, one per 320 steps, and are
  often lost past the table's slices; the five on car 1 in `c4-four7`
  came with the bust speed poked out. So a fourth cop on one racer,
  let alone one ahead, was not seen in a game with no pokes.
- **What raises the count, and a plan** (read November 2026:
  `0x402e10`, `0x403f80`, `0x4043a0`, `0x403b20`, `0x401780`,
  `0x402420`; not run). Cops join a record in three ways: a free cop
  picks the racer (`0x402e10`, within 16 slices; a computer racer
  scores the cops / 3 less those on it, a human the cops − 1 less
  them, so picking adds a cop to a human's record more readily); the
  call for help every 320 steps (`0x4043a0`: free cops on the far
  side of the target within `0x5521ac`[tickets] × 2 slices, 100 for
  0-1 tickets, 140 for 2-3, 180 from 4, and cops with +0x711 bit 1
  within × 8, come with the siren and join on arrival); and the
  roadblock (`0x403b20`, a human target only, heat over 3), whose two
  cops are sent 51 slices or more **ahead** of the target. A
  roadblock's cops (+0x711 bit 4) take no other racer while their own
  (+0x730, the nearest) is away: `0x402e10` waits until it has been
  within 4 slices for 256 steps (4 s), or 960 steps have passed, then
  `0x4018b0` puts the cop on its record. A cop ahead of its target in
  its own running direction is "past" it (`0x401780`: `0x56ea68` =
  the slices to the target < 0), and once the record's +0x1C is 2 it
  is asked for the target's speed × 0.5, 0 for a stopped racer: it
  stands where it is. So the case needs: a human racer chased long
  enough from behind for heat over 3 (cops on the record + chases
  lost + tickets + time / 2048: a second chase after a ticket gives
  it with two cops), a roadblock set ahead, the racer stopping 3-4
  slices (about 20-25 m) short of it with two or more cops from
  behind starting their stops within 16 m, then one roadblock cop
  joining by the 4 s rule and, with +0x50 at 3, holding from 20-40 m
  ahead. The two roadblock cops stand at their lanes across the same
  slice, so one may be under 16 m and the other over 20 by the
  `0x4965b0` measure, which weighs the lateral distance by a
  quarter. That is minutes of driving by keys on a road the
  roadblock's place decides; it wasn't run.

**Run** (November 2026, nfs-host, a racer's Hot Pursuit on Hometown:
the single race's clicks with `-poke at=0x474ca0:0x6fd3b8:3`, a normal
car, 2 laps, 6 computer cops, one computer racer; the player's car
driven by the computer through its task slots, as in
[bnk.md](bnk.md)'s object-sound runs; `-watch` at the state writes,
orders, radio calls, strip and stop addresses; the cars, the chase
records `0x56e410` and the strips `0x7dd850` dumped each second).
Race clock = run time − 48.9 s.
- **The first cop met** (`r2-cop1`): cop 3 on patrol came towards the
  two racers; car 1, 4 slices ahead of the player, came within 16
  slices first and was picked (state 1 → 5 at 71.10 s), so the player
  went by unchased (its +0x6F0 was never looked at, 0). Hold 160 at
  4.87 m (75.48 s), record +0x1C = 2 at 78.09 s (168 steps: the next
  call after 160), car 1 5 → 6, the stop (78.35 s) and the ticket 384
  steps on (84.47 s); car 1 then free for 960 steps.
- **The chase of the player**: cop 5 (89.17 s, lost at 97.79 s past the
  table's slices) and cop 2 (91.39 s), which drove at 46-59 m/s (the
  player at 50) for the next 65 s. Every 320 steps the calls for help:
  kind 3 orders to cops 6 (104.25 s), 7 (109.38 s) and 4 (119.59 s),
  each joining the chase (state 7 → 5).
- **A roadblock** (`r2-cop1`): at 124.63 s (step 5364), with three cops
  on the record and h = 0 + 3 + 0 + 1 = 4, cop 2 radioed (`0x40b860`)
  and sent cops 3 and 6, then 398 and 111 slices ahead, to slice 822
  (kind 4; states 8, 9 at 145.2 and 154.8 s, 10 and 11 at 158.9 and
  162.9 s, each turned by `0x403180`). The player had stopped short of
  it, at slice 782. In `r2-cop5` (the record's +0x14 poked to 4 at
  92 s, so h = 5 at once) cops 4 and 7 were sent to slice 539 at 104.25
  s; cop 4 turned across the road at 112.18 s (11); cop 7 came at
  122.5 s with the player within 240 m, so it waited in 10 and went
  back to patrol when the player was 5 slices past (122.86 s). The
  player hit cop 4 at 50 m/s and drove on at 17 m/s; cop 4 took it as
  its target from state 11 (120.83 s). Frames (`r2-cop6`, the same
  run with `-frames`): the police car across the road at 120.5 s, the
  player striking it at 121.3 s (111 then 38 mph).
- **A strip** (`r2-cop5`): cop 4 radioed at 124.69 s (`0x40b910`); 320
  steps later (129.71 s) cop 6, 50+ slices ahead, got the kind 0 order
  from slice 777 and bit 8; it reached the roadside (3, 134.7 s),
  stopped (4) and laid its strip at 139.51 s: `0x4c9dd0(4, 774, 7,
  6316)`, a 12 m strip from (337.7, −30.9, 518.8) to (335.8, −30.1,
  507.0) beside the cop. Nobody hit it: the player was stopped at slice
  699 at 148.4 s, and car 1 went by at 170 s 6 m beyond the strip's
  end. The strip stayed out to the end (200 s): its racer, the player,
  never got 50 slices past.
- **A racer on a computer cop's strip** (`c3-spk6`, November 2026):
  `r2-cop5` again with the strip moved to lane 6, the odd cops' lane,
  by poking the lane in `0x403d6a`'s `mov ebx, 7` (byte `0x403d6b` = 6,
  at 30 s). Cop 4 laid it at 139.51 s (`0x4c9dd0(4, 774, 6, ...)`). Car
  1 came by at about 42 m/s and ran over it at 170.37 s, in slice 771-
  774: in that step `0x4c9f50` cleared it, `0x401650` set car 1's
  record (`0x56e470`) +0x1C = 2, and the strip's cop took car 1 as its
  target (`0x4019a1`). Car 1 went to its state 5 at 170.41 s and to 6
  at 173.43 s; cop 3, already on the record, began its stop at 174.19
  s and cop 4 at 178.51 s (`0x403750`, `0x401e40`), and cop 4 gave the
  ticket at 180.25 s, 384 steps after the first stop began (car 1 had
  1 ticket). Lanes 4, 5 and 10 (`c3-spk4`, `c3-spk5`, `c3-spk10`) put
  the strip where nobody drove; car 1 crossed slice 771 at x 319.7,
  z 523.9 (lane 7's strip ran z 507.0-518.8, lane 4's 527.9-538.9).
- **The player's ticket** (`r2-cop4`): as `r2-cop1` up to 93 s; then
  the player's tasks were put back to the human's (the Sc32 and Sc64
  entries, list `0x185e6d0` + 8 + 16 × n: function at +4; poking car
  +0x600 alone does nothing, the lists hold the functions), +0x6E0 =
  2, and keypad 2 (brake) held. Cop 2 began its hold at 19.68 m
  (96.01 s; the player at 9.6 m/s), the player off the gas took 19 off
  it at each call, and at 97.03 s (64 steps) record +0x1C = 2; the
  player 2 → 6 (97.04 s) → 7 (97.08 s), and cop 2 began the stop
  (5 → 6, 97.13 s, `0x401e40`). Cop 5 came at 102.42 s and joined it
  (6 at 102.53 s). The ticket (`0x401ff0`) came at 103.19 s, step 3990,
  386 steps after the stop began (+0x28 = step + 384 for 0 tickets),
  given by cop 5 (whichever cop's `0x401fb0` first finds the step
  past); the player 7 → 2 and put back on the road, tickets 1, free for
  960 steps. Frames: 96.7 s, the player braking at 6 mph, "tickets:
  0/3"; 99.6 s, the scene (no HUD, "TICKETED", the officer at the
  car); 103.8 s, the HUD back with "tickets: 1/3".
- **The speech at the player's ticket** (`c3-spch`, `r2-cop4` rerun
  with `0x401580`, `0x412880`, `0x4c8b30` and `0x4c8c6a` watched, and
  `-wav`). The same steps: cop 2's stop began at 97.127 s, and in that
  step `0x401f78` found the target human (+0x200 bit 4) and called
  `0x401580`; `0x412880` passed 3 tickets left (`0x6fd4bc` 3, laps + 1,
  less 0 taken) and `0x4c8b30` was called for player 0 with d² 1
  (volume 126). It picked **slot 0x12** (0x11 + random of 2) and
  started it (`0x4c8c6a`, bank entry 8, pan 0 outside split screen).
  `copspch` slot 18 matches the recording best from 97.24 s
  (normalised correlation 0.074, the other slots 0.03-0.04). Before
  it `0x4c8b30` mutes the radio's two voices (`SNDvol` 0 on
  `0x7dd370`, `0x7dd374`) and sets `0x560118`, which keeps new radio
  lines from starting (`0x4c85a0`). Frames: 3970 (97.6 s), the scene,
  "TICKETED", the officer beyond the car; 4070 (103.3 s), the race
  view, "tickets: 1/3". So a stop by a computer cop of a human racer
  plays `copspch` by the tickets left, as Summit_Night's two scenes.
- **A human racer by keys** (`c6-h2`, November 2026: the racer's
  pursuit with nothing poked but the race type, the player's car
  driven only by keys). Standing on the grid (`c6-h0`), the player
  was never picked: cop 3, on patrol the other way, passed it at
  slice 0 at 142 s, but a racer scores only when chased, 6.71 m/s over
  or with +0x6F0 over 2 (`0x403f80`). So with cop 3 coming: A at
  133.0 s (the car starts in neutral), keypad 8 at 133.5-140.0 s, then
  keypad 2 held. At 138.475 s the player, at slice 14 and 24.5 m/s
  (the start's limit 22.35 m/s), had +0x6F0 2.10, and cop 3, 8 slices
  back, took it (`0x4019a1`); cop 6 joined at 146.13 s. The player
  hit the wall at slice 25 (141 s), and keypad 2 held from rest
  reversed it, up to 15 m/s, until it stopped against cop 6 at slice
  32 (151.2 s). Cop 6's hold began at 150.79 s (192 steps: the
  reversing car's +0x508 was over 11, so nothing came off) and ended
  at 153.89 s (`0x4022bd`); the player went 2 → 6 → 7 by 154.25 s,
  cop 6's stop began at 154.00 s (`0x401e40`, cop 3's at 160.12 s),
  and cop 6 gave the ticket at 160.13 s (`0x401ff0`), 392 steps (6.13
  s, as in Summit_Night) after its stop began; the player was put back
  at slice 37 in its state 2. `c6-h1` (each key 1 s later) did the
  same: cops 3 and 6, the hold's end at 154.97 s, the player 6 and 7
  at 156-158 s, cop 6's stop at 160.51 s and its ticket at 166.67 s.
- In `r2-cop1` and `r2-cop5`, with the computer still driving the
  player's car, cop 2 also stopped it (hold 192 at 4.70 m, 155.90 s;
  record +0x1C = 2 at 156.90 s) and the car pulled over in the
  computer racer's state 6. A human target must be in state 7, so cop
  2 stayed in its stop test (+0x710 0x50) to the end, 43 s: an artefact
  of driving the player's car that way.

### Seen in footage (Summit_Night)

A YouTube recording of a one-player Hot Pursuit (`Summit_Night`, 2
laps; menu at 0:00: Aston Martin DB7, Summit (2 Laps), assists on,
player OGC). **The player is a racer, not a cop.** There are two
racers, OGC and Lead Foot (also a DB7): the one computer racer the
code gives. The HUD says "tickets: 0/3", laps + 1, as `0x474640` sets
`0x6fd4bc`. Times below are the race clock (the HUD's lap time).

- **First ticket** (lap 1, 2:37-2:50). The player hits a row of SUVs
  and a pickup across the road at 2:38 (a roadblock, probably) and
  stops. At 2:42.4 the view jumps; a cop SUV drives in from the left
  at 2:43.1. The HUD goes and a scene plays: a "TICKETED" banner, an
  officer on foot at the driver's door, the cop car behind, lights on.
  It lasts 6.13 s (clip 3:06.33-3:12.47); the race clock runs on
  (2:43.59 to 2:49.79). The HUD comes back with 1/3, speed 0.
- **Second ticket** (lap 1, 4:27-4:51). A dark cop sedan with blue
  lights sits one or two car lengths behind from 4:27.8. The player's
  speed is low (the needle near 50 on the 0-250 dial, mph on the
  results screen) from about 4:36 and 0 at 4:43.6. The same scene,
  6.13 s again (clip 5:07.10-5:13.23, clock 4:44 to 4:50.60), then
  2/3. The player drives off at 4:51.0; no cop follows at once.
- Both stops last 384 steps, as `0x5521b8` gives for 0 and 1 tickets
  (6.0 s; the 0.13 s more is the time to the first step of the
  scene). The 7 m and 20.12 m/s tests can't be measured from the
  clip, but both times the player was slow and the cop close.
- **Lead Foot arrested.** Out of sight (19 s behind): at 4:42.78 the
  racer list shows "Lead Foot arrested" (text 330) instead of a gap,
  and at 4:43.78 the place goes from "1st of 2" to "1st of 1". The
  results screen gives him "N/A", "Busted", tickets 3: out at
  `0x6fd4bc` = 3, as `0x401ff0` does.
- The player finishes with 2/3 (results: 1st, 9:35.03, tickets 2/3;
  laps 5:12.41 and 4:22.62). After the line the clock stops and the
  car slides to a stop (camera round the car).
- The "assists on" of the menu is "No" in the track records: the Hot
  Pursuit turned them off (`0x4746ab`).
- No spike strip and no "ARRESTED" banner are seen; the player is
  never the cop, so the human cop's 3 s stop and the cop's quota
  (+0x2D4) aren't seen either (they are in the next section).

### Seen in footage (the player as the cop)

Three YouTube recordings of a one-player Hot Pursuit with the player
(OGC) as the cop: `Atlantica_Night` (Pursuit Corvette, 8 laps, track
2), `Aquatica_Night` (Pursuit Diablo SV, 2 laps, track 6) and
`Empire_City_Night` (Pursuit El Niño, 2 laps, track 8). Each has six
computer racers and "tickets: 0/6", as `0x474640` sets. Times are the
HUD's race clock unless marked "clip". The game runs 64 steps a
second. The HUD there has the racer list on (other layouts hide it).

- **The cop's HUD.** A racer list: name, "n tickets" and speed in KPH
  (`0x48a660`, rows set by `0x404ff0` through `0x485180`). A row is
  dark grey past 50 slices (no speed), grey within 50, light grey for
  the nearest, **yellow for the target**, and "arrested" (text 330)
  once out. During a chase a green radar panel shows at the top left
  (`0x47d480`, only while the cop has a target) and a box marks the
  target car. A row of eight spikes at the top right is the spike
  strip (below). There is no radar detector: `0x482b00` draws it
  only for a racer. The top centre is the mirror, the top right dial
  the rev counter.
- **The chase starts** (footage and code agree). `Empire_City_Night`:
  the light bar comes on at 0:02.62 (H). The chase starts at 0:03.23:
  Ram Rod's row turns yellow and the radar panel comes up, as his
  speed goes from 77 to 81 KPH. Empire City's limit there is 20.12
  m/s (`0x40ed40`), and 20.12 + 2 m/s is 79.6 KPH. Gotcha (80 KPH) and
  Ace (82 KPH) were over it 0.3 s before and weren't taken: only the
  nearest racer (+0x730) is tested (`0x4053b0`).
- **The stop.** 18 stops, six a clip. At the step the stop scene
  starts the target shows 0-35 KPH (Atlantica 35, 3, 9, 3, 11, 13;
  Aquatica 10, 8, 9, 10, 1, 11; Empire 9, 6, 0, 0, 7, 6), so under
  both 20.12 and 11.18 m/s. A cop game has 20.12 (above; read in
  `cp-stop1`). Most
  targets were rammed or spun first (`Empire_City_Night` 1:26.95-1:28.34:
  Wild One 163 to 7 KPH after a side hit). The 7 m can't be measured.
  A chase can be short: 1.5 s from the yellow row to the scene (Empire,
  clip 99.60-101.13).
- **The scene lasts 192 steps.** The HUD is gone for 3.03-3.07 s at
  all 18 stops: state 5 with a human cop's +0x28 = step + 192.
  The race clock runs on (`Atlantica_Night` 0:48.89 to 0:52.06). The
  camera is fixed beside the racer's car, an officer on foot at its
  door and the cop car behind with its lights on. There is no
  "TICKETED" banner: `0x482b00` returns early for a cop with +0x9A4
  set.
- **After the ticket.** The ticket count goes up by one and the row
  says "arrested" after one ticket, with 8 laps as with 2:
  `0x6fd4bc` = 1. The arrested car stays at the roadside and the cop
  drives on (Empire 1:31.8). Whether the cop was moved behind the
  leader there (`0x405220`, the cop assist) can't be told from the
  clips: the view after the scene is a dark road with no car in
  sight. The siren is off (`0x401e40`); the next
  yellow row comes 2.7 s after the scene at the soonest (Empire, clip
  38.17-40.83), so H was pressed again.
- **A target not stopped.** `Atlantica_Night` 2:48-2:56: Gotcha is
  the target, crashed and at 0 KPH for 5 s, but the cop isn't within
  7 m and nothing happens. At 2:56 the yellow row moves to Ace, who is
  stopped. **The siren went off and on**: at 2:55.57 the cop's light
  bar goes dark, the radar panel goes and Gotcha's row is no longer
  yellow; at 2:55.78 (0.21 s later) the light bar is back and Ace's
  row is yellow, with the panel and box. Frames at 30 a second
  (clip 192.2-192.7). A human cop's chase ends only in `0x405630`
  (siren off, 300 m by `0x4965b0`, or a finished target) or with the
  ticket; nothing else changes +0x624 for it. With the siren on again
  `0x4053b0` takes the nearest racer (+0x730) whose +0x6F0 is over 2
  m/s, and +0x6F0 is the most it has ever been over (kept, not the
  speed now), so a stopped racer can be taken. Ace was the nearest.
- **The quota.** The sixth ticket ends the race. The results' quota
  time is the race clock at the end of the sixth scene: Atlantica
  scene from 5:01.7, quota 5:04.78; Aquatica 3:31.3, 3:34.34; Empire
  2:55.8, 2:58.87. So +0x2D4 = step − 512 at the ticket, as
  `0x401ff0` does. The HUD then comes back for 3.4-3.8 s with the
  lap counter at the last lap (the cop's counter shows the lowest lap
  of the racers still in, `0x482b00`) and the clock from 0:00, then
  the results.
- **The results.** "Pursuit Summary": Player (OGC, the car), Busts
  "6/6 (Met Quota)" (text 404), Quota Time, and "Sort By". Then
  Performance, Track Records and "Race Summary": each racer N/A,
  Busted, tickets 1. With one cop the sort by +0x2D4 isn't seen (with
  two, run: "The results with two cops", above). In
  a run where the scripted cop gave no tickets (`cop-list`, nfs-host)
  the race ended when the racers finished: Busts 0/6, Quota Time N/A.
- **Speech.** No `copspch` line is heard (the racer's ticket lines
  aren't played for a cop). The officer bank is `off02a` in Atlantica
  and Aquatica and `off08a` in Empire City (lines only in those
  banks), never B or C; the dispatcher is `disp02` in Aquatica and
  `disp08` in Empire City. Atlantica's dispatcher lines are all
  shared, but one (slot 20) is only in `disp02` and `disp08`. That
  fits the track-mod-4 rule (bnk.md).
- **No computer cops** are seen on the player's side in any clip:
  the stops, scenes and racer list show only the player's car.
- **Spike strips.** The spike row at the top right dims (it is still
  drawn, greyer) once before each of the 18 stops, 1.4 to 10 s before
  the scene, and at no other time. The 14 dims not cut by a scene or
  the race's end last 2.90-2.97 s, close to the strip's 0xC0 steps
  (3 s, [net.md](net.md)). So the player probably laid a strip in
  every chase. No strip is seen on the dark roads, and no racer is
  seen hitting one. **Our exe can't draw that.** The icon is one quad
  of the HUD's quad list `0x749a38` (vertices at `0x791acc` + player
  × 4, set up at `0x47bc40`: white, the spike picture of `hud98.fsh`'s
  atlas, u 0.709-0.9375, v 0.719-0.762, at `CopSpikeBeltIndicator` of
  the `.pos` layout). Each frame `0x482b00` adds it (`0x49bd30`) while
  the cop has no strip out and takes it out (`0x49bdc0`) while one is
  out, and `0x49be40` draws the list. There is no dim picture and no
  colour change, and nothing about the arming step: the row is gone
  from the press until the strip is cleared. Runs agree (`cop-spike2`;
  `cp-spike1`: gone from the press at 43.1 s until a racer hit the
  strip at 190.1 s, then back). It isn't a renderer's alpha either:
  the quad isn't sent at all. So the recorded game is probably
  another build, whose icon dims for the arming time; this one is
  not known.
- **The racer list's "over".** `0x485180` writes "%d %s - %d %s %s"
  ("81 KPH - 8 KPH over", text 350) for a racer with +0x6F0 over 2
  m/s, at each step of a cop's states (`0x404ff0`), and
  `0x48a660` draws that text as the third column of every row in
  state 4 or more (within 50 slices). The clips never show it, not
  even for the target, which must be over that to be chased. Our exe
  shows it (`cp-stop8`, frame 2418 at 54.4 s: "Ace 0 tickets 0 MPH -
  11 MPH over" in yellow, the other rows "69 MPH - 19 MPH over";
  `cp-spike1`: "Gotcha 0 tickets 84 MPH - 45 MPH over"). In those runs
  the list itself was off in the saved options (`0x6fbc58` and
  `0x6fbdfc` 0; `0x485880` sets them to 1 unless the renderer is
  softtria) and was poked on. The clips' rows end at the speed, at
  every length ("196 KPH"), so the text isn't cut by the box: another
  build again, as for the spike icon.

## The handling, routine by routine

An index of the car-handling code (November 2026), in the order a step
runs it, with where each is written up. All are Sc32 tasks (32 a
second, The clock above) unless marked Sc64. The ones not written up
before were read for this index (`gdecomp`, `asm`, the constants read
from the exe). The key claims were then checked in runs (Checked in
runs, below).

**Every car: the dynamics task `0x4a2830`** (Car dynamics, above).

| Routine | Does |
|---|---|
| `0x4a2260` | downforce on the road over 10 m/s (computer cars doubled); drag by axis in the air |
| `0x4a00d0`, `0x4a0440` | the road's height under the car (+0x158), the lowest corner's clearance (+0x15C) |
| `0x4a21a0` | a roof over the road (slices where `0x420bf0` says covered): when the car's centre is above the road height plus the roof's height (`0x552e54`[slice]), a contact with the roof (`0x42efe0`, the road's down as the normal) and the air spin clamp `0x4a20c0` |
| `0x4a1610` | on the road: out of the road, the speed into it taken out, the body turned towards the road plane at most 0.5 turn/s |
| `0x4a1ce0`, `0x4a1210` | landing: twelve body points against the road, the speed along the normal and the spin × (0.5 + e) |
| `0x4296c0` | at the landing's end (`0x4a2068`), only for a car that comes down flat on its roof (+0x10C ≤ −0.9, `0x537200`) and on the ground: a body wobble for the drawing, rate +0x8C4 = 1/240 and +0x8C8 = 24-35 ticks with a random sign, twice (+0x8CC, +0x8D0), read by the body drawing `0x42a7d0`, which adds rate × n/12 × sin(n/12 turns) to the tilts +0x920 (pitch) / +0x924 (roll) each tick, held to ±0.00625 × P +0x294, and counts n down (run in `e10-h7`, `e12-tp*`, `e12-cl*`, Checked in runs) |
| `0x4a20c0` | in the air: each spin kept within ±2.4 turns/s |
| `0x4a2690` | +0x10C, the car's up · the road's up |
| `0x4a2630` | puts a car on the road's height (+0x9C = road + half height), clears +0x164, +0x8B and the vertical speed |

**The human's car: `0x4ae4c1`** (+0x608), which calls `0x4ad7a3` on
the road and upright, else the tipped path:

| Routine | Does |
|---|---|
| `0x4ad250` | clears a wheel record (12 dwords), each of the four at the start of the tick |
| `0x4ad672`, `0x4ad6eb` | the slip ratio +0x5B0 and the road speed +0x5A4 (+0x5B0 and +0x5A4, below) |
| `0x4af254` | the yaw term added to the axles' slip (The human's car, step 3) |
| `0x4a9bc4` | the controls: pedal and steering ramps, gears (Controls; Gears) |
| `0x4a83d3` | the assists' pedal driver (The assists' driver) |
| `0x4a90a0`, `0x4aa1b4`, `0x4a9769`, `0x4a97cb` | the automatic box, a manual change, the drop to third, reverse and neutral (Gears) |
| `0x4ad2e1` | the front wheels' angle (The front wheels' angle) |
| `0x4aeffc` | both pedals held, slow: velocity and spin × 0.8 |
| `0x4aaed1` | the engine, drag, engine braking, the over-speed (The engine and gears) |
| `0x4aab41` | the wheel loads, downforce, surface and weather grip (step 6) |
| `0x4ae5c2` | the weight transfer, front to rear |
| `0x4aca4f`, `0x4ac3af`, `0x4ac094`, `0x4abd9c` | a tyre, its friction circle, the hand-brake wheel, the Tire Factor's grip loss (Tyres) |
| `0x4ae68e` | the forward acceleration (+0x4E8) ÷ the lateral grip multiplier (P +0x248), then, in a dry single race only (not weather 1 or 2, not a network game, not Tournament or Knockout), × the Tire Factor t: with t over 1 in reverse, neutral, first or second (gear byte ≤ 3) × (1 + 0.1 t) (`0x53deb4`); with t under 1 in any gear × (1 + 0.1 (t − 1)) (`0x53deb8`, `0x53debc`); +0x4E4 cleared |
| `0x4ae79b` | gas ≥ 0.5, brake ≥ 0.75, \|steering\| > 64, under 5 m/s: velocity zeroed |
| `0x4aea88` | **the yaw limit** (after the tyres' yaw is added, `0x4ae442`): with gas ≥ 0.5, brake ≥ 0.75, \|+0x51C\| > 64 and under 5 m/s the yaw (+0x4FC) is zeroed; with \|+0x51C\| under 4 in reverse it is × 0.95 (`0x53ded8`). Then L = turning circle radius (P +0x258) × \|yaw\| × 2π × 0.5 (`0x53dedc`, `0x53dee4`), the speed of a car on a circle of half that radius at that yaw: gripping (\|lateral\| < \|forward\|), when both speeds are under L the yaw × min(\|forward\| / L, 0.98), else × 0.95 (\|+0x5B0\| under 0.2) or 0.99. Sliding sideways, only under 2 m/s (`0x53defc`, the road-frame speed): the same limit or × 0.9 / 0.95, and the velocity × 0.7 under 5 m/s, else × 0.8. So the car can't turn tighter than its `carp.txt` turning circle allows; in runs that cap is reached only near a standstill, and at driving speeds the yaw is simply × 0.95 a tick (Checked in runs, below) |
| `0x4ae85b`, `0x4ae98b` | the lateral damping, the hand brake's and over-rev's speed loss (step 8) |
| `0x4af140` | the loss in neutral (The creep on the grid) |
| `0x4aa8e7` | **tipped or in the air** (+0x8A, or +0x10C < 0.9, `0x4ae4ee`-`0x4ae504`; also the computer's `0x408626`, `0x4089b0`): `0x4ae4c1` itself clears the over-speed flag, the slip ratio and both skid levels (+0x594, +0x5B0, +0x5C0, +0x5C4) and drops the rpm 500 a tick to the minimum (`0x4ae50a`-`0x4ae57f`), then calls `0x4aa8e7`. (`0x4aa4f4`, listed here before, isn't on this path: called from the dynamics task `0x4a2b52` and the walls `0x430c5b`, it takes the road-frame lateral and forward speeds into +0x4EC and +0x4F4, sets the rpm from the speed in gear (max(min rpm, |speed| × +0x52C[gear] × redline), not in neutral) and clears the same four fields.) `0x4aa8e7` is **the body's friction on the road**: only with the clearance under 0.2 (`0x3e4cccce` as an int) and the road's normal y at least 0.5 (`0x53dcc4`), the velocity's part along the road plane × 0.94 a tick and the spin about the road's normal × 0.98 (on the ground, +0x164 0); in air mode × 0.98 and × 0.998, and not at all when no axis of the car is near the road's normal (each pair of \|up · n\|, \|right · n\|, \|forward · n\| sums to 0.9 or less, `0x53dccc`). The roll and pitch spins are left alone: a car on its side or roof slides to a stop on the road |
| `0x4a2f30` (Sc64) | the human's orientation by the spin / 64 |

**The computer's cars: `0x4088f0`** (+0x608; The computer's cars):
the car's frame (+0x4EC-+0x500), `0x408600` (the lane aim `0x408870`,
the sound's gear and rpm `0x4061b0`, reversing `0x4060d0`, the point
`0x406e50`, the heading error `0x4070c0`, one of the controllers
`0x4076b0`, `0x407c30`, `0x407fa0`, the acceleration `0x4064f0` and
braking `0x4063e0`), `0x4068a0` for a car with +0x88 (no forces),
`0x407380` for a car out of the race, then `ai_integrate` `0x4071a0`
(× 1/32) and the orientation `0x4a2f10` (spin / 32).

**Both: the collisions** `0x4311d0` (wall `0x4304b0`, cars
`0x4310c0` → `0x430c80`, `0x42fee0`, `0x42f280`, `0x42e820`; Rolls
from car hits) and **the impacts** `0x42b900` (`0x42ac40`, the reset
`0x429260` → `0x428f90`).

### Checked in runs

November 2026, the `e8-h` runs: `nfs-host -native -deterministic
50000` on Hometown (track poke 0 at `0x474ca0`), a single race, dry,
the player's car (record `0x5e1120`) driven by keys (`+up` for the
gas, `+left`) or by `-drive 0x5e1120:0x5e1acc:41000` (steered at car 1,
full gas). Each routine was watched at its first instruction and at
its `ret` path (`-watch` with `-watchread` of the car's fields as
absolute addresses), so each pair of lines is one call, in and out.
The human's tick is first called at 40.98 s in these runs, and the
car first moves at 49.1 s (the start, step 512; the gas held from 41 s
only revs the engine until then). The player's car record gave P at `0x5eabe0` and
the set-up record at `0x6fd52c` (`-dumpmem` of the record at 55 s,
`e8-h0`); P +0x258 (the turning circle) read 6.1 and P +0x248 3.3.

- **The yaw limit (`0x4aea88`)**, `e8-h1` (gas from 41 s, full left
  50-52.5 s, then into the scenery). In and out at `0x4aea88` and
  `0x4aeff3` with +0x4FC, +0x4F4, +0x4EC, +0x5B0, +0x51C and P +0x258:
  all 393 calls on the gripping branch came out as the code predicts,
  to float rounding. But **the cap itself almost never binds.** At
  full lock the yaw rose to 0.297 turns/s at 9-12 m/s (a circle of
  5-6.4 m radius, v / 2πω), well under the cap |forward| / (π × 6.1)
  = 0.48-0.62 turns/s at those speeds; the cap is the circle of radius
  P +0x258 / 2, 3.05 m, so +0x258 acts as a diameter. It bound twice in
  the run, both when the car had stopped against a wall (0.58 m/s:
  the yaw 0.0638 in, 0.0300 out, exactly 0.58 / (π × 6.1); 0.30 m/s:
  0.0308 to 0.0155). Every other call took the "else" branch: the yaw
  × 0.95 each tick (the slip ratio under 0.2 throughout), so at
  driving speeds this routine is a **5% yaw damping a tick**, not a
  limit. The sliding branch (|lateral| ≥ |forward|) came up once in
  motion (2.45 forward, 3.22 lateral: the road-frame speed over 2 m/s,
  so untouched, as the code says).
- **The Tire Factor on the forward acceleration (`0x4ae68e`)**, `e8-h1`
  and `e8-h3`, the set-up's Tire Factor (`0x6fd584`, set-up +0x58)
  poked to 1.5 at 41 s and (in `e8-h1`) to 0.5 at 56 s; watched after
  the ÷ P +0x248 (`0x4ae6c3`) and at the end (`0x4ae785`) with +0x4E8
  and the gear byte. The ratio out / in was exactly **1.15** with t
  1.5 in first and second (gear bytes 2 and 3; 132 and 141 calls in
  `e8-h3`), exactly **1.00** in third (148 calls), and **0.95** with
  t 0.5 in first and second (102, 89 calls): 1 + 0.1t when t > 1 in
  the low gears only, and 1 + 0.1(t − 1) when t < 1 in any gear, as
  read. So a Tire Factor over 1 adds 10% of itself to the push in the
  first two gears and nothing above them, while one under 1 takes
  off from every gear.
- **Gear changes**, `e8-h3` (`-watch 0x4a9414`, the new gear in al,
  with +0x4F4): the automatic box changed first to second at 19.33
  m/s (53.02 s) and second to third at 36.18 m/s (57.42 s). With the
  automatic ratios measured before (271.8 and 144.8 rpm per m/s, Gears
  above) those are 5,254 and 5,239 rpm against R − R/8 = 5,250; the
  test reads the road speed +0x5A4, not +0x4F4, which accounts for the
  few rpm either side.
- **The body's friction on the road (`0x4aa8e7`)**, `e8-h2` and
  `e8-h4`: `e8-h0`'s run again (gas from 52 s), with the car turned on
  its roof at 55 s by negating its right and up rows (+0xC0, +0xCC,
  poked as dumped at that moment in `e8-h0`; the runs are deterministic
  and the bytes replaced matched). +0x10C read −1.000, the clearance
  0.000-0.004 and +0x164 0, so every tick took the tipped path
  (`0x4ae588`) and the on-the-ground case: the velocity across the
  road × **0.9400** each call (19.27 m/s at 55.06 s, 9.16 at 55.40,
  1.05 at 56.51, 0.06 at 58.01) and nothing else moved it between calls
  (18.109 out, 18.106 in at the next): the car slides to a stop in
  about 1.5 s and stays on its roof. In `e8-h4` the yaw spin (+0xE8)
  was also poked to 1 turn/s: × **0.9800** in each call, as read, but
  between calls it lost a further 1.5-1.6% (0.9330 out, 0.9184 in at
  the next). That loss is the reset timer's (`e10-h6`, below). At 56 s `e8-h4` set +0x164
  (air ticks) to 1 on the roof: for the next half second the calls
  took the air case, velocity × **0.9800** and spin × **0.9980**,
  then +0x164 cleared and 0.94 / 0.98 came back.
- **The rest of the spin's loss on the roof: the reset timer
  `0x429260`**, `e10-h6` (November 2026; `e8-h4`'s run to 57 s, the
  spin poked at 55 s only, `-watchmem 0x5e1208 4`, every store to
  +0xE8 with its instruction, and `0x4aa8e7` in and out). Each tick
  the spin was stored by `0x4aa8e7` (the × 0.98, at `0x4e002a` and
  `0x4dffda` inside `0x4e0110`) and then once more at `0x429333`, in
  `0x429260` (from the impacts task `0x42b900`, 32 a second), and
  nowhere else. All 64 of those stores (55.06-57.02 s) were the value
  before × 0.984375 to float rounding (0.98437495-0.98437505). In the
  code (`0x4292cc`-`0x429341`): while the timer +0x8A0 is running
  (over 0), the clearance +0x15C is under 5, +0x184 is 0, the car is
  on the ground (+0x164 0) and tipped past 60° (+0x10C under 0.5,
  `0x3f000000` compared as an int), all three spins (+0xE4, +0xE8,
  +0xEC) lose 1/64 (`0x5371d8`, 0.015625) a tick. So a car on its side
  or roof loses 2% and then 1/64 of its spin a tick, × 0.9647 in all,
  and its spin halves in about 19 ticks (0.6 s). With +0x164 set (the
  air case, `e8-h4` from 56 s) the test fails, which is why only the
  × 0.998 was seen then.
- **The roof wobble (`0x4296c0`)**, `e10-h7` (November 2026; `e8-h4`'s
  run to 58 s, on its roof at 55 s, +0x164 poked to 1 at 56 s, no spin
  poke; `0x4296c0` watched in and at its `ret` `0x429783`, and the body
  drawing `0x42a7d0` at `0x42aa90`, after its clamp, with +0x920 and
  +0x924). From the code: only with +0x10C at most −0.9 (`0x537200`, a
  double) and +0x164 0 it sets +0x8C4 = 1/240 (`0x3b888889`) and
  +0x8C8 = rand % 12 + 24, then flips +0x8C4's sign when the next
  rand % 256 is over 128; the same again for +0x8CC/+0x8D0
  (`0x4eb1fc` is the rand). `0x42a7d0` (each tick, through `0x42b781`;
  not while +0x164 is set, `0x42a9a0`) adds `0x429790`(rate, n) =
  rate × n/12 × sin(n/12 turns) to +0x924 (from +0x8CC, +0x8D0) and
  +0x920 (from +0x8C4, +0x8C8), clamps both to ± a limit, and then takes
  n down by 1, or 2 while n > 1 when the car's pitch-roll factor
  (P +0x294) is under 0.5 (`0x42abb0`-`0x42ac26`). The hit's jolt
  `0x4295a0` writes the same four fields (Collisions, col.md). In the
  run the landing ran and ended in the tick after the poke (56.007 s,
  `[esp]` `0x4a2076`): +0x10C −1.000, +0x8C4 +1/240 with +0x8C8 31,
  +0x8CC −1/240 with +0x8D0 35. From the next tick +0x920 and +0x924
  followed the formula to float rounding about their resting values
  (−0.00082 and 0; for example n = 31: (1/240)(31/12)sin(210°) =
  −0.00538, and +0x920 read −0.00620, 0.00538 below its rest), each
  held at ±0.00644 by the clamp for the three ticks at the top of each
  of the first swings, the counters went down by 1 a tick (P +0x294 at
  least 0.5 for this car), and both were back at rest at 56.98 s and
  57.12 s. So the wobble is a decaying swing of the drawn body, a
  period of 12 ticks (0.375 s) and 0.75-1.1 s long, at most 0.012 of a
  turn (4.4°) before the clamp. It touches no physics field: it only
  feeds the body's two drawn tilts.
- **The drawn body's tilts: +0x920 is the pitch, +0x924 the roll**
  (November 2026, `e12-tp0`-`tp2`, `e12-tq0`-`tq2`). From the code:
  `0x42a7d0` does not build a matrix; the car drawing does, in
  `0x4b93c0` (from `car_draw_wheels` `0x4bab70`, and `0x4bae00`,
  `0x4bb200`). It clamps +0x920 to ±0.25 turns (`0x5400c8`,
  `0x5400d0`) and +0x924 to ±0.3 (`0x5400d8`, `0x5400e0`), stores them
  back, and makes Rz = `0x4dfe40`(+0x924), rows (c, s, 0), (−s, c, 0),
  (0, 0, 1), and Rx = `0x4dfd80`(+0x920), rows (1, 0, 0), (0, c, s),
  (0, −s, c) (c and s of the angle in turns, `0x4ea970`), and their
  product Rz · Rx (`0x4e0280`, row-major A · B) at `0x4b9516`. When the
  car is tipped (+0x10C under 0.9, `0x5400e8`) that product is
  multiplied into the rows the caller passes for the drawing (its
  third argument, T · rows, `0x4b953c`) and the tilt matrix is left as
  the identity. The car's rows are its
  right, up and forward (+0xC0, +0xCC, +0xD8; points are rows times
  the matrix), and the model's axes are x right, y up, z forward
  ([fce.md](fce.md)), so +0x920 turns the body about its right axis
  (pitch: the up row goes to (0, c, s), so a positive +0x920 leans the
  roof forward and dips the nose) and +0x924 about its forward axis
  (roll: the right row goes to (c, s, 0), a positive +0x924 lifts the
  right side). The sources agree: what 0x42a7d0 copies into +0x920
  before the wobble is +0x928, which each tick is × 0.75 and, when
  |a| is over 1.5 (`0x5372a0`), less a × (1/240 × P +0x294) / 64,
  with a = the forward acceleration +0x4E8 − (−9.81) × the forward
  row's y (+0xDC), held to ±1/240 × P +0x294 (`0x42a83a`-`0x42a900`):
  speeding up tilts the nose up. +0x924 gets +0x92C, the same from the
  lateral acceleration +0x4E0 and the right row's y (+0xC4), but
  added rather than taken off, its threshold 4.5 (`0x5372b0`), its
  gain and hold 0.00625 × P +0x294
  (`0x42a8ed`-`0x42a997`). Then the random shake from the road's
  bumpiness (+0x930, set in `0x4294e0` from P +0x298) adds (r − 0.5) ×
  +0x930 × 0.1 to +0x920 and × 0.07 to +0x924 (× the set-up's
  Suspension Stiffness, set-up +0x50, in a single race or Hot Pursuit
  off the network; `0x42aab4`-`0x42ab62`). The camera reads the same
  two in the **in-car and bumper views only** (Camera views, below):
  `0x421730`, called from `0x422520` (`0x422537`) when the camera's
  +0x104 is not 0, turns +0x924 into a turn about z (`0x4ea820`) and
  +0x920 plus the camera's own +0x50 into a turn about x (`0x4ea680`),
  multiplies them (`0x4e0280`, z then x) and multiplies the product
  into the camera's rows +0x2C in place (`0x4217cc`-`0x4217d4`). With
  +0x104 1 the pitch is doubled (2.0 at `0x536e0c`) and the roll kept;
  otherwise (+0x104 2) the pitch is kept and the roll halved (0.5 at
  `0x536e10`); either way the roll is halved again when the car's
  +0x5A4 (road speed) is over 60 (`0x42177b`). So the view leans with
  the body as it is drawn, the in-car view nodding twice as far.
  Run: `e12-tp*` (`e12-runs/tb.sh`: `e10-h7`'s start, Hometown, gas
  from 52 s, on the road, to 55 s; `0x4b951b`, just after the product,
  watched with the nine floats at edi, +0x920, +0x924 and +0x10C).
  With nothing poked the player's car's (`0x5e1120`) matrix was the
  identity (+0x920, +0x924 0; the watch's lines ran out at 45.5 s, the
  car still on the grid, and the poked runs' matrices below are from
  the same moments). `e12-tp1` set the
  player's +0x920 to 0.05 turns at every `0x4b9405` (`-pokeevery`, just
  before the drawing reads it): the matrix read (1, 0, 0), (0, 0.951,
  0.309), (0, −0.309, 0.951), a turn of 18° about x; `e12-tp2` set
  +0x924 instead: (0.951, 0.309, 0), (−0.309, 0.951, 0), (0, 0, 1), about
  z. `e12-tq0`-`tq2`, the same with the chase view (key `c` at 53 s)
  and frames from 54 s, show it: with +0x920 at 0.05 the body is drawn
  tail up and nose down, its wheels left on the road (`tq1`); with
  +0x924 at 0.05 it is rolled with its right side up (`tq2`); `tq0`
  level.
- **The tilts' clamp is 0.00625 × the car's pitch roll factor**
  (P +0x294). In `0x42a7d0` `[ebp−0x18]` is `[0x5efde4]` × P +0x294
  (`0x42a80c`-`0x42a81b`, stored at `0x42a851`) and `[ebp−0x1c]` is
  `[0x5efde0]` × P +0x294 (`0x42a800`-`0x42a806`, stored at
  `0x42a84c`). `0x5efde4` and `0x5efde0` are written only at the race
  constants' set-up `0x42c590` (from `0x428402`), as 0.00625
  (`0x3bcccccd`, `0x42c62b`/`0x42c648`) and 1/240 (`0x3b888889`,
  `0x42c626`/`0x42c642`); their other readers are the hit's jolt
  `0x4295a0` and the camera's `0x471650`/`0x4716e0`. So both final
  clamps of +0x920 and +0x924 are ±0.00625 × P +0x294, and +0x928's own
  hold is ±P +0x294 / 240. Run: `e12-cl3` and `e12-cl12` (`cl.sh`,
  Hometown, the player's car set to list entries 3 and 12 at
  `0x474ca0`, to 43 s; `0x42aa90` watched with `[ebp−0x18]`,
  `[ebp−0x1c]`, the car's P pointer and P +0x294 of the player's
  P `0x5eabe0`). Entry 12 (P +0x294 1.03, as for the car of `e10-h7`
  and `e12-tb0`, where nothing was poked) gave 0.0064375 and 0.0042917
  for the player's car, entry 3 (P +0x294 0.92) 0.00575 and
  0.0038333, exactly 0.00625 and 1/240 times the field; the computer
  cars in the same runs had 0.00575 (P `0x5eb08c`), 0.00625 (P
  `0x5eb538`) and 0.0064375 (P `0x5eb9e4`), each pair in the same
  ratio 1.5 : 1, so factors 0.92, 1.00 and 1.03 (not read directly). So
  `e10-h7`'s 0.00644 was 0.00625 × 1.03.
- **Landing** was checked before (`sw1-f`, Car dynamics above): the
  speed into the road × (0.5 + e) and the spin × (0.5 + e), with no
  bounce; not run again.
- **The computer cars' simpler model**, `e8-h5` (no keys, to 52 s;
  `ai_integrate` watched at `0x407271`, the forces summed into +0x2E8,
  the velocity before; `0x4072d9`, the velocity after; `0x407328`,
  after the damping loop; the reads relative to ecx, the car). All 7
  computer cars, 700 calls each point from the start (48.92-52 s): the
  tick `0x56ec58` read 0.03125 every call, and every new velocity was
  the old + the world force × 1/32 to float rounding (the first: car 1,
  force (2.925, −0.017, −3.106), velocity (0.001, 0, −0.001) to (0.092,
  −0.001, −0.098)). Their slip ratio +0x5B0 and road speed +0x5A4 read
  0 throughout, as the code says (only the human's tick works them
  out). The damping loop changed nothing: `0x56ec68` was 0 (the loop
  skipped) or, on 3-5 calls a car, 1.0 with `0x56ec78` = 1 (one pass
  × 1).

### Camera views (the drawn tilts)

Found November 2026 from the code and run in `g3-cv0`, `g3-cv1`, `h3-cm5`, `h3-lb1`, `h3-tr1`.

- **The camera objects.** Each view has one object per screen (two for
  split screen) in the table `0x552ec8` (mode × 8 + screen × 4; modes
  1-24, `0x5dda10`-`0x5e0ef4`); `0x5e10b0[screen]` points at the one in
  use and its +0 is the mode. The camera's set-up `0x422cf0` clears
  each object and fills it by mode through the jump table `0x422c88`
  (+0x110-+0x118 the eye's offset along the car's right, up and
  forward, +0x50 a pitch offset, +0x104 the tilt flag). Only three
  modes set +0x104: mode 1 to 1 (`0x422ee0`, offset 0, 0.6, −1), mode
  2 to 2 (`0x422f36`, offset 0, 0.6, +1, +0x50 0.03) and mode 10 to 1
  (`0x4235f0`); all others 0, so they never call `0x421730`.
- **Each camera tick** (`0x426450`, table `0x4263f0`) mode 1 runs
  `0x4240c0` and mode 2 `0x424260`: both put the eye at the car's
  position + its rows × the offset (the up offset × car +0x10C; the
  bumper's forward offset is half the car's half length +0x11C,
  `0x4242c2`) and copy the car's rows +0xC0 into the camera's +0x2C
  (or `0x421570`'s turned copy while `0x79c4a0[screen]` is set, the
  forward offset then negated: the look-back, seen in `h3-lb1` below), so `0x421730` tilts a
  copy of the car's own rows.
  In mode 1 `0x4240c0` sets +0x50 0 and the eye ±0.4 to the side by
  the car's +0x9A8 (0 left, 1 right, 2 the middle) when `0x7922fc` is
  set (written by `0x488a80`, which unpacks the cockpit art), else
  +0x50 0.02, the side 0 and the forward offset half of +0x11C
  (`0x424124`), as the bumper's.
- **The view key.** `0x423b80` (from the input routine `0x4c4e40`,
  `0x4c50fd`/`0x4c5128`, not in a replay) moves to `0x5e103c[mode]`,
  a cycle built by `0x423a60` from the four view slots `0x6fbbc0`-
  `0x6fbbcc` (mode numbers, 0 for an empty slot), which `0x472300`
  takes from the options `0x6fbba8`-`0x6fbbb4` through the u16 list
  at `0x55629e` (option 0-6 to mode 0, 1, 4, 3, 2, 5, 6). The defaults
  (`0x4723f0`) are options 1, 2, 3, 0: modes 1, 4, 3 and no fourth.
  Mode 10 is not in the list. How it can be reached is under Modes 5,
  6 and 10, below.
- **Run** `g3-cv0` (`g3-runs/cv.sh`: `e12-tb`'s start, Hometown, gas
  from 52 s, the view key `c` at 54, 57, 60, 63 and 66 s; `-watch`
  `0x423b80` and `0x421730` with the camera's mode, +0x104, +0x50, the
  current object `0x5e10b0`, the player's +0x920, +0x924, +0x5A4;
  dumps of `0x6fbbb0` and `0x5e1038` at 53 s; frames every 40 from
  frame 2150, kept outside the repo). The slots read 1, 4, 3, 0 and the
  cycle 1 → 4 → 3 → 1. The race began in mode 1 and the key went 1 →
  4 → 3 → 1 → 4. `0x421730` ran once a frame (about every 45 ms,
  `[esp]` `0x42253c`, object `0x5e0dd8`, +0x104 1, +0x50 0) from
  45.6 s to 54.0 s and from 60.05 s to 63.0 s, exactly while mode 1
  was on, and never in modes 4 and 3. The frames show mode 1 as the
  in-car view (the dashboard and wheel), 4 the near chase view and 3
  the far one. The tilts it read stayed inside the clamp
  (+0x920 −0.0064 to +0.0052, +0x924 −0.0003 to +0.0056; P +0x294
  1.03 gives ±0.0064), the speed +0x5A4 up to 30.2. `g3-cv1`, the same
  with `0x6fbbcc` poked to 2 at `0x474ca0`: the cycle became 1 → 4 →
  3 → 2 → 1, and in mode 2 (60.05-63.05 s) `0x421730` ran with the
  object `0x5de100`, +0x104 2, +0x50 0.03; its frames show the view
  from the front of the car with no body or dashboard (the bumper
  view).
- **Modes 5, 6 and 10** (November 2026, from `0x422cf0`'s cases and
  the tick routines, run in `h3-cm5`, `h3-lb1`). The set-up (+0x110,
  +0x114, +0x118 the eye's right, up and forward offsets; +0x11C,
  +0x120; +0x50) for the chase views and mode 10:

  | Mode | Offset | +0x11C | +0x120 | +0x50 |
  |---|---|---|---|---|
  | 3 (far chase) | 0, 1.65, −5.5 | 0.25 | 0.2 | 0.04 |
  | 5 | 0, 1.65, −4.75 | 2.0 | 0 | 0.04 |
  | 4 (near chase) | 0, 1.0, −4.0 | 0.25 | 0.2 | 0.03 |
  | 6 | 0, 1.0, −3.25 | 2.0 | 0 | 0.03 |
  | 10 | 2.0, 0.417, −2.4 | 0.136 | (none) | 0.01 |

  The dispatcher `0x426450` (table `0x4263f0`) runs `0x424740` for
  modes 3 and 5 and `0x4243a0` for 4 and 6, so 5 and 6 are the far
  and near chase views with other numbers. In both routines the eye's
  offset from the car moves towards its target each tick by
  +0x11C × 0.27027 (`0x423fb0`, the constant at `0x536e94`) of the gap,
  added through `0x4e0000`/`0x4e0050`/`0x4dffb0`. That is 0.068 for 3
  and 4 (a lagging camera) and 0.54 for 5 and 6 (an almost rigid one).
  +0x120 scales the offset down with the car's +0x4E8 (`0x424030`:
  the forward and right offsets by 1 − (1 − (s + 3)/6) × +0x120, s
  being +0x4E8 capped at 3, or −3 when +0x4E8 is 0 or less, and the
  height by its square). That is 0 for 5
  and 6, so their distance stays fixed. `0x424740` (3 and 5) also falls
  back to (0, 1.0, −3.25), mode 6's offset, when `0x5e1014[screen]` is
  over 32 (`0x426370` counts it up while both the car's slice and the
  camera's +0x84 pass `0x420bf0`). `0x5e1014[screen]` is the camera's time under
  cover in ticks (col.md, side code 2): run `n3-runs/uc2` saw it go
  0 to 51 at about 64 a second through a covered stretch, with the near
  offset taken (`0x4247af`, over 0x20) exactly while it was over 32. The set-up's bytes at
  +0x58 + m mark the modes it may blend into through mode 19
  (`0x422610`). 3 has 4, 5, 6, 7, 11, 12, 13, 17, 21 and 24; 4, 5 and
  6 each have the other three chase modes and 17, 21, 24.
  Run `h3-cm5` (`h3-runs/cm.sh`: `g3-cv`'s start, Hometown, gas from 52
  s, the view slots `0x6fbbc0`-`0x6fbbcc` poked to 5, 6, 10, 0 at
  `0x474ca0`, the view key at 54, 57 and 62 s, the look-back key B
  held 59-60.5 s and 63.5-65 s, 67 s; `-watch` at `0x423b80`,
  `0x421730`, `0x42253c` and `0x4225ca` with the mode, +0x104, the
  current object `0x5e10b0`, `0x79c4a0`, +0x920, +0x924 and the mode-10
  object's rows `0x5df0dc`-`0x5df0fc`; frames every 20, kept outside
  the repo). The race began in **mode 10** (`0x5df0b0`, 45.6-54.0 s),
  then 5 (54.0-57.0 s), 6 (57.0-62.0 s) and 10 again (62.0 s on). The
  frames show 5 as a far chase view and 6 as a near chase view, both
  following the car closely. Mode 10 has the eye low and to the
  car's right, a little behind it, with the car's right flank along
  the left edge and the road ahead.
- **How mode 10 is reached.** Nothing in the exe passes 10 to the camera
  setters (`0x4238b0`, `0x422610`, `0x423960`, `0x423b50`, `0x426450`,
  `0x422520`, `0x4228e0`). A capstone scan of the call sites found no
  10 within eight instructions of them, and no store of 10 in the
  camera code (`0x421000`-`0x427000`) or to `0x5e10xx`, `0x6fbbxx` or
  `0x7cd9xx`. The replay director `0x4bd640` takes its modes from
  `0x55fe44` (15, 8, 9, 13, 3, 12, 7, 11, 1) and `0x55fe68` (1, 1, 8, 8,
  8, 9, 8, 8, 13, 12, 8, 8, 8, 3, 12, 8). The options give only modes
  0-6 (`0x55629e`). The view key (`0x423b80`) moves to
  `0x5e103c[mode]`, and when that mode is over 6 it switches straight to
  `0x5e1054`, which is `0x5e103c[6]` (`0x423cd8`-`0x423cf0`). The
  race's start camera, mode 16 (`0x4253d0`), also hands over to
  `0x5e1054` (`0x42542a`, `0x4254d4`). The cycle builder `0x423a60`
  fills every entry with the first slot's mode and then links the
  slots, so `0x5e103c[6]` is the mode after 6 when a slot holds 6,
  else the first slot's. A race therefore starts in the first slot's
  view unless mode 6 is in the list, in which case it starts in the
  view after 6. With 5, 6, 10 that was 10 (`h3-cm5`), and the key
  cycles 10 → 5 → 6 → 10. With 1, 10, 0, 0 (`h3-lb1`, below) the key at
  55.06 s went to 10 and was sent straight back to 1: `0x421730` kept
  running on the mode-1 object, and no mode-10 tick happened. So mode
  10 is never used by the retail game. It only shows when a view slot
  holds it and 6 comes before it.
- **Mode 10's tick and the tilt.** `0x425990` puts the eye at the car's
  position + its rows × the offset each tick, rigidly. Its set-up has
  +0x104 1, so `0x422520` calls `0x421730` (the tilt, pitch × 2). But
  it also has +0x108 1, and `0x422290` then rebuilds all three rows
  from scratch. Forward points from the eye to the car's position
  +0x98, and up is the world's (0, 1, 0), or the old up when forward is
  near vertical (`0x536e7c`). The rows are then turned about x by
  +0x50 (`0x4ea680`) and, for mode 10 only, about y by +0x11C =
  0.136 turn (`0x4eaab0`, `0x422398`), away from the car. So the tilt
  is thrown away. In `h3-cm5` at 66.032 s the rows were (−0.342, 0.067,
  −0.937 / 0.078, 0.996, 0.043) at `0x421730`'s entry, (…, 0.002,
  0.998, 0.069) after it (`0x42253c`), and (−0.342, 0.067, −0.937 /
  0.078, 0.996, 0.043) again after `0x422290` (`0x4225ca`). It was the
  same at every frame from 45.6 s: the drawn mode-10 rows don't depend
  on +0x920/+0x924. (Modes 3-7 and 9 have +0x108 set too, so they look
  at the car the same way, but their +0x104 is 0.)
- **The in-car rows before and after the tilt.** Run `h3-tr1`
  (`h3-runs/tr.sh`: `g3-cv`'s start with the default views, so mode 1,
  gas from 52 s, 56 s; `-watch` at `0x421730` and at its return
  `0x42253c`, reading the mode-1 object's rows `0x5e0e04`-`0x5e0e24`,
  the player's rows +0xC0 (`0x5e11e0`) and +0x920, +0x924). At each
  entry the camera's rows equal the car's +0xC0 exactly. With mode 1
  nothing changes them after `0x42253c` (+0xF4, +0xF8 and +0x108 are
  0), so the rows after it are the ones drawn. At 54.609 s (+0x920
  −0.00215, +0x924 0, the largest tilt after 53 s) the right row stayed
  (−0.7329, 0.0021, −0.6804). Up went from (0.0074, 1.0000, −0.0048) to
  (0.0258, 0.9994, −0.0246), and forward from (0.6803, −0.0086,
  −0.7329) to (0.6799, −0.0356, −0.7325). After × before⁻¹ is a turn
  about the camera's x by 0.02701 radians (cos 0.99964). That is 2π ×
  0.0043 turn, twice +0x920 (+0x104 1, mode 1's +0x50 0), with no roll
  as +0x924 was 0. At 50.013 s, with +0x920 −7.7e−8, before and after
  were equal. So the drawn in-car view is the car's own frame pitched
  by twice +0x920, as worked out from the code above.
- **The look-back.** `0x79c4a0[screen]` is the held state of input 0x15
  (`0x4c5134`: `0x48f7b0(0x15, 0)` each frame; 0x17 for the second
  screen). That is the look-behind key B (net.md). Run `h3-lb1`
  (`cm.sh` with the slots 1, 10, 0, 0, B held 53.0-54.0 s and
  57.0-57.6 s, the view key at 55 s, 58 s, frames every 3 from 2430).
  `0x79c4a0` read 1 at `0x421730` exactly over 53.079-54.0 s and
  57.049-57.621 s (59 stops), in mode 1. The frames then show the road
  behind the car: the start grid's chequered line at 53.2 s, the cars
  following at 57.3 s, with no dashboard. Before and after, they show
  the dashboard looking ahead. In the chase views (`0x4243a0`,
  `0x424740`) the forward offset is negated while it is set. The speed
  pull-in (`0x424030`) and the lag are skipped, so the eye goes straight
  to the mirrored offset. In mode 10 `0x425990` flips the sign bit of
  +0x118 (the forward offset) on **every tick** while it is set
  (`0x425a13`), not once. In `h3-cm5` (B held 63.541-65.007 s) the
  mode-10 rows switched between two headings, (−0.860, 0.013, 0.509)
  and (−0.345, 0.084, −0.935), every 2-3 frames. After release they
  stayed on the second, the side the last tick had left. So the eye
  jumps between behind-right and ahead-right of the car while B is held.
  Run `n3-runs/lb10` (`lb10.sh`: Hometown, no computer driver, view
  slots `0x6fbbc0`-`0x6fbbcc` poked to 5, 6, 10, 0 at `0x474ca0` so the
  race starts in mode 10, gas from 52 s, B held 54.0-55.0 s; `-watch
  0x425990` reading the mode-10 object's +0x118 and `0x79c4a0`; every
  frame from 2350, with `-framelog`) confirmed it in the frames. Mode
  10's tick ran 2 or 3 times a frame; while `0x79c4a0` was 1
  (54.046-55.021 s) +0x118 changed sign at every tick, between −2.4 and
  +2.4, so a frame of 3 ticks ended on the other side and a frame of 2
  on the same side: of the hold's 25 frames, 11 ended with the eye
  ahead. The frames show the view from behind-right alternating with
  one from ahead-right looking back at the car's nose, in the pattern of
  the watched sign; after release the view stayed behind.

## Other car fields

- **+0x124-+0x154, the road frame at the car.** `0x49ffa0` (from the
  road contact task `0x4a08c0`, and `0x4a0580`) gets nine numbers and
  a point from `0x420230` for the car's track position (+0xC, whose
  +0x10 is the slice +0x1C) and stores them × 1/65536: **+0x124 the
  road's right, +0x130 its up, +0x13C its forward, +0x148 the road
  point** (the virtual road point's position, bytes 0-11 of the
  36-byte point in [col.md](col.md)); +0x154 is the slice's heading
  (`0x49fee0`). The three vectors are the point's normal, forward and
  right bytes (16-18, 20-22, 24-26; `0x420230` reads them as ints at
  +0xD, +0x11, +0x15 and shifts down 24). On a road polygon with its
  own normal (track position +0x14 set, +0x6C 0, +0x64 2: the point is
  inside a polygon, next bullet) up and forward come from the polygon
  and right is their cross product. `0x4a0aa0`,
  which puts a car on a slice (start, reset `0x429100`'s callers,
  `0x40cd80`), writes the point's vectors directly and normalises them
  (`0x4972f0`). So +0x13C is the road's own direction at the car,
  not the car's: the computer's +0x7C0 is the speed along the road,
  and the road height (`0x4a00d0`, through `0x4968a0`) is the plane
  through +0x148 with up +0x130 (−32000 when up y ≤ 0.1). It is not
  the matrix at +0x190 the human's tick uses. The step loop skips a car
  whose **+0x89** is 0 (`0x49ffab`, `0x4a284d`): +0x89 is "in play".

- **+0xC, the track position** (0x78 bytes; read November 2026 from
  every access in `0x41e420`-`0x420840`, the `field` tool's
  displacement lists for +0x64/+0x6C/+0x70 there, and the callers of
  `0x41e940`, `0x41f6e0`, `0x420230`, `0x4207e0`, `0x41fd60`). Copies
  of it, built the same way, are made on the stack for points that
  are not the car (`0x4205a0`/`0x420650`: the officer's spot, lights'
  flares; `0x4304b0`: each corner of the car). Its fields: +0 the
  slice's block (`0x571370` + (slice >> 3) × 0x5C0), +4/+8/+0xC the
  point (16.16), +0x10 the slice, +0x14 the road polygon (14 bytes,
  frd.md) or 0, +0x18 its 8-byte record, +0x1C the record's vector
  (block +0x59C + record byte 0 × 12), +0x20 the slice's position
  record (frd.md: first polygon, count), +0x54-+0x60 pointers to the
  polygon's vertices 1, 0, 3, 2 (`0x41e6a0`: "corners" 0-3 below) and
  +0x24-+0x53 those corners as 16.16 ints (`0x41e7c0`), and:
  - **+0x64, the frame's source**: 1 (`0x41e940`; `0x41f6e0` with
    mode 0) until a polygon is found, then 2 (`0x41e990`, `0x41f090`,
    `0x41f500`, `0x41f870`). Nothing sets it back to 1 when a later
    search fails, which is why `0x420230` also wants +0x14. Its only
    reader is `0x420230`.
  - **+0x6C, the side the point is off**: 0 when it is inside a
    polygon of the slice's run (set with +0x64 = 2, and at the start
    of each search, `0x41ea00`); **1** when the search walked down to
    the run's first polygon and the point is beyond its outer edge
    (`0x41ee86`), **2** when it walked up to the last one and is beyond
    that one's (`0x41ebd7`). Corners 1-2 (vertices 0-3) of polygon i
    are the edge it shares with i−1 and corners 3-0 (vertices 2-1) the
    one with i+1 (the inside tests of `0x41e990`, `0x41f870`'s
    neighbour steps). With +0x6C set and
    nothing found, `0x41f500` puts the position on that edge polygon
    (+0x14, +0x18, +0x1C, +0x64 = 2) but leaves +0x6C, so `0x420230`
    still takes the road point's frame. Readers: `0x41e990` (a set side
    restarts the search from the run's middle), `0x41f500`, `0x420230`,
    `0x430350`, `0x41fd60` and `0x401d78`.
  - **The walls.** The values are the side bits of the car-against-
    track test `0x4304b0` (from `car_collisions_task` `0x4311d0`): for
    each corner of the car it builds a position from the car's
    (`0x41f6e0`); `0x430350` calls the corner off the road when it has
    no polygon record, +0x6C is set, or the surface is 0 or 14 (else it
    goes by heights); then
    `0x41fd60` ORs the car's and the corner's +0x6C and adds 1 or 2 when
    the corner is within 1.5 units of the edge 1-2 or 3-0 of the car's
    polygon, 4 or 8 past its edge 0-1 or 2-3 (the slice's ends);
    `0x41f870` steps to the neighbour (polygon i−1 for 1, i+1 for 2,
    slice +1 for 4, −1 for 8), and `0x430210` makes the wall's normal
    from that edge.
  - +0x68 u16 1 when the slice changed in the last update, +0x6A u16 1
    when the slice or polygon did (`0x41e420`, `0x41f650`); +0x70 the
    polygon's number in the run (−1 at a reset); +0x74 the mode given
    to `0x41f6e0` (0: the slice only, no polygon; the car's own update
    `0x49fe70` passes 1 − +0x88, so a car with +0x88 set keeps only its
    slice; `0x4207e0` sets −1 when asked about a position without a
    polygon).
  - Run (`c9-tp1`, November 2026: Redrock, racer's pursuit by keys,
    `-count` and `-watch` on `0x41ebd7`, `0x41ee86`, 40 stops): side 1
    was set 1,299 times, side 2 55 times; at the stops side 1 came with
    +0x70 0 (slices 33-36 and 99) and side 2 with +0x70 11 (slices
    62-72), as read. `0x41f570` (the edge snap) ran 1,302 times and the
    wall step `0x4308e2` 1,316. All 40 stops were stack copies
    (`0x4304b0`'s corners).
  - **Side 1 is the driver's left, side 2 his right** (November 2026),
    driving the track's forward way (slice numbers rising) on an
    unmirrored track. From the files: over all nine tracks (each
    slice's run of list-4 polygons in the `.frd`, against the slice's
    `.col` point; a script outside the repo), the run's last polygon's
    centre is along the point's right bytes from its first's at
    12,667 of the 12,706 slices (the 39 others are Atlantica's roofed
    slices 429-448 and 500-518, whose runs start over the roof:
    [frd.md](frd.md), The 39 other slices), the
    first polygon's outer edge (vertices 0-3) a median 17-35 m along
    −right by track and the last's (1-2) 10-27 m along +right; edge 0-1 is the
    front one at every polygon and vertex 1 lies right of vertex 0
    at 80-91 % of them (Hometown, Redrock, Lost Canyons), so a polygon's corners are 0 front left, 1 front right, 2
    back right, 3 back left. The right bytes are the cross of the
    normal and forward bytes in that order (up × forward) at every
    slice, which is the driver's right in the game's left-handed space
    (x right, y up, z forward; [fce.md](fce.md), and `viewScreen`'s
    positive x scale in frd.h). Run (`d2-w1`, `d2-w2`, Redrock,
    racer's pursuit, set-up pokes only, the gas from 40 s; car 0
    dumped every 0.25 s, `-watch` on `0x41ee86`/`0x41ebd7` with the
    corner's point, slice and +0x70): with the left key held
    56-58 s (`d2-w1`, steering −128) car 0 went to −22.7 m along
    the `.col` right and stopped against the left wall at slice 25
    (+0x70 1); every side mark within 3.4 m of it was side 1
    (`0x41ee86`, +0x70 0, −26 m). With the right key (`d2-w2`,
    steering +127) it hit the right wall at 57.4 s (32.9 → 14.5 m/s,
    +0x70 up to 10) at +9-10 m, and the marks by it were side 2
    (`0x41ebd7` at 57.31-57.62 s, +0x70 11 of 12, corners 11.6-12.1 m
    along +right, the right wall at 11.6). Car 0's own +0x6C stayed 0
    (its centre never left the run). On a track run backward
    (`0x6fd4c0`) side 1 is on the driver's right; with the mirror flag
    (`0x6fd4c4`) only the picture is flipped, so side 1 is drawn on the
    screen's right.

- **+0x8, the race distance** (int; the network status message sends
  its low 16 bits). `0x42ce10`, from the Sc32 task `0x42ce70`: lap
  (+0x204) × the track's road slices (`0x5dd940`) + the car's slice
  (+0x1C), or slices − slice when the track is run backward; 0 while
  +0x208 > 0. Seen at run time counting 1, 2, 3... as the car drove
  off the grid (`-watchmem` on car +8).
- **+0x208, the laps owed**: crossings of the start line the wrong way
  not yet made good. `0x42cc20` (each step; +0x1C the slice, +0x4 the
  slice before): going from slice 0 to one over 500 adds 1; going from
  over 500 to 0-50 takes 1 off while it is over 0, and only then adds
  a lap (+0x204). Mirrored when `0x6fd4c0` is set (the track run
  backward). The race's set-up (`0x42be2f`) starts it at 1 when
  `0x6fd4c0` is set, else 0, so that the first crossing from the grid
  doesn't count. Copied between cars at `0x4085b2`. A car moved past
  the line by writing both +0x1C and +0x4 (as the lap tests' placing
  pokes did) never pays that lap, so its race distance stays 0 and the
  computer driver's progress check (`0x409b70`) puts it 5 slices on
  every 0x180 steps from step 0x480 (tracks.md, Music laps, `e10-rs`).
- **+0x160, the car's weather.** Set each Sc32 tick by
  `car_road_contact_task` `0x4a08c0`: the race's weather kind (`0x4dcdd0`:
  0x8ca22c when `g_weather`), and 0 when the car is under cover
  (`0x420bf0`).
- **+0x8D4, the hit's smoke timer.** An impact harder than 30 (5 for
  traffic; strength +0x168) starts it at strength × 0.15 (`0x42ad35`).
  Each tick (`0x42b480`, from the +0x61C task) it counts up and puts
  particles at the hit point (+0x8D8, car frame): kind 6 to 16, kind 9
  to 64, then `0x4caea0`. It stops when the car is back on its wheels
  and its spin is under 0.2 (`0x42b64d`): on the ground (+0x164 0),
  upright (+0x10C ≥ 0.5) and the spin's x and z (+0xE4, +0xEC) both
  under 0.2 (`0x537310`). A car whose data has +0x380 bit 0x20
  (`0x4396e0`) makes no smoke. Over 64 it is `0x4caea0`'s fire (col.md,
  Car hits). While it runs, the count at +0x998
  that the sound code keeps (`0x41188f`) doesn't move. Not damage.
- **`0x471270`**: the force-feedback jolt of an impact, for the car on
  the force-feedback device (`0x471dc0`): the hit point (+0x178) in the
  car's frame, its direction by atan2, and the strength.

## How it was worked out

- The field offsets of P from the debug writer `0x4b2340`, whose jump
  table (`0x4b218c`) has one case per label; each case reads one P
  offset.
- Readers of P: every load of car +0x528 followed through registers and
  stack slots by a script over the disassembly.
- The tasks read instruction by instruction (`0x4a2830`, `0x4ad7a3`,
  `0x4aaed1`, `0x4a78d6`, `0x4aab41`). Constants read from the
  executable.
- The race settings from the option rows at `0x5563e8`-`0x5566f4` and
  `text.eng` (`reluva-tool text-dump`).
- The tyre, steering, gear and landing code by a script that folds
  the unoptimised code's register loads into names (car, wheel, P),
  then by hand.
- Runs: `nfs-host -native -deterministic 50000` with the usual keys,
  `-watchmem` on the player's car (record `0x5e1120`, records 0x9AC
  bytes apart, found with `-watch 0x4ad7a3`): +0x4EC-+0x4F4,
  +0x1A8-+0x1B0 and +0x8; `-keys` with `52000:+up,64000:-up` for the
  throttle.
- The November 2026 sweep (runs named `sw1-`): the static-analysis
  tools (`field car 0x6e0`, `decomp`, `callers`) to find the
  functions, the disassembly to confirm them; runs with the menu clicks
  `16000:click,24000:click,32000:move0/240,36000:click`, keypad 8 from
  52 s, then keypad 6, Space or H; `-watch` on single instructions,
  `-watchmem` on one car field and `-dumpmem at=ADDR#N` of the car and
  the stack; `-poke` for the Hot Pursuit (at `0x474ca0`) and for the
  landing (+0x164 and +0xA8 at 51 s).
- The `gh1-` runs (November 2026): `gh1-ai` (`-watch 0x4076b0 -watch
  0x407fa0 -watch 0x407c30 -watch 0x407380 -watchmem 0x5e227c 0x20`,
  keypad 8 from 52 s) and `gh1-cop` (the Hot Pursuit pokes, H at 47 s,
  `-watch` on the state stores and `-watchmem 0x5e1800 4`). The store
  lines carry no time: each was given the time of the `-watch` line
  before it. Constants read from the executable (doubles at
  `0x536304`-`0x536344`, `0x5361b4`-`0x5361d4`, `0x5363e4`-`0x536404`).

**+0x5B0 and +0x5A4** (traced to their writers with a cross-reference
of every access to those offsets):

- +0x5B0 is the slip ratio: the road-frame lateral speed over the
  forward speed, +0x4EC / +0x4F4, when |+0x4F4| is over 5, else 0
  (`0x4ad672`, stored at `0x4ad6cd`).
- +0x5A4 is the road speed: an approximate hypot of those two
  (`0x4967e0`, piecewise), negative when going backward (`0x4ad6eb`,
  stored at `0x4ad77a`).
- Both are worked out only in the human car's tick (`0x4ad7a3`).
  Everything else writes them 0 (`0x408950` and `0x4073c8` among the
  computer cars' code, `0x42bf8a`), so for computer cars they stay 0.

## What isn't known

- The cop's stop and ticket were run five ways: by the 7 m test with
  a computer racer held near the cop by a poke (`cp-stop7b`), by a
  strip (`cp-spike1`), by the 7 m test after the cop blocked a racer
  at speed (`cq-ram4`), after a bump with both driving on
  (`c3-bumpA`), and with two human cops in split screen sharing one
  target (`c3-two3`). A computer cop's stop of a human racer plays
  `copspch` by the tickets left (`c3-spch`: slot 0x12 with 3 left).
  Run since (`c4-`, November 2026): two cops over TCP sharing one
  target, the copies agreeing step for step with nothing but 06 sent
  (`c4-net1`); a fourth computer cop's hold begun at 35.5 m against
  the doubled 40 m (`c4-four7`); and the Pursuit Summary with two cops
  that made the quota, sorted by Quota Time (key 7) by default and by
  Player or Busts when those titles are clicked (`c4-res3`). Left
  open: those were set up with pokes (sirens, `-drive`, a held racer,
  the quota poked to 1, the bust speed disabled and the record set);
  a fourth cop's hold under the 40 m ran its 160 steps only with the
  16 m test poked out (`c6-four10`). The code says why: from behind,
  a cop drives in too fast for a hold wholly beyond 20 m; past a
  stopped target it is asked for 0 m/s and stands, so a fourth cop
  20-40 m ahead would hold to the end. No run had a fourth cop
  ahead (`c7-f1`), and with no pokes no record had more than 2 cops
  (`d1-r1`, `d1-r3`; 3 in `r2-cop1`), while the 40 m needs four on
  one record (Why a hold beyond 20 m needs a cop past the target):
  still open. What adds cops to one record, and a plan through a
  roadblock whose cops stand ahead of a stopped human racer, are read
  (same section, What raises the count); not run in November 2026
  either: it needs a second chase with heat over 3, a roadblock set
  51 or more slices ahead and the player stopped 3-4 slices short of
  it with two cops behind, minutes of keys whose every step depends
  on the one before. The network game was run to its results with
  nothing forced (`c7-net10`): the lobby's menus by clicks, a stop by
  a strip laid with S, the race ended by the racers' laps. A block
  by keys failed there (the racers steer round a standing cop) while
  the race differed run to run; with `-deterministic` in both copies
  it repeats, and a block planned on one run (`d1-n3`) stopped and
  ticketed car 5 in both copies on the next (`d1-n4`). Left open: a
  network chase from behind ending in a pin (`c7-q1`'s kind). Tried
  once (`e4-n1`, net.md, What isn't known): `-drive` in both copies
  keeps them in step, but straight-at-the-target steering left the
  cop on Hometown's walls, a kilometre behind; still open.
- The computer cops' driver (`0x4032e0`) was run in a racer's pursuit
  (`r2-cop1`, `r2-cop4`, `r2-cop5`, `c3-spk6`): chases, calls for help,
  two roadblocks, strips and a racer running over one, and the
  player's ticket. A whole chase by keys was run with the player as
  the cop, first as a block (`c5-b1`), then as a chase from behind
  on Atlantica against class C racers, ending in a ram into the wall
  and a stop and ticket (`c7-q1`; on Hometown the racers cruise
  faster and the bends need steering for 20 s or more, `c6-g4`,
  `c6-it*`).
  A human racer driven by keys was chased and ticketed (`c6-h2`), but
  it had crashed and stopped first. `0x401370` is read (the top
  speed). What each radio line
  says is in bnk.md (What the radio lines say); the disabled cop, the
  crash and the places were run (above, Radio runs). Settled
  (November 2026, state 4, Failed at the covered bridge): the
  officer's side test fails on a real road. Fifteen stops at
  Hometown's first covered bridge with the player up to 3.3 m from
  its surface-0 strip passed (`e2-h1`-`h15`); with keys planned from
  dumped probe runs the player stood along the strip, its side 0.4 m
  from it, and cop 3's stop ran `0x401dc4` (`e4-q1`, `q1v`): the
  officer stood on the car's other side until the ticket. No car-state
  poke was used. The other places (Hometown 510-519, Empire City
  207-213, Redrock 3-21) weren't tried; Redrock's grid is at slice 1,
  4 m left, with no surface-0 polygon near it, and the cop that
  passes it there (cop 6, at 29 m/s at about 166 s, `e4-r0`, the
  player left on the grid) took no one.
  The other tracks' places and the quarter and half mile were heard
  (above, Radio runs on other tracks). Why the "from" place after
  Hometown's half mile wasn't started in `s7-x1` is settled (`c8-q1`-`q3`,
  above): a stop of the player began 0.45 s into the 0x7F line, and
  `copspch` muted it and flushed the 14 lines queued behind it. A
  disabled cop or a crash without a poke is still not heard: only a
  car flat on its roof (+0x10C under −0.9, on the ground) reaches
  them, there is no damage, wall or timer path, and the on-road code
  takes out any roll (above, What a disabled cop or a crash needs). No
  car rolled in the 15 `s7-` runs nor in the four keys-only `c8-f*`,
  `c8-r*` runs, where no car even tipped 41° (`0x429452` never ran),
  with wall hits at up to 49 m/s and Redrock's crest at 40 m/s. Since
  (`d2-b6`, `b8`, `b10`, above, A roll by keys): the player's car,
  steered by one key pulse onto the bank left of Hometown's grid,
  rolled onto its roof and lay there 2.5-2.8 s until the reset, with
  no poke of car state; `0x429452` ran. The code puts no limit in the
  way (the spins are in turns/s; Rolls in the air). **The crash is
  now heard without a poke** (`d2-c4`, `d2-c4v`): the player left on
  the grid until cop 3 took it at 138.475 s, then rolled onto the
  same bank by one left pulse; `0x403910` and `0x40bc30` ran at
  142.221 s and slot 0x1A was said at 148.869 s. Left open: the
  disabled cop (`0x403950`), which needs a computer cop on its roof
  (keys steer only the player; no computer cop tipped 41° in any `d3-`
  run). A slow target does draw its chasing cop off the road to it
  (+0x711 bit 2, `0x406b60`): in `e2-a2` cop 3 followed the player
  onto Hometown's bank and stopped on it level (A cop drawn onto the
  bank). Led up the bank's steep part in nineteen keys-only runs
  (`e4-c*`, `u*`, `w*`, `x*`, `y*`, `z*`; A cop led up the bank's steep
  part) it turned off the road at it at up to 81° and climbed to 3.3
  m up, in air mode for half a second in `w3`, but always braked to
  under about 21 m/s first (the target's speed + 5.81 m/s near a slow
  target) and stayed within 11° of flat; the player itself rolled
  four more times beside it (`x2l`, `y2`, `z145200`, `z145400`, the crash line queued). Still
  open. Whether a side hit in the air or a car landing on another can
  roll a car is settled from the code but not seen (above, Rolls from
  car hits): cars are 3D boxes (`0x42fee0`, top and bottom faces
  included), and only a contact off the car's centre height rolls it,
  about 0.011 u turn/s side-on 0.6 m above or below the centre (0.44
  at 40 m/s), 0.0216 u for a slide across a car under or on top; the
  clamps allow it, but on the road the landing's damping takes it out
  (a hit tips 25° only from about 82 m/s), so it needs the car to stay
  in the air some 0.8 s after the hit. Run (`d3-`): hard hits on the
  road at up to 42 m/s and a cop flying 1 m high into the standing
  player from behind (`d3-h85`) left every car within 11° of flat; no
  run had a side-on hit in the air or a car landing across another
  (`e2-h1`, a hand-brake turn after the covered bridges' drop meant to
  leave the player across cop 3's way, ended facing backwards and
  reversing away: cop 3 passed 8.8 m from it). Nor did the `e4-`
  bank runs: the player's rolls there were its own, on the slope, with
  the cop standing beside; not tried on purpose in November 2026.
  The player rolled by itself off Hometown's road at slice 320 at 40
  m/s (`d3-g5`), and the crash line was queued again. With the player
  as the only cop no computer cop exists to say either.
- Why the recorded game's spike icon dims for 3 s and its racer list
  has no "over" text, when this exe hides the icon until the strip is
  cleared and shows the text: probably another build, not found.
- Settled (November 2026, Other car fields, +0xC): the track
  position's +0x64 is where the frame comes from (1 the slice, 2 a
  polygon was found) and +0x6C the side of the slice's road polygons
  the point is beyond (0 on a polygon, 1 past the first's outer edge,
  2 past the last's), the side bits of the wall test `0x4304b0`.
  Which is the driver's left is settled too (November 2026, same
  bullet): side 1 is the left and side 2 the right, driving forward
  on an unmirrored track, from the polygon order against the `.col`
  right bytes on all nine tracks and the wall runs `d2-w1` (left
  wall, side 1) and `d2-w2` (right wall, side 2). The `c9-tp1` stops
  hadn't settled it because +0x148 need not be the `.col` point on a
  polygon (lateral offsets against it jumped by up to 8 m between
  dumps in `d2-w1`; against the `.col` point they were smooth).
- The lane votes' own rules (`0x40cee0`, `0x40cfc0`, `0x40d1f0`,
  `0x40d2e0`, `0x40d3d0`, `0x40d720`-`0x40d9f0`): how a computer car
  weighs cars ahead and the best line.
- Settled (November 2026, The clock, the tick and the units of spin):
  from the code, the computer controllers' yaw rates are turns a
  second (the spin is turned into the body as spin / 32 in Sc32,
  `0x4a2f10`, by rotations in turns), and a sample per `0x4088f0` call
  is 32 a second (Sc32 runs on the even steps of 64, `0x4c4a6a`; the
  128 Hz timer, `0x4b6160`, makes a control frame every 4th tick).
- The car handling, as far as read (November 2026, The handling,
  routine by routine): every routine of the human's tick, the
  computer's model and the dynamics task is listed with what it does.
  Checked in runs since (Checked in runs, `e8-h`): the yaw limit
  (`0x4aea88`, which binds only near a standstill and otherwise damps
  the yaw 5% a tick), the body's friction on the road when tipped and
  in the air (`0x4aa8e7`), the Tire Factor on the forward acceleration
  (`0x4ae68e`, 1.15 in first and second only with t 1.5), the gear
  changes up and the computer cars' integration. Since then (`e10-h6`,
  `e10-h7`): the rest of a car's spin loss on its roof is the reset
  timer `0x429260` (all three spins × (1 − 1/64) a tick while the
  timer runs, on the ground, +0x10C under 0.5), and the roof wobble
  (`0x4296c0`) is a decaying swing of the drawn body's two tilts
  (+0x920, +0x924), 12 ticks a period, as read. Since (`e12-tp*`,
  `e12-tq*`, `e12-cl*`): +0x920 is the drawn body's pitch (a turn
  about its right axis, in `0x4b93c0`) and +0x924 its roll (about its
  forward axis), and the clamp's limit is 0.00625 (`0x5efde4`, a
  constant set at `0x42c648`) × the car's pitch roll factor P +0x294,
  0.92-1.03 for the cars run.
