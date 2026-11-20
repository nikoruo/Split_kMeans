/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef SPLIT_H
#define SPLIT_H

#include "algorithms.h"
#include "dataset.h"
#include "rng.h"
#include "tracking.h"

#include <stdbool.h>
#include <stddef.h>

/* Owns its centroid array; free with freeClusteringResult. */
typedef struct
{
    double sse;
    DataPoint* centroids;
} ClusteringResult;

ClusteringResult allocateClusteringResult(size_t numCentroids, size_t dimensions);
void freeClusteringResult(ClusteringResult* result, size_t numCentroids);
void splitClusterIntraCluster(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit,
                              size_t localMaxIterations, Tracker* tracker, Rng* rng);
double splitClusterGlobalV2(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit,
                            size_t localMaxIterations, Tracker* tracker, Rng* rng);
double splitClusterGlobal(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit,
                          size_t globalMaxIterations, Tracker* tracker, Rng* rng);
void localRepartition(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, bool* clustersAffected);
double tentativeSseDrop(DataPoints* dataPoints, size_t clusterLabel, size_t localMaxIterations,
                        double originalClusterSSE, Rng* rng);
ClusteringResult tentativeSplitterForBisecting(DataPoints* dataPoints, size_t clusterLabel, size_t localMaxIterations,
                                               Tracker* tracker, Rng* rng);
size_t countClusterSizes(const DataPoints* dataPoints, size_t numClusters, size_t* sizes);
void requireSplittableCluster(size_t splittable, size_t numClusters, Algorithm splitType);

#endif /* SPLIT_H */
