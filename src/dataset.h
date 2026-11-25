/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef DATASET_H
#define DATASET_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "sha256.h"

/* Owns an immutable raw-byte snapshot; release it with freeInputSnapshot. */
typedef struct
{
    unsigned char* bytes;
    size_t length;
    char sha256[SHA256_HEX_SIZE];
} InputSnapshot;

/* Owns N * D row-major coordinates and N labels; free with freeDataset. */
typedef struct
{
    double* coordinates;
    size_t* partitions; /* SIZE_MAX means unassigned. */
    size_t size;
    size_t dimensions;
} Dataset;

/* Owns capacity * D row-major coordinates; free with freeCentroids. */
typedef struct
{
    double* coordinates;
    size_t size;
    size_t capacity;
    size_t dimensions;
} Centroids;

/* Row pointers are borrowed and remain valid until the owning buffer is freed or reallocated. */
static inline double* datasetRow(Dataset* dataset, size_t index)
{
    assert(index < dataset->size);
    return dataset->coordinates + index * dataset->dimensions;
}

static inline const double* datasetRowConst(const Dataset* dataset, size_t index)
{
    assert(index < dataset->size);
    return dataset->coordinates + index * dataset->dimensions;
}

static inline double* centroidRow(Centroids* centroids, size_t index)
{
    assert(index < centroids->size);
    return centroids->coordinates + index * centroids->dimensions;
}

static inline const double* centroidRowConst(const Centroids* centroids, size_t index)
{
    assert(index < centroids->size);
    return centroids->coordinates + index * centroids->dimensions;
}

/* Equal pointers or non-overlapping ranges of at least dimensions doubles. */
static inline void copyCoordinates(double* destination, const double* source, size_t dimensions)
{
    if (destination != source) memcpy(destination, source, dimensions * sizeof(double));
}

static inline bool coordinatesEqual(const double* first, const double* second, size_t dimensions)
{
    for (size_t d = 0; d < dimensions; ++d)
        if (first[d] != second[d]) return false;
    return true;
}

/* Allocations require positive sizes and dimensions; invalid shapes or allocation failures exit. */
Dataset allocateDataset(size_t size, size_t dimensions);
Centroids allocateCentroids(size_t size, size_t dimensions);
void freeDataset(Dataset* dataset);
void freeCentroids(Centroids* centroids);
/* Destination must be initialized and independently owned, unless it is source; copying may reallocate it. */
void deepCopyCentroids(const Centroids* source, Centroids* destination);
void resetPartitions(Dataset* dataset);
/* Requires a nonempty dataset, positive dimensions and positive limit; returns the distinct-row count up to that limit. */
size_t countDistinctRowsUpTo(const Dataset* dataset, size_t limit);

/* Returned datasets and centroids are owned by the caller. */
Dataset readDataset(const char* filename);
Centroids readCentroids(const char* filename);
InputSnapshot readInputSnapshot(const char* filename);
void freeInputSnapshot(InputSnapshot* snapshot);
/* Parse a live snapshot; filename is used only in diagnostics. */
Dataset readDatasetSnapshot(const InputSnapshot* snapshot, const char* filename);
Centroids readCentroidsSnapshot(const InputSnapshot* snapshot, const char* filename);
size_t readKFromFile(const char* path);

#endif /* DATASET_H */
