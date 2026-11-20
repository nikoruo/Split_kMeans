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

void generateRandomCentroids(size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids, Rng* rng);
void generateKMeansPlusPlusCentroids(size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids, Rng* rng);
void initialCentroids(InitMethod init, size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids,
                      Rng* rng);
void partitionStep(DataPoints* dataPoints, const Centroids* centroids);
void centroidStep(Centroids* centroids, const DataPoints* dataPoints);
double runKMeans(DataPoints* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker, KMeansStage stage);
double runKMeansWithTracking(DataPoints* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker);

#endif /* KMEANS_H */
