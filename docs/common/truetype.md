# TrueType fonts and their hinting

## What it is

The games' own `.ttf` files (And Then There Were None's `innswfte.ttf`,
`rulyrrg_.ttf`, `arial.ttf` and `arialn.ttf`; Orient Express's) as Windows
draws them. A TrueType font carries programs as well as outlines: a font
program (`fpgm`) that defines functions, a control value program (`prep`)
run at each size, and each glyph's own instructions. Windows runs them to
fit the outline to the pixel grid before it fills it ("hinting"), so a
stem lands on whole pixels and comes out solid, at a whole number of
pixels to the em. Unhinted, as stb_truetype draws them, the same strokes
fall between pixels and come out thin and grey.

## What Reluva supports

`graphics/truetype.cpp` draws the text: stb_truetype (`third_party/stb`)
reads the font and fills the outlines. A font with programs is hinted
first by `graphics/truetype_hinting.cpp` (`ttf::Hinter`), Reluva's own
interpreter of the TrueType instructions, written from the specification:
Apple's TrueType Reference Manual and Microsoft's OpenType pages on `glyf`
and the instruction set. Then:

- **The size** is the nearest whole number of pixels to the em to the one
  asked for, as Windows scales a hinted font.
- **The outline** is the hinted points (in 64ths of a pixel), the glyph's
  origin at its first phantom point; its **advance** the hinted distance
  between the two phantom points, in whole pixels.
- **The coverage** is raised to 1/2.2 before it's blended: Windows'
  smoothed text is heavier than the share of each pixel the outline
  covers *(seen, not traced)*: below.

The interpreter has the whole instruction set but for the variation
instructions (GETVARIATION, GETDATA), which no font of the time has. It
answers GETINFO as Windows' rasterizer of the time: version 35, drawing
in grey levels. Each glyph starts with the projection, freedom and dual
vectors on the x axis, the rest of the graphics state as the control
value program left it. A program that fails leaves the glyph as scaled,
as the rasterizer does. Composite glyphs have their parts hinted on their
own, placed (their offsets rounded to the grid where they ask), then their
own program run.

## Sources and checks

- **Against FreeType**, as a black box: a program using the system's
  FreeType library (its version 35 interpreter, the one that follows the
  Windows rasterizer of the time) and Reluva's interpreter each hinted
  every glyph of the four fonts above at every size from 8 to 40 pixels
  to the em, and their points were compared. They agree to within an
  eighth of a pixel on all but 0 to 5 glyphs of 168 to 400 at almost
  every size, those a pixel apart; and on all of their advances but a
  handful. The one size where they part more, `rulyrrg_.ttf` at 24
  pixels, is where a control value falls on exactly half a pixel. Its
  source wasn't read.
- **Against the original game**: And Then There Were None's options screen
  in a screenshot of the original, its labels (`innswfte.ttf` at 18) in
  the same places, the same width, solid where the original's are. The
  text's pixels differed from the original's by 59 grey levels on average
  unhinted, 9 hinted, and 6 with the coverage at 1/2.2: the best of
  powers from 1/1.8 to 1/3.0 (1/2.0 to 1/2.4 all within 0.1 of it). Drawn
  black and white instead (no smoothing) it was 9.3. The dialogue's lines
  (Arial Narrow) against gameplay footage: the same widths, to a pixel.
- **The unit tests** (`common.unit.truetype`) build a font of their own:
  a stem rounded to the grid by MDAP and MIRP and the rest moved with it by
  IUP, control values changed by the control value program through a
  function, IF and ELSE on the size, and LOOPCALL; and a DELTAP at one size.

- **Confidence:** published (the specification) for the instructions,
  confirmed against FreeType's interpreter and the original's screen;
  the smoothing's 1/2.2 seen, not traced.

## Left to do

- **The same pixels on every platform.** The interpreter keeps its
  vectors in doubles, made with `std::hypot`, and the smoothing's table
  is made with `std::pow`. Fused multiply-adds are off everywhere
  (`CMakeLists.txt`), but maths libraries needn't agree to the last bit,
  so where a distance falls exactly on a rounding's edge a stroke could
  land a pixel apart from one platform to another (rarely). Checked on
  Linux only. To make it exact: the vectors in 2.14 fixed point and the
  arithmetic in whole numbers, as the specification has them (and
  Windows' rasterizer works), and the table written out as its 256
  values; then the FreeType comparison and the options screen measured
  again.

## Used by

- **`awe`**: both games' text: the dialogue, the interface, the menus
  ([../engines/awe/games/attwn/systems/interface.md](../engines/awe/games/attwn/systems/interface.md)).
