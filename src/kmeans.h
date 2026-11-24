/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef KMEANS_H
#define KMEANS_H

#include "dataset.h"
#include "rng.h"
#include "tracking.h"

/** @brief Coordinate-distinct initial centroids of KM, RKM and RS (--init). */
typedef enum
{
    INIT_RANDOM,
    INIT_KMEANSPP
} InitMethod;

/* Owns centroid sums and counts; free with freeKMeansScratch. */
typedef struct KMeansScratch
{
    Centroids sums;
    size_t* counts;
} KMeansScratch;

/* Owns the index or distance buffer for one initialization method; free with freeInitScratch. */
typedef struct
{
    size_t* indices;
    double* distances;
    size_t capacity;
} InitScratch;

/* Borrows the source coordinates, row indices and local partition buffer. */
typedef struct
{
    const Dataset* source;
    const size_t* indices;
    size_t* partitions;
    size_t size;
} DatasetView;

static inline const double* datasetViewRow(const DatasetView* view, size_t index)
{
    assert(index < view->size);
    return datasetRowConst(view->source, view->indices[index]);
}

KMeansScratch allocateKMeansScratch(size_t capacity, size_t dimensions);
void freeKMeansScratch(KMeansScratch* scratch);
InitScratch allocateInitScratch(size_t pointCount, InitMethod init);
void freeInitScratch(InitScratch* scratch);
/* Scratch must match the initialization method and hold all dataset rows. */
void generateRandomCentroids(size_t numCentroids, const Dataset* dataPoints, Centroids* centroids, Rng* rng,
                             InitScratch* scratch);
void generateKMeansPlusPlusCentroids(size_t numCentroids, const Dataset* dataPoints, Centroids* centroids, Rng* rng,
                                     InitScratch* scratch);
void initialCentroids(InitMethod init, size_t numCentroids, const Dataset* dataPoints, Centroids* centroids,
                      Rng* rng, InitScratch* scratch);
void partitionStep(Dataset* dataPoints, const Centroids* centroids);
/* Scratch must match the dimensionality and have capacity for the current centroids. */
void centroidStep(Centroids* centroids, const Dataset* dataPoints, KMeansScratch* scratch);
/* Recomputes only marked cluster means, retaining empty and unmarked centroids. */
void centroidStepAffected(Centroids* centroids, const Dataset* dataPoints, const bool* affected,
                          KMeansScratch* scratch);
/* K-means calls require positive iterations and matching scratch; they return the final state's SSE. */
double runKMeans(Dataset* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker, KMeansStage stage,
                 KMeansScratch* scratch);
double runKMeansView(DatasetView* view, size_t iterations, Centroids* centroids, Tracker* tracker, KMeansStage stage,
                     KMeansScratch* scratch);
double runKMeansWithTracking(Dataset* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker,
                             KMeansScratch* scratch);

#endif /* KMEANS_H */
