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
| stage | `alphaFunc GT0\|LT128\|GE128`, `depthFunc`, `depthWrite`, `detail` | see above |

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
- **Colour:** without `rgbGen`, white. Without `alphaGen`, opaque.
- **Texture coordinates:** without `tcGen`, the lightmap coordinates for a
  `$lightmap` stage and the texture coordinates otherwise. `tcMod`s apply
  in the order written.
- **Lightmaps:** `$lightmap` on a surface without a lightmap is white.
  Lightmaps and vertex colours are stored at half brightness: on loading,
  each colour is doubled, and a component over 255 scales the whole colour
  down to keep its hue. (The original doubles or quadruples, depending on
  its gamma mode; doubling matches its windowed look.)
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
  thins towards the volume's surface (its plane).
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

Not drawn yet: `deformVertexes text*` and `projectionShadow` (the
original's shader for its shadow mode 3); `GL_SRC_ALPHA_SATURATE`. `reluva-tool idtech3 shader-check <game folder>` lists
what a game's scripts use that isn't handled, including directives the
parser doesn't know (game additions).

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
have. Drawn: in stages, `oneshotAnimMap` (31) and `clampAnimMap` (1),
animMaps whose frames play once and then hold the last, or are clamped as
`clampMap`'s *(published)*. Read but not
drawn yet: `videoMap` (13, a stage showing a RoQ video, `video/<name>`;
the stage is left out until RoQ is decoded, the rest of its shader drawn).
Skipped and listed as unknown: on its surfaces `surfaceSprites` (41) with
`ssVariance`, `ssWind`, `ssWindIdle`, `ssFadeMax`, `ssFadeScale`,
`ssFaceUp`, `ssHangDown` and the effects' `ssFXDuration`, `ssFXGrow`,
`ssFXAlphaRange`, `ssFXWeather` (grass and weather drawn as sprites over a
surface, by their names *(inferred)*); `sun` (11) and `lightColor` (2) on
sky shaders, and `notc` (12) ([jk2 PLAN.md](../games/jk2/PLAN.md), phase
2). The rest are Quake III's, drawn as above: `deformVertexes autosprite`
(4, flares), `projectionShadow` (1, not drawn).

Sources: the Quake III Arena shader manual id Software published for map
makers, and the GPL Quake III Arena source (`code/renderer/tr_shader.c`)
for how the engine reads it; Raven's animMap forms as the GPL Jedi
Outcast renderer reads and draws them (`tr_shader.cpp`, `tr_shade.cpp`).
