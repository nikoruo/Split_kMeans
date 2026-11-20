/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"

#include "split.h"
#include "kmeans.h"
#include "metrics.h"
#include "tracking.h"
#include "util.h"

#include <assert.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Frees the owned centroid buffer of a clustering result.
 */
void freeClusteringResult(ClusteringResult* result)
{
    if (result == NULL) return;
    freeCentroids(&result->centroids);
}

/**
 * @brief A clustering result with SSE DBL_MAX and `numCentroids` zeroed centroids of `dimensions` attributes.
 */
ClusteringResult allocateClusteringResult(size_t numCentroids, size_t dimensions)
{
    assert(numCentroids > 0 && dimensions > 0);

    ClusteringResult result;
    result.sse = DBL_MAX;
    result.centroids = allocateCentroids(numCentroids, dimensions);

    return result;
}

ClusterIndex allocateClusterIndex(size_t pointCount, size_t capacity)
{
    const size_t limit = SIZE_MAX / sizeof(size_t);
    if (pointCount == 0 || capacity == 0 || pointCount > (limit - 1) / 2 ||
        capacity > (limit - 1 - 2 * pointCount) / 2)
    {
        fprintf(stderr, "Error: Invalid cluster index size\n");
        exit(EXIT_FAILURE);
    }

    ClusterIndex index = {0};
    index.indices = malloc((2 * pointCount + 2 * capacity + 1) * sizeof(size_t));
    handleMemoryError(index.indices);
    index.localPartitions = index.indices + pointCount;
    index.offsets = index.localPartitions + pointCount;
    index.sizes = index.offsets + capacity + 1;
    index.pointCount = pointCount;
    index.capacity = capacity;
    return index;
}

void freeClusterIndex(ClusterIndex* index)
{
    if (index == NULL) return;
    free(index->indices);
    *index = (ClusterIndex){0};
}

size_t rebuildClusterIndex(ClusterIndex* index, const Dataset* dataPoints, size_t numClusters)
{
    assert(dataPoints->size == index->pointCount);
    assert(numClusters > 0 && numClusters <= index->capacity);

    size_t splittable = countClusterSizes(dataPoints, numClusters, index->sizes);
    index->offsets[0] = 0;
    for (size_t c = 0; c < numClusters; ++c)
    {
        index->offsets[c + 1] = index->offsets[c] + index->sizes[c];
        index->sizes[c] = index->offsets[c];
    }
    /* Stable slices preserve the original point and random-seed order. */
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        size_t label = dataPoints->partitions[i];
        index->indices[index->sizes[label]++] = i;
    }
    for (size_t c = 0; c < numClusters; ++c)
    {
        index->sizes[c] = index->offsets[c + 1] - index->offsets[c];
    }
    index->clusterCount = numClusters;
    index->dataset = dataPoints;
    return splittable;
}

static DatasetView clusterView(const Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel)
{
    assert(index->dataset == dataPoints && clusterLabel < index->clusterCount);
    assert(index->sizes[clusterLabel] >= 2);

    DatasetView view;
    view.source = dataPoints;
    view.size = index->sizes[clusterLabel];
    view.indices = index->indices + index->offsets[clusterLabel];
    view.partitions = index->localPartitions;
    for (size_t i = 0; i < view.size; ++i)
    {
        view.partitions[i] = dataPoints->partitions[view.indices[i]];
    }
    return view;
}

static ClusteringResult localTwoMeans(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel,
                                      size_t iterations, Tracker* tracker, KMeansStage stage, bool prePartition,
                                      size_t newCluster, Rng* rng)
{
    DatasetView view = clusterView(dataPoints, index, clusterLabel);
    size_t c1 = rng_below(rng, view.size);
    size_t c2 = c1;
    while (c2 == c1)
    {
        c2 = rng_below(rng, view.size);
    }

    ClusteringResult result = allocateClusteringResult(2, dataPoints->dimensions);
    copyCoordinates(centroidRow(&result.centroids, 0), datasetViewRow(&view, c1), dataPoints->dimensions);
    copyCoordinates(centroidRow(&result.centroids, 1), datasetViewRow(&view, c2), dataPoints->dimensions);
    result.sse = runKMeansView(&view, iterations, &result.centroids, tracker, stage, prePartition);

    if (newCluster != SIZE_MAX)
    {
        for (size_t i = 0; i < view.size; ++i)
        {
            dataPoints->partitions[view.indices[i]] = view.partitions[i] == 0 ? clusterLabel : newCluster;
        }
    }
    return result;
}

static void appendSplitCentroids(Centroids* centroids, size_t clusterToSplit, const Centroids* local)
{
    assert(centroids->size < centroids->capacity && local->size == 2);
    assert(centroids->dimensions == local->dimensions);
    copyCoordinates(centroidRow(centroids, clusterToSplit), centroidRowConst(local, 0), centroids->dimensions);
    ++centroids->size;
    copyCoordinates(centroidRow(centroids, centroids->size - 1), centroidRowConst(local, 1), centroids->dimensions);
}

/**
 * @brief Splits one cluster by running a local k-means within that cluster.
 *
 * Picks two different points of the target cluster as seeds (different indices; equal coordinates
 * are possible, 6.6), runs k-means (K=2) on that cluster's points only, then replaces the old centroid
 * with the first local centroid and appends the second as a new global centroid. Partitions for the
 * affected points are rewritten accordingly.
 *
 * @param dataPoints All points with current partitions.
 * @param centroids Global centroids; updated in-place (size increases by one).
 * @param clusterToSplit Index of the cluster to split.
 * @param localMaxIterations Max iterations for the local k-means refinement.
 * @param tracker Optional: `_inner.csv` rows of the local 2-means (stage split) and the ground truth for the level-3
 * log.
 * @errors Exits on allocation failure. Asserts that the cluster has at least 2 points.
 */
void splitClusterIntraCluster(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids,
                              size_t clusterToSplit, size_t localMaxIterations, Tracker* tracker, Rng* rng)
{
    assert(centroids->size < centroids->capacity && centroids->dimensions == dataPoints->dimensions);
    ClusteringResult result = localTwoMeans(dataPoints, index, clusterToSplit, localMaxIterations, tracker,
                                            KM_STAGE_SPLIT, true, centroids->size, rng);
    appendSplitCentroids(centroids, clusterToSplit, &result.centroids);
    freeClusteringResult(&result);
}

/**
 * @brief SKM-Global split: local 2-means within the chosen cluster, then global k-means over all data.
 *
 * The two local seeds are two different points of the cluster (different indices; equal coordinates are
 * possible, 6.6). The local result replaces the old centroid and is appended as the new one; global k-means
 * then runs until the SSE stops improving (localMaxIterations is SIZE_MAX by default).
 *
 * @param dataPoints All points with current partitions.
 * @param centroids Global centroids; updated in place (size increases by one).
 * @param clusterToSplit Index of the cluster to split; it has at least 2 points (asserted).
 * @param localMaxIterations Max iterations for both the local and the global k-means.
 * @param tracker Optional: the after-split snapshot, the `_inner.csv` rows (stages split and global) and the ground
 * truth for the level-3 log.
 * @return SSE after the global k-means.
 */
double splitClusterGlobalV2(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids,
                            size_t clusterToSplit, size_t localMaxIterations, Tracker* tracker, Rng* rng)
{
    assert(centroids->size < centroids->capacity && centroids->dimensions == dataPoints->dimensions);
    ClusteringResult result = localTwoMeans(dataPoints, index, clusterToSplit, localMaxIterations, tracker,
                                            KM_STAGE_SPLIT, false, centroids->size, rng);
    appendSplitCentroids(centroids, clusterToSplit, &result.centroids);

    trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

    double sse = runKMeans(dataPoints, localMaxIterations, centroids, tracker, KM_STAGE_GLOBAL);

    freeClusteringResult(&result);

    return sse;
}

/**
 * @brief SKM-Global-Basic split: two random points of the chosen cluster as centroids, then global k-means.
 *
 * Selects two different points of the cluster (different indices), overwrites the original centroid
 * with the first, appends the second as a new centroid, then runs global k-means for
 * `globalMaxIterations` to refine assignments over all data.
 *
 * @param dataPoints All points with current partitions.
 * @param centroids Global centroids; updated in-place (size increases by one).
 * @param clusterToSplit Index of the cluster to split.
 * @param globalMaxIterations Max iterations for the global refinement.
 * @param tracker Optional: the after-split snapshot, the `_inner.csv` rows (stage global) and the ground truth for the
 * level-3 log.
 * @return SSE after global refinement.
 * @errors Exits on allocation failure. Asserts that the cluster has at least 2 points.
 * @note Runs as --algo skm-global-basic, the basic comparison for SKM-Global: global k-means starts right after
 *       the two random seeds, without the local 2-means. Whether this gives worse results is measured in 6.7.
 */
double splitClusterGlobal(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids,
                          size_t clusterToSplit, size_t globalMaxIterations, Tracker* tracker, Rng* rng)
{
    assert(centroids->size < centroids->capacity && centroids->dimensions == dataPoints->dimensions);
    assert(index->dataset == dataPoints && clusterToSplit < index->clusterCount);
    size_t clusterSize = index->sizes[clusterToSplit];
    assert(clusterSize >= 2);
    const size_t* clusterIndices = index->indices + index->offsets[clusterToSplit];

    // Select two random points from the cluster to be the new centroids
    size_t c1 = rng_below(rng, clusterSize);
    size_t c2 = c1;
    while (c2 == c1)
    {
        c2 = rng_below(rng, clusterSize);
    }

    // Get the indices of the randomly selected data points
    size_t datapoint1 = clusterIndices[c1];
    size_t datapoint2 = clusterIndices[c2];

    // Add the first new centroid (overwrite the current centroid at clusterToSplit)
    copyCoordinates(centroidRow(centroids, clusterToSplit), datasetRowConst(dataPoints, datapoint1),
                    dataPoints->dimensions);

    // Add the second new centroid to the global centroids list
    centroids->size++;
    copyCoordinates(centroidRow(centroids, centroids->size - 1), datasetRowConst(dataPoints, datapoint2),
                    dataPoints->dimensions);

    trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

    // Run global k-means, this time including the new centroids
    double resultSse = runKMeans(dataPoints, globalMaxIterations, centroids, tracker, KM_STAGE_GLOBAL);

    return resultSse;
}

/**
 * @brief SKM-Local repartition after a split, in two passes over the same centroids.
 *
 * Pass 1: every point of the split cluster and of the new cluster moves to its nearest centroid among all
 * centroids. Pass 2: every other point moves to the split or the new centroid when one of them is closer than
 * its own (the nearer of the two). Pass 1 marks both clusters of a moved point in `clustersAffected`, pass 2
 * the cluster it left (the caller marks the split and the new cluster). No centroid is recomputed here (6.1).
 *
 * @param dataPoints Dataset with current partitions.
 * @param centroids Global centroids; the new centroid is at index `centroids->size - 1`.
 * @param clusterToSplit Index of the cluster that was split.
 * @param clustersAffected Output array (size >= number of clusters) marking touched clusters.
 * @note O(N·D); indices are not checked.
 */
void localRepartition(Dataset* dataPoints, Centroids* centroids, size_t clusterToSplit, bool* clustersAffected)
{
    size_t newClusterIndex = centroids->size - 1;

    // new clusters -> old clusters
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->partitions[i] == clusterToSplit || dataPoints->partitions[i] == newClusterIndex)
        {
            size_t nearestCentroid = findNearestCentroid(datasetRowConst(dataPoints, i), centroids);

            if (dataPoints->partitions[i] != nearestCentroid)
            {
                clustersAffected[dataPoints->partitions[i]] = true; // Mark the old cluster as affected
                clustersAffected[nearestCentroid] = true;                 // Mark the new cluster as affected
                dataPoints->partitions[i] = nearestCentroid;
            }
        }
    }

    // old clusters -> new clusters
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        size_t currentCluster = dataPoints->partitions[i];

        // skip new cluster -> old cluster
        if (currentCluster == clusterToSplit || currentCluster == newClusterIndex) continue;

        // Calculate distance to the current centroid
        double currentDistance = calculateSquaredEuclideanDistance(datasetRowConst(dataPoints, i),
                                                                    centroidRowConst(centroids, currentCluster),
                                                                    dataPoints->dimensions);

        // Calculate distance to the cluster to be split
        double distanceToSplit = calculateSquaredEuclideanDistance(datasetRowConst(dataPoints, i),
                                                                    centroidRowConst(centroids, clusterToSplit),
                                                                    dataPoints->dimensions);

        // Calculate distance to the new cluster
        double distanceToNew = calculateSquaredEuclideanDistance(datasetRowConst(dataPoints, i),
                                                                  centroidRowConst(centroids, newClusterIndex),
                                                                  dataPoints->dimensions);

        if (distanceToSplit < currentDistance || distanceToNew < currentDistance)
        {
            clustersAffected[currentCluster] = true; // Mark the old cluster as affected
            dataPoints->partitions[i] = distanceToSplit <= distanceToNew ? clusterToSplit : newClusterIndex;
        }
    }
}

/**
 * @brief Estimates SSE improvement from splitting one cluster with local k-means.
 *
 * Views the points of `clusterLabel`, initializes two seeds from that set,
 * runs k-means with K=2 locally, and returns the decrease relative to the
 * original single-cluster SSE (`originalClusterSSE - localSseAfterSplit`).
 *
 * @param dataPoints Dataset with current partitions; used to extract the cluster’s points.
 * @param clusterLabel Cluster id to test a tentative split for.
 * @param localMaxIterations Max iterations for the local k-means refinement.
 * @param originalClusterSSE Current SSE of the cluster before splitting.
 * @return SSE drop (non-negative when the split helps); 0 for a cluster with fewer than 2 points.
 * @errors Exits on allocation failure.
 */
double tentativeSseDrop(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel, size_t localMaxIterations,
                        double originalClusterSSE, Rng* rng)
{
    assert(index->dataset == dataPoints && clusterLabel < index->clusterCount);
    if (index->sizes[clusterLabel] < 2) return 0.0;
    ClusteringResult result = localTwoMeans(dataPoints, index, clusterLabel, localMaxIterations, NULL, KM_STAGE_NONE,
                                            false, SIZE_MAX, rng);
    double sseDrop = originalClusterSSE - result.sse;
    freeClusteringResult(&result);
    return sseDrop;
}

/**
 * @brief One bisecting try: local k-means with K=2 on the points of `clusterLabel`.
 *
 * The seeds are two different points of the cluster (different indices, drawn uniformly). Returns the two
 * centroids and the local SSE; the caller keeps the try with the lowest SSE and frees each result.
 *
 * @param dataPoints Dataset with current partitions; used to extract the cluster’s points.
 * @param clusterLabel Cluster id to bisect.
 * @param localMaxIterations Max iterations for the local k-means refinement.
 * @param tracker Optional: `_inner.csv` rows of the try (stage try) and the ground truth for the level-3 log.
 * @return ClusteringResult with two centroids and the SSE for the cluster's points.
 * @errors Exits on allocation failure. Asserts that the cluster has at least 2 points.
 */
ClusteringResult tentativeSplitterForBisecting(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel,
                                               size_t localMaxIterations, Tracker* tracker, Rng* rng)
{
    return localTwoMeans(dataPoints, index, clusterLabel, localMaxIterations, tracker, KM_STAGE_TRY, false, SIZE_MAX,
                         rng);
}

/**
 * @brief Counts the points of clusters 0..numClusters-1 into `sizes`; returns how many have at least 2 points.
 */
size_t countClusterSizes(const Dataset* dataPoints, size_t numClusters, size_t* sizes)
{
    memset(sizes, 0, numClusters * sizeof(size_t));
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        assert(dataPoints->partitions[i] < numClusters);
        sizes[dataPoints->partitions[i]]++;
    }

    size_t splittable = 0;
    for (size_t c = 0; c < numClusters; ++c)
    {
        if (sizes[c] >= 2) splittable++;
    }
    return splittable;
}

/**
 * @brief Exits when no cluster has 2 or more points. Cannot happen while K <= N; reports a bug instead of hanging.
 */
void requireSplittableCluster(size_t splittable, size_t numClusters, Algorithm splitType)
{
    if (splittable == 0)
    {
        fprintf(stderr, "Error: %s found no cluster with 2 or more points to split (K=%zu)\n",
                getAlgorithmName(splitType), numClusters);
        exit(EXIT_FAILURE);
    }
}
