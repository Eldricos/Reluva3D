# Liberation Sans

The Liberation Sans typeface, regular and bold, version 2.1.5, by Red Hat
and Google (from Ascender's Arimo), <https://github.com/liberationfonts>,
under the SIL Open Font License 1.1 (see `LICENSE`, upstream's own).
`LiberationSans-Regular.ttf` and `LiberationSans-Bold.ttf` are included
unchanged as Ubuntu 24.04's `fonts-liberation` package (1:2.1.5-3)
installs them; their name tables say `Version 2.1.5`.

Its letters are as wide as Arial's, so text laid out for Arial keeps its
lines. Reluva draws Windows programs' GDI text in it
(`src/emulation/x86/gdi.cpp`), where they ask for Arial or another
proportional face, built into the program by `cmake/ReluvaEmbed.cmake`.
