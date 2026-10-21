// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Fragment shader for gfx::Renderer3D draw calls: texture times colour,
// with an optional alpha test. Resource sets follow SDL_gpu.h: a fragment
// shader's textures are set 2, its uniform buffers set 3.
#version 450

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D tex;

layout(set = 3, binding = 0) uniform Fragment {
    vec4 params; // x: alpha test (0 none, 1 > 0, 2 < 0.5, 3 >= 0.5)
};

void main() {
    vec4 c = texture(tex, inUv) * inColor;
    int test = int(params.x + 0.5);
    if ((test == 1 && c.a <= 0.0) || (test == 2 && c.a >= 0.5) || (test == 3 && c.a < 0.5))
        discard;
    outColor = c;
}
