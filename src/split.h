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

/* Owns a local two-centroid result and borrows dimension-compatible k-means scratch with capacity >= 2.
 * The scratch must outlive this object; free the result with freeSplitScratch. */
typedef struct
{
    KMeansScratch* kmeans;
    ClusteringResult trial;
} SplitScratch;

/* Owns one backing allocation for membership and local-label slices; borrows the dataset after rebuild.
 * Rebuild after coordinates or partitions change before another split/trial, with labels below the cluster count. */
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
/* Rebuilds stable slices for pointCount rows and 1 <= numClusters <= capacity; returns the count with
 * at least two distinct coordinates. */
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
/* clustersAffected has at least K initially clear entries; clusterToSplit is below K-1. Rebuilds the index. */
size_t localRepartition(Dataset* dataPoints, ClusterIndex* index, Centroids* centroids, size_t clusterToSplit,
                        RepartitionMode mode, bool* clustersAffected);
double tentativeSseDrop(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel, size_t localMaxIterations,
                        double originalClusterSSE, Rng* rng, SplitScratch* scratch);
/* For an eligible cluster, writes two dimension-matched trial centroids and labels for its members into a
 * dataset-sized buffer.
 * For an ineligible cluster returns 0 without writing those outputs; an optional trace's count is reset. */
double cacheTentativeSplit(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel,
                           size_t localMaxIterations, double originalClusterSSE, Centroids* trialCentroids,
                           size_t* trialLabels, Tracker* tracker, InnerTrace* trace, Rng* rng, SplitScratch* scratch);
/* Requires cached labels for every current member; applying them invalidates the membership index. */
void applyTentativeSplit(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids, size_t clusterLabel,
                         const Centroids* bestCentroids, const size_t* bestLabels);
/* Requires an eligible cluster, positive iterations, two centroids and dataset-sized labels; writes member labels only. */
double runBisectingTrial(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel, size_t localMaxIterations,
                         Centroids* trialCentroids, size_t* trialLabels, Tracker* tracker, Rng* rng,
                         SplitScratch* scratch);
size_t countClusterSizes(const Dataset* dataPoints, size_t numClusters, size_t* sizes);
void requireSplittableCluster(size_t splittable, size_t numClusters, Algorithm splitType);

#endif /* SPLIT_H */
