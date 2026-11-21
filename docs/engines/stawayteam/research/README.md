# Star Trek: Away Team: research notes

Working notes, kept as they were written: they don't follow the page
templates, and they may hold guesses since settled or corrected. What the
engine relies on goes on its pages in `formats/` and `systems/`, each
with its source, as they're written.

The notes are about the US English retail release (`Star Trek.exe`,
2,437,120 bytes, MD5 `cd908f2d79a8a331045fa1d80414ef0a`). Addresses are
the executable's virtual addresses (image base 0x400000). Facts are marked
with the standard's confidence words
([DOCUMENTATION_STANDARD.md](../../../DOCUMENTATION_STANDARD.md#sources-and-confidence))
where they aren't read straight off the files.

| File | Contents |
|---|---|
| [file-survey.md](file-survey.md) | What the install and the CD hold: the program's imports, `DATA.dat`'s layout and contents, and a first look at each kind of file in it |
| [binary-objects.md](binary-objects.md) | How the game saves objects in binary (`.seq16`, `.mdl16`, `.way`): its classes and their properties, the writer and reader, each kind of property, the classes with data of their own |
| [classes.txt](classes.txt) | Data: every class and its properties, written by `tools/stawayteam-re/stat_classes.py` and read by `tools/stawayteam-re/stat_objects.py`; its format is in its own first lines and in `stat_classes.py` |
