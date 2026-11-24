/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef SPLIT_H
#define SPLIT_H

#include "algorithms.h"
#include "dataset.h"
#include "kmeans.h"
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

/* Owns a local two-centroid result and borrows k-means scratch; free with freeSplitScratch. */
typedef struct
{
    KMeansScratch* kmeans;
    ClusteringResult trial;
} SplitScratch;

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
/* Requires membership rebuilt for the current partitions and centroids. */
double indexedClusterSSE(const Dataset* dataPoints, const ClusterIndex* index, const Centroids* centroids,
                         size_t clusterLabel);
/* Uses the old cluster's slice immediately after one split, before rebuilding membership. */
void splitClusterSSEs(const Dataset* dataPoints, const ClusterIndex* index, const Centroids* centroids,
                      size_t clusterLabel, double* firstSSE, double* secondSSE);
/* Rebuilds membership and recomputes only marked nonempty cluster means. */
size_t updateAffectedCentroids(Dataset* dataPoints, ClusterIndex* index, Centroids* centroids, const bool* affected);

ClusteringResult allocateClusteringResult(size_t numCentroids, size_t dimensions);
void freeClusteringResult(ClusteringResult* result);
SplitScratch allocateSplitScratch(KMeansScratch* kmeans);
void freeSplitScratch(SplitScratch* scratch);
/* Split helpers require current membership, positive iteration limits and capacity for one more centroid. */
void splitClusterIntraCluster(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids,
                              size_t clusterToSplit, size_t localMaxIterations, Tracker* tracker, Rng* rng,
                              SplitScratch* scratch);
double splitClusterGlobal(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids,
                          size_t clusterToSplit, size_t globalMaxIterations, Tracker* tracker, Rng* rng,
                          KMeansScratch* scratch);
size_t localRepartition(Dataset* dataPoints, ClusterIndex* index, Centroids* centroids, size_t clusterToSplit,
                        RepartitionMode mode, bool* clustersAffected);
double tentativeSseDrop(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel, size_t localMaxIterations,
                        double originalClusterSSE, Rng* rng, SplitScratch* scratch);
/* Saves the final trial centroids and labels indexed by dataset row; trace is optional. */
double cacheTentativeSplit(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel,
                           size_t localMaxIterations, double originalClusterSSE, Centroids* trialCentroids,
                           size_t* trialLabels, Tracker* tracker, InnerTrace* trace, Rng* rng, SplitScratch* scratch);
void applyTentativeSplit(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids, size_t clusterLabel,
                         const Centroids* bestCentroids, const size_t* bestLabels);
double runBisectingTrial(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel, size_t localMaxIterations,
                         Centroids* trialCentroids, size_t* trialLabels, Tracker* tracker, Rng* rng,
                         SplitScratch* scratch);
size_t countClusterSizes(const Dataset* dataPoints, size_t numClusters, size_t* sizes);
void requireSplittableCluster(size_t splittable, size_t numClusters, Algorithm splitType);

#endif /* SPLIT_H */
