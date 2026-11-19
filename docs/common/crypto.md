# Encryption for network play

## What it is

The pieces an engine's network play needs to keep what it sends private
and unchanged on the way: X25519 key agreement and the ChaCha20-Poly1305
sealed box. In `src/common/crypto.cpp` (`crypto::x25519`,
`x25519Public`, `randomKey`, `chacha20`, `poly1305`, `seal`, `open`).
Written from the RFCs alone.

## What Reluva supports

- **X25519** (RFC 7748): a private key from the system's random source,
  its public key, and the secret two machines share from one's private
  key and the other's public one.
- **ChaCha20** and **Poly1305**, and the two together as the sealed box
  of RFC 8439 (an encrypted message and a 16-byte tag over it and over
  data sent as it is). A message changed on the way, or under another
  key, isn't opened.

Not here: signatures, certificates, or anything that says whose a public
key is. An engine that needs to know who it's talking to has to bind the
keys to something both sides know (a password, say).

## How it works

- **X25519.** Numbers modulo 2^255 - 19 are sixteen 16-bit limbs held in
  64-bit integers, so sums and products need carrying only at the end;
  2^256 is 38 modulo the prime, so what carries out of the top comes back
  in at the bottom times 38. The scalar multiplication is the RFC's
  Montgomery ladder, swapping by masks rather than branches, and the
  inverse is a power (p - 2).
- **Poly1305.** The accumulator is five 26-bit limbs, so each product
  fits in 64 bits; 2^130 is 5 modulo its prime.
- **The sealed box.** Poly1305's key is ChaCha20's first block under the
  message's key and nonce, and the message is encrypted from block 1, as
  RFC 8439 section 2.8 has it. Tags are compared in full, whatever
  differs first.

## Sources and checks

- **Sources:** RFC 7748 (*Elliptic Curves for Security*) and RFC 8439
  (*ChaCha20 and Poly1305 for IETF Protocols*), both published by the
  IETF.
- **Checks:** `common.unit.crypto` runs the RFCs' own test vectors:
  RFC 7748 section 6.1's two key pairs and their shared secret, RFC 8439
  section 2.3.2's ChaCha20 block, 2.5.2's Poly1305 tag and 2.8.2's sealed
  box, and that a changed message or changed associated data isn't
  opened. When the code was written it was also compared on 200 random
  keys, nonces and messages with the Python `cryptography` package's
  X25519 and ChaCha20-Poly1305, with no differences; that comparison
  isn't kept in the repository.
- **Confidence:** published unless marked (docs/DOCUMENTATION_STANDARD.md).

## Used by

- **`age`**: Midtown Madness 2's network play seals every message after
  joining, with keys agreed when a player joins
  ([network-protocol.md](../engines/age/games/mm2/systems/network-protocol.md#encryption)).
