/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"

#include "dataset.h"
#include "util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void checkShape(size_t size, size_t dimensions)
{
    if (size == 0 || dimensions == 0 || size > SIZE_MAX / dimensions || size * dimensions > SIZE_MAX / sizeof(double) ||
        size > SIZE_MAX / sizeof(size_t))
    {
        fprintf(stderr, "Error: Invalid dataset dimensions or size exceeds addressable memory\n");
        exit(EXIT_FAILURE);
    }
}

static uint64_t hashCoordinates(const double* point, size_t dimensions)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t d = 0; d < dimensions; ++d)
    {
        double value = point[d] == 0.0 ? 0.0 : point[d];
        const unsigned char* bytes = (const unsigned char*)&value;
        for (size_t b = 0; b < sizeof(value); ++b)
        {
            hash ^= bytes[b];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

size_t countDistinctRowsUpTo(const Dataset* dataset, size_t limit)
{
    assert(dataset->size > 0 && dataset->dimensions > 0 && limit > 0);
    if (limit > dataset->size) limit = dataset->size;
    if (limit > SIZE_MAX / 2)
    {
        fprintf(stderr, "Error: Distinct-coordinate index exceeds addressable memory\n");
        exit(EXIT_FAILURE);
    }
    size_t capacity = 1;
    while (capacity < 2 * limit)
    {
        if (capacity > SIZE_MAX / 2)
        {
            fprintf(stderr, "Error: Distinct-coordinate index exceeds addressable memory\n");
            exit(EXIT_FAILURE);
        }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(size_t))
    {
        fprintf(stderr, "Error: Distinct-coordinate index exceeds addressable memory\n");
        exit(EXIT_FAILURE);
    }
    size_t* table = malloc(capacity * sizeof(*table));
    handleMemoryError(table);
    for (size_t i = 0; i < capacity; ++i) table[i] = SIZE_MAX;

    size_t distinct = 0;
    for (size_t i = 0; i < dataset->size; ++i)
    {
        const double* point = datasetRowConst(dataset, i);
        size_t slot = (size_t)hashCoordinates(point, dataset->dimensions) & (capacity - 1);
        while (table[slot] != SIZE_MAX &&
               !coordinatesEqual(point, datasetRowConst(dataset, table[slot]), dataset->dimensions))
            slot = (slot + 1) & (capacity - 1);
        if (table[slot] == SIZE_MAX)
        {
            table[slot] = i;
            if (++distinct == limit) break;
        }
    }
    free(table);
    return distinct;
}

void freeDataset(Dataset* dataset)
{
    if (dataset == NULL) return;
    free(dataset->coordinates);
    free(dataset->partitions);
    *dataset = (Dataset){0};
}

void freeCentroids(Centroids* centroids)
{
    if (centroids == NULL) return;
    free(centroids->coordinates);
    *centroids = (Centroids){0};
}

Dataset allocateDataset(size_t size, size_t dimensions)
{
    checkShape(size, dimensions);
    Dataset dataset = {0};
    dataset.coordinates = calloc(size * dimensions, sizeof(double));
    handleMemoryError(dataset.coordinates);
    dataset.partitions = malloc(size * sizeof(size_t));
    if (dataset.partitions == NULL)
    {
        free(dataset.coordinates);
        handleMemoryError(NULL);
    }
    dataset.size = size;
    dataset.dimensions = dimensions;
    for (size_t i = 0; i < size; ++i) dataset.partitions[i] = SIZE_MAX;
    return dataset;
}

Centroids allocateCentroids(size_t size, size_t dimensions)
{
    checkShape(size, dimensions);
    Centroids centroids;
    centroids.coordinates = calloc(size * dimensions, sizeof(double));
    handleMemoryError(centroids.coordinates);
    centroids.size = size;
    centroids.capacity = size;
    centroids.dimensions = dimensions;
    return centroids;
}

/**
 * @brief Load whitespace-separated double vectors, enforcing a consistent dimensionality per row.
 *
 * Skips whitespace-only lines. The first non-empty row defines the expected column count;
 * every subsequent non-empty row must match or the function terminates with an error.
 * Returns an empty container if the file has no data rows.
 *
 * @param filename Path to a space/tab-delimited text file of doubles.
 * @return Dataset with one row per data point and partitions initialized to SIZE_MAX.
 * @errors Exits on file open/read failures, allocation failures, inconsistent column counts,
 *         values that are not finite numbers, and lines longer than LINE_BUFSZ - 1 characters.
 * @note Parsing uses strtod and the current locale (the program never changes it, so dot decimals).
 */
Dataset readDataset(const char* filename)
{
    FILE* file;
    if (FOPEN(file, filename, "r") != 0)
    {
        handleFileError(filename);
    }

    // First pass: count data rows and establish expected dimensionality
    size_t lineCount = 0;
    size_t expectedDims = SIZE_MAX;
    size_t lineNumber = 0;
    char countBuffer[LINE_BUFSZ];

    while (readDataLine(countBuffer, sizeof(countBuffer), file, filename, ++lineNumber) != NULL)
    {
        // Determine if the line contains any non-whitespace characters
        bool hasTokenChar = false;
        for (const unsigned char* p = (const unsigned char*)countBuffer; *p; ++p)
        {
            if (!isspace(*p))
            {
                hasTokenChar = true;
                break;
            }
        }
        if (!hasTokenChar)
        {
            continue; // whitespace-only line
        }

        // Count tokens to validate dimensionality
        char tmp[LINE_BUFSZ];
        STRCPY(tmp, sizeof(tmp), countBuffer);
        size_t tokenCount = 0;
        char* ctx = NULL;
        char* tok = STRTOK(tmp, " \t\r\n", &ctx);
        while (tok != NULL)
        {
            ++tokenCount;
            tok = STRTOK(NULL, " \t\r\n", &ctx);
        }

        if (tokenCount == 0)
        {
            continue; // defensive: no tokens after tokenization
        }

        if (expectedDims == SIZE_MAX)
        {
            expectedDims = tokenCount;
        }
        else if (tokenCount != expectedDims)
        {
            fclose(file);
            fprintf(stderr, "Error: Inconsistent column count in '%s' line %zu (expected %zu, got %zu)\n", filename,
                    lineNumber, expectedDims, tokenCount);
            exit(EXIT_FAILURE);
        }

        ++lineCount;
    }

    // No data rows -> return empty container
    if (lineCount == 0)
    {
        fclose(file);
        Dataset empty = {0};
        return empty;
    }

    // Second pass: parse into the flat dataset
    rewind(file);

    Dataset dataPoints = allocateDataset(lineCount, expectedDims);

    char line[LINE_BUFSZ];
    size_t currentPoint = 0;
    lineNumber = 0;

    while (currentPoint < lineCount && readDataLine(line, sizeof(line), file, filename, ++lineNumber) != NULL)
    {
        // Skip whitespace-only lines
        bool hasTokenChar = false;
        for (const unsigned char* p = (const unsigned char*)line; *p; ++p)
        {
            if (!isspace(*p))
            {
                hasTokenChar = true;
                break;
            }
        }
        if (!hasTokenChar)
        {
            continue;
        }

        // Count tokens to confirm dimensionality
        size_t tokenCount = 0;
        char lineCopy[LINE_BUFSZ];
        STRCPY(lineCopy, sizeof(lineCopy), line);
        char* countContext = NULL;
        for (char* ct = STRTOK(lineCopy, " \t\r\n", &countContext); ct != NULL;
             ct = STRTOK(NULL, " \t\r\n", &countContext))
        {
            ++tokenCount;
        }

        if (tokenCount == 0)
        {
            continue; // defensive
        }
        if (expectedDims != SIZE_MAX && tokenCount != expectedDims)
        {
            fclose(file);
            freeDataset(&dataPoints);
            fprintf(stderr, "Error: Inconsistent column count in '%s' line %zu (expected %zu, got %zu)\n", filename,
                    lineNumber, expectedDims, tokenCount);
            exit(EXIT_FAILURE);
        }

        // Parse values
        char* context = NULL;
        char* token = STRTOK(line, " \t\r\n", &context);
        size_t dim = 0;
        while (token != NULL && dim < expectedDims)
        {
            if (!parseFiniteDouble(token, &datasetRow(&dataPoints, currentPoint)[dim]))
            {
                fclose(file);
                fprintf(stderr, "Error: '%s' in '%s' line %zu, column %zu is not a finite number\n", token, filename,
                        lineNumber, dim + 1);
                freeDataset(&dataPoints);
                exit(EXIT_FAILURE);
            }
            ++dim;
            token = STRTOK(NULL, " \t\r\n", &context);
        }

        // Final sanity check
        if (dim != expectedDims)
        {
            fclose(file);
            freeDataset(&dataPoints);
            fprintf(stderr, "Error: Row truncated in '%s' (expected %zu values, got %zu)\n", filename, expectedDims,
                    dim);
            exit(EXIT_FAILURE);
        }

        ++currentPoint;
    }

    fclose(file);

    if (currentPoint != lineCount)
    {
        fprintf(stderr, "Error: Expected to read %zu points but read %zu points\n", lineCount, currentPoint);
        freeDataset(&dataPoints);
        exit(EXIT_FAILURE);
    }

    return dataPoints;
}

/**
 * @brief Reads centroids from a whitespace-delimited text file.
 *
 * Treats each non-empty row as one centroid; columns are coordinates. Reuses the
 * allocation from `readDataset` and returns it as `Centroids` (ownership transfers
 * to the returned object; free with `freeCentroids`).
 *
 * @param filename Path to a space/tab-separated text file of doubles.
 * @return Centroids with `size == rows` and `dimensions == columns`.
 * @errors The errors of readDataset (open and read errors, inconsistent column counts, values that are not
 *         finite numbers, overlong lines). Prints the size at log level 2 and every centroid at level 3.
 * @note Skips whitespace-only lines; parsing uses the current C locale for `strtod`.
 */
Centroids readCentroids(const char* filename)
{
    Dataset points = readDataset(filename);
    Centroids centroids = {points.coordinates, points.size, points.size, points.dimensions};
    free(points.partitions);

    if (logLevel >= 2 && centroids.size > 0)
    {
        printf("Read %zu centroids from %s, each with %zu dimensions\n", centroids.size, filename,
               centroids.dimensions);
    }

    if (logLevel >= 3)
    {
        for (size_t i = 0; i < centroids.size; ++i)
        {
            printf("Centroid %zu (dimensions: %zu) attributes: ", i, centroids.dimensions);
            for (size_t j = 0; j < centroids.dimensions; ++j)
            {
                printf("%.0f ", centroidRow(&centroids, i)[j]);
            }
            printf("\n");
        }
    }

    return centroids;
}

/**
 * @brief Reads a single positive integer K from a file (strict, no extra content).
 */
size_t readKFromFile(const char* path)
{
    FILE* fp;
    if (FOPEN(fp, path, "rb") != 0)
    {
        handleFileError(path);
    }

    /* Skip UTF-8 BOM if present */
    int c1 = fgetc(fp);
    if (c1 == 0xEF)
    {
        int c2 = fgetc(fp);
        int c3 = fgetc(fp);
        if (!(c2 == 0xBB && c3 == 0xBF))
        {
            if (c3 != EOF) ungetc(c3, fp);
            if (c2 != EOF) ungetc(c2, fp);
            ungetc(c1, fp);
        }
    }
    else if (c1 != EOF)
    {
        ungetc(c1, fp);
    }

    long k = 0;
    if (fscanf(fp, "%ld", &k) != 1 || k <= 0)
    {
        fclose(fp);
        fprintf(stderr, "Bad K in %s\n", path);
        exit(EXIT_FAILURE);
    }

    /* Ensure no extra non-whitespace content remains */
    int ch;
    do
    {
        ch = fgetc(fp);
    } while (ch != EOF && isspace((unsigned char)ch));
    if (ch != EOF)
    {
        fclose(fp);
        fprintf(stderr, "Bad K (extra content) in %s\n", path);
        exit(EXIT_FAILURE);
    }

    fclose(fp);
    return (size_t)k;
}

/* Reuses destination capacity when possible; an empty source clears it. */
void deepCopyCentroids(const Centroids* source, Centroids* destination)
{
    if (source == destination) return;
    if (source->size == 0)
    {
        freeCentroids(destination);
        return;
    }
    if (destination->capacity < source->size || destination->dimensions != source->dimensions)
    {
        freeCentroids(destination);
        *destination = allocateCentroids(source->size, source->dimensions);
    }
    destination->size = source->size;
    copyCoordinates(destination->coordinates, source->coordinates, source->size * source->dimensions);
}

/**
 * @brief Sets all data point partitions to 0.
 */
void resetPartitions(Dataset* dataPoints)
{
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        dataPoints->partitions[i] = 0;
    }
}
