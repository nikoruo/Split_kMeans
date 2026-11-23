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
#include <math.h>
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
        capacity > (limit - 1 - 2 * pointCount) / 3)
    {
        fprintf(stderr, "Error: Invalid cluster index size\n");
        exit(EXIT_FAILURE);
    }

    ClusterIndex index = {0};
    index.indices = malloc((2 * pointCount + 3 * capacity + 1) * sizeof(size_t));
    handleMemoryError(index.indices);
    index.localPartitions = index.indices + pointCount;
    index.offsets = index.localPartitions + pointCount;
    index.sizes = index.offsets + capacity + 1;
    index.eligible = index.sizes + capacity;
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

    countClusterSizes(dataPoints, numClusters, index->sizes);
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
    size_t splittable = 0;
    for (size_t c = 0; c < numClusters; ++c)
    {
        index->sizes[c] = index->offsets[c + 1] - index->offsets[c];
        index->eligible[c] = 0;
        if (index->sizes[c] < 2) continue;
        const double* first = datasetRowConst(dataPoints, index->indices[index->offsets[c]]);
        for (size_t i = index->offsets[c] + 1; i < index->offsets[c + 1]; ++i)
        {
            if (!coordinatesEqual(first, datasetRowConst(dataPoints, index->indices[i]), dataPoints->dimensions))
            {
                index->eligible[c] = 1;
                ++splittable;
                break;
            }
        }
    }
    index->clusterCount = numClusters;
    index->dataset = dataPoints;
    return splittable;
}

static DatasetView clusterView(const Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel)
{
    assert(index->dataset == dataPoints && clusterLabel < index->clusterCount);
    assert(index->eligible[clusterLabel]);

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

static void selectSplitSeeds(const Dataset* dataPoints, const size_t* indices, size_t count, Rng* rng,
                             size_t* first, size_t* second)
{
    assert(count >= 2);
    *first = rng_below(rng, count);
    const double* firstPoint = datasetRowConst(dataPoints, indices[*first]);
    for (size_t attempt = 0; attempt < dataPoints->size; ++attempt)
    {
        size_t candidate = rng_below(rng, count);
        if (!coordinatesEqual(firstPoint, datasetRowConst(dataPoints, indices[candidate]), dataPoints->dimensions))
        {
            *second = candidate;
            return;
        }
    }

    size_t eligible = 0;
    for (size_t i = 0; i < count; ++i)
        if (!coordinatesEqual(firstPoint, datasetRowConst(dataPoints, indices[i]), dataPoints->dimensions)) ++eligible;
    if (eligible == 0)
    {
        fprintf(stderr, "Error: Split cluster has fewer than two distinct data points\n");
        exit(EXIT_FAILURE);
    }
    size_t rank = rng_below(rng, eligible);
    for (size_t i = 0; i < count; ++i)
    {
        if (!coordinatesEqual(firstPoint, datasetRowConst(dataPoints, indices[i]), dataPoints->dimensions))
        {
            if (rank == 0)
            {
                *second = i;
                return;
            }
            --rank;
        }
    }
    fprintf(stderr, "Error: Could not select a distinct split seed\n");
    exit(EXIT_FAILURE);
}

static ClusteringResult localTwoMeans(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel,
                                      size_t iterations, Tracker* tracker, KMeansStage stage, bool prePartition,
                                      size_t newCluster, Rng* rng, Centroids* bestCentroids, size_t* bestLabels)
{
    DatasetView view = clusterView(dataPoints, index, clusterLabel);
    size_t c1, c2;
    selectSplitSeeds(dataPoints, view.indices, view.size, rng, &c1, &c2);

    ClusteringResult result = allocateClusteringResult(2, dataPoints->dimensions);
    copyCoordinates(centroidRow(&result.centroids, 0), datasetViewRow(&view, c1), dataPoints->dimensions);
    copyCoordinates(centroidRow(&result.centroids, 1), datasetViewRow(&view, c2), dataPoints->dimensions);
    if (bestCentroids != NULL)
        result.sse = runKMeansViewBest(&view, iterations, &result.centroids, tracker, stage, prePartition,
                                       bestCentroids, bestLabels);
    else
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
 * Picks two coordinate-distinct points of the target cluster as seeds, runs k-means (K=2) on that
 * cluster's points only, then replaces the old centroid
 * with the first local centroid and appends the second as a new global centroid. Partitions for the
 * affected points are rewritten accordingly.
 *
 * @param dataPoints All points with current partitions.
 * @param centroids Global centroids; updated in-place (size increases by one).
 * @param clusterToSplit Index of the cluster to split.
 * @param localMaxIterations Max iterations for the local k-means refinement.
 * @param tracker Optional: `_inner.csv` rows of the local 2-means (stage split) and the ground truth for the level-3
 * log.
 * @errors Exits on allocation failure. Asserts that the cluster has at least two distinct points.
 */
void splitClusterIntraCluster(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids,
                              size_t clusterToSplit, size_t localMaxIterations, Tracker* tracker, Rng* rng)
{
    assert(centroids->size < centroids->capacity && centroids->dimensions == dataPoints->dimensions);
    ClusteringResult result = localTwoMeans(dataPoints, index, clusterToSplit, localMaxIterations, tracker,
                                            KM_STAGE_SPLIT, true, centroids->size, rng, NULL, NULL);
    appendSplitCentroids(centroids, clusterToSplit, &result.centroids);
    freeClusteringResult(&result);
}

/**
 * @brief Retained local-then-global split helper; the SKM-Global runner applies its cached trial instead.
 *
 * The two local seeds have distinct coordinates. The local result replaces the old centroid and is appended
 * as the new one; global k-means
 * then runs until the SSE stops improving (localMaxIterations is SIZE_MAX by default).
 *
 * @param dataPoints All points with current partitions.
 * @param centroids Global centroids; updated in place (size increases by one).
 * @param clusterToSplit Index of the cluster to split; it has at least two distinct points (asserted).
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
                                            KM_STAGE_SPLIT, false, centroids->size, rng, NULL, NULL);
    appendSplitCentroids(centroids, clusterToSplit, &result.centroids);

    trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

    double sse = runKMeans(dataPoints, localMaxIterations, centroids, tracker, KM_STAGE_GLOBAL);

    freeClusteringResult(&result);

    return sse;
}

/**
 * @brief SKM-Global-Basic split: two random points of the chosen cluster as centroids, then global k-means.
 *
 * Selects two coordinate-distinct points of the cluster, overwrites the original centroid
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
 * @errors Exits on allocation failure. Asserts that the cluster has at least two distinct points.
 * @note Runs as --algo skm-global-basic, the basic comparison for SKM-Global: global k-means starts right after
 *       the two random seeds, without the local 2-means. Whether this gives worse results is measured in 6.7.
 */
double splitClusterGlobal(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids,
                          size_t clusterToSplit, size_t globalMaxIterations, Tracker* tracker, Rng* rng)
{
    assert(centroids->size < centroids->capacity && centroids->dimensions == dataPoints->dimensions);
    assert(index->dataset == dataPoints && clusterToSplit < index->clusterCount);
    size_t clusterSize = index->sizes[clusterToSplit];
    assert(index->eligible[clusterToSplit]);
    const size_t* clusterIndices = index->indices + index->offsets[clusterToSplit];

    // Select two random points from the cluster to be the new centroids
    size_t c1, c2;
    selectSplitSeeds(dataPoints, clusterIndices, clusterSize, rng, &c1, &c2);

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

size_t updateAffectedCentroids(Dataset* dataPoints, ClusterIndex* index, Centroids* centroids, const bool* affected)
{
    size_t splittable = rebuildClusterIndex(index, dataPoints, centroids->size);
    for (size_t c = 0; c < centroids->size; ++c)
    {
        if (!affected[c]) continue;
        if (index->sizes[c] == 0)
        {
            if (logLevel >= 3) fprintf(stderr, "Warning: Cluster %zu has no points assigned.\n", c);
            continue;
        }

        double* mean = centroidRow(centroids, c);
        memset(mean, 0, centroids->dimensions * sizeof(*mean));
        for (size_t i = index->offsets[c]; i < index->offsets[c + 1]; ++i)
        {
            const double* point = datasetRowConst(dataPoints, index->indices[i]);
            for (size_t d = 0; d < centroids->dimensions; ++d) mean[d] += point[d];
        }
        for (size_t d = 0; d < centroids->dimensions; ++d) mean[d] /= index->sizes[c];
    }
    return splittable;
}

/**
 * @brief SKM-Local repartition after a split, using one fixed set of centroids.
 *
 * Both adds a global-nearest pass for the children. Inward compares every point to its current centroid
 * and both children; inward-nonchild skips the children. Rebuild membership and update touched means once.
 *
 * @param dataPoints Dataset with current partitions.
 * @param centroids Global centroids; the new centroid is at index `centroids->size - 1`.
 * @param clusterToSplit Index of the cluster that was split.
 * @param index Reusable membership index, rebuilt after the moves.
 * @param clustersAffected Initially clear array (size >= number of clusters), filled with touched clusters.
 * @return Number of clusters with at least two points.
 */
size_t localRepartition(Dataset* dataPoints, ClusterIndex* index, Centroids* centroids, size_t clusterToSplit,
                        RepartitionMode mode, bool* clustersAffected)
{
    assert(mode == REPARTITION_INWARD || mode == REPARTITION_INWARD_NONCHILD || mode == REPARTITION_BOTH);
    const size_t newClusterIndex = centroids->size - 1;
    size_t movedOutward = 0;
    size_t movedInward = 0;
    size_t childSwitches = 0;

    clustersAffected[clusterToSplit] = true;
    clustersAffected[newClusterIndex] = true;

    if (mode == REPARTITION_BOTH)
    {
        for (size_t i = 0; i < dataPoints->size; ++i)
        {
            if (dataPoints->partitions[i] == clusterToSplit || dataPoints->partitions[i] == newClusterIndex)
            {
                size_t nearestCentroid = findNearestCentroid(datasetRowConst(dataPoints, i), centroids);
                if (dataPoints->partitions[i] != nearestCentroid)
                {
                    clustersAffected[dataPoints->partitions[i]] = true;
                    clustersAffected[nearestCentroid] = true;
                    dataPoints->partitions[i] = nearestCentroid;
                    ++movedOutward;
                }
            }
        }
    }

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        size_t currentCluster = dataPoints->partitions[i];
        bool isChild = currentCluster == clusterToSplit || currentCluster == newClusterIndex;
        if (mode != REPARTITION_INWARD && isChild) continue;

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
            ++movedInward;
            if (isChild) ++childSwitches;
        }
    }

    size_t splittable = updateAffectedCentroids(dataPoints, index, centroids, clustersAffected);
    size_t touched = 0;
    for (size_t c = 0; c < centroids->size; ++c)
    {
        if (clustersAffected[c]) ++touched;
    }

    if (logLevel >= 3)
        printf("(SKM-Local) repartition: touched=%zu pass1=%zu pass2=%zu child=%zu\n",
               touched, movedOutward, movedInward, childSwitches);
    return splittable;
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
 * @return SSE drop (non-negative when the split helps); 0 for a cluster without two distinct points.
 * @errors Exits on allocation failure.
 */
double tentativeSseDrop(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel, size_t localMaxIterations,
                        double originalClusterSSE, Rng* rng)
{
    assert(index->dataset == dataPoints && clusterLabel < index->clusterCount);
    if (!index->eligible[clusterLabel]) return 0.0;
    ClusteringResult result = localTwoMeans(dataPoints, index, clusterLabel, localMaxIterations, NULL, KM_STAGE_NONE,
                                            false, SIZE_MAX, rng, NULL, NULL);
    double sseDrop = originalClusterSSE - result.sse;
    freeClusteringResult(&result);
    return sseDrop;
}

static double takeFiniteTrial(ClusteringResult* result, size_t clusterLabel)
{
    double sse = result->sse;
    freeClusteringResult(result);
    if (sse == DBL_MAX || !isfinite(sse))
    {
        fprintf(stderr, "Error: Cannot evaluate split of cluster %zu: no finite trial SSE\n", clusterLabel);
        exit(EXIT_FAILURE);
    }
    return sse;
}

double cacheTentativeSplit(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel,
                           size_t localMaxIterations, double originalClusterSSE, Centroids* bestCentroids,
                           size_t* bestLabels, Tracker* tracker, InnerTrace* trace, Rng* rng)
{
    assert(index->dataset == dataPoints && clusterLabel < index->clusterCount);
    assert(bestCentroids != NULL && bestLabels != NULL);
    if (!index->eligible[clusterLabel])
    {
        if (trace != NULL) trace->count = 0;
        return 0.0;
    }
    if (!isfinite(originalClusterSSE))
    {
        fprintf(stderr, "Error: Cannot evaluate split of cluster %zu: non-finite SSE\n", clusterLabel);
        exit(EXIT_FAILURE);
    }

    const bool capture = tracker != NULL && tracker->inner && trace != NULL;
    InnerTrace previous = {0};
    if (capture) previous = trackerCaptureBegin(tracker, trace);
    ClusteringResult result = localTwoMeans(dataPoints, index, clusterLabel, localMaxIterations,
                                            capture ? tracker : NULL, capture ? KM_STAGE_SPLIT : KM_STAGE_NONE,
                                            false, SIZE_MAX, rng, bestCentroids, bestLabels);
    if (capture) trackerCaptureEnd(tracker, trace, previous);
    return originalClusterSSE - takeFiniteTrial(&result, clusterLabel);
}

void applyTentativeSplit(Dataset* dataPoints, const ClusterIndex* index, Centroids* centroids, size_t clusterLabel,
                         const Centroids* bestCentroids, const size_t* bestLabels)
{
    assert(index->dataset == dataPoints && clusterLabel < index->clusterCount);
    assert(bestCentroids != NULL && bestLabels != NULL && index->eligible[clusterLabel]);
    const size_t newCluster = centroids->size;
    for (size_t i = index->offsets[clusterLabel]; i < index->offsets[clusterLabel + 1]; ++i)
    {
        size_t point = index->indices[i];
        assert(bestLabels[point] < 2);
        dataPoints->partitions[point] = bestLabels[point] == 0 ? clusterLabel : newCluster;
    }
    appendSplitCentroids(centroids, clusterLabel, bestCentroids);
}

/**
 * @brief One bisecting try: local k-means with K=2 on the points of `clusterLabel`.
 *
 * The seeds are two coordinate-distinct points of the cluster, drawn by row. Returns the two
 * centroids and the local SSE; the caller keeps the try with the lowest SSE and frees each result.
 *
 * @param dataPoints Dataset with current partitions; used to extract the cluster’s points.
 * @param clusterLabel Cluster id to bisect.
 * @param localMaxIterations Max iterations for the local k-means refinement.
 * @param tracker Optional: `_inner.csv` rows of the try (stage try) and the ground truth for the level-3 log.
 * @return ClusteringResult with two centroids and the SSE for the cluster's points.
 * @errors Exits on allocation failure. Asserts that the cluster has at least two distinct points.
 */
ClusteringResult tentativeSplitterForBisecting(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel,
                                               size_t localMaxIterations, Tracker* tracker, Rng* rng)
{
    return localTwoMeans(dataPoints, index, clusterLabel, localMaxIterations, tracker, KM_STAGE_TRY, false, SIZE_MAX,
                         rng, NULL, NULL);
}

double runBisectingTrial(Dataset* dataPoints, const ClusterIndex* index, size_t clusterLabel, size_t localMaxIterations,
                         Centroids* bestCentroids, size_t* bestLabels, Tracker* tracker, Rng* rng)
{
    assert(bestCentroids != NULL && bestLabels != NULL);
    ClusteringResult result = localTwoMeans(dataPoints, index, clusterLabel, localMaxIterations, tracker, KM_STAGE_TRY,
                                            false, SIZE_MAX, rng, bestCentroids, bestLabels);
    return takeFiniteTrial(&result, clusterLabel);
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
 * @brief Exits when no cluster contains two distinct coordinates.
 */
void requireSplittableCluster(size_t splittable, size_t numClusters, Algorithm splitType)
{
    if (splittable == 0)
    {
        fprintf(stderr, "Error: %s found no cluster with two distinct data points to split (K=%zu)\n",
                getAlgorithmName(splitType), numClusters);
        exit(EXIT_FAILURE);
    }
}
