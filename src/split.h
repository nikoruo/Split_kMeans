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
    Centroids centroids;
} ClusteringResult;

/* Owns membership and local-label scratch; rebuild after the dataset's partitions change. */
typedef struct
{
    size_t* indices;
    size_t* localPartitions;
    size_t* offsets;
    size_t* sizes;
    size_t* eligible;
    size_t pointCount;
    size_t capacity;
    size_t clusterCount;
    const Dataset* dataset;
} ClusterIndex;

ClusterIndex allocateClusterIndex(size_t pointCount, size_t capacity);
void freeClusterIndex(ClusterIndex* index);
/* Rebuilds stable slices and returns the number with at least two distinct coordinates. */
size_t rebuildClusterIndex(ClusterIndex* index, const Dataset* dataPoints, size_t numClusters);
/* Rebuilds membership and recomputes only marked nonempty cluster means. */
size_t updateAffectedCentroids(Dataset* dataPoints, ClusterIndex* index, Centroids* centroids, const bool* affected);

ClusteringResult allocateClusteringResult(size_t numCentroids, size_t dimensions);
void freeClusteringResult(ClusteringResult* result);
/* Split helpers require current membership and capacity for one more centroid. */
void splitClusterIntraCluster(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids,
                              size_t clusterToSplit, size_t localMaxIterations, Tracker* tracker, Rng* rng);
double splitClusterGlobalV2(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids,
                            size_t clusterToSplit, size_t localMaxIterations, Tracker* tracker, Rng* rng);
double splitClusterGlobal(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids,
                          size_t clusterToSplit, size_t globalMaxIterations, Tracker* tracker, Rng* rng);
size_t localRepartition(Dataset* dataPoints, ClusterIndex* index, Centroids* centroids, size_t clusterToSplit,
                        RepartitionMode mode, bool* clustersAffected);
double tentativeSseDrop(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel, size_t localMaxIterations,
                        double originalClusterSSE, Rng* rng);
/* Saves the best trial centroids and labels indexed by dataset row; trace is optional. */
double cacheTentativeSplit(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel,
                           size_t localMaxIterations, double originalClusterSSE, Centroids* bestCentroids,
                           size_t* bestLabels, Tracker* tracker, InnerTrace* trace, Rng* rng);
void applyTentativeSplit(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids, size_t clusterLabel,
                         const Centroids* bestCentroids, const size_t* bestLabels);
ClusteringResult tentativeSplitterForBisecting(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel,
                                               size_t localMaxIterations, Tracker* tracker, Rng* rng);
double runBisectingTrial(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel, size_t localMaxIterations,
                         Centroids* bestCentroids, size_t* bestLabels, Tracker* tracker, Rng* rng);
size_t countClusterSizes(const Dataset* dataPoints, size_t numClusters, size_t* sizes);
void requireSplittableCluster(size_t splittable, size_t numClusters, Algorithm splitType);

#endif /* SPLIT_H */
