// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The stb_image implementation (third_party/stb), compiled once for the
// whole program: image/image.cpp decodes pictures with it, and
// common/zip.cpp uses its inflate, which comes with PNG support. It's here
// in common/ because zip.cpp is: common/ links nothing above it.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_FAILURE_USERMSG
#include "stb_image.h"
