/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_HEX_SIZE 65

typedef struct
{
    uint32_t state[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} Sha256;

void sha256Init(Sha256* hash);
void sha256Update(Sha256* hash, const void* data, size_t length);
void sha256Final(Sha256* hash, char output[SHA256_HEX_SIZE]);

#endif /* SHA256_H */
