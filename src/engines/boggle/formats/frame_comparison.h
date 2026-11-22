// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/image.h"
namespace reluva::boggle {
struct FrameComparison {uint64_t pixels=0,changed=0;unsigned maxDelta=0;double meanAbsolute=0,rmse=0;};
Error compareFrames(const image::Image& reference,const image::Image& actual,int x,int y,int w,int h,FrameComparison& out);
}
