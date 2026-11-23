# Star Trek: Armada: research notes

Working notes, kept as they were written: they don't follow the page
templates, and they may hold guesses since settled or corrected. What the
engine relies on is on its pages in [formats/](../formats/) and
[systems/](../systems/), each with its source.

The names in them are the program's own, as its export table and its
run-time type information give them, made readable with LLVM's
`llvm-undname`. Each note says the command that makes it from your own
copy.

| File | Contents |
|---|---|
| [script-api.md](script-api.md) | The 213 functions and variables of `Armada.exe` that the missions' and the computer player's DLLs import, with how many import each |
| [classes.md](classes.md) | The 459 classes the program's run-time type information names |
| [startup-trace.md](startup-trace.md) | The retail program's start-up in the emulator: the files it opens, in order, and where the run has stopped so far |
