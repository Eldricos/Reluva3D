# Birth of the Federation reverse-engineering tools

The scripts that made the samples Star Trek: Birth of the Federation's
formats were worked out from, kept so the findings in
[docs/engines/botf/](../../docs/engines/botf/) can be checked and
extended. How they were used:
[REVERSE_ENGINEERING.md](../../docs/engines/botf/REVERSE_ENGINEERING.md).

They are research scripts, not part of Reluva: not built by CMake, not
tested. They read (never write) a copy of the game you own; nothing of the
game is in this folder, and nothing they write (reports, the first bytes of
files, whole files) belongs in the repository.

## Setting up

Python 3.8 or later, with nothing but its standard library. Each script
takes the game's install folder (and the CD, where it reads one) and
writes a folder of samples; `--help` shows its options.

## The scripts

| Script | What it makes |
|---|---|
| `botf_sample.py` | A report on the install's and CD's files: sizes, the hashes detection uses, their first and last bytes, the archives' listings |
| `botf_res_sample.py` | Files of chosen kinds from `stbof.res`: their first bytes, and chosen ones whole (`--ext`, `--whole`, `--all-whole`); its docstring has the commands used for each format |
| `botf_probe.py` | Checks of the layouts against whole files: `alt.res`'s directory and every file decompressed, the `.snd` archives' entries |
| `botf_fonts.py` | The `.fnt` fonts: their headers, and two whole to study end to end |
