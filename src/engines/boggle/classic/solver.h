// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/round.h"
namespace reluva::boggle {
// Complete, sorted English board vocabulary with one legal path per word.
// A budget failure clears output; partial results are never labelled complete.
Error findBoardWords(const Board& board,const Dictionary& dictionary,const UserDictionary* user,
                     std::vector<AcceptedWord>& out,size_t visits=500000);
} // namespace reluva::boggle
