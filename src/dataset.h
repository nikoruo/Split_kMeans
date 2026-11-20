/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef DATASET_H
#define DATASET_H

#include <assert.h>
#include <stddef.h>
#include <string.h>

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

/* Row pointers are borrowed and remain valid until the owning buffer is freed. */
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

static inline void copyCoordinates(double* destination, const double* source, size_t dimensions)
{
    if (destination != source) memcpy(destination, source, dimensions * sizeof(double));
}

Dataset allocateDataset(size_t size, size_t dimensions);
Centroids allocateCentroids(size_t size, size_t dimensions);
void freeDataset(Dataset* dataset);
void freeCentroids(Centroids* centroids);
void deepCopyCentroids(const Centroids* source, Centroids* destination);
void resetPartitions(Dataset* dataset);

/* Returned datasets and centroids are owned by the caller. */
Dataset readDataset(const char* filename);
Centroids readCentroids(const char* filename);
size_t readKFromFile(const char* path);

#endif /* DATASET_H */
