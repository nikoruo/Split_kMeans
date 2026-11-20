/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef KMEANS_H
#define KMEANS_H

#include "dataset.h"
#include "rng.h"
#include "tracking.h"

/** @brief Initial centroids of KM, RKM and RS (--init). */
typedef enum
{
    INIT_RANDOM,
    INIT_KMEANSPP
} InitMethod;

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

void generateRandomCentroids(size_t numCentroids, const Dataset* dataPoints, Centroids* centroids, Rng* rng);
void generateKMeansPlusPlusCentroids(size_t numCentroids, const Dataset* dataPoints, Centroids* centroids, Rng* rng);
void initialCentroids(InitMethod init, size_t numCentroids, const Dataset* dataPoints, Centroids* centroids,
                      Rng* rng);
void partitionStep(Dataset* dataPoints, const Centroids* centroids);
void centroidStep(Centroids* centroids, const Dataset* dataPoints);
double runKMeans(Dataset* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker, KMeansStage stage);
/* prePartition preserves the intra split's initial assignment. */
double runKMeansView(DatasetView* view, size_t iterations, Centroids* centroids, Tracker* tracker, KMeansStage stage,
                     bool prePartition);
double runKMeansWithTracking(Dataset* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker);

#endif /* KMEANS_H */
