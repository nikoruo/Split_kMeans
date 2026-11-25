/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t roundConstants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)};

static uint32_t rotateRight(uint32_t value, unsigned count)
{
    return (value >> count) | (value << (32 - count));
}

static void sha256Block(Sha256* hash, const unsigned char block[64])
{
    uint32_t words[64];
    for (size_t i = 0; i < 16; ++i)
    {
        words[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16) |
                   ((uint32_t)block[4 * i + 2] << 8) | (uint32_t)block[4 * i + 3];
    }
    for (size_t i = 16; i < 64; ++i)
    {
        const uint32_t x = words[i - 15];
        const uint32_t y = words[i - 2];
        const uint32_t small0 = rotateRight(x, 7) ^ rotateRight(x, 18) ^ (x >> 3);
        const uint32_t small1 = rotateRight(y, 17) ^ rotateRight(y, 19) ^ (y >> 10);
        words[i] = words[i - 16] + small0 + words[i - 7] + small1;
    }

    uint32_t a = hash->state[0], b = hash->state[1], c = hash->state[2], d = hash->state[3];
    uint32_t e = hash->state[4], f = hash->state[5], g = hash->state[6], h = hash->state[7];
    for (size_t i = 0; i < 64; ++i)
    {
        const uint32_t big1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const uint32_t choose = (e & f) ^ (~e & g);
        const uint32_t first = h + big1 + choose + roundConstants[i] + words[i];
        const uint32_t big0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t second = big0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    hash->state[0] += a;
    hash->state[1] += b;
    hash->state[2] += c;
    hash->state[3] += d;
    hash->state[4] += e;
    hash->state[5] += f;
    hash->state[6] += g;
    hash->state[7] += h;
}

void sha256Init(Sha256* hash)
{
    *hash = (Sha256){{UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85), UINT32_C(0x3c6ef372),
                      UINT32_C(0xa54ff53a), UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
                      UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)},
                     0, {0}, 0};
}

void sha256Update(Sha256* hash, const void* data, size_t length)
{
    if (length > UINT64_MAX / 8 - hash->bytes)
    {
        fprintf(stderr, "Error: SHA-256 input exceeds its maximum length\n");
        exit(EXIT_FAILURE);
    }
    hash->bytes += length;
    const unsigned char* input = data;
    while (length > 0)
    {
        const size_t available = sizeof(hash->block) - hash->used;
        const size_t count = length < available ? length : available;
        memcpy(hash->block + hash->used, input, count);
        hash->used += count;
        input += count;
        length -= count;
        if (hash->used == sizeof(hash->block))
        {
            sha256Block(hash, hash->block);
            hash->used = 0;
        }
    }
}

void sha256Final(Sha256* hash, char output[SHA256_HEX_SIZE])
{
    const uint64_t bits = hash->bytes * UINT64_C(8);
    hash->block[hash->used++] = 0x80;
    if (hash->used > 56)
    {
        memset(hash->block + hash->used, 0, sizeof(hash->block) - hash->used);
        sha256Block(hash, hash->block);
        hash->used = 0;
    }
    memset(hash->block + hash->used, 0, 56 - hash->used);
    for (size_t i = 0; i < 8; ++i)
        hash->block[56 + i] = (unsigned char)(bits >> (56 - 8 * i));
    sha256Block(hash, hash->block);

    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i)
    {
        const unsigned byte = (hash->state[i / 4] >> (24 - 8 * (i % 4))) & 0xffU;
        output[2 * i] = digits[byte >> 4];
        output[2 * i + 1] = digits[byte & 0x0fU];
    }
    output[64] = '\0';
}
