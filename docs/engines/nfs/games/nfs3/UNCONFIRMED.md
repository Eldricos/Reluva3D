# Need for Speed III: unconfirmed items

What Reluva does that hasn't been checked against the running original.
The hosted game runs the original's own code, so this is mostly about the
stand-ins under it and the data viewer's own models; [PLAN.md](PLAN.md)'s
Open questions has the details of each.

## Read from the code, not seen running

- How the wall hits' particles look in play, and wall code 3's material
  ([formats/col.md](formats/col.md)).
- The MMX sound mixer's 8-bit routines: no retail sound is 8-bit, so they
  run only in `reluva-tool nfs mmx-check` ([formats/bnk.md](../../formats/bnk.md)).

## Seen running, not traced in the code

- Which renderer the eighteen YouTube day and night clips' game used:
  not known; by day without weather the fog's difference is slight
  ([formats/hrz.md](formats/hrz.md#the-weather-against-footage-november-2026)).

## Inferred

- The Pro Logic decoder model's placing of the voices behind: only the
  matrix's sums are measured, not a real decoder of the time.

## Guessed

- Whether the Classics release differs from the original 1998 retail in
  data or code; other releases need their own hashes ([README.md](README.md)).

## Settled

- November 2026: the hosted game runs on main's x86 emulator (its
  decoded-instruction cache and JIT) to the same menus and races as on the
  branch's interpreter, checked with `reluva-tool nfs host` into a
  Hometown race, natively and on the Glide stand-in ([TESTING.md](TESTING.md)).
- November 2026: the `Rain`, `Snow` and `Night` clips were recorded
  through the Direct3D renderer (`d3da.dll`), from the user's own copy
  of this CD in a virtual machine, as the user reported
  ([PLAN.md](PLAN.md#open-questions)). So the `Rain` clip's missing fog,
  where the 3dfx path draws a heavy haze, is d3da's: the game sends fog
  only to the 3dfx renderers ([formats/hrz.md](formats/hrz.md#the-weather-against-footage-november-2026)).
