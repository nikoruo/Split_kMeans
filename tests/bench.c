/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROUNDS 4
#define MAX_ALGOS 9
#define MAX_RUNS 3
#define LINE_SIZE 512

typedef struct
{
    const char* dataset;
    const char* groundTruth;
    size_t k;
    size_t runs;
    const char* algorithms;
    const char* outputs[MAX_ALGOS];
} BenchCase;

static const BenchCase cases[] = {
    {"s1", "s1-cb.txt", 15, 3,
     "km,rkm,rs,skm-random,skm-intra,skm-global,skm-global-basic,skm-local,bisecting",
     {"KM", "RKM", "RS", "SKM_Random", "SKM_Intra", "SKM_Global", "SKM_Global_Basic", "SKM_Local", "Bisecting_KM"}},
    {"dim064", "dim064.txt", 16, 3,
     "km,rkm,rs,skm-random,skm-intra,skm-global,skm-global-basic,skm-local,bisecting",
     {"KM", "RKM", "RS", "SKM_Random", "SKM_Intra", "SKM_Global", "SKM_Global_Basic", "SKM_Local", "Bisecting_KM"}},
    {"worms_2d", "worms_2d-gt.txt", 35, 1, "km,skm-random,skm-intra,skm-local,bisecting",
     {"KM", "SKM_Random", "SKM_Intra", "SKM_Local", "Bisecting_KM"}},
    {"birch1", "b1-gt.txt", 100, 1, "skm-intra,skm-local", {"SKM_Intra", "SKM_Local"}},
};

#define CASE_COUNT (sizeof(cases) / sizeof(cases[0]))

typedef struct
{
    char identity[LINE_SIZE];
    double best[2];
} Sample;

static Sample samples[CASE_COUNT][MAX_ALGOS][MAX_RUNS];

static void fail(const char* message, const char* detail)
{
    fprintf(stderr, "bench: %s: %s\n", message, detail);
    exit(EXIT_FAILURE);
}

/* These paths are passed inside double quotes to the system shell. */
static void checkShellPath(const char* path)
{
    if (*path == '\0' || strpbrk(path, "\"$`%!\r\n") != NULL) fail("unsafe or empty path", path);
}

static bool parseUnsignedField(const char* text, uint64_t* value)
{
    if (*text == '\0') return false;
    for (const char* p = text; *p != '\0'; ++p)
    {
        if (*p < '0' || *p > '9') return false;
    }
    errno = 0;
    char* end;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > UINT64_MAX) return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool parseNonnegativeField(const char* text, double* value)
{
    errno = 0;
    char* end;
    *value = strtod(text, &end);
    return end != text && *end == '\0' && errno == 0 && isfinite(*value) && *value >= 0.0;
}

static void checkedPath(char* path, size_t size, const char* root, size_t round, const char* dataset, size_t build)
{
    int n = snprintf(path, size, "%s%cround%zu%c%s%c%s", root, PATHSEP, round + 1, PATHSEP, dataset, PATHSEP,
                     build == 0 ? "primary" : "reference");
    if (n < 0 || (size_t)n >= size) fail("output path too long", root);
}

static void runCase(const char* binary, const char* output, const BenchCase* job, const char* nullDevice)
{
    char command[3 * PATH_MAX + 1024];
    int n = snprintf(command, sizeof(command),
                     "cd . && \"%s\" --seed 20261119 --log-level 1 --no-track --repeats 25 --max-swaps 300 "
                     "--algo %s -r %zu -o \"%s\" -k %zu \"data%c%s.txt\" \"gt%c%s\" > %s",
                     binary, job->algorithms, job->runs, output, job->k, PATHSEP, job->dataset, PATHSEP,
                     job->groundTruth, nullDevice);
    if (n < 0 || (size_t)n >= sizeof(command)) fail("command too long", output);
    if (system(command) != 0) fail("command failed", command);
}

static void readResults(const char* output, size_t caseIndex, size_t round, size_t build)
{
    const BenchCase* job = &cases[caseIndex];
    for (size_t a = 0; a < MAX_ALGOS && job->outputs[a] != NULL; ++a)
    {
        char path[PATH_MAX];
        int n = snprintf(path, sizeof(path), "%s%c%s%c%s_runs.csv", output, PATHSEP, job->dataset, PATHSEP,
                         job->outputs[a]);
        if (n < 0 || (size_t)n >= sizeof(path)) fail("result path too long", output);
        FILE* file = fopen(path, "r");
        if (file == NULL) fail("cannot read results", path);

        char line[LINE_SIZE];
        if (fgets(line, sizeof(line), file) == NULL || strcmp(line, "run;seed;sse;ci;ms\n") != 0)
        {
            fail("invalid results header", path);
        }
        for (size_t r = 0; r < job->runs; ++r)
        {
            if (fgets(line, sizeof(line), file) == NULL || strchr(line, '\n') == NULL)
                fail("missing or oversized result row", path);
            line[strcspn(line, "\r\n")] = '\0';
            char* fields[5] = {line, NULL, NULL, NULL, NULL};
            for (size_t field = 0; field < 4; ++field)
            {
                char* separator = strchr(fields[field], ';');
                if (separator == NULL) fail("invalid result row", path);
                *separator = '\0';
                fields[field + 1] = separator + 1;
            }
            uint64_t run, seed, ci;
            double sse, ms;
            if (!parseUnsignedField(fields[0], &run) || run != r + 1 ||
                !parseUnsignedField(fields[1], &seed) || !parseNonnegativeField(fields[2], &sse) ||
                !parseUnsignedField(fields[3], &ci) || ci > job->k || !parseNonnegativeField(fields[4], &ms))
                fail("invalid result row", path);
            for (size_t field = 1; field < 4; ++field) fields[field][-1] = ';';

            Sample* sample = &samples[caseIndex][a][r];
            if (round == 0 && build == 0)
            {
                strcpy(sample->identity, line);
                sample->best[0] = DBL_MAX;
                sample->best[1] = DBL_MAX;
            }
            else if (strcmp(sample->identity, line) != 0)
            {
                fprintf(stderr, "bench: result mismatch in %s, run %zu\n  expected: %s\n  actual:   %s\n", path,
                        r + 1, sample->identity, line);
                exit(EXIT_FAILURE);
            }
            if (ms < sample->best[build]) sample->best[build] = ms;
        }
        if (fgets(line, sizeof(line), file) != NULL || ferror(file)) fail("extra rows or read error", path);
        if (fclose(file) != 0) fail("cannot close results", path);
    }
}

static void writeSummary(const char* root, bool compare)
{
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s%csummary.csv", root, PATHSEP);
    if (n < 0 || (size_t)n >= sizeof(path)) fail("summary path too long", root);
    FILE* file = fopen(path, "w");
    if (file == NULL) fail("cannot write summary", path);
    fprintf(file, "dataset;algorithm;run;seed;sse;ci;primary_best_ms;reference_best_ms\n");
    double totals[2] = {0.0, 0.0};
    for (size_t c = 0; c < CASE_COUNT; ++c)
    {
        for (size_t a = 0; a < MAX_ALGOS && cases[c].outputs[a] != NULL; ++a)
        {
            for (size_t r = 0; r < cases[c].runs; ++r)
            {
                const Sample* sample = &samples[c][a][r];
                totals[0] += sample->best[0];
                fprintf(file, "%s;%s;%s;%.17g;", cases[c].dataset, cases[c].outputs[a], sample->identity,
                        sample->best[0]);
                if (compare)
                {
                    totals[1] += sample->best[1];
                    fprintf(file, "%.17g", sample->best[1]);
                }
                fputc('\n', file);
            }
        }
    }
    int writeError = ferror(file);
    if (fclose(file) != 0 || writeError) fail("cannot write summary", path);
    printf("Results: %s\n", root);
    printf("Sum of per-run minima: primary %.3f ms", totals[0]);
    if (compare)
    {
        printf(", reference %.3f ms", totals[1]);
        if (totals[0] > 0.0) printf(" (reference / primary %.3f)", totals[1] / totals[0]);
        else printf(" (ratio unavailable: primary time is zero)");
    }
    putchar('\n');
}

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        fprintf(stderr, "Usage: %s <primary> <reference|-> <output-root> <NUL|/dev/null>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const bool compare = strcmp(argv[2], "-") != 0;
    checkShellPath(argv[1]);
    if (compare) checkShellPath(argv[2]);
    checkShellPath(argv[3]);
    if (strcmp(argv[4], "NUL") != 0 && strcmp(argv[4], "/dev/null") != 0) fail("invalid null device", argv[4]);
    if (!platform_make_directories(argv[3])) fail("cannot create output root", argv[3]);

    char root[PATH_MAX];
    int n = snprintf(root, sizeof(root), "%s%cbench-%" PRIu64, argv[3], PATHSEP, platform_ticks());
    if (n < 0 || (size_t)n >= sizeof(root)) fail("output path too long", argv[3]);
    if (MAKE_DIR(root) != 0) fail("cannot create new benchmark directory", root);

    for (size_t round = 0; round < ROUNDS; ++round)
    {
        for (size_t c = 0; c < CASE_COUNT; ++c)
        {
            for (size_t order = 0; order < (compare ? 2u : 1u); ++order)
            {
                size_t build = compare && round % 2 != 0 ? 1 - order : order;
                char output[PATH_MAX];
                checkedPath(output, sizeof(output), root, round, cases[c].dataset, build);
                printf("Round %zu: %s %s\n", round + 1, cases[c].dataset, build == 0 ? "primary" : "reference");
                fflush(stdout);
                runCase(argv[build + 1], output, &cases[c], argv[4]);
                readResults(output, c, round, build);
            }
        }
    }
    writeSummary(root, compare);
    return EXIT_SUCCESS;
}
