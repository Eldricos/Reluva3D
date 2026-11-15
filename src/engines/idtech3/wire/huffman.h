// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace reluva::idtech3::wire {

/**
 * Quake III's adaptive Huffman code (Jedi Outcast's protocol 15, see
 * docs/engines/idtech3/games/jk2/systems/network.md): a tree that grows a
 * leaf for each new byte and reshapes itself as each byte's count goes up,
 * keeping its nodes ordered by weight (the FGK scheme, as Sayood describes
 * it). Two ends that count the same bytes in the same order have the same
 * tree, so the codes needn't be sent.
 *
 * The games use it twice: their messages, with a tree built once from a
 * table of byte counts and then left alone; and the connect packet, each
 * compressed on its own from an empty tree.
 */
class Huffman {
public:
    Huffman();

    // One more of `symbol`: a new leaf if it hasn't been seen, then the
    // tree reshaped for its new count.
    void add(uint8_t symbol);
    bool seen(uint8_t symbol) const { return _leaf[symbol] >= 0; }

    // `symbol`'s code (it must have been seen), root first, each bit put at
    // `bit` (lowest bit of each byte first) and `bit` moved on.
    void writeCode(uint8_t symbol, std::vector<uint8_t> &out, size_t &bit) const;
    // A symbol read from `in` at `bit`; -1 past the end of `in`.
    int readCode(const std::vector<uint8_t> &in, size_t &bit) const;

    // The tree for the games' messages: each byte added as many times as
    // `counts` says.
    static Huffman fromCounts(const std::array<uint32_t, 256> &counts);
    // The connect packet's compression of the bytes after `offset`: their
    // count (2 bytes, high first), then each byte's code, or for a byte not
    // seen before the code of "not yet transmitted" and its 8 bits.
    static std::vector<uint8_t> compress(const std::vector<uint8_t> &data, size_t offset);
    static std::vector<uint8_t> decompress(const std::vector<uint8_t> &data, size_t offset);

private:
    static constexpr int kNone = -1;
    static constexpr int kNotYetTransmitted = 256;
    struct Node {
        int weight = 0;
        int symbol = kNone; // a byte, kNotYetTransmitted, or kNone inside the tree
        int parent = kNone;
        int left = kNone;
        int right = kNone;
        // The shared slot naming the highest-ranked node of the same
        // weight (the block's leader).
        int block = kNone;
    };

    int newBlock(int leader);
    void freeBlock(int block) { _blockLeader[size_t(block)] = kNone; }
    int nextRanked(int node) const;
    int previousRanked(int node) const;
    // Two nodes' places in the tree and in the ranking swapped.
    void swapInTree(int a, int b);
    void swapRanks(int a, int b);
    void increment(int node);
    // A leaf's code, root first.
    void writePath(int leaf, std::vector<uint8_t> &out, size_t &bit) const;
    int pathBit(int node, int child) const { return _nodes[size_t(node)].right == child ? 1 : 0; }

    std::vector<Node> _nodes;
    int _root = 0;
    std::array<int, 257> _leaf{}; // by symbol; kNone if not seen
    std::vector<int> _ranked;     // nodes from the lowest weight up; 0 is "not yet transmitted"
    std::vector<int> _rank;       // by node
    std::vector<int> _blockLeader;
};

} // namespace reluva::idtech3::wire
