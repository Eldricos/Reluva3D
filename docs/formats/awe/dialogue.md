# Dialogue

Conversations are tables in `Text/Dialogue` (a table per act, and three
generic ones), played by the game's dialogue manager. A `talk` command
([script.md](script.md)) starts one, and its `talk` event lasts until
it's over.

## How this was worked out

From `motoe.exe`'s dialogue manager (`0x438f00`..`0x43e000`: the table
loader, `talk`'s start at `0x43a630`, the choices, the line player), read
in a disassembler, and checked against the tables: `dia_observation`
(act 2), `dia_masterman` (act 6B), the generic tables, `dia_barnabyham`
(act 7A) and the solutions (act 8). Of the 2,858 lines, 2,725 have their
recording under the name below, and 525 an event file.

In what follows, "DM" is the dialogue manager object
(`[[0x498818]+0x14c]`, `this` in its functions), and **sure**, **likely**
and **guess** say how much of each point was read rather than inferred.
`0x43e750` is a case-insensitive "contains" test (an empty needle always
matches), `0x407ae0` evaluates a condition.

## The tables

`Name, Act, Conver., Thread, Nest, Speaker, Target, Line, Line
Translation, Condition, Repeat, Prevent, Event, Povent, Duration,
Optional, Notes`. The game loads `gen_antoinette.txt`, `gen_comments.txt`
and `gen_characters.txt`, and an act's `act<NN><x>.txt` when the act
starts (`0x4224fb`).

## 0. Overview of the flow

1. A table is loaded into a tree: **conversation part** (Name + Conver.) →
   **threads** (Thread column) → **lines** (Nest column). Several parts share a
   Name (dia_observation a, b, c...).
2. `talk <name> <code> [opts]` calls `DM::Start` (0x43a630) with
   (name, part, thread, nest, gen, number, plus, greet).
3. Start picks the part. Then either
   * it plays one thread straight away (an explicit thread, or a part with a
     single thread), or
   * it plays the **greeting** thread `1` (first time) or `2` (the regreet,
     later times), then opens the **choice menu**, or opens the menu at once.
4. The menu lists every *available* lettered thread (a, b, c...) by the text
   of its current line (normally Antoinette's question), the generic extras of
   the NPC (gen_<npc>_print...), and the exit thread `3`.
   One choice and no exit means it is played without asking.
5. A chosen thread plays line by line. Per line: Prevent (wait) → speak (voice,
   head turn, the Event column or the event file running alongside) → the line
   ends on voice end, timer or click → Povent → 0.4 s pause → next line.
6. When a thread ends, the menu is rebuilt. When nothing is left: with `+`,
   the next part (Conver.+1) starts; otherwise the conversation ends, the box
   slides away, and the runner's `talk` event is done (`0x409a80("talk")`).
7. Progress (which thread has been heard, and where it would resume) is kept
   per (Name, Act, Conver., Thread) in a list in the story state
   (`[0x498810]+0x2b8`), saved with the game. The condition
   `talk <name> <act> <code>` reads it.

---

## 1. Data structures

### 1.1 Loader 0x438f00(path) (sure)

* Skips the file if it is already in DM+0x610 (a list of loaded paths,
  count DM+0x614), else adds the path.
* Reads line by line (`0x43e220`). The first line (headings) is skipped. Lines
  shorter than 20 characters are skipped. Columns are read **by position**,
  split on tabs: Name, Act, Conver., Thread, Nest, Speaker, Target, Line,
  Line Translation, Condition, Repeat, Prevent, Event, Povent, Duration,
  Optional, Notes. (gen_characters.txt and gen_antoinette.txt have no Optional
  column, so their Notes text ("added", "teeth clicking") lands in Optional and
  `wtoi` gives 0. Harmless.)
* A text value of `"0"` means empty for Translation, Condition, Prevent,
  Event, Povent and Repeat. Missing columns give empty strings.
* Codes:
  * **Conver.**: letter → `toupper(c)-'A'` (a=0, x=23); number n → `n+0x63`
    (so `0`=99, `1`=100, `10`=109).
  * **Thread**: letter → `toupper(c)-'A'`, plus `(c2-'0')` if there is a second
    character (so `z1` = 26); number n → `n+0x5f` (`1`=0x60, `2`=0x61,
    `3`=0x62).
  * **Nest**: letter → `toupper(c)-'A'`, plus `toupper(c2)-'@'` if two
    characters; number n → `n+0x5f`.
* A new part node starts whenever Name or Conver. changes. A new thread node
  starts whenever the Thread code changes. So rows must be grouped. Parts are
  appended to DM+0x8 (count DM+0xc) in file order. Threads and lines keep file
  order.

### 1.2 Part node, 0x90 bytes (sure)

| off | content |
|---|---|
| +0x00 | Conver. code |
| +0x08 | list of threads (circular, sentinel), +0x0c count |
| +0x10 | Name (wchar[32]) |
| +0x50 | Act (wchar[32]) as written (`06b`, `0`) |

Lookup `0x439c20(name, code)`: the first part with `wcsicmp(Name)==0` and the
same code.

### 1.3 Thread node, 0x1c bytes (sure for the fields, likely for the names)

| off | content |
|---|---|
| +0x00 | Thread code |
| +0x04 | word **status**: 0 not finished, 1 finished ("heard") |
| +0x0c | list of lines, +0x10 count |
| +0x14 | **current line**: where the thread resumes / which line is shown as its choice (0 = none) |
| +0x18 | flag: thread borrowed from another conversation (gen extras, generated exit/regreet) |

Lookup `0x439c70(part, code)`: the first thread with that code.

### 1.4 Line record, 0x1324 bytes (sure)

| off | column / meaning |
|---|---|
| +0x000 | Nest code |
| +0x004 | Repeat: `toupper(letter)-'A'`, else -1 (digits such as `1` give -1) |
| +0x008 | Speaker (wchar[64]) |
| +0x088 | Target (wchar[64]) |
| +0x108 | Line (wchar[1024]); replaced by Line Translation if that is not empty or `0`. Longer than 1022 characters is cut, with a warning |
| +0x908 | Condition |
| +0xb08 | Prevent |
| +0xd08 | Event |
| +0xf08 | Povent |
| +0x1108 | text width in pixels (`0x414c10(1,text,&w)`, the dialogue font) |
| +0x110c | row height (written by the same call, likely) |
| +0x1110 | number of rows: 1 if width ≤ 0x3b0 (944), else word-wrapped by 0x43d9b0 at 944 px (two spaces after `.`, `!`, `?`) |
| +0x1112 | alternative index: 0, or previous line's index + 1 if the previous line of the same thread has the same Nest code |
| +0x1114 | Duration, second number (`"a,b"` → b) |
| +0x1118 | Duration, first number (a) |
| +0x111c | Optional (wtoi) |
| +0x111e | voice base name: `Name + Act + Conver + Thread + Nest`, the column texts as written (e.g. `dia_observation02aa1a`) |
| +0x1320 | picture: if Notes contains `.jpg`/`.png`/`.tga`, the picture loaded by 0x439ad0 (cached in DM+0x604), else 0 |

### 1.5 Dialogue manager fields used here (sure unless marked)

| off | meaning |
|---|---|
| +0x10 | current part |
| +0x14 | current thread |
| +0x18 | current line |
| +0x1c | highlighted choice line |
| +0x20 | active (a conversation is running / the box is up) |
| +0x24 | choice mode (menu shown) |
| +0x28 | text timer (s) when there is no voice |
| +0x2c, +0x30 | time since the conversation / line started |
| +0x38 | Prevent runner (a script runner: +0x3c busy, +0xd0 queue count, +0xdc its current character) |
| +0x130 | Event runner (+0x1d4 its character) |
| +0x228 | Povent runner (+0x22c busy, +0x2c0 queue count, +0x2cc its character) |
| +0x320 | pending action after the Povent runner empties: 1 end talk, 2 next line, 3 menu |
| +0x322 | waiting for the Prevent runner before speaking |
| +0x324 | draw the box while Povent runs (0 if Povent has walk/rotate) |
| +0x328 | path of the playing voice; +0x528 voice handle |
| +0x52c | "offer finished repeatable threads" (see 3.3) |
| +0x530 | `plus` (talk code ended in `+`) |
| +0x534 | "go on to the next part" (set at a line end) |
| +0x538 | `gen` |
| +0x53c | `greet` |
| +0x540 | re-entrancy guard while trying following parts |
| +0x544 | force the box and subtitles (dia_barnabyham, hamradio) |
| +0x548, +0x54a, +0x54c, +0x550 | box position and slide state (1 in, 2 out) |
| +0x554 | number of lines started in this thread |
| +0x556/+0x558 | mouse position saved by keyboard navigation |
| +0x55a | box height: 0xa0 (160), 0xb4 (180) after `act >07a`, 0x40 (64) for movie subtitles |
| +0x55c | which window: 0 = `[screen+0xb8]` (dialoguewin01), 1 = `[screen+0xbc]` (dialoguewin02) |
| +0x55e, +0x560 | alternative choice: last index, requested index (`number`) |
| +0x564 | last voice level (lip sync) |
| +0x568, +0x56c | text colour index (fonts.txt colours): 6 choices, 7 highlighted choice, 8 Antoinette, 9/10 other speakers |
| +0x570, +0x572 | colour alternation state, last non-Antoinette speaker |
| +0x5f8 | list of characters whose head was turned (reset to `home` at the end), count +0x5fc |
| +0x618 | always 0 (initialised only, never set) |
| +0x61c | dry run (the `cantalk` condition) |
| +0x620 | gen extras present in the menu |
| +0x624 | `gen_<npc>_` prefix (wchar[64]) |
| +0x6a4, +0x6e4 | name and code of the gen extra conversation that was chosen |
| +0x6e8 | exit variant chosen |
| +0x6ec | pause before the next line (0.4 s) |
| +0x6f0 | movie subtitle mode |

---

## 2. Starting: `talk` and 0x43a630

### 2.1 The `talk` verb, 0x40ba25 (sure)

`talk <name> <code> <opt3> <opt4>`:

* code empty → part = 100 (`1`).
* code starts with a letter: part = `toupper(code[0])-'A'`; if `len>1` and
  `code[1]!='+'`, thread = `toupper(code[1])-'A'`; if `len>2` and
  `code[2]!='+'`, nest = `toupper(code[2])-'A'`. thread and nest default -1.
* code starts with a digit: part = `wtoi(code)+0x63` (thread, nest = -1).
* plus = 1 if the code's **last** character is `+`.
* opt3 then opt4: `nogen` → gen=0, `forcegen` → gen=1, `nogreet` → greet=0,
  anything else → number = `wtoi(opt)`. Defaults: greet=1, number=-1,
  gen = the runner's `+0xf4`, which `0x408c80` sets from its 4th argument:
  **1 when the script comes from clicking an NPC object (type `NPCX`,
  0x4162a3), else 0**.
* Calls `DM::Start(name, part, thread, nest, gen, number, plus, greet)`.
  * Returns non-zero → the event waits for `event done "talk"`, except
    for `talka`, which ends at once.
  * Returns 0 and gen → `0x43a550(gen)` (the "nothing to say" line); if that
    starts something, the event waits likewise.
  * Otherwise the event ends at once.

`0x43a550`: if DM+0x624 is not empty: `Start(<+0x624>notalk, 100, -1, -1,
gen=1, -1, plus=0, greet=1)` (e.g. `gen_arbuthnot_notalk` part `1`). If
+0x624 is empty: `gen_canttalkf` if the list
`"gen_countessandrenyi_ gen_debenham_ gen_dragomiroff_ gen_hubbard_ gen_ohlsson_ gen_schmidt_"`
contains +0x624, else `gen_canttalkm`. Because +0x624 is empty on that path
and an empty needle always matches, this is in practice always
`gen_canttalkf` (a quirk). Note that +0x624 is only recomputed when the
requested part exists, so it may be left over from the previous conversation.

### 2.2 DM::Start(name, part, thread, nest, gen, number, plus, greet), 0x43a630 (sure)

1. `c = find(name, part)`; none → return 0. DM+0x10 = c.
2. Not in a dry run: closes or resets the inventory UI (`0x4194d0`, and for
   names not containing `gen_` or `com_`, `0x419e80` and `0x419970`; *likely*:
   this closes the inventory and drops the held item).
3. Box: height 160, window 0. If the condition `act >07a` holds and this is not
   movie mode: height 180, window 1 (the solution scenes use the taller box).
4. DM+0x544 = 1 if name is `dia_barnabyham` or `hamradio` (wcsicmp), else 0.
5. DM+0x52c=1, +0x20=1, +0x6f0=0, +0x2c=+0x30=0, +0x530=plus, +0x538=gen,
   +0x53c=greet, +0x620=0, +0x560=number; if number≠-1 then +0x55e=-1.
   Colour state is reset.
6. `0x43a030`: for every thread of c, status=0, current=0, then load the saved
   record (name, act, part, thread) from the story state: status = saved
   status; if the saved nest ≠ -1, current = `pickLine(thread, nest, fwd=1)`.
7. `0x43a3d0`: DM+0x624 = `"gen_%s_"` with the Speaker of the first line (in
   thread then line order) whose Speaker is not `antoinette`; empty if none.
8. If +0x624 is set and part < 100: `0x43a300` loads saved progress (fwd=0)
   for every lettered part (code < 99) of every conversation whose name
   contains +0x624 (gen_<npc>_print...). If gen: n = `0x43a470` (the summed row
   count of the available extra threads); if n>0, +0x620 = 1 (extras shown).
   A thread's rows are its current line's (+0x1110); one with no current line
   gets `pickLine(t, a, fwd=0)` as its current line (0x43a4da), and if that
   finds none (its first nest's conditions all fail) it adds nothing, though
   it is "available" (3.3 looks further on).
9. Choosing what to play:
   * **Explicit thread, or a part with exactly one thread and no extras**:
     t = thread (or `a`, code 0, if thread = -1, even when the one thread
     is another: an opening-only part fails here). DM+0x14 = t; none →
     return 0 (note: +0x20 is left at 1; see 9.6). If t.status≠0, t.current=0 and +0x624 is set,
     or if t is not available (3.3) → **exhausted** (see below). Else play t
     from `nest` (or from t.current, or from its first line) and return 1.
   * **Otherwise** (thread = -1 and several threads or extras): n = the
     extras' rows from step 8 plus the rows of the available lettered threads
     (`1`, `2` and `3` do not count), counted as in step 8 (0x43a9ae).
     n=0 → **exhausted**. Else:
     * if gen: delete the part's own thread `3`, if any (0x43b380), and build a
       new exit thread `3` (0x43af20, 2.4) appended to c.
     * visit tag: key = `"%s %s %ld"` (scene name, from `[0x498858]+4`
       lower-cased; Name; part code), v = `tag(key)` (-99 if missing).
       If gen && greet && part < 100 && v > 0: delete the part's own thread `2`
       and build a generated regreet thread `2` (0x43b180, 2.5). Then
       `tag key = 1`.
     * start thread: `1` if it exists, unless its status is 1 with no current
       line (already heard), in which case `2`; if there is no `1` and v>0, `2`.
     * If there is a start thread and greet: play it (0x43b9e0(-1)). Else open
       the menu (`0x43b560(1, 0)`).
   * **Exhausted**: if plus (and not already inside this retry): for
     k = part+1, part+2...: if no part k exists → return 0; else
     `r = Start(name, k, -1, -1, gen, -1, plus, greet)`; return 1 at the first
     success. Without plus: +0x20=0, return 0.
10. Box show (on success): if neither window is visible, the chosen one starts
    sliding in from y=768 (+0x54a=1). If the other one is up, they are swapped
    instantly at y = 768 - height.
11. Return 1.

Tags: when a scene is left (0x423daa → 0x43aee0) every tag whose name
contains the scene's name is set to 2. So the visit tag is 1 if you already
talked during this visit to the scene and 2 if during an earlier visit, which
picks `gen_regreet1` or `gen_regreet2`. **Sure**, the reading too: the
lines say so. `gen_regreet1`'s are "I don't think I'm quite done yet",
"Oh! Another thing!", "Before I go, there was something else...", "I've
remembered something else I wanted to say" (this visit);
`gen_regreet2`'s "You did not expect to see me again so soon, did
you?", "Hello, I am afraid I am back again", "Something has occurred to
me since I left you", "I'd like to talk to you again, if you don't mind"
(an earlier one).

### 2.3 Special cases (sure)

* `gen_` / `com_` in the name: the inventory is not fully reset (step 2).
  Otherwise these conversations are ordinary numbered parts (a single thread
  `a` → played directly; no progress saved, 4.6).
* `act >07a`: taller window 2 (180 px), and the exit is always
  `gen_solutionexit`.
* `dia_barnabyham`, `hamradio`: the box and the spoken text are always drawn,
  even with subtitles off (+0x544).
* Movie subtitles: the movie player (0x42ae47) loads `Text\Movies\<movie>.txt`
  as a table, calls `Start(<movie>, 0, -1, -1, 0, -1, 0, 1)`, then 0x43a610
  (+0x6f0=1, box height 64). In this mode a line waits until the elapsed time
  reaches its Duration's first number, there is no slide, no pause between
  lines, no click skip and no progress saved. When the `movie` event ends, the
  runner calls 0x43b970 (dialogue reset).

### 2.4 The exit thread, 0x43af20 (sure)

name = `+0x624 + "exit"` (e.g. `gen_arbuthnot_exit`), r = `rand()%4`; if
`word[0x4986f8] > 0` and r == 3, r = 4 (`word[0x4986f8]` is the time of day,
9.3: after dark the 5th variant, "Good evening"). If `act >07a`: name =
`gen_solutionexit`, r = 0. Find the r-th thread with code `3` among the parts
with that name, in load order, and copy all its lines into the new thread
(DM+0x6e8 = r). If none is found, fall back to `gen_exit` with the same r.
Thread `3` is always "available" and always listed last.

### 2.5 The regreet thread, 0x43b180 (sure)

name = `"gen_regreet%d"` (visit tag value), r = `rand()%4`; copy the r-th
thread `2` of the parts named so. Data: `gen_regreet1` and `gen_regreet2`
each have parts 0..3 with thread `2`.

---

## 3. How a conversation plays

### 3.1 Line choice: pickLine(thread, nest, fwd), 0x439ca0 (sure)

* Collect the lines of the thread with this Nest code whose Condition holds
  (`0x407ae0`, an empty Condition holds).
* None: if fwd, try nest+1, nest+2... while lines with that Nest code exist
  (a whole nest level whose conditions all fail is skipped). Stop with null at
  the first missing nest code. Without fwd, return null.
* One: return it.
* Several (alternatives): index `i = +0x560`. If `i == +0x55e` and i≠-1, keep
  it. If i == -1, pick `rand()%count` different from +0x55e. Else use i-1.
  Store i in both. So `talk x a <n>` forces the n-th alternative (1-based), and
  without a number the choice is random, differs from the previous talk's
  choice, and stays the same for the rest of this talk.
* Debug only: in a dry run with the debug flag `[0x498818]+0x88`, a line whose
  condition fails logs `REQUIRED LINE NOT DELIVERED: <text>`.

### 3.2 Which threads play automatically (sure)

* Thread `1` (0x60): the opening, played before the menu when the part is
  started without a thread and with greet. Once finished (status 1, no current
  line) it is replaced by `2`.
* Thread `2` (0x61): the regreet. Own (barnabyham), or generated from
  `gen_regreetN` when gen is set.
* Thread `3` (0x62): the exit, listed last in the menu; choosing it ends the
  conversation. Its current line is re-picked at random each time it ends.
* Threads `1`, `2` and any code ≥ 0x60 are never listed as choices. Codes
  < 0x60 (letters, `z1`...) are choices. `x` is an ordinary letter (code 23);
  no special case was found for it.
* A part with one thread (and no extras), or a talk naming a thread, plays that
  thread directly with no menu.

### 3.3 Availability of a thread, 0x43b870 (sure)

* Code `3` → always available.
* status == 1: available only if t.current ≠ 0 **and** DM+0x52c ≠ 0 **and**
  t.current's Condition holds **and** the first-line check below passes.
* Otherwise (or after the above): available if `pickLine(t, a, fwd=1)` exists
  and its Condition holds.

So a finished thread comes back only if its last line had a Repeat letter (4.5)
and only while +0x52c is set.

### 3.4 The menu, 0x43b560(first, keepBox) (sure)

* DM+0x52c = first. Count the threads other than `1`/`2` that are available;
  give each a current line (`pickLine(t, a, fwd=0)` if it has none; none →
  skipped). If first, an available thread with status ≠ 0 gets status 0 again
  (in memory only). With extras (+0x620), count those too (0x43a470).
* `first` is 1 when the menu opens at the start of a conversation or after
  thread `1`/`2`, and 0 after any other thread. The effect: a repeatable
  finished thread is offered at the start of each conversation, and once
  played it is not offered again in the same conversation.
* count == 0: if +0x534 (go on), `Start(name, part+1, -1, -1, gen, -1, plus,
  greet)`; if that fails or there is no +0x534 → end (0x43b8f0(keepBox)).
* count == 1: if the part has thread `3` (so only the exit is left): with
  +0x534, try the next part as above; else **play the exit thread
  automatically**. Without thread `3`: **play the single thread at once**.
* count ≥ 2: choice mode (+0x24=1), colour 6, the box is shown again if
  needed, +0x1c = 0.

Choices shown, in order (drawing 0x43cf90, hit test 0x43c810/0x43cce0):

1. Picture choices: the lettered threads whose current line has a picture
   (Notes = an image path; only the Act08a solution evidence). n pictures are
   centred in a row: x0 = (744 - (112n - 16))/2 + 280, step 112, drawn at
   y = box top + 42. Hit rectangle (x-8, 810-h, x+104, 906-h), where h is the
   box height.
2. Text choices: each available lettered thread's current line, in thread
   order, at x=40, starting at y = box top + 25. Each takes
   `rows*rowHeight + 4` px (0x43d370). If an exit thread exists, a choice that
   would reach y ≥ 745 is skipped, keeping room for the exit. Hit rectangle:
   x 0..1024, y..y+height. A bullet (`dialoguewin05.png` highlighted,
   `dialoguewin04.png` otherwise) is drawn at (x-14, y+5). Picture threads
   also get a text row.
3. Gen extras (`gen_<npc>_*` lettered parts other than the current one, their
   available lettered threads), drawn the same way (0x43a220 / 0x43a0b0).
   Choosing one records its conversation name/code in +0x6a4/+0x6e4 and marks
   the thread +0x18 = 1.
4. The exit thread `3`, if y < 745.

Highlight: each frame in choice mode, +0x1c = the line under the mouse
(0x43c810), unless the mouse wheel chose last (below). Click: 0x43d8c0 (3.6).

The mouse wheel (`WM_MOUSEWHEEL`, 0x40142d: the delta over 120, a step a
notch). Away from the user, 0x416380 → 0x43d810 → 0x43cc30, a choice back;
towards, 0x4163a0 → 0x43d850 → 0x43cb70, a choice on. Each step keeps the
mouse position in +0x556/+0x558, and the update (0x43d767) goes back to the
line under the mouse once the mouse leaves it; while it's kept, the cursor
is hidden (0x43d7f0, from 0x414156). The steps go through the available
lettered threads with a line, in order, then the exit thread (`3`), and
round:

- on (0x43cb70): none highlighted → the first; the last → the exit, or
  none without one; the exit → the first;
- back (0x43cc30): none → the exit, or the last without one; the first →
  the exit, or none without one; the exit → the last.

The middle button (`WM_MBUTTONDOWN`, 0x401557 → 0x4163c0 → 0x43d890) takes
the highlighted choice. The fields are reset to −1 when a menu is offered
and when the conversation ends (0x4388ab, 0x438dfb, 0x43ba50).

Both steps walk only the part's own threads (`+0x10`'s list: lettered,
available, with a line) and the goodbye (`0x439c70(part, 0x62)`); the gen
extras, other conversations' threads, aren't in that list, so the wheel
passes over them (the mouse can still take them). On: none highlighted
→ the first; the goodbye → the first; the last → the goodbye, or none
without one. Back: none → the goodbye, or none without one (0x43cc68);
the first → the goodbye, or none; the goodbye → the last. Reluva
(`DialogueManager::wheel`, `middleClick`) does the same.

Choosing (0x43ba50): +0x24=0, DM+0x14 = the thread, DM+0x18 = its current
line, play it. The choice text is therefore also the first spoken line
(Antoinette's question).

### 3.5 After a thread ends (sure; see 4.5 for the line-end code)

* Thread `3` → the conversation ends.
* Povent contained `terminate` → the conversation ends.
* Povent contained `advance` → all threads of the part except `3` and borrowed
  ones are marked finished and saved (0x43b810), then the menu → count 0 →
  next part (because advance also sets +0x534).
* If the Povent runner is still busy → +0x320=3 (menu once it is idle).
* Else the menu: `first = 1` if the thread was `1` or `2`, else 0.

### 3.6 The trailing `+` (sure)

`plus` (+0x530) makes the conversation continue with part+1 instead of ending:

* at Start, when the requested part has nothing available (2.2, "exhausted");
* at a line end, if the thread has no next line, +0x534 = 1, and then the menu
  with no choices (or only the exit) starts part+1 (3.4). +0x534 is also set by
  `advance`.

A part whose choices are not all exhausted still shows its menu; `+` only acts
when a part runs dry. Example: acts.txt runs `talk dia_observation a+`. Part a:
Poirot's opening `1`, then the menu a/b/c. When two are done, the third plays
by itself. Then parts b, c, d, e, f follow, each with its own `1` opening.
After f, Start(g) fails and the conversation ends.

### 3.7 Numbered parts vs lettered parts (sure)

* Lettered parts (code < 99) save thread progress (4.6). Numbered parts
  (`0`=99, `1`=100...) do not: in-memory status is reset by every Start, so
  `talk gen_locked 1` etc. replay every time (with a random alternative).
* Extras and regreets apply only to part < 100.
* Numbered threads: see 3.2.

---

## 4. Playing a line

### 4.1 Line start, 0x43c070 (sure)

1. Colour (0x43ba80): Antoinette (Speaker contains `antoinette`) → colour 8.
   Another speaker → 9, toggling to 10 whenever the speaker differs from the
   previous non-Antoinette speaker. Choice mode → 6.
2. Reset the Prevent runner. If **Prevent** is not empty: queue it on the
   Prevent runner (`0x408c80`), set that runner's character to the speaker,
   +0x322=1 and **wait**. The update speaks when the runner is idle (no event
   running and none queued). Otherwise speak now.
3. Speak = 0x43bd30 (4.2), then 0x43c100 (4.3). +0x554 is incremented.

### 4.2 Speaking, 0x43bd30 (sure, the character calls are likely)

Skipped in a dry run. Speaker character = `0x43bb60`: the scene object named
like Speaker (`0x4367b0(name,1)`); for `poirot` also `poirotbed`; then the same
search in the other scenes of `[0x498824]+0xa50` whose name contains
`[0x498824]+0xa64` (*likely*: the scenes of the same location). Not found →
no animation; the line still plays.

* Character found: `0x404720` (*likely*: start the talking state).
* **Target** (if not empty and not `0`), split on spaces into w1 w2 w3:
  * w1 = `up` or `down`: the head animation `"head"+w1` (`headup`,
    `headdown`) for w2 seconds (default 0.5): `0x441270`.
  * w3 present: `0x441560(char, wtoi(w1), wtoi(w2), w3=="up"?1 : w3=="down"?2 : 0)`
    (*likely*: head angles).
  * w2 = `up` or `down`: `0x4415b0(char, w1, 1|2)`: look at character w1
    tilted up or down (`antoinette up` is the common case; *likely* she is
    shorter or seated).
  * w2 numeric: `0x441560(char, wtoi(w1), wtoi(w2), 0)`.
  * only w1: `0x4415b0(char, w1, 0)`: turn the head toward w1.

  The character is remembered in +0x5f8. When the box starts sliding out
  (0x43bc90), every remembered character gets `0x4415b0(char,"home",0)`.
* Voice (0x43c260): stop the previous voice if any. Path =
  `"sound\vo\" + line+0x111e`, then `"_%d"` with the alternative index if it is
  > 0, then `".wav"`. Example: `sound\vo\gen_locked01aa_1.wav`. Played with
  `0x424f90(path, 0, -1, 0, 1)`; the handle goes to +0x528.
* No voice (play failed, so +0x528 = 0): the mouth moves all along
  (`0x4046c0` starts `talk`).
  Text timer +0x28 = Duration's second number if > 0, else
  `wcslen(text)/15` seconds (integer division), at least 1.0.
* Voice playing: +0x28 = 0. Lip sync each frame (0x43d3c0): the voice level
  (`0x425140`) ≠ -1 and changed → `0x4046c0` (mouth on) if > 0, else
  `0x4046f0` (mouth off). See [characters.md](characters.md#talking).

### 4.3 Event and the event file, 0x43c100 (sure)

Reset the Event runner. If the **Event** column is not empty, queue it there.
Otherwise the file
`text\dialogue\Events\%s_%s_%s.txt` (Name of the *current part*, its Act,
code) is loaded into the Event runner if it exists (on disk, or in the
archive: the `acx` check). The code is part + thread + nest:

* part: `%d` of (code-99) if code ≥ 100, else `%c` of code+'a';
* thread: `%d` of (code-0x5f) if code ≥ 0x60; `z%d` of (code-25) if code ≥ 26;
  else `%c`;
* nest: `%c` of nest+'a'.

So `dia_arbuthnot_06a_a1b.txt` is part a, thread 1, nest b. The Event column
**replaces** the file; they never both run. The Event runner's character is
the speaker. It is updated every frame while the line plays, so its commands
run **alongside** the line (`anim agree`...). When the line ends,
`0x409810` **fast-forwards** it: every remaining event is ended and the next
one started, until the queue is empty.

Generated threads (exit, regreet, extras) keep their own voice names (from
their source rows) but look up event files under the current part's name.
This is a quirk; such files do not exist.

### 4.4 When a line ends (sure)

The update (0x43d430, called from 0x416d8f) ends the line (0x43c310) when:

* the voice stops playing (`0x425370` false), or
* with no voice, the text timer +0x28 runs out, or
* the player clicks (0x43d8c0) while not in choice mode, at least **0.5 s**
  after the line started (+0x30 ≥ [0x48260c]=0.5), and not in movie mode. The
  voice is stopped.

Nothing ends the line while the box is sliding, while the Prevent runner is
busy, or while a Povent or pause is pending.

### 4.5 Line end, 0x43c310 → 0x43c375 (sure)

1. Stop the voice, fast-forward the Event runner, stop the talking anim
   (`0x404760`).
2. Read the markers in **Povent** (`0x408e70`: case-insensitive substring
   search in each comma-separated command, honouring `(condition)` guards):
   `juncture`, `terminate`, `advance`.
3. If Povent is not empty:
   * `keepBox` = 1 if it contains `talk` or `leavebox`; `nobox` sets it back
     to 0.
   * +0x324 = 0 if it contains `walk` or `rotate` (the box is hidden while it
     runs), else 1.
   * Where it runs: if it contains `mainloop` or `terminate` (plain substring),
     or the conversation is about to run out (no next line, no other available
     thread (0x43b510), and no next part with `+` (0x43b4e0)), it is
     **inserted into the main script queue right after the current event**
     (`0x409440` / add / `0x409470`). Those commands then run after the `talk`
     event finishes. Otherwise it goes on the dialogue's Povent runner, which
     runs while the dialogue waits.
4. t.current = this line. next = `pickLine(t, nest+1, fwd=1)`; DM+0x18 = next.
   +0x534 = (next == 0 && plus) || advance.
5. Next exists and no juncture/terminate/+0x534: if the Povent runner is busy,
   +0x320 = 2 (continue when idle). Otherwise wait **0.4 s** (+0x6ec; 0 in
   movie mode or if the part's name contains `_movie`), then start the next
   line. Progress is **not** saved here.
6. Otherwise (the thread stops here):
   * status = 1, unless `juncture`. With `terminate`, status = 1 even if a next
     line exists.
   * No next line: if this line's Repeat ≠ -1, t.current =
     `pickLine(t, Repeat, fwd=0)` (resume point for later); else if t is `3`,
     t.current = a new random exit line; else t.current = 0.
     Next line exists (juncture/terminate/advance): t.current = next.
   * **Save** (if part < 99, t is not `3`, not movie mode):
     `0x410f50(Name, Act, part, thread, status, t.current ? t.current.nest : -1)`.
     A borrowed extra thread (t+0x18) is saved under (+0x6a4, `"0"`, +0x6e4).
   * Then: terminate or t = `3` → end the conversation
     (0x43b8f0(keepBox)). Else advance → mark the part finished (0x43b810).
     Then, if the Povent runner is busy, +0x320=3; else the menu (3.5).

### 4.6 Meaning of the Povent markers (sure for the code; the runner treats all of them as no-ops)

The runner's handlers for `juncture`, `terminate`, `advance`, `leavebox`,
`nobox` and `mainloop` just end the event (0x40a39b, 0x40b28b, 0x409fa4,
0x40a3ce, 0x40a8ec, 0x40a6e6). They mean something only as markers in
**Povent**. In the Event column they do nothing: the three `E=terminate` rows
in gen_characters have no effect.

* `juncture`: stop the thread here without finishing it. Its resume point is
  the next line, it stays offered, and the menu comes back. Not used in the
  data.
* `terminate`: finish the thread (status 1, resume at the next line if any)
  and end the conversation now. The Povent goes to the main queue, so
  `terminate, talk gen_giveevidence 1, talk dia_firstsolution a+ ...` chains
  new conversations after this one. (171 uses.)
* `advance`: finish the whole part and go on to part+1 (46 uses, in the
  solution conversations).
* `talk` / `leavebox`: keep the box. At the end, `0x43b8f0(1)` does not slide
  the box out, and +0x20 stays set, so the next `talk` reuses the box.
* `nobox`: overrides that; the box slides out.
* `walk` / `rotate`: hide the box while this Povent runs.
* `mainloop`: send this Povent to the main script queue even though the
  dialogue continues. Not used in the data.
* `_movie` is not a verb: a part whose **Name** contains `_movie` has no 0.4 s
  pause between lines.

---

## 5. Ending, and telling the runner

`0x43b8f0(keepBox)`:

* If active and keepBox == 0: the box slides out (+0x54a=2, from 768-h toward
  768 at 500 px/s), heads are turned home (0x43bc90). When the slide finishes
  (in 0x43d430): +0x20=0, both windows hidden, and `0x43b8f0(0)` again, which
  now takes the second branch.
* Else: +0x14 = +0x10 = 0; if keepBox == 0, +0x20 = 0. Then, if the Povent
  runner is idle, `0x43b9b0`: if the main runner's current event's verb is
  `talk` (wcsicmp), `0x409a80("talk", 0)`, i.e. **event done "talk"**. If the
  Povent runner is still busy, +0x320 = 1, and the update calls 0x43b9b0 once
  it is idle.

So the `talk` event (and `talk` inside Povent chains) waits until the box is
gone, or until the end for keepBox. Reset 0x43b970 (on `movie` done) clears
the active flags and resets all three runners. 0x43a600 (after a `cantalk`
dry run) clears +0x20 and the slide state.

Exit options: see 2.4. The exit thread is the last choice, and choosing it
plays Antoinette's goodbye line (`gen_<npc>_exit`, a random one of 4-5;
`gen_exit` when the NPC has none; `gen_solutionexit` after act 07a), then
ends. Exits exist only with gen (NPC clicks or `forcegen`).

---

## 6. The dialogue box UI

* Windows: 0x414190 loads `interface\dialoguewin%02d.png` for 01..05 into
  `[0x498818]+0xb8..+0xc8`, at x=0, y=768. Sizes: 01 = 1024x160 (the normal
  box), 02 = 1024x180 (after act 07a), 04 and 05 = 10x11 (the choice bullet,
  normal and highlighted). 03 is missing.
* The box's y = 768 - height, sliding at 500 px/s. Drawn (vtable +0xc) when in
  choice mode, or when subtitles are on (`[0x490068]`, 1 by default; *likely*
  the subtitles option), or when +0x544 is set. While the Povent runner works,
  it is drawn only if +0x324.
* The spoken line is drawn **only if subtitles are on (or ham radio)**,
  horizontally centred, vertically centred in the box: y = box top +
  (h - rows*rowHeight)/2, wrapped at 944 px, in the speaker's colour.
* Colours (fonts.txt, colour list from index 0): 6 "dialogue text when
  choosing", 7 "highlighted dialogue text when choosing", 8 "dialogue for main
  character", 9 "second character", 10 "third character". Font: the
  fonts' second (0x43dd1e and 0x43dd31 measure and draw with font 1;
  0x414c10 takes it from the fonts loaded from `fonts.txt` in their rows'
  order, 0x413b4c): "calisto mt 12, shadow, dialogue".
* Layout of choices: 3.4. Choices are picked by click (0x43d8c0: in choice
  mode, the line under the cursor → its thread → play), or by the mouse
  wheel moving the highlight (0x43d810, 0x43d850, from 0x416391 and
  0x4163b1) and the middle button taking it (0x43d890, from 0x4163c0; not
  the keyboard, as first thought). Clicks are ignored while the box slides,
  during the 0.4 s pause, and while Prevent or Povent run.
* Picture choices (act 8's evidence): a choice whose current line's Notes
  name a `.jpg`, `.png` or `.tga` has that picture too (0x43cf90). The n
  pictures sit in a row from x = (744 − (112n − 16))/2 + 280, 112 apart,
  42 pixels down the box (`[0x483098]` = 42.0); each is hit from 8 pixels
  before it to 8 after its 96 (0x43cdc3), tested before the text rows
  (0x43c810, then 0x43cce0 for the thread). The text rows are drawn as
  well. Checked against the recording (Part 10, 16:12 and 18:30): Reluva's
  box for the same choices (`awe-acts -3d -shot 1104c12`, `1104c17`)
  has its rows, bullets and pictures in the same places.
* Movie mode (0x43a610): the movie player starts the conversation named
  after the movie from `Text\Movies\<movie>.txt` (0x42ae7d) and then sets
  +0x6f0 and the box height 64. The window isn't drawn (0x43cffc skips it
  and the head), there is no 0.4 s pause between lines (+0x6ec = 0), no
  voice (0x43c6f0), and a line waits for the movie's clock to reach its
  Duration's start (+0x1118; its length is +0x1114). So the line shows
  centred in the bottom 64 pixels, in the speaker's colour, with Captions
  on. As it isn't a dry run, the talk-start inventory changes apply.

---

## 7. Lines heard: bookkeeping and "REQUIRED LINE NOT DELIVERED"

* Not tags: a separate list in the story state, `[0x498810]+0x2b8` (count
  +0x2bc), with records of 0x90 bytes: Name wchar[32], Act wchar[32] (+0x40),
  part code (+0x80), thread code (+0x84), word status (+0x88), int resume nest
  (+0x8c, -1 = none). Find: 0x411030. Set (replace): 0x410f50. Delete: 0x4110d0.
  Cleared at 0x4107a5, written to the save file at 0x41190e..., read back at
  0x412373. Nothing is written while `+0x2c0` (the story state's "frozen" flag)
  is set.
* Written only when a thread stops (4.5), and by `advance` (0x43b810: status 1,
  nest -1 for every thread of the part). Only lettered parts.
* The visit tags `"<scene> <name> <part>"` are ordinary tags (2.2).
* Conditions (`0x408636` in the condition evaluator): **`talk <name> <act>
  <code>`** → 0x439f60:
  * code of 3 letters (part, thread, nest): true if a record exists for
    (name, act, part, thread) and (status ≠ 0, or nest = -1, or saved nest >
    the given nest). Roughly: that line has been passed (the thread stopped
    after it).
  * 2 letters: true if a record exists with status ≠ 0 or nest -1 (the thread
    has finished).
  * 1 letter (a part) → 0x439e40: for threads a, b, c... (stopping at the first
    missing letter), every line with Optional == 0 must pass the 3-letter test.
    The first that fails logs **`REQUIRED LINE NOT DELIVERED: <line text>`**
    (0x43ebb0, the debug log) and the condition is false.
  * `!talk ...` negates, as for any condition.
* `cantalk <name> <code> ...` (0x407f24): a dry run of Start with +0x61c=1;
  true if it would start. Speaking is skipped; the visit tag is still set
  (9.7). Its words are parsed as `talk`'s, with gen on unless
  `nogen` (0x407f3a). Reluva's dry run (`DialogueManager::canTalk`) stops
  where the conversation would show its box, and puts back what it
  changed.
* The next condition verb after `talk` in that chain is `test` (0x40869a), a
  separate verb that was not read here.

---

## 8. Check against the tables

* **dia_observation (Act02a)**, started by `talk dia_observation a+`
  (acts.txt): parts a..f, each with an opening `1` by Poirot and lettered
  threads that start with Antoinette's line. Prevent/Povent carry `scene
  restaurant_table.NN` camera cuts before and after lines. This agrees with
  the code: the opening plays first, then the menu, the last choice plays by
  itself, and `+` chains the parts. Agrees.
* **Act06b dia_masterman**: Condition `talk dia_masterman 06b baa` on the first
  line of thread b/c means the thread is offered only after thread b/a has
  stopped past line a. This matches 3.3 and section 7. Its Povent
  `endpass, take passmasterman` and Event `wait 1, startpass masterman` fit
  4.3 and 4.5. Agrees.
* **gen_characters.txt**: `gen_<npc>_exit` parts `0`..`4`, thread `3` (2.4);
  `gen_<npc>_print` part a, thread a, with Condition `act 06b, has ribbon,
  has menucards` (gen extras); `gen_<npc>_notalk` part `1` (2.1).
  `gen_antoinette.txt`: `gen_regreet1/2` parts 0..3 thread `2`, `gen_exit`,
  `gen_solutionexit`, `gen_canttalkf/m`. Agrees.
* **Act07a dia_barnabyham**: own `1`, `2`, `3` threads, timer conditions,
  started from the radio (gen=0, so the own exit is kept). Agrees.
* **Act08a solutions**: picture choices via Notes; Povent `advance` /
  `terminate, talk ...`; window 02 after act 07a. Agrees.
* **Voice files**: `gen_locked01aa_1.wav`, `dia_tags03abaa_1..3.wav` match the
  `_%d` alternative rule; `com_belgrade01aa.wav` = `com_belgrade`+`0`+`1`+`a`+`a`.
  Agrees (`dia_observation02adce_01.wav` does not fit; probably a stray file).
* Repeat: 301 `a`, mostly on the last line of `*notalk` threads (repeatable
  random small talk), which fits 4.5. Duration is always 0 in the act tables
  (only movie subtitle tables would use it). Optional `1` on 25 lines.

---

## 9. Open questions

1. ~~`0x4194d0`, `0x419e80`, `0x419970` (inventory changes at Start)~~:
   0x4194d0 closes the inventory window (when it's open, +0x20, and the
   talk wasn't started from it, +0x44); for a name with neither `gen_` nor
   `com_` in it (0x43e750, any case), 0x419e80 puts the item in hand back
   and 0x419970 stops the item just taken blinking.
2. ~~`0x404720` / `0x404760` / `0x4046c0` / `0x4046f0` (talking state, mouth),
   `0x441270` / `0x441560` / `0x4415b0` (head): the parameter meanings are
   inferred~~: read. 0x404720 plays `talkidle1` unless `eavesidle` is
   playing; 0x404760 stops `talk` and `talkidle1`..`4` (and their
   `%stalkidle` forms); 0x4046c0 and 0x4046f0 play and stop `talk`, the
   mouth. 0x441270(w1, w2) sends the head home (0x429e70) and plays
   `head`+w1 for w2 seconds, eased 0.5; 0x441560(w1, w2, tilt) keeps the
   tilt (+0x14fa), makes the cell (w1, w2) a place in the world through the
   scene's grid (0x406dd0) and looks there (0x4414f0); 0x4415b0(w1, tilt):
   `home` sends the head home, an object's name looks at it (its +0x10,
   +0x18), anything else is a number (0x43e310) of degrees from the body's
   heading. These are `turn`'s forms ([characters.md](characters.md#turn-the-head)).
3. ~~`word[0x4986f8]` (enables exit variant 4)~~: the time of day (set by
   0x402800 with the `time` verb, read by the night test 0x4027b0). Above 0,
   the fourth goodbye drawn becomes the fifth: "Good evening" for "Good
   day" (`gen_arbuthnot_exit` 3 and 4).
4. ~~`[0x490068]` is assumed to be the subtitles option~~: it is the
   `Captions` setting (read and written under that name, 0x42ba10,
   0x42bba4).
5. ~~`0x43cb70` vs `0x43cc30`~~: the window procedure divides
   `WM_MOUSEWHEEL`'s delta by 120 (0x40142d); each notch forward calls
   0x43d810 → 0x43cc30 (up the list), each back 0x43d850 → 0x43cb70 (down
   it). `[0x48b08c]`/`[0x48b090]` are the mouse's place, set by
   `WM_MOUSEMOVE` (0x401495) and kept within x 1004 and y 748, or 758
   while a conversation is on (+0x20); clicks use them (0x416036).
6. ~~Start leaves +0x20 = 1 when the named thread does not exist~~: so it
   does, and it matters. A part of one thread with no extras is played
   from thread `a` (0x43a835: code 0, whatever its one thread is), so a
   part with only an opening fails (0x43aaa3, a plain return 0: no `+`
   from there). Two do in the install, both clicks on a character in 6B:
   `dia_hubbardsalon a` and `dia_arbuthnotshoes a`. The `talk` then falls
   back to the "nothing to say" line (gen is 1), whose Start resets +0x20.
   The walkthrough recordings have it so: Hubbard's "I'm not in the mood
   to talk right now" in Salon_02 in 6B (Part 7, 6:20.9) and Arbuthnot's
   (Part 7, 0:52.7), found by matching the voice files against the
   videos' sound; their openings ("Have you enjoyed rummaging around in my
   room?", "See here, what do you think you're playing at?") are never
   heard. With gen 0 the dialogue would be left active; nothing in the
   install does that.
7. ~~`cantalk` dry run~~: Start runs on as far as for a real talk; only
   the inventory's changes, the required-line message (0x439ce5) and the
   line's playing (0x43bd3d) look at +0x61c, and cantalk's 0x43a600 only
   clears +0x20 and the slide (+0x54a), and only when Start succeeded. What
   lasts is the visit tag (2.2, step 9): a cantalk that would open the
   menu marks the character met this visit, so the talk after it can
   regreet. The rest (exit and regreet threads, statuses) is built again
   by the next Start.
8. ~~`+0x618`~~: written only with 0 (0x438808, 0x438d88, 0x43b93f), so
   its branches in 0x43b8f0 and 0x43c35d are dead code.
9. ~~The visit tag's scene name lower-cased~~: 0x472e21 is `wcsrchr`, and
   0x43e9f0 copies what follows the last `\`: the scene file's name, its
   case kept. Tags are looked up without regard to case (0x47fa1b,
   `_wcsicmp`), so that comes to the same.
10. The update order between the Povent runner and the pending action
    (+0x320) within one frame is as read from the code (a fixed order in
    the frame's update, no branch between them); it hasn't been watched
    in the running game. A difference would last one frame, with nothing
    to see, so it's left as read. If the other AWE games give a reason to
    run the original under a debugger (Wine here, or x64dbg on Windows),
    two breakpoints would settle it.


## In Reluva

`src/engines/awe/dialogue.cpp` follows this: `DialogueBook` reads the
tables into parts, threads and lines with the game's codes; the
`DialogueManager` plays them with three script runners of its own
(Prevent, Event, Povent), keeps progress in the `GameState` for the
`talk` condition, builds the exit and "again" threads and the generic
extras for characters clicked, and slides the box in and out. The
Scenes page is its host: it plays the voices (`Sound/VO`), draws the box
(`src/engines/awe/dialogue_box.cpp`) with act 8's picture choices,
and passes on clicks, the wheel and the middle button. The speakers'
heads and mouths move, the Captions option hides the said lines, and a
talk's start closes the inventory and lets go of the item in hand
(`talkStarting`). Movie subtitles are drawn as the movie mode draws them
(timed by the movie's clock rather than run through the manager, which
comes to the same: no voice, no pause, no clicks), and starting them
counts as a talk start.

Found by the playthrough search (acts.md): Reluva had counted every
available extra and thread at a part's start and in the menu, also those
with no line to offer (step 8). A character's fingerprint offer, whose
first line wants the ribbon and the menu cards, so kept a part that had
been heard going, and `talk dia_hubbard a+` never went on to part b.
