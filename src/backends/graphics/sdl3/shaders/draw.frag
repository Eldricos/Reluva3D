// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Fragment shader for gfx::Renderer3D draw calls: texture times colour,
// with an optional alpha test, table fog and linear fog; or (params.y) the light the dynamic lights
// cast on the pixel, fading linearly to nothing at each one's radius, alone
// (1) or times the texture (3). Resource sets follow SDL_gpu.h: a fragment
// shader's textures are set 2, its uniform buffers set 3.
#version 450

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inWorld;
layout(location = 3) in vec2 inFog;
layout(location = 4) in float inFogW;
layout(location = 5) in float inLinearFog;
layout(location = 6) in vec3 inSpecular;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D tex;

layout(set = 3, binding = 0) uniform Fragment {
    vec4 params; // x: alpha test (0 none, 1 > 0, 2 < 0.5, 3 >= 0.5), y: dynamic lights instead (1),
                 // times the texture (3), the brightness pass (2, z: 1 / gamma), or volume lights (4),
                 // w: a fog volume pass (1)
    vec4 fog;    // rgb: table fog's colour, w: its value (0 none; DrawCall::tableFog)
    vec4 blend;  // x, y: blendOut's factors (y 0: none)
    vec4 lightCount; // x; yzw: the camera, for volume lights
    vec4 lightPos[16]; // xyz, w: radius
    vec4 lightColor[16];
    vec4 linearFogColor; // rgb: what the linear fog mixes towards (DrawCall::fogColor)
};

vec3 dynamicLight() {
    vec3 sum = vec3(0.0);
    int n = int(lightCount.x + 0.5);
    for (int i = 0; i < n; ++i) {
        float d = distance(inWorld, lightPos[i].xyz);
        sum += lightColor[i].rgb * clamp(1.0 - d / lightPos[i].w, 0.0, 1.0);
    }
    return sum;
}

// A volume light's glow between the camera and the pixel: the sphere's
// density (1 - (d / r)^2) along the line, 1 straight through it all.
float volumeGlow(vec3 eye, vec3 at, vec4 l) {
    vec3 r = at - eye;
    float len = length(r);
    if (len <= 0.0 || l.w <= 0.0)
        return 0.0;
    vec3 u = r / len, c = l.xyz - eye;
    float along = dot(c, u), r2 = l.w * l.w, b2 = dot(c, c) - along * along;
    if (b2 >= r2)
        return 0.0;
    float half_ = sqrt(r2 - b2);
    float s1 = max(0.0, along - half_) - along, s2 = min(len, along + half_) - along;
    if (s2 <= s1)
        return 0.0;
    return ((s2 - s1) * (1.0 - b2 / r2) - (s2 * s2 * s2 - s1 * s1 * s1) / (3.0 * r2)) * 3.0 / (4.0 * l.w);
}

// A blend the pipeline does as (one, one minus source alpha), for fewer
// pipelines (blend.y > 0): the source's factor applied here (blend.x: 0
// zero, 1 one, 2 its alpha), and the destination's as one minus the alpha
// put out (blend.y: 1 zero, 2 one, 3 one minus the source's alpha).
vec4 blendOut(vec4 c) {
    int d = int(blend.y + 0.5);
    if (d == 0)
        return c;
    int s = int(blend.x + 0.5);
    float fs = s == 0 ? 0.0 : (s == 1 ? 1.0 : c.a);
    float fd = d == 1 ? 0.0 : (d == 2 ? 1.0 : 1.0 - c.a);
    return vec4(c.rgb * fs, 1.0 - fd);
}

void main() {
    if (params.y > 3.5) {
        // The volume lights' glow and fog, blended over drawn geometry.
        vec3 sum = vec3(0.0);
        float hidden = 0.0;
        int n = int(lightCount.x + 0.5);
        for (int i = 0; i < n; ++i) {
            float g = volumeGlow(lightCount.yzw, inWorld, lightPos[i]);
            sum += lightColor[i].rgb * g;
            hidden += lightColor[i].a * g;
        }
        outColor = blendOut(vec4(sum, min(hidden, 1.0)));
        return;
    }
    if (params.y > 2.5) {
        // The texture times the dynamic lights' light, alpha tested.
        vec4 t = texture(tex, inUv);
        int test = int(params.x + 0.5);
        if ((test == 1 && t.a <= 0.0) || (test == 2 && t.a >= 0.5) || (test == 3 && t.a < 0.5))
            discard;
        outColor = blendOut(vec4(t.rgb * dynamicLight(), 1.0));
        return;
    }
    if (params.y > 1.5) {
        // The brightness pass: the frame through the curve (params.z = 1 / gamma).
        outColor = vec4(pow(texture(tex, inUv).rgb, vec3(params.z)), 1.0);
        return;
    }
    if (params.y > 0.5) {
        outColor = blendOut(vec4(dynamicLight(), 1.0));
        return;
    }
    if (params.w > 0.5) {
        // A fog volume: thickening with the square root of the depth.
        outColor = vec4(inColor.rgb, sqrt(clamp(inFog.x * inFog.y, 0.0, 1.0)));
        return;
    }
    vec4 c = texture(tex, inUv) * inColor;
    int test = int(params.x + 0.5);
    if ((test == 1 && c.a <= 0.0) || (test == 2 && c.a >= 0.5) || (test == 3 && c.a < 0.5))
        discard;
    c.rgb = min(c.rgb + inSpecular, vec3(1.0));
    if (fog.w > 0.0) {
        float d = 1.0 / fog.w;
        float f = clamp((1.0 - exp(-d * inFogW)) / (1.0 - exp(-d * 52428.8)), 0.0, 1.0);
        c.rgb = mix(c.rgb, fog.rgb, f);
    }
    c.rgb = mix(c.rgb, linearFogColor.rgb, inLinearFog);
    outColor = blendOut(c);
}
