# Need for Speed III: threads and the timer

Which threads `nfs3.exe` makes, what its multimedia timer does, and
where the race simulation runs. Worked out in October 2026 from the
retail `nfs3.exe` (its disassembly) and a run in `nfs-host` with `-log`
into a Hometown race. See [nfs.md](nfs.md) for how Reluva schedules
them.

## In short

**The race simulation runs on the game's main thread.** The timer and
its helper threads only count ticks and wake other threads: one polls
the controls, one mixes the sound, two read files. The main thread's
race loop compares a 64 Hz tick count, kept by the timer, with how many
steps it has done, and runs the missing ones before it draws.

## The timer

`0x48eff0(rate)` starts it (`timeGetDevCaps`, `timeBeginPeriod(1)`). It
is called with rate 128 from start-up (`0x4b6160`). It was also called
earlier (`0x4f8a93`) with 0, which means 100 Hz.

- **A one-shot timer, re-armed each time**: `timeSetEvent(delay, 1,
  0x48ed30, 0, 0)` (0 = `TIME_ONESHOT`), at `0x48edbe`.
- The callback `0x48ed30` adds the period (`1000 << 16 / rate`, 16.16
  ms, at `0x79c170`) to its due time (`0x79c178`) and sets the next
  delay from it. At 128 Hz the delays alternate 7 and 8 ms (log: 530
  of 8, 122 of 7).
- On each tick it signals two events: the tick thread's (`0x79c17c`)
  and the sound thread's (`0x79c168`). It then calls `0x4f2a60`, which
  signals the network service thread's event if there is one.
- Start-up checks the tick thread really counts (`0x79c1a4` must move
  within 5 periods). If not, it retries once more. When it works,
  `0x56441c` is set to `0x48ed00`.
- `0x48ef50` stops it (`timeKillEvent`, `timeEndPeriod`). It is
  registered to run at exit (`0x4f2a78`).

## Threads

All of them but the C runtime's are made by `win\threads.c`
`createthread` (`0x4df7a0`). It calls `CreateThread(0, stack,
0x4df730, start, CREATE_SUSPENDED)`, then `SetThreadPriority`, then
`ResumeThread`. Its priority argument maps -3..3 to IDLE, LOWEST,
BELOW_NORMAL, NORMAL, ABOVE_NORMAL, HIGHEST and TIME_CRITICAL (table
`0x4dfa10`). Each thread waits on its own event (`0x4dfb70`, which
calls `WaitForMultipleObjectsEx` at `0x4df705`).

| Start | Made by | Priority | Woken | What it does |
|---|---|---|---|---|
| `0x4cd4e0` | start-up | above normal (1) | window messages | the window's message loop (`GetMessageA` `0x4cd59f`, `DispatchMessageA`). It also starts iSND, so it makes the sound thread |
| `0x48eb90` | `0x48eff0` (timer start) | highest (2) | each tick | the tick thread: counts `0x79c1a4` and `0x79c1a0` (128 Hz), keeps an 18.2 Hz count (`0x79c16c`, ms × 1193 / 65536), then calls up to 8 tick handlers (table `0x79c130`, added by `0x48ee30`) |
| `0x48ec40` | `0x48ec80`, from iSND start (`0x4e2331`) | time critical (3) | each tick | the sound thread: calls `iSNDdirectserve` (`GetProcAddress` result at `0x9ef634`) inside a critical section (`0x4e2234`). This is the software mixer and the DirectSound `Lock`/`Unlock` |
| `0x42dc20` | start-up (`0x4b6205`), handle `0x7a3aac` | above normal (1) | every 4th tick (32 Hz) | the controls thread: reads the devices (`0x48f550`), then puts the bits in a 32-entry ring (`0x79c2b8`) and counts `0x79c4ac` |
| `0x4e53c0` | stream open (`0x4e4578`) | above normal (1) | on demand | `cmn\cdstream.c` `streamreaderthread`: reads the music (`.mus`) from the CD (`D:\GameData\Audio\pc\show5.mus` in the menus). Stopped with `TerminateThread` and made again for the race's music |
| `0x4f1820` | `initasync` (`0x4f0e9f`) | above normal (1) | on demand | `win\async.c` `asyncreader`: reads files in the background. It waited once and did nothing in this run |
| `0x4f27a0` | `0x4f2890` | highest (2) | each tick (`0x4f2a60`) | calls handlers in a table (`0x9f0c00`) under a critical section; `cmn\connect.c` adds them. Network play only; not made in this run |
| `0x4f5b10` | `win\ipx.c` | highest (2) | - | IPX receive. Network only |
| `0x4f68c0` | `win\tcp.c` | highest (2) | - | TCP receive (`TCPrecv`). Network only |
| `0x4fa210` | `win\tapipkt.c` | above normal (1) | - | TAPI (modem). Network only |
| `0x51af10` | `win\comm.c` | highest (2) | - | serial port reads. Serial play only |
| `0x4fbda0` | `win\nfile.c` `FILE_startdevice` | above normal (1) | - | a file device thread. Not made in this run |
| `0x512ad0` | `0x512d1a` | above normal (1) | - | not studied; near an `EAGAME.%08x...` format string |

The C runtime's own `_beginthread` (`0x52ec04`, calls `CreateThread`
at `0x52ec9b`) is reached only through a pointer (`0x56b1c0`). The game
never called it.

## The tick handler and the game clock

The race's only tick handler is `0x42dc60`. It is added by `0x42dcb0`
(called at `0x4c62fa`, as a race starts) and removed by `0x42dd00`
(`0x4a3665`). On each 128 Hz tick it counts:

| Counter | Rate | Read by |
|---|---|---|
| `0x5efe00` | 128 Hz | `0x471ee2`... |
| `0x5efdf8` | 64 Hz: **the game tick** | the race loop (`0x4c4e4b`, `0x4c4ea4`) |
| `0x5efdfc` | 32 Hz; also wakes the controls thread | the race start countdown (`0x4c4be7`, `0x4c4d97`) |

64 a second matches the game tick that [frd.md](../formats/nfs/frd.md) gives for the keyed
objects.

## The race loop, on the main thread

`0x4c4e40`, called from `0x4a37e3` in a loop, goes round until the
race ends (`0x7d369c`). Each time round it:

1. runs the due system tasks (`cmn\systask.c`, `0x4e7630`). They are
   polled here, against `0x79c1a0`, not run on the timer;
2. moves the step target `0x7d36ac` on by the 64 Hz ticks gone by
   (`0x4c4ea4`-`0x4c4ebf`);
3. handles each control frame the controls thread has made: while
   `0x7d3688 <= 0x79c4ac`, it does the keys for that frame (pause,
   views, `0x48f7b0`) and adds 1 to `0x7d3688` (`0x4c5276`);
4. runs the simulation: while the steps done (`0x7d36b0`) are behind
   the target, it calls `0x4c81c0(step)` and `0x4c48d0(step)`, then
   adds 1 to `0x7d36b0` (`0x4c52c0`-`0x4c5462`; every 32nd step also
   `0x4c7bd0`);
5. draws the frame (`0x4bc680`, `0x4c5489`).

The main thread calls `0x4c81c0` and `0x4c48d0`, never a timer or
helper thread. The timer only sets how many steps are due.

## Checked at run time

`nfs-host -log 400000000 -native -deterministic 50000`, the usual keys
into a Hometown race, 2.5 billion instructions. Its output was summed
on the fly by awk (a 70 KB summary, not a full log). The logged thread
ids:

| Id | Thread |
|---|---|
| 260 | main (WinMain): every THRASH call, so all drawing |
| 264 | `0x4cd4e0` window messages |
| 268, then 276 | `0x48eb90` tick thread: made at 100 Hz, killed, made again at 128 Hz |
| 272 | Reluva's WINMM timer thread, running `0x48ed30` |
| 280 | `0x4f1820` async reader |
| 284 | `0x48ec40` sound |
| 288, then 296 | `0x4e53c0` CD stream reader (menus, then race) |
| 292 | `0x42dc20` controls |

In the race (from the `Tr00.frd` load to the end of the run, about 11.5
s of game time):

- 1,466 `timeSetEvent` calls from the callback (about 128 a second).
- The tick thread woke 1,466 times. It set the controls thread's event
  312 times, and the controls thread woke 312 times.
- The sound thread woke 1,466 times. It did 1,466 sets of
  `GetCurrentPosition`, `GetStatus`, `Lock` and `Unlock`, and all of
  the mixing (2,547 mixes to 16 bits).
- The race's stream reader opened its music file once and read it 18
  times.
- All the drawing was on thread 260: `drawtri` from `0x433fff`,
  `drawquad` from `0x4c1439` and so on.
- `nfs-host`'s per-second line: "game thread 94%, timer 0%". The rest
  of the instructions are the sound thread's mixing.

Thread creation seen in the run, in order: `0x4cd4e0`, `0x48eb90`,
`0x48eb90`, `0x4f1820`, `0x48ec40` (from thread 264), `0x4e53c0`,
`0x42dc20`; in the race, `TerminateThread` of the stream reader, then
`0x4e53c0` again. With the WINMM thread that is 8 threads in all, 7 of
them the game's own.

## What isn't known

- What `0x4c81c0` and `0x4c48d0` each do in a step (cars, AI,
  network?), and how the target in step 2 is limited when the main
  thread falls behind.
- What thread `0x512ad0` does.
