/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

/* rng.h: seeded PCG32 generator (O'Neill 2014, XSH RR) seeded through SplitMix64.
 * All randomness in the program goes through an explicit Rng*.
 */
#ifndef RNG_H
#define RNG_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint64_t state;
    uint64_t inc; /* stream selector, always odd */
} Rng;

/* Advances *x and returns the next SplitMix64 value. */
uint64_t rng_splitmix64(uint64_t* x);

/* Same seed gives the same sequence on every platform. */
void rng_seed(Rng* rng, uint64_t seed);

/* Independent, reproducible seed for stream `index` (for example run i) of a master seed. */
uint64_t rng_derive_seed(uint64_t master, uint64_t index);

uint32_t rng_u32(Rng* rng);

/* Uniform in [0, bound) without modulo bias; bound must be > 0. */
size_t rng_below(Rng* rng, size_t bound);

/* Uniform in [0, 1) with 53 random bits. */
double rng_unit(Rng* rng);

#endif /* RNG_H */
