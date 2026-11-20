/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef DATASET_H
#define DATASET_H

#include <stddef.h>

/**
 * @brief Represents a single data point in a multi-dimensional space.
 *
 * The point owns its `attributes` array. `partition` is a zero-based cluster label;
 * `SIZE_MAX` indicates "unassigned".
 */
typedef struct
{
    double* attributes;  /**< Coordinates of the data point; length == dimensions. */
    size_t dimensions;   /**< Number of dimensions (length of the attributes array). */
    size_t partition;    /**< Zero-based cluster label; SIZE_MAX means unassigned. */
} DataPoint;

/**
 * @brief A contiguous collection of data points.
 *
 * Ownership: the container owns the array and each element's `attributes`.
 * Use `freeDataPoints` to release.
 */
typedef struct
{
    DataPoint* points;   /**< Array of DataPoint structures (size elements). */
    size_t size;         /**< Number of data points. */
} DataPoints;

/**
 * @brief A collection of centroids (cluster representatives).
 *
 * Ownership: the container owns the array and each element's `attributes`.
 * Use `freeCentroids` to release.
 */
typedef struct
{
    DataPoint* points;   /**< Array of centroid points (size elements). */
    size_t size;         /**< Number of centroids (K). */
} Centroids;

/* The caller owns the allocated points and releases them with the matching free functions. */
DataPoint allocateDataPoint(size_t dimensions);
DataPoints allocateDataPoints(size_t size, size_t dimensions);
Centroids allocateCentroids(size_t size, size_t dimensions);
void deinitDataPoint(DataPoint* point);
void freeDataPointArray(DataPoint* points, size_t size);
void freeDataPoints(DataPoints* dataPoints);
void freeCentroids(Centroids* centroids);
void deepCopyDataPoint(DataPoint* destination, const DataPoint* source);
void deepCopyCentroids(const Centroids* source, Centroids* destination);
void resetPartitions(DataPoints* dataPoints);

/* Returned datasets and centroids are owned by the caller. */
DataPoints readDataPoints(const char* filename);
Centroids readCentroids(const char* filename, int logLevel);
size_t readKFromFile(const char* path);

#endif /* DATASET_H */
