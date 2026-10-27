# Character lighting

*Murder on the Orient Express* lights its 3D characters with Direct3D 8's
fixed-function lighting, per vertex. The scene's `LITE` records are the
lights, the camera record holds the ambient colour, and each character
has its own material. The backgrounds are prerendered and aren't lit.

## How this was worked out

This comes from `motoe.exe` (English 1.0), read in a disassembler to make
Reluva work with the game. The Direct3D 8 device calls were found by
their vtable offsets (`SetMaterial` +0xa8, `SetLight` +0xb0,
`LightEnable` +0xb8, `SetRenderState` +0xc8). The code read:

- the `LITE` class: its constructor (`0x420cb0`), reader (`0x421ab0`),
  scene-entry method (`0x420ee0`, which calls `SetLight` and
  `LightEnable`), the night check it uses (`0x420eb0`), the flicker
  (`0x421400`) and the light-index picker (`0x436460`);
- the model draw's setup (`0x427160`): `SetMaterial`, `D3DRS_AMBIENT`,
  `D3DRS_LIGHTING`, `D3DRS_SPECULARENABLE`, `D3DRS_NORMALIZENORMALS`;
- the night test (`0x4027b0`) and the two night colours' initialisers
  (`0x4010a0`, `0x401100`);
- the scene reader's `OUTD` and `NVNT` (`0x435c44`), the camera's
  constructor (`0x406960`) and the common fields' reader (`0x40fa20`);
- the models' vertex format (`0x42675a`: position, normal, one texture
  coordinate; no vertex colour, so the material's colours count).

The colours, cone angles and attenuation values were then counted over
the 426 `LITE` records of the English install.

## The lights

Each `LITE` becomes one Direct3D light, at the lowest index no other
light in the scene uses. On entering a scene, every light is set
(`SetLight` with its `D3DLIGHT8`) and switched on if:

- its `on` field isn't 0,
- its condition holds (when the scene is entered),
- it isn't `shadowOnly` (38 lights are: they only place the
  [shadows](shadows.md)),
- and the scene isn't lit for the night.

A point or spot light with all three attenuation factors 0 gets
`att1` = 0.0001. Scenes have up to five lights, at most four of them on.
Types: 218 point, 27 spot, 181 directional. A typical scene has one
directional light (the main one, often warm, diffuse up to about 0.86)
and one dim point light; 170 of the 394 scenes have none, and their
characters get only the ambient light.

## The material and the render states

For each character, the draw sets (`0x427160`):

- the material from its common fields (D3DMATERIAL8 at +0x8c):
  diffuse, ambient, specular, emissive and power. Nearly every character
  has diffuse 1, ambient 0.498, specular 1, emissive 0 and power 5; a
  few have a little emissive (up to 0.2). Models attached to a character
  are drawn with its material;
- `D3DRS_AMBIENT`, below;
- `D3DRS_LIGHTING` from the object's `b0c` (1 everywhere);
- `D3DRS_SPECULARENABLE` on;
- `D3DRS_NORMALIZENORMALS` when the object is scaled.

The texture is modulated by the lit colour (the default texture stage).

## The ambient light

`D3DRS_AMBIENT` is a byte colour: each channel times 255, truncated.

- By day: the camera record's first three fields (`ambientR`..`B`,
  camera +0x180), 0.3125 grey when unset. Common values are 0.31 grey,
  0.55/0.63/0.67 (the snowy forest) and 0.31/0.27/0.20.
- At night, outdoors (`OUTD`): (0.2734375, 0.3515625, 0.546875), a blue.
- At night, otherwise: (0.234375, 0.2734375, 0.4296875).

## Night

The scene is lit for the night (`0x4027b0`) unless it's marked `NVNT`,
when the time of day is 2, or when it's 1 and the scene is marked `OUTD`
(the forest and the train's exterior). The acts' "night" (time 1) only
darkens those outdoor scenes; the indoor ones, and the Belgrade exterior,
keep their lights.

At night every `LITE` is switched off and the night ambient applies, so
the characters are a dark blue. The exception is a scene showing a
picture named `spotlight.tga`: its lights stay on, and indoors the
ambient is white. No scene of the install has one.

## The colour of a vertex

Direct3D's formula, with the material M, the ambient A and, for each
light on, its diffuse D, ambient La and direction L from the vertex
towards the light:

    colour = M.emissive + A·M.ambient
             + Σ strength · (M.ambient·La + M.diffuse·D·max(0, N·L))

clamped to 0..1, where strength is:

- directional: 1, with L opposite the light's direction;
- point: 1 / (att0 + att1·d + att2·d²) at distance d (not clamped, so
  close lights can more than double), 0 past the range;
- spot: as point, times the cone factor: 1 within theta/2 of the
  direction, 0 beyond phi/2, linear between (falloff 1).

Specular is added after the texture: Σ strength · M.specular ·
specular · (N·H)^power, H halfway between L and the direction to the
eye.

## In Reluva

`src/engines/awe/lighting.cpp` builds the scene's lights and ambient
light, and lights each vertex of the CPU-skinned characters with the
formula above; the result is the vertex colour the texture is multiplied
by. The brightness option still scales it. Not done:

- the specular term (the lights' specular colours are small, up to about
  0.13, so it is a faint sheen at most);
- conditions are tested every frame, not only when the scene is entered;
- only the current scene's lights count. Whether the game leaves a
  light of a scene visited before on (one whose index the current
  scene's lights don't reuse) wasn't checked;
- the flicker, and the `light` command (neither is used by the
  install).
