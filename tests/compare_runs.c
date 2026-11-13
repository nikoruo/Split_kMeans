/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

/* compare_runs: checks <Algo>_runs.csv files against the golden copies.
 *
 * Usage: compare_runs [--update] <golden_root> <output_root> <relative_file>...
 * Each file is compared line by line on the columns run;seed;sse;ci. The ms column
 * is timing and is dropped. SSE is printed with %.17g, so equal text means equal bits.
 * --update writes the output files (without ms) over the golden files instead.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 1024
#define KEPT_FIELDS 4
#define MAX_REPORTED 5

typedef struct
{
    char** lines;
    size_t count;
} Lines;

static void freeLines(Lines* lines)
{
    for (size_t i = 0; i < lines->count; ++i)
    {
        free(lines->lines[i]);
    }
    free(lines->lines);
    lines->lines = NULL;
    lines->count = 0;
}

/* Truncates the line before field KEPT_FIELDS + 1 (the ms column). */
static void keepComparedFields(char* line)
{
    size_t separators = 0;
    for (char* p = line; *p != '\0'; ++p)
    {
        if (*p == ';' && ++separators == KEPT_FIELDS)
        {
            *p = '\0';
            return;
        }
    }
}

/* Returns 0 and fills `out` on success; prints the reason and returns -1 otherwise. */
static int readLines(const char* path, Lines* out)
{
    out->lines = NULL;
    out->count = 0;

    FILE* file = fopen(path, "r");
    if (file == NULL)
    {
        fprintf(stderr, "compare_runs: cannot read %s\n", path);
        return -1;
    }

    char buffer[LINE_MAX_LEN];
    size_t capacity = 0;
    while (fgets(buffer, sizeof(buffer), file) != NULL)
    {
        size_t length = strcspn(buffer, "\r\n");
        if (buffer[length] == '\0' && !feof(file))
        {
            fprintf(stderr, "compare_runs: line too long in %s\n", path);
            fclose(file);
            freeLines(out);
            return -1;
        }
        buffer[length] = '\0';
        keepComparedFields(buffer);

        if (out->count == capacity)
        {
            capacity = capacity ? capacity * 2 : 64;
            char** grown = realloc(out->lines, capacity * sizeof(*grown));
            if (grown == NULL)
            {
                fprintf(stderr, "compare_runs: out of memory\n");
                fclose(file);
                freeLines(out);
                return -1;
            }
            out->lines = grown;
        }

        out->lines[out->count] = malloc(strlen(buffer) + 1);
        if (out->lines[out->count] == NULL)
        {
            fprintf(stderr, "compare_runs: out of memory\n");
            fclose(file);
            freeLines(out);
            return -1;
        }
        strcpy(out->lines[out->count], buffer);
        out->count++;
    }

    fclose(file);
    return 0;
}

static int writeLines(const char* path, const Lines* lines)
{
    FILE* file = fopen(path, "w");
    if (file == NULL)
    {
        fprintf(stderr, "compare_runs: cannot write %s\n", path);
        return -1;
    }
    for (size_t i = 0; i < lines->count; ++i)
    {
        fprintf(file, "%s\n", lines->lines[i]);
    }
    return fclose(file) == 0 ? 0 : -1;
}

static int compareFile(const char* goldenPath, const char* outputPath)
{
    Lines golden;
    Lines output;
    if (readLines(outputPath, &output) != 0)
    {
        return -1;
    }
    if (readLines(goldenPath, &golden) != 0)
    {
        fprintf(stderr, "compare_runs: no golden file; run make golden-update in an algorithm-change commit\n");
        freeLines(&output);
        return -1;
    }

    size_t mismatches = 0;
    const size_t common = golden.count < output.count ? golden.count : output.count;
    for (size_t i = 0; i < common; ++i)
    {
        if (strcmp(golden.lines[i], output.lines[i]) != 0)
        {
            if (mismatches < MAX_REPORTED)
            {
                fprintf(stderr, "%s:%zu\n  expected: %s\n  actual:   %s\n", outputPath, i + 1, golden.lines[i], output.lines[i]);
            }
            ++mismatches;
        }
    }
    if (golden.count != output.count)
    {
        fprintf(stderr, "%s: %zu lines, golden has %zu\n", outputPath, output.count, golden.count);
        ++mismatches;
    }

    freeLines(&golden);
    freeLines(&output);
    return mismatches == 0 ? 0 : -1;
}

static int updateFile(const char* goldenPath, const char* outputPath)
{
    Lines output;
    if (readLines(outputPath, &output) != 0)
    {
        return -1;
    }
    const int result = writeLines(goldenPath, &output);
    freeLines(&output);
    return result;
}

int main(int argc, char** argv)
{
    int first = 1;
    int update = 0;
    if (argc > 1 && strcmp(argv[1], "--update") == 0)
    {
        update = 1;
        first = 2;
    }
    if (argc - first < 3)
    {
        fprintf(stderr, "Usage: %s [--update] <golden_root> <output_root> <relative_file>...\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* goldenRoot = argv[first];
    const char* outputRoot = argv[first + 1];
    size_t failed = 0;
    size_t total = 0;

    for (int i = first + 2; i < argc; ++i)
    {
        ++total;
        char goldenPath[LINE_MAX_LEN];
        char outputPath[LINE_MAX_LEN];
        if (snprintf(goldenPath, sizeof(goldenPath), "%s/%s", goldenRoot, argv[i]) >= (int)sizeof(goldenPath) ||
            snprintf(outputPath, sizeof(outputPath), "%s/%s", outputRoot, argv[i]) >= (int)sizeof(outputPath))
        {
            fprintf(stderr, "compare_runs: path too long: %s\n", argv[i]);
            ++failed;
            continue;
        }

        if ((update ? updateFile(goldenPath, outputPath) : compareFile(goldenPath, outputPath)) != 0)
        {
            ++failed;
        }
    }

    if (update)
    {
        printf("compare_runs: updated %zu of %zu golden files\n", total - failed, total);
    }
    else
    {
        printf("compare_runs: %zu of %zu files match the golden copies\n", total - failed, total);
    }
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
