// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Vertex shader for gfx::Renderer3D draw calls. Resource sets follow
// SDL_gpu.h: uniform buffers of a vertex shader are set 1.
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec2 inUv2;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

layout(set = 1, binding = 0) uniform Draw {
    mat4 viewProj;
    vec4 texRow0;  // xyz: first row of the texture coordinate transform
    vec4 texRow1;  // xyz: second row
    vec4 color;
    vec4 flags;    // x: use uv2, y: use the vertex colour
};

void main() {
    vec3 uv = vec3(flags.x > 0.5 ? inUv2 : inUv, 1.0);
    outUv = vec2(dot(texRow0.xyz, uv), dot(texRow1.xyz, uv));
    outColor = (flags.y > 0.5 ? inColor : vec4(1.0)) * color;
    gl_Position = viewProj * vec4(inPos, 1.0);
}
