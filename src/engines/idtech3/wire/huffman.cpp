// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/wire/huffman.h"

namespace reluva::idtech3::wire {

namespace {

void putBit(std::vector<uint8_t> &out, size_t &bit, int value) {
    if (bit / 8 >= out.size())
        out.resize(bit / 8 + 1);
    if (value)
        out[bit / 8] |= uint8_t(1u << (bit % 8));
    ++bit;
}

int getBit(const std::vector<uint8_t> &in, size_t &bit) {
    if (bit / 8 >= in.size())
        return -1;
    const int v = (in[bit / 8] >> (bit % 8)) & 1;
    ++bit;
    return v;
}

} // namespace

Huffman::Huffman() {
    _leaf.fill(kNone);
    Node nyt;
    nyt.symbol = kNotYetTransmitted;
    _nodes.push_back(nyt);
    _leaf[kNotYetTransmitted] = 0;
    _ranked.push_back(0);
    _rank.push_back(0);
}

int Huffman::newBlock(int leader) {
    for (size_t i = 0; i < _blockLeader.size(); ++i)
        if (_blockLeader[i] == kNone) {
            _blockLeader[i] = leader;
            return int(i);
        }
    _blockLeader.push_back(leader);
    return int(_blockLeader.size() - 1);
}

int Huffman::nextRanked(int node) const {
    const size_t r = size_t(_rank[size_t(node)]) + 1;
    return r < _ranked.size() ? _ranked[r] : kNone;
}

int Huffman::previousRanked(int node) const {
    const int r = _rank[size_t(node)];
    return r > 0 ? _ranked[size_t(r - 1)] : kNone;
}

void Huffman::swapInTree(int a, int b) {
    // Each parent's child pointer is changed in turn, as the original does:
    // for two children of one parent this changes nothing, and the tree
    // depends on that.
    const int pa = _nodes[size_t(a)].parent;
    const int pb = _nodes[size_t(b)].parent;
    if (pa != kNone) {
        Node &p = _nodes[size_t(pa)];
        (p.left == a ? p.left : p.right) = b;
    } else {
        _root = b;
    }
    if (pb != kNone) {
        Node &p = _nodes[size_t(pb)];
        (p.left == b ? p.left : p.right) = a;
    } else {
        _root = a;
    }
    _nodes[size_t(a)].parent = pb;
    _nodes[size_t(b)].parent = pa;
}

void Huffman::swapRanks(int a, int b) {
    const int ra = _rank[size_t(a)];
    const int rb = _rank[size_t(b)];
    _ranked[size_t(ra)] = b;
    _ranked[size_t(rb)] = a;
    _rank[size_t(a)] = rb;
    _rank[size_t(b)] = ra;
}

void Huffman::increment(int node) {
    if (node == kNone)
        return;
    Node *n = &_nodes[size_t(node)];
    // To the top of its block first, so its new weight keeps the order.
    // (The block's leader isn't moved in the tree when it's the node's
    // parent, but is in the ranking.)
    if (const int next = nextRanked(node); next != kNone && _nodes[size_t(next)].weight == n->weight) {
        const int leader = _blockLeader[size_t(n->block)];
        if (leader != n->parent)
            swapInTree(leader, node);
        swapRanks(leader, node);
    }
    // Out of its block: the node below it leads it now, or it's gone.
    if (const int prev = previousRanked(node); prev != kNone && _nodes[size_t(prev)].weight == n->weight)
        _blockLeader[size_t(n->block)] = prev;
    else
        freeBlock(n->block);
    ++n->weight;
    // Into the block of its new weight, or one of its own.
    if (const int next = nextRanked(node); next != kNone && _nodes[size_t(next)].weight == n->weight)
        n->block = _nodes[size_t(next)].block;
    else
        n->block = newBlock(node);
    const int parent = n->parent;
    if (parent == kNone)
        return;
    increment(parent);
    n = &_nodes[size_t(node)];
    if (previousRanked(node) == parent) {
        swapRanks(node, parent);
        if (_blockLeader[size_t(n->block)] == node)
            _blockLeader[size_t(n->block)] = parent;
    }
}

void Huffman::add(uint8_t symbol) {
    if (_leaf[symbol] != kNone) {
        increment(_leaf[symbol]);
        return;
    }
    // "Not yet transmitted" splits into a new inner node holding itself
    // (left) and the new leaf (right), both ranked just above it.
    const int nyt = 0;
    const auto inner = int(_nodes.size());
    const int leaf = inner + 1;
    const int above = nextRanked(nyt);
    _nodes.resize(_nodes.size() + 2);
    _rank.resize(_nodes.size());
    Node &in = _nodes[size_t(inner)];
    in.weight = 1;
    in.block = above != kNone && _nodes[size_t(above)].weight == 1 ? _nodes[size_t(above)].block : newBlock(inner);
    Node &lf = _nodes[size_t(leaf)];
    lf.weight = 1;
    lf.symbol = symbol;
    lf.block = in.block;
    _ranked.insert(_ranked.begin() + 1, inner);
    _ranked.insert(_ranked.begin() + 1, leaf);
    for (size_t r = 1; r < _ranked.size(); ++r)
        _rank[size_t(_ranked[r])] = int(r);
    const int oldParent = _nodes[nyt].parent;
    if (oldParent != kNone) {
        Node &p = _nodes[size_t(oldParent)];
        (p.left == nyt ? p.left : p.right) = inner;
    } else {
        _root = inner;
    }
    in.left = nyt;
    in.right = leaf;
    in.parent = oldParent;
    _nodes[nyt].parent = inner;
    lf.parent = inner;
    _leaf[symbol] = leaf;
    increment(oldParent);
}

void Huffman::writeCode(uint8_t symbol, std::vector<uint8_t> &out, size_t &bit) const {
    writePath(_leaf[symbol], out, bit);
}

void Huffman::writePath(int leaf, std::vector<uint8_t> &out, size_t &bit) const {
    std::vector<int> path;
    for (int n = leaf, p = _nodes[size_t(n)].parent; p != kNone; n = p, p = _nodes[size_t(p)].parent)
        path.push_back(pathBit(p, n));
    for (auto it = path.rbegin(); it != path.rend(); ++it)
        putBit(out, bit, *it);
}

int Huffman::readCode(const std::vector<uint8_t> &in, size_t &bit) const {
    int n = _root;
    while (_nodes[size_t(n)].symbol == kNone) {
        const int b = getBit(in, bit);
        if (b < 0)
            return -1;
        n = b ? _nodes[size_t(n)].right : _nodes[size_t(n)].left;
    }
    return _nodes[size_t(n)].symbol;
}

Huffman Huffman::fromCounts(const std::array<uint32_t, 256> &counts) {
    Huffman h;
    for (size_t s = 0; s < 256; ++s)
        for (uint32_t i = 0; i < counts[s]; ++i)
            h.add(uint8_t(s));
    return h;
}

std::vector<uint8_t> Huffman::compress(const std::vector<uint8_t> &data, size_t offset) {
    if (data.size() <= offset)
        return data;
    const size_t count = data.size() - offset;
    std::vector<uint8_t> out(data.begin(), data.begin() + std::ptrdiff_t(offset));
    std::vector<uint8_t> packed{uint8_t(count >> 8), uint8_t(count)};
    size_t bit = 16;
    Huffman h;
    for (size_t i = offset; i < data.size(); ++i) {
        const uint8_t c = data[i];
        if (h.seen(c)) {
            h.writeCode(c, packed, bit);
        } else {
            h.writePath(h._leaf[kNotYetTransmitted], packed, bit);
            for (int k = 7; k >= 0; --k)
                putBit(packed, bit, (c >> k) & 1);
        }
        h.add(c);
    }
    // A whole byte more than the bits need, as the original rounds.
    packed.resize((bit + 8) / 8);
    out.insert(out.end(), packed.begin(), packed.end());
    return out;
}

std::vector<uint8_t> Huffman::decompress(const std::vector<uint8_t> &data, size_t offset) {
    if (data.size() < offset + 2)
        return data;
    const std::vector<uint8_t> packed(data.begin() + std::ptrdiff_t(offset), data.end());
    const size_t count = size_t(packed[0]) << 8 | packed[1];
    std::vector<uint8_t> out(data.begin(), data.begin() + std::ptrdiff_t(offset));
    size_t bit = 16;
    Huffman h;
    for (size_t i = 0; i < count; ++i) {
        int c = h.readCode(packed, bit);
        if (c == kNotYetTransmitted) {
            c = 0;
            for (int k = 0; k < 8; ++k) {
                const int b = getBit(packed, bit);
                if (b < 0)
                    return out;
                c = c << 1 | b;
            }
        }
        if (c < 0)
            return out;
        out.push_back(uint8_t(c));
        h.add(uint8_t(c));
    }
    return out;
}

} // namespace reluva::idtech3::wire
