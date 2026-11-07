// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/round.h"
namespace reluva::boggle {
// Complete, sorted English board vocabulary with one legal path per word.
// A budget failure clears output; partial results are never labelled complete.
// Extend only prefixes shorter than prefixLimit. A final multi-letter cube
// may cross that boundary; words remain bounded by the reader's 64-byte limit.
Error findBoardWords(const Board& board,const Dictionary& dictionary,const UserDictionary* user,
                     std::vector<AcceptedWord>& out,size_t visits=500000,size_t prefixLimit=64);
} // namespace reluva::boggle
