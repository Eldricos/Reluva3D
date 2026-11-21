# Star Trek: Away Team reverse-engineering tools

The scripts that worked out how Star Trek: Away Team saves its objects,
kept so the findings in [docs/engines/stawayteam/](../../docs/engines/stawayteam/)
can be checked and extended. How they were used:
[REVERSE_ENGINEERING.md](../../docs/engines/stawayteam/REVERSE_ENGINEERING.md).

They are research scripts, not part of Reluva: not built by CMake, not
tested. They read (never write) a copy of the game you own; nothing of the
game is in this folder.

## Setting up

Python 3.8 or later, with nothing but its standard library, and GNU
objdump (binutils) for the disassembly `stat_classes.py` reads.

## The scripts

| Script | What it does |
|---|---|
| `stat_classes.py` | Reads each class's registration out of `Star Trek.exe`'s disassembly and writes the schema: every class, its version, parent and properties in order, each with the kind of value it saves (`docs/engines/stawayteam/research/classes.txt` is its output for the US English release) |
| `stat_objects.py` | Decodes every sequence, model and waypoint map in `DATA.dat` with that schema, and says whether each comes out to its last byte with every object's version its class's |
| `stat_table.py` | Writes Reluva's table of the classes (`src/engines/stawayteam/formats/class_table.cpp`) from the schema |

```sh
objdump -d -M intel --no-show-raw-insn "Star Trek.exe" > star-trek.asm
python3 stat_classes.py "Star Trek.exe" star-trek.asm --data DATA.dat -o classes.txt
python3 stat_objects.py classes.txt DATA.dat
python3 stat_objects.py classes.txt DATA.dat "Characters/Klingon.mdl16" --show
python3 stat_table.py classes.txt -o ../../src/engines/stawayteam/formats/class_table.cpp
```
