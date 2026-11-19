// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Encryption for engines' network play: X25519 key agreement (RFC 7748)
// and the ChaCha20-Poly1305 sealed box (RFC 8439), written from those
// RFCs. See docs/common/crypto.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace reluva::crypto {

using Key = std::array<uint8_t, 32>;
using Nonce = std::array<uint8_t, 12>;
using Tag = std::array<uint8_t, 16>;

/**
 * X25519: `scalar` times the curve point whose u coordinate is `point`
 * (RFC 7748, section 5). The scalar is clamped as the RFC says.
 */
Key x25519(const Key &scalar, const Key &point);
/** The public key of a private one: its scalar times the base point (u = 9). */
Key x25519Public(const Key &secret);
/** A new private key from the system's random source. */
Key randomKey();

/**
 * ChaCha20's keystream XORed over `data` in place, starting at block
 * `counter` (RFC 8439, section 2.4).
 */
void chacha20(const Key &key, const Nonce &nonce, uint32_t counter, uint8_t *data, size_t size);
/** One 64-byte ChaCha20 block (RFC 8439, section 2.3). */
std::array<uint8_t, 64> chacha20Block(const Key &key, const Nonce &nonce, uint32_t counter);

/** Poly1305's tag of `data` under a one-time `key` (RFC 8439, section 2.5). */
Tag poly1305(const Key &key, const uint8_t *data, size_t size);

/**
 * ChaCha20-Poly1305 (RFC 8439, section 2.8): `plain` encrypted, with the
 * tag of it and of `aad` (sent as it is) after it.
 */
std::vector<uint8_t> seal(const Key &key, const Nonce &nonce, const uint8_t *aad, size_t aadSize, const uint8_t *plain,
                          size_t size);
/**
 * The other way: false, `plain` untouched, if the tag doesn't match (the
 * datagram was changed, or isn't under this key).
 */
bool open(const Key &key, const Nonce &nonce, const uint8_t *aad, size_t aadSize, const uint8_t *sealed, size_t size,
          std::vector<uint8_t> &plain);

} // namespace reluva::crypto
