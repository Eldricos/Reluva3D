# id Tech 3: shader scripts (`scripts/*.shader`, `shaders/*.shader`)

> **Status:** decoded · **Code:** `src/engines/idtech3/formats/shader.cpp`, `world_renderer.cpp` · **Checked on:** generated test maps and the retail Elite Force maps; `reluva-tool idtech3 shader-check` on Elite Force's shaders: 1872 in 1.0, 1891 in 1.2, 2059 with the Expansion Pack, and Jedi Outcast's 1187 in 1.02a (November 2026) · **Confidence:** confirmed unless marked

## Overview

Shader scripts say how each surface is drawn: a file is a list of shaders, each with its stages, drawn as passes.

## Layout

Quake III and Elite Force read the scripts in `scripts/`; Jedi Outcast
reads them in `shaders/` (its paks have none in `scripts/`, which holds
its ICARUS scripts) *(inferred)*. Reluva reads each game's folder
(`GameInfo::shaderDir`).

Tokens as in [text.md](text.md). A file is a list of shaders:

```
textures/borg/floor          // the name: an image path without extension
{
    surfaceparm metalsteps   // shader-level directives, one per line
    {                        // a stage: one rendering pass
        map $lightmap
    }
    {
        map textures/borg/floor.tga
        blendFunc filter
    }
}
```

When several files define a name, the first one read wins. Reluva reads
the files in name order. Names are looked up without their extension, as
the original does: a model surface naming `models/x/y.tga` uses the script
`models/x/y` if there is one, and only otherwise the image.

A surface whose shader name has no definition is drawn with the image of
that name (`.tga`, else `.jpg`) multiplied by its lightmap.

### Interpreted

| Where | Directive | Meaning |
|---|---|---|
| shader | `surfaceparm <name>` | a flag for the compiler and game; `nodraw` surfaces aren't drawn |
| shader | `skyParms <farbox> <cloudheight> <nearbox>` | the shader is a sky; `-` for none |
| shader | `fogParms ( r g b ) <distance>` | a fog volume |
| shader | `cull front\|back\|none` | which side is hidden (`disable`, `twosided` = none) |
| shader | `deformVertexes wave <div> <func> <base> <amp> <phase> <freq>` | along the normals, phase offset by (x + y + z) / div |
| shader | `deformVertexes bulge <width> <height> <speed>` | along the normals by height * sin(s * width + time * speed) |
| shader | `deformVertexes move <x> <y> <z> <func> ...` | the whole surface by the vector times the wave |
| shader | `deformVertexes normal <amplitude> <frequency>` | the positions stay; each normal gets amplitude × noise added on each axis, the noise a 4D value noise (random values from −1 to 1 at whole-numbered points, blended in straight lines) of the position × 0.98 (x offset by 0, 100, 200 for the three axes) and the shader's time × frequency; then made unit length. It changes only lighting and environment mapping: a shimmer |
| shader | `sort`, `polygonOffset`, `nomipmaps`, `nopicmip`, `portal` | see above |
| stage | `map <image>` / `clampMap <image>` | the stage's texture; `$lightmap` is special, and `$whiteimage` and `*white` both name a plain white image |
| stage | `animMap <fps> <image>...` | frames played at `fps` |
| stage | `clampAnimMap`, `oneshotAnimMap` (Raven's, Jedi Outcast on) | as `animMap`, its images clamped; or played once from the shader's time, then held on the last |
| stage | `blendFunc add\|filter\|blend` or `<src> <dst>` | `GL_ONE GL_ONE`, `GL_DST_COLOR GL_ZERO`, `GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA`, or explicit factors |
| stage | `rgbGen identity\|identityLighting\|vertex\|exactVertex\|oneMinusVertex\|wave ...\|const ( r g b )\|entity\|oneMinusEntity` | the colour (lightingDiffuse uses the vertex colours for now) |
| stage | `alphaGen vertex\|oneMinusVertex\|wave ...\|const <a>` | the alpha (`portal`, `lightingSpecular`, `entity`: opaque for now) |
| stage | `tcGen base\|lightmap\|environment\|vector ( ) ( )` | where texture coordinates come from |
| stage | `tcMod scroll\|scale\|rotate\|stretch\|transform\|turb\|entityTranslate` | texture coordinate animation, in order |
| stage | `alphaFunc GT0\|LT128\|GE128\|GE192`, `depthFunc`, `depthWrite`, `detail` | see above; `GE192` is Raven's (Jedi Outcast's grass sprites use it): alpha at least 0.75 (both programs parse it; jk2sp.exe 0x47da90 and 0x45c3ed) *(confirmed)* |

Waves are `<func> <base> <amplitude> <phase> <frequency>`, func one of
`sin`, `triangle`, `square`, `sawtooth`, `inversesawtooth`, `noise`.

## Behaviour

### How the renderer draws a shader

Read from the GPL Quake III Arena renderer (`tr_shader.c`,
`tr_shade_calc.c`, `tr_sky.c`, `tr_bsp.c`) for behaviour; Reluva's code
(`src/engines/idtech3/render/world_renderer.cpp`) is written from this description.

- **Stages** are drawn in order, each a pass over the surface.
- **Depth:** a stage writes depth unless it blends (anything but
  `GL_ONE GL_ZERO`), or unless it says `depthWrite`. `depthFunc equal`
  draws only where the depth matches.
- **Colour:** without `rgbGen`, white. Without `alphaGen`, opaque, but
  `rgbGen vertex` takes the vertices' alpha too unless an `alphaGen` came
  before it (the GPL Quake III and Jedi Outcast renderers both do; Jedi
  Outcast's `white`, blended with only `rgbGen vertex`, is how its menus
  draw their see-through highlights).
- **Texture coordinates:** without `tcGen`, the lightmap coordinates for a
  `$lightmap` stage and the texture coordinates otherwise. `tcMod`s apply
  in the order written.
- **Lightmaps:** `$lightmap` on a surface without a lightmap is white.
  Lightmaps and vertex colours are stored at half brightness: on loading,
  each colour is doubled, and a component over 255 scales the whole colour
  down to keep its hue. (The original doubles or quadruples, depending on
  its gamma mode; doubling matches its windowed look. Jedi Outcast's
  engine doubles only by its fullscreen gamma ramp:
  [rbsp.md](rbsp.md#in-reluva).)
- **Surfaces without a script** get implicit shaders: with a lightmap, the
  lightmap then the image of the shader's name with `blendFunc filter`;
  without one, the image times the vertex colours. Triangle soups always
  use their vertex colours. Any negative or out-of-range lightmap number
  means vertex colours.
- **Sort order** (lower draws first): `sort` if given (portal 1, sky 2,
  opaque 3, decal 4, seeThrough 5, banner 6, underwater 8, additive 10,
  nearest 16, or a number). Otherwise portal 1, `polygonOffset` 4, and
  if the first stage blends and some stage does, 5 when that stage writes
  depth or 9 when it doesn't; everything else 3.
- **Sky:** the far box is drawn on a box around the camera, the images
  `<name>_rt _bk _lf _ft _up _dn` on its sides. The sky shader's stages
  are the cloud layers, drawn over every side but the bottom. Their
  texture coordinates come from where each direction meets a dome
  `cloudHeight` (default 512) above the camera on a world of radius 4096:
  the arc cosines of the normalized hit point's x and y.
- **Fog:** a surface in a fog volume gets a last pass of the volume's
  `fogParms` colour, more opaque with depth along the view, fully at its
  distance. As the original's, when the eye is outside the volume the fog
  thins towards the volume's surface (its plane). Jedi Outcast's engines
  choose which shaders get the pass by their sort (jk2sp.exe 0x47f9ea,
  jk2mp.exe 0x48fe03): one sorting as opaque or before, drawn where the
  surface is (depth equal); else one with `surfaceparm fog`, drawn over
  it; any other (glass, glows, most effects) none *(confirmed)*. Elite
  Force's choice isn't read; Reluva gives its shaders every pass.
- **Pictures** (menus, the HUD): a shader registered with
  `R_RegisterShader` keeps its mipmaps, so a picture drawn smaller than
  its image shrinks through them; one registered with
  `R_RegisterShaderNoMip` has none. A name's first registration decides,
  as the renderer keeps the shader it made then *(inferred, from Quake
  III's GPL source; the calls are separate in every program)*.
- **Autosprites:** the level's `autosprite` surfaces are kept aside and
  rebuilt as they're drawn, each quad facing the camera round its middle;
  `autosprite2` quads turn about their long axis (the fire shaders). They
  work in mirrors too. Models don't use them.
- **Portals and mirrors:** as the original's renderer. The view from a
  mirror's reflection, or a portal's camera, is drawn first inside the
  surface's shape (marked in the stencil) and clipped at its plane; then
  the surface and the main view. See
  [idtech3.md](../README.md) (Renderer).

## In Reluva

Not drawn yet: `deformVertexes text*` and `GL_SRC_ALPHA_SATURATE`;
autosprites on models (no Jedi Outcast or Elite Force model uses one:
their four autosprite shaders are the level's flares and lights).
`projectionShadow`, the shadow mode 3 shader, is drawn for Jedi Outcast's
single player: each opaque surface of a model flagged with a shadow plane
(render flag 0x100, its height at 24 in `refEntity_t`), unless it's the
player's own first-person body, is drawn again in it, flattened along
the model's light onto the plane, in the model's own space with the
world's up as the ground's normal and the light bent up to at least 0.5
along it (jk2sp.exe 0x4747b4, 0x46759a, 0x45b64e; the deform 0x480860)
*(confirmed)*. Reluva does it with one matrix before the model's
placement rather than moving each point. `reluva-tool idtech3 shader-check <game folder>` lists
what a game's scripts use that isn't handled, including directives the
parser doesn't know (game additions).

Surface sprites (`render/surface_sprites.cpp`, used by the world
renderer) are placed once as the level loads, on the level's own
surfaces (not doors' and lifts'), and each frame those within reach of
the eye are turned, swayed and faded. Where they differ from the
original's:

- Each sprite's place, size, flip, lean and fade point come from the
  original's table of 256 random numbers (jk2mp.exe, 0x4ead20), walked as
  it walks it: from a place in the table (the low byte of the sum of the
  triangle's corners' x and y) by a step (the low byte of corner 1's x,
  corner 2's y and corner 3's z, made odd and at least 3), each sprite's
  cell on a grid spaced by the density over Quake III's fast inverse
  square root of the triangle's area (`Q_rsqrt`, 0x4339e0, whose small
  error sets how many cells there are). As the GPL source has it,
  vertical sprites draw both their cell's draws before checking them,
  oriented ones check each as drawn and step once more after the fade's
  draw, and effects find a new spot each life from the time. jk2mp.exe's
  three routines read the same table, make the step odd (0x496615,
  0x49738f, 0x497c51), cut floats to bytes with `_ftol` and space by
  `Q_rsqrt`; the order of their draws wasn't traced. So sprites stand where the
  original's do, save where the original's sums, in the x87's longer
  floats, round otherwise (a point right on a triangle's edge), and on
  curves, which Reluva cuts into triangles its own way.
- On curves, sprites are placed on the original's triangles: the grid
  turned on its side when taller than wide, each row reversed
  (`R_SubdividePatchToGrid`'s flip), each square's two triangles with
  their corners in the original's order (`RB_SurfaceGrid`); Reluva draws
  the curve itself with triangles of its own, at full detail. Far off,
  the original leaves out rows and columns of a curve, and the sprites
  stand on what's left, so they move as the eye does; Reluva's do the
  same. Each row and column keeps the error the original's subdivision
  gives it (jk2mp.exe 0x472a60: 1 over how far the curve strayed from
  straight where it was cut, set where the run's middle is, or where a
  cut's middle goes, and never moved along as columns are added, so a
  middle cut again is left at 0 and always drawn); stitching gives an
  added one the error of the other patch's row or column through its
  point. Each view keeps those whose error is no more than
  `r_lodCurveError` (250, kept between runs) over how far the curve's
  group is in front of the eye past its radius, at least 1
  (`LodErrorForVolume`, 0x4942e0; RB_SurfaceGrid, 0x493f50), and the
  sprites for that set are placed the first time a view needs them. No
  shipped level has sprites on a curve (they're all on flat surfaces of
  four Yavin levels), so this matters only for mods' levels.
- The wind is Jedi Outcast's renderer settings', as its
  `R_SurfaceSpriteFrameUpdate` works it out once for each frame's time:
  `r_windSpeed` blowing towards `r_windAngle`, damped towards each change
  by `r_windDampFactor` per 50 ms, gusting every `r_windGust` seconds or
  more (the gaps and strengths random: the engine's `flrand`, jk2mp.exe's
  `Q_flrand` on the seed its weather shares, 0x495dab, and jk2sp.exe's
  over `rand()`, 0x488412; `raven/random.h`), and
  `r_windPointForce` pushing grass within 750 units of `r_windPointX`,
  `r_windPointY` away and down. Grass leans with it and bobs; effects
  drift along it over their lives. Weather sprites show as much as
  `r_surfaceWeather` says (their density divided by it), none at 0.
  jk2sp.exe and jk2mp.exe register all of these with no wind and no
  weather (`r_windDampFactor` 0.1), as Reluva does. Rain and snow set them
  while they fall, in both programs
  ([../games/jk2/systems/weather.md](../games/jk2/systems/weather.md)).
- With a field of view narrower than the first ordinary one drawn
  (between 50 and 135 degrees), sprites reach farther: their fade
  distances times the tangent of half the first's over the tangent of
  half this one's.

## Sources and checks

Every Quake III directive is parsed and drawn, except those listed at the
end. The retail Elite Force maps look as in the original, and
`shader-check` finds **no directive outside Quake III's set** in Elite
Force's shaders: 1872 in 1.0, 1891 in 1.2, 2059 with the Expansion Pack.
All are drawn but `deformVertexes projectionShadow` (1, the original's
shader for r_shadows 3's projected shadows); autosprites (in 1.2, 2:
`textures/scavenger/fire1` and `fire2`; the Expansion Pack adds
`textures/singularity/flare_red` and `flare_blue`) on the level's surfaces,
not on models (none uses them there); `deformVertexes normal` (2, the CTF
flags) as below.

Jedi Outcast's 1187 shaders (1.02a) use 18 directives Quake III doesn't
have, as the GPL Jedi Outcast renderer reads and draws them:

- **Drawn:** `oneshotAnimMap` (31) and `clampAnimMap` (1), animMaps
  whose frames play once and then hold the last, or are clamped as
  `clampMap`'s; surface sprites (`surfaceSprites` and its `ss*` options,
  41 shaders; next section); `videoMap` (13), a stage showing a RoQ video
  (`video/<name>.roq`, [roq.md](../../../common/video/roq.md)) on a loop,
  silent, from when it's first drawn. Without its video, the stage and
  every stage after it are left out, as the original's: its cinematic
  player can't open the file (`CIN_PlayCinematic` gives −1), so the stage
  has no image and is made inactive, and the finished shader keeps only
  the stages before the first inactive one (`GeneratePermanentShader`);
  nothing left, nothing is drawn. 9 of 1.02a's 13 name videos the release
  doesn't have at all (lightspeed, Kyle rummaging, Morgan's four, a CTF
  test), each its shader's only stage, so neither the original nor Reluva
  draws those shaders; the other 4 (the opening logos, Mon Mothma's
  three) play.
- **Read, nothing to draw:** `notc` (12) keeps the shader's textures from
  being compressed, which Reluva never does; `lightColor` (2) only the map
  compiler reads; `sun` (11, sky shaders) sets the direction light falls
  on models from in a level without a light grid, which none of Jedi
  Outcast's levels lacks.

The rest are Quake III's, drawn as above: `deformVertexes autosprite` (4,
flares), `projectionShadow` (1, not drawn).

### Raven's surface sprites

A stage with `surfaceSprites <type> <width> <height> <density>
<fadedist>` isn't drawn over its surface: its image is drawn as small
quads scattered over the surface's triangles (grass, ferns and vines on
Yavin's levels, bubbles, rain splashes), after the shader's other stages.
Only a shader's second stage on can be one. The options, each on a line
of its own in the stage (a value out of range is ignored):

| Option | Meaning |
|---|---|
| `surfaceSprites vertical\|oriented\|effect w h density fadedist` | the type; size in units; about how far apart; where they begin to fade (at least 32). Resets the options below |
| `ssFademax d` | where they're gone (past fadedist; default fadedist × 1.33) |
| `ssFadescale s` | how much wider they grow as they fade |
| `ssVariance w h` | up to this much bigger (1 = double), each its own, width and height by the same draw |
| `ssHangdown`, `ssFaceup`, `ssAnyangle` | which way: hanging under a ceiling (vertical), lying flat a unit above the surface (oriented, effect); only the first given counts |
| `ssWind s`, `ssWindidle s` | how far they lean in the wind, how far they sway with none (`ssWind` sets both unless `ssWindidle` came first) |
| `ssVertskew s` | a lean of its own each, up to s × its height |
| `ssFXDuration ms`, `ssFXGrow w h`, `ssFXAlphaRange a b` | an effect's life, how much it grows over it, its alpha from start to end |
| `ssFXWeather` | an effect shown only while it rains or snows |

- **Where they stand.** On triangles whose corners all face up (normal's
  z at least 0.5; for flat ones 0.99; for hanging ones down, z at most
  -0.5). Each triangle is cut into a grid of cells, `density` divided by
  the square root of its area seen from above; a sprite stands somewhere
  in each cell, dropped if that's outside the triangle.
- **Vertical** sprites stand `height` up from the point (or hang down),
  turned about their height to face the camera, each a little off it in
  one of four ways in turn (10 and 30 degrees either side), the far top
  corner pushed a fifth of the width back; their tops sway in a small
  circle, `height × windIdle × 0.075` across, at `(x + y) × 0.02 + time ×
  0.0015` radians (time in milliseconds). **Oriented** ones face the
  camera wholly. **Effects** are oriented ones that live `ssFXDuration`
  each, starting at their own times, standing somewhere else in their
  cell each life, growing by `ssFXGrow` and fading through
  `ssFXAlphaRange` (in then out, when they fade to nothing from a size of
  their own).
- **Fading.** Each corner's nearness is 1 at fadedist and 0 at fademax,
  by its squared distance from the eye; a sprite's, its corners' by its
  place; each fades out from its own point in the last part of the range
  (250 units' share of it, or all of it if the range is shorter), so a
  field thins out rather than ending in a line.
- **Light.** Grey, the surface's vertex colour's blue at the point; half
  as strong around a mid-grey when the stage adds its light (`GL_ONE
  GL_ONE`), which then fades by brightness instead of alpha. Half are
  drawn mirrored; both sides show.

Sources: the Quake III Arena shader manual id Software published for map
makers, and the GPL Quake III Arena source (`code/renderer/tr_shader.c`)
for how the engine reads it; Raven's directives as the GPL Jedi Outcast
renderer reads and draws them (`tr_shader.cpp`, `tr_shade.cpp`,
`tr_surfacesprites.cpp`, `tr_image.cpp` for `noTC`, `tr_light.cpp` for
the sun). Surface sprites: a unit test places and draws them on a made-up
floor; Yavin's levels were looked at in `reluva-viewer`, not yet against
the original.
