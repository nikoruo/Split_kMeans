/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#include "rng.h"

#include <assert.h>

uint64_t rng_splitmix64(uint64_t* x)
{
    uint64_t z = (*x += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

void rng_seed(Rng* rng, uint64_t seed)
{
    uint64_t sm = seed;
    const uint64_t initstate = rng_splitmix64(&sm);
    const uint64_t initseq = rng_splitmix64(&sm);

    /* pcg32_srandom_r from the PCG reference implementation */
    rng->state = 0;
    rng->inc = (initseq << 1) | 1u;
    (void)rng_u32(rng);
    rng->state += initstate;
    (void)rng_u32(rng);
}

uint64_t rng_derive_seed(uint64_t master, uint64_t index)
{
    uint64_t x = master ^ (index * UINT64_C(0xD1B54A32D192ED03));
    return rng_splitmix64(&x);
}

uint32_t rng_u32(Rng* rng)
{
    const uint64_t old = rng->state;
    rng->state = old * UINT64_C(6364136223846793005) + rng->inc;
    const uint32_t xorshifted = (uint32_t)(((old >> 18) ^ old) >> 27);
    const uint32_t rot = (uint32_t)(old >> 59);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

static uint64_t rng_u64(Rng* rng)
{
    const uint64_t hi = rng_u32(rng);
    return (hi << 32) | rng_u32(rng);
}

size_t rng_below(Rng* rng, size_t bound)
{
    assert(bound > 0);

    if ((uint64_t)bound <= UINT32_MAX)
    {
        /* Lemire 2019, "Fast random integer generation in an interval" */
        const uint32_t b = (uint32_t)bound;
        uint64_t m = (uint64_t)rng_u32(rng) * b;
        uint32_t low = (uint32_t)m;
        if (low < b)
        {
            const uint32_t threshold = (uint32_t)(0u - b) % b;
            while (low < threshold)
            {
                m = (uint64_t)rng_u32(rng) * b;
                low = (uint32_t)m;
            }
        }
        return (size_t)(m >> 32);
    }

    const uint64_t b = (uint64_t)bound;
    const uint64_t threshold = (0u - b) % b;
    uint64_t r;
    do
    {
        r = rng_u64(rng);
    } while (r < threshold);
    return (size_t)(r % b);
}

double rng_unit(Rng* rng)
{
    return (double)(rng_u64(rng) >> 11) * (1.0 / 9007199254740992.0);
}
