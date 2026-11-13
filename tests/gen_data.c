/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

/* gen_data: deterministic synthetic datasets for the regression tests.
 *
 * Usage: gen_data <outdir>
 * Writes <outdir>/<group>/{data,gt,centroids}/<name>.txt with the groups main (regular
 * scenarios), edge (edge cases) and pending (edge cases that still fail). The folders must exist.
 *
 * All coordinates are integers, so parsing and sums are exact and the files are
 * byte-identical on every platform. Ground truth = the generating centers.
 */

#include "rng.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_DIMS 64
#define MAX_K 16

typedef struct
{
    const char* group;     /* "main", "edge" or "pending" */
    const char* name;
    size_t dims;
    size_t k;
    size_t sizes[MAX_K];   /* points per cluster */
    long spread;           /* noise = (sum of 4 uniform draws in [-spread, spread]) / 2; 0 gives duplicates */
    long centerRange;      /* random centers in [0, centerRange) when centers is NULL */
    const long* centers;   /* k * dims values, or NULL */
    uint64_t seed;
    size_t constantDim;    /* 1-based dimension that keeps its center value without noise; 0 = none */
} Scenario;

static const long grid4[] = { 0, 0, 1000, 0, 0, 1000, 1000, 1000 };
static const long line5[] = { 0, 0, 300, 0, 600, 0, 900, 0, 1200, 0 };
static const long far4[] = { 0, 0, 2000, 0, 0, 2000, 2000, 2000 };
static const long grid15[] = {
    0, 0, 500, 0, 1000, 0, 1500, 0, 2000, 0,
    0, 500, 500, 500, 1000, 500, 1500, 500, 2000, 500,
    0, 1000, 500, 1000, 1000, 1000, 1500, 1000, 2000, 1000,
};
static const long single1[] = { 100, 200 };
static const long eight8[] = { 0, 0, 10, 0, 20, 0, 30, 0, 0, 10, 10, 10, 20, 10, 30, 10 };
static const long dup3[] = { 5, 5, 50, 5, 5, 50 };
static const long tiny2[] = { 0, 0, 100, 100 };
static const long const3[] = { 0, 0, 7, 400, 0, 7, 0, 400, 7 };
static const long pair2[] = { 0, 0, 600, 0 };

static const Scenario scenarios[] = {
    { "main", "s01_blobs2d", 2, 4, { 100, 100, 100, 100 }, 60, 0, grid4, 101, 0 },
    { "main", "s02_blobs8d", 8, 6, { 80, 80, 80, 80, 80, 80 }, 150, 10000, NULL, 102, 0 },
    { "main", "s03_overlap", 2, 5, { 120, 120, 120, 120, 120 }, 200, 0, line5, 103, 0 },
    { "main", "s04_unbalanced", 2, 4, { 300, 25, 25, 25 }, 40, 0, far4, 104, 0 },
    { "main", "s05_k15", 2, 15, { 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40 }, 30, 0, grid15, 105, 0 },

    { "edge", "e01_k1", 2, 1, { 50 }, 20, 0, single1, 201, 0 },
    { "edge", "e02_k_equals_n", 2, 8, { 1, 1, 1, 1, 1, 1, 1, 1 }, 0, 0, eight8, 202, 0 },
    { "edge", "e03_duplicates", 2, 3, { 20, 20, 20 }, 0, 0, dup3, 203, 0 },
    { "edge", "e04_tiny", 2, 2, { 2, 1 }, 0, 0, tiny2, 204, 0 },
    { "edge", "e05_constant_dim", 3, 3, { 30, 30, 30 }, 25, 0, const3, 205, 3 },
    { "edge", "e06_highdim", 64, 4, { 20, 20, 20, 20 }, 100, 5000, NULL, 206, 0 },
    { "edge", "e07_k2", 2, 2, { 50, 50 }, 40, 0, pair2, 207, 0 },
};

static long noise(Rng* rng, long spread)
{
    if (spread == 0)
    {
        return 0;
    }
    const size_t width = (size_t)(2 * spread + 1);
    long sum = 0;
    for (int i = 0; i < 4; ++i)
    {
        sum += (long)rng_below(rng, width) - spread;
    }
    return sum / 2;
}

static FILE* openOutput(const char* outdir, const char* group, const char* kind, const char* name)
{
    char path[1024];
    if (snprintf(path, sizeof(path), "%s/%s/%s/%s.txt", outdir, group, kind, name) >= (int)sizeof(path))
    {
        fprintf(stderr, "gen_data: path too long for %s\n", name);
        return NULL;
    }
    FILE* file = fopen(path, "w");
    if (file == NULL)
    {
        fprintf(stderr, "gen_data: cannot write %s (does the folder exist?)\n", path);
    }
    return file;
}

static int writeScenario(const char* outdir, const Scenario* s)
{
    long centers[MAX_K * MAX_DIMS];
    Rng rng;
    rng_seed(&rng, s->seed);

    for (size_t i = 0; i < s->k * s->dims; ++i)
    {
        centers[i] = s->centers ? s->centers[i] : (long)rng_below(&rng, (size_t)s->centerRange);
    }

    FILE* data = openOutput(outdir, s->group, "data", s->name);
    FILE* gt = openOutput(outdir, s->group, "gt", s->name);
    FILE* kFile = openOutput(outdir, s->group, "centroids", s->name);
    int ok = data != NULL && gt != NULL && kFile != NULL;

    size_t n = 0;
    if (ok)
    {
        for (size_t c = 0; c < s->k; ++c)
        {
            for (size_t p = 0; p < s->sizes[c]; ++p)
            {
                for (size_t d = 0; d < s->dims; ++d)
                {
                    const long value = centers[c * s->dims + d] +
                        (d + 1 == s->constantDim ? 0 : noise(&rng, s->spread));
                    fprintf(data, d == 0 ? "%ld" : " %ld", value);
                }
                fputc('\n', data);
                ++n;
            }

            for (size_t d = 0; d < s->dims; ++d)
            {
                fprintf(gt, d == 0 ? "%ld" : " %ld", centers[c * s->dims + d]);
            }
            fputc('\n', gt);
        }
        fprintf(kFile, "%zu\n", s->k);
    }

    if (data) ok &= fclose(data) == 0;
    if (gt) ok &= fclose(gt) == 0;
    if (kFile) ok &= fclose(kFile) == 0;

    if (ok)
    {
        printf("%s/%s: N=%zu D=%zu K=%zu\n", s->group, s->name, n, s->dims, s->k);
    }
    return ok;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <outdir>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int ok = 1;
    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        ok &= writeScenario(argv[1], &scenarios[i]);
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
