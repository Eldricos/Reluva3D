# Need for Speed III: car handling (`carp.txt` and the car tasks)

How `nfs3.exe` turns a car's `carp.txt` into driving: where the file's
fields go, which per-step tasks use them, and the race settings that
change them. Worked out in October 2026 from the retail `nfs3.exe` (its
disassembly) and runs in `nfs-host` into a Hometown race. The step
tasks themselves are listed in
[nfs-threads.md](../../engines/nfs-threads.md); this is what they do.

Status: **read from the code**, with a few values checked at run time
(below). Not implemented in Reluva.

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
view. +0x50D works as the **hand brake** (by what it does below); over-
rev and over-speed also set it.

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
  instead of the ramps.
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

**The hand-brake wheel** (`0x4ac094`): +0x59C counts the wheels (1 a
front, 2 a rear). G = load × downforce factor × (1 − S), M as above;
f = G/M if M > G, else 0.75. Lateral out = lateral slip force × f ×
H, forward = drive force × f; in neutral both × 0.05. H (`0x4ac9dd`)
falls with the time held, x = +0x514 / 384: H = 0.75 × (1 − 0.8 / (1 +
e^(−4π(x − 0.5)))) (1 while +0x514 is 0), from 0.75 to 0.15, half way at 6 s dry (2 s in
rain, 1.5 s in snow).

## Car dynamics, every car (`0x4a2830`)

Skipped before step 512 (the start) and for a car not in play (+0x89).

1. `0x4a2260`: **downforce**, over 10 m/s on the road: velocity's up part
   (car frame) −= downforce (doubled for computer cars) × forward speed;
   **in the air**, a drag by axis (0.006, 0.002, 0.004; traffic 0.006,
   0.004, 0.004).
2. `0x4a00d0` the road's height under the car (+0x158; plane at +0x124,
   normal +0x130), `0x4a21a0` a roof over the road, `0x4a0440` the lowest
   corner's height over the road (+0x15C, from the half sizes +0x114,
   +0x118, +0x11C).
3. +0xB8 = max(|vx|, |vz|) + ¼ min(...), the cheap ground speed.
4. **Gravity**: in the air (+0x164 > 0), vy −= 0.306563. On the road,
   gravity is split along the road's three axes: the up part is always
   added; the side and forward parts only on a steep road (normal y <
   0.87); on a flatter one **the forward part is added while the car is
   upright** (+0x10C > 0.9, `0x4a2af7`).
5. Clearance < 0.6: on the road (`0x4a1610`: push out of the road,
   remove the into-road speed under 0.5 m/s, turn the body towards the
   road at most 0.5 rad/s) or landing (`0x4a1ce0`, below). Else in
   the air: +0x164 counts, spin kept within ±2.4 (`0x4a20c0`).

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

Partly read. Each tick, per car:

- skipped when `0x4be1a0` says so or +0x620 has bit 4;
- in the air or tipped (+0x8A, or +0x10C < 0.9): no grip, no skid,
  the rpm falls 500 a tick to the minimum, then `0x4aa8e7`;
- before the start (`0x7d3680` 0) velocity and spin are zeroed;
- on the road: velocity and spin into the **car's** frame (+0xC0,
  +0xCC, +0xD8 rows) at +0x4EC-+0x500, then one of three models:
  - **+0x88 set** (`0x4068a0`): no forces. The velocity is rebuilt from
    the road's normal and the car's side axis (× +0x7C4), the spin
    zeroed, the forward speed set to +0x7C0, and the car put on the
    road (`0x4a0980`). +0x88 is set at `0x46681e`;
  - +0x620 bit 0x80: `0x408520`, which copies another car's brake
    (+0x509);
  - else `0x408600`: tipped (+0xD0 < 0.3) `0x4aa8e7`; out of the race
    (+0x2C4 3 or 4) `0x407380`; else `0x408870(0.2)`, `0x4061b0`,
    `0x4060d0`, `0x4070c0`, `0x407c30` (not followed).
- The **speed wanted** (`0x409330`): +0x7B0 = the speed table's byte
  for this slice pair (m/s). Over 67.05 m/s and with +0x700 < 1 it becomes
  67.05 + (speed − 67.05) × +0x700. It is kept under P's top speed cap
  × `0x40ee80`'s factor.
- **speedsF.bin / speedsR.bin** (`0x40e000`; R when `0x6fd4c0`, the
  track run backward): after a 16-byte header, one byte per two road
  slices, the speed (`0x40e080`; 64 without the file); then one byte
  per slice whose high 4 bits `0x40e0c0` returns (7 without the file;
  used at `0x4096e6`, not followed).

## Other car fields

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
  doesn't count. Copied between cars at `0x4085b2`.
- **+0x8D4, the hit's smoke timer.** An impact harder than 30 (5 for
  traffic; strength +0x168) starts it at strength × 0.15 (`0x42ad35`).
  Each tick (`0x42b480`, from the +0x61C task) it counts up and puts
  particles at the hit point (+0x8D8, car frame): kind 6 to 16, kind 9
  to 64, then `0x4caea0`. It stops when the car is back on its wheels
  and its spin is under 0.2 (`0x42b64d`). While it runs, the count at +0x998
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

## What isn't known

- The computer cars' driving (`0x408600` and the functions it calls,
  `0x4068a0`'s +0x7C0 and +0x7C4, the slice nibble of the speed
  table), and the assists' driver `0x4a83d3`.
- +0x5B0 (it scales the drive, the steering at speed, the automatic's
  change down and the traction assist) and +0x5A4 (a speed, read with
  the gear ratios) were not traced to their writers.
- Which key sets control bit 0x04 (+0x50D, the hand brake by its
  effect).
- Landing: whether the scaled speed into the road is reversed later;
  as read it keeps its sign.
- Not checked at run time: the tyre and steering formulas above (read
  only); the gear change and the neutral loss were.
