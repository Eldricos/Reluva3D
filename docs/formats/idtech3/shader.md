# id Tech 3: shader scripts (`scripts/*.shader`)

Status: **structure implemented** (`src/engines/idtech3/shader.cpp`); only
the directives below are interpreted, every other one is kept as written.
Not yet checked against Elite Force's scripts.

Sources: the Quake III Arena shader manual id Software published for map
makers, and the GPL Quake III Arena source (`code/renderer/tr_shader.c`)
for how the engine reads it.

## Layout

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
the files in name order.

A surface whose shader name has no definition is drawn with the image of
that name (`.tga`, else `.jpg`) multiplied by its lightmap.

## Interpreted so far

| Where | Directive | Meaning |
|---|---|---|
| shader | `surfaceparm <name>` | a flag for the compiler and game, e.g. `nodraw`, `trans`, `metalsteps` |
| shader | `skyParms ...` | the shader is a sky |
| stage | `map <image>` / `clampMap <image>` | the stage's texture; `$lightmap` and `$whiteimage` are special |
| stage | `animMap <fps> <image>...` | frames played at `fps` |
| stage | `blendFunc add\|filter\|blend` | shorthands for `GL_ONE GL_ONE`, `GL_DST_COLOR GL_ZERO`, `GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA` |
| stage | `blendFunc <src> <dst>` | explicit OpenGL blend factors |

Still to interpret for the renderer: `rgbGen`, `alphaGen`, `tcGen`,
`tcMod`, `alphaFunc`, `depthWrite`, `depthFunc`, `cull`, `deformVertexes`,
`sort`, `fogParms`, `polygonOffset`, `nopicmip`, `nomipmaps`, and any
directives Elite Force adds (to be found in its scripts).
