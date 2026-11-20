/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"

#include "baselines.h"
#include "kmeans.h"
#include "metrics.h"
#include "split.h"
#include "tracking.h"
#include "util.h"

#include <assert.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Random Swap local repartition after one centroid moved, in two passes.
 *
 * Pass 1: every point of the swapped cluster (`removed`) moves to its nearest centroid. Pass 2: every other
 * point moves to the swapped centroid when it is closer than its own. Other points keep their labels.
 *
 * @param dataPoints Dataset with current partitions.
 * @param centroids Centroids after the swap; index `removed` denotes the modified centroid.
 * @param removed Index of the centroid whose coordinates were changed; must be < centroids->size (not checked).
 * @note O(N·D). The centroids are not updated here (the step-4 update is commented out, 6.4); the two k-means
 * iterations follow.
 */
void localRepartitionForRS(Dataset* dataPoints, Centroids* centroids, size_t removed)
{
    // from removed -> to existing
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->partitions[i] == removed)
        {
            size_t nearestCentroid = findNearestCentroid(datasetRowConst(dataPoints, i), centroids);
            dataPoints->partitions[i] = nearestCentroid;
        }
    }

    // from existing -> to created
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->partitions[i] != removed)
        {
            size_t currentPartition = dataPoints->partitions[i];

            // Calculate distance to the current centroid
            double distanceToCurrent = calculateSquaredEuclideanDistance(datasetRowConst(dataPoints, i),
                                                                          centroidRowConst(centroids, currentPartition),
                                                                          dataPoints->dimensions);

            // Calculate distance to the removed centroid
            double distanceToRemoved = calculateSquaredEuclideanDistance(datasetRowConst(dataPoints, i),
                                                                          centroidRowConst(centroids, removed),
                                                                          dataPoints->dimensions);

            // Assign to the closer centroid
            if (distanceToRemoved < distanceToCurrent)
            {
                dataPoints->partitions[i] = removed;
            }
        }
    }

    // centroidStep(centroids, dataPoints);
}

/**
 * @brief Random Swap: moves a random centroid to a random data point, runs two k-means iterations and keeps the swap if
 * the SSE improves.
 *
 * A rejected swap restores the centroids and partitions. The tracker records the initial
 * solution (count 0) and every improvement (count = swap number); CI is computed only for
 * the tracker and the level-2 log, with the stopwatch paused.
 *
 * @param dataPoints Dataset; its partitions must belong to `centroids` on entry.
 * @param centroids Initial centroids; updated in place to the best solution.
 * @param maxSwaps Number of trial swaps.
 * @return The best SSE found.
 */
double randomSwap(Dataset* dataPoints, Centroids* centroids, size_t maxSwaps, Tracker* tracker, Rng* rng)
{
    double bestSse = DBL_MAX;
    size_t kMeansIterations = 2;
    size_t numCentroids = centroids->size;
    size_t dimensions = centroids->dimensions;
    assert(dataPoints->dimensions == dimensions);

    size_t totalAttributes = numCentroids * dimensions;

    double* backupAttributes = malloc(totalAttributes * sizeof(double));
    handleMemoryError(backupAttributes);

    size_t* backupPartitions = malloc(dataPoints->size * sizeof(size_t));
    handleMemoryError(backupPartitions);

    size_t bestCI = trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);

    for (size_t i = 0; i < maxSwaps; ++i)
    {
        if (logLevel >= 3) printf("Swap %zu\n", i + 1);

        // Backup
        memcpy(backupAttributes, centroids->coordinates, totalAttributes * sizeof(double));
        memcpy(backupPartitions, dataPoints->partitions, dataPoints->size * sizeof(size_t));

        // Swap
        size_t randomCentroidId = rng_below(rng, centroids->size);
        size_t randomDataPointId = rng_below(rng, dataPoints->size);

        copyCoordinates(centroidRow(centroids, randomCentroidId), datasetRowConst(dataPoints, randomDataPointId),
                        dimensions);

        localRepartitionForRS(dataPoints, centroids, randomCentroidId);

        // K-means
        double resultSse = runKMeans(dataPoints, kMeansIterations, centroids, tracker, KM_STAGE_NONE);

        // If 1) SSE improves, we keep the change
        // if not, 2) we reverse the swap
        if (resultSse < bestSse)
        {
            bestSse = resultSse;

            // Row and snapshot for each improvement, with its swap number (decided 2026-11-19)
            bestCI = trackerRecord(tracker, i + 1, "step", dataPoints, centroids, SIZE_MAX);

            // Alternative: the row without the snapshot files
            // trackerAddRow(tracker, i + 1, resultSse, trackerCi(tracker, centroids));

            if (logLevel >= 2)
            {
                if (bestCI == CI_NA) bestCI = trackerCi(tracker, centroids);
                char ciText[CI_TEXT_SIZE];
                printf("(RS) Swap %zu: Best CI: %s and Best SSE: %.5f\n", i + 1, formatCi(bestCI, ciText), resultSse);
            }
        }
        else
        {
            memcpy(centroids->coordinates, backupAttributes, totalAttributes * sizeof(double));
            memcpy(dataPoints->partitions, backupPartitions, dataPoints->size * sizeof(size_t));

            // Alternative (v1): one row per swap; a rejected swap repeats the best solution so far
            // trackerAddRow(tracker, i + 1, bestSse, bestCI);

            // Alternative: row and snapshot also for a rejected swap (the restored best solution)
            // trackerRecord(tracker, i + 1, "step", dataPoints, centroids, SIZE_MAX);
        }
    }

    free(backupAttributes);
    free(backupPartitions);

    return bestSse;
}

/**
 * @brief Bisecting k-means: grows K by splitting the cluster with the largest SSE.
 *
 * The first split (to K=2) is one local 2-means. Then each split picks, among the clusters with at least 2
 * points, the one with the largest stored SSE, runs `bisectingIterations` local 2-means tries on it, places the
 * two centroids of the try with the lowest SSE, and reassigns every point with a global partition step. Only the
 * stored SSEs of the two new clusters are updated, although the partition step can move points of other clusters
 * too (6.3). A final k-means follows.
 *
 * @param dataPoints Dataset; partitions updated in-place across splits.
 * @param centroids Global centroids; grown in-place up to maxCentroids.
 * @param maxCentroids Target number of clusters (K); K=1 skips the split.
 * @param maxIterations Max iterations for the local k-means and final global k-means.
 * @param bisectingIterations Local 2-means tries per split; at least 1 (main checks it).
 * @param tracker Records each split (count = split number) and the final k-means; carries the ground truth for CI.
 * @return SSE after the final k-means.
 * @errors Exits on allocation failures via helpers.
 */
double runBisectingKMeans(Dataset* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations,
                          size_t bisectingIterations, Tracker* tracker, Rng* rng)
{
    assert(centroids->capacity >= maxCentroids);
    // The initial split always makes two clusters, so the list holds at least two
    double* SseList = malloc((maxCentroids < 2 ? 2 : maxCentroids) * sizeof(double));
    handleMemoryError(SseList);
    ClusterIndex membership = {0};
    if (maxCentroids > 1) membership = allocateClusterIndex(dataPoints->size, maxCentroids);
    double bestSse = DBL_MAX;

    Centroids newCentroids = allocateCentroids(2, dataPoints->dimensions);
    trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);

    size_t iterationCount = 1; // Helper for tracking progress

    // K=1: no split; the final k-means below refines the single centroid
    if (maxCentroids > 1)
    {
        // Step 0: Only 1 cluster, so no need for decision making
        size_t initialClusterToSplit = 0;
        rebuildClusterIndex(&membership, dataPoints, centroids->size);
        splitClusterIntraCluster(dataPoints, &membership, centroids, initialClusterToSplit, maxIterations, tracker, rng);

        SseList[0] = calculateClusterSSE(dataPoints, centroids, 0);
        SseList[1] = calculateClusterSSE(dataPoints, centroids, 1);

        trackerRecord(tracker, 1, "step", dataPoints, centroids, initialClusterToSplit);

        iterationCount = 2;
    }

    // Repeat until we have K clusters
    while (centroids->size < maxCentroids)
    {
        // Step 1: Choose the cluster to split (the highest SSE among clusters with at least 2 points)
        size_t splittable = rebuildClusterIndex(&membership, dataPoints, centroids->size);
        requireSplittableCluster(splittable, centroids->size, ALGO_BISECTING);
        size_t clusterToSplit = SIZE_MAX;
        double maxSSE = 0.0;

        for (size_t j = 0; j < centroids->size; ++j)
        {
            if (membership.sizes[j] >= 2 && (clusterToSplit == SIZE_MAX || SseList[j] > maxSSE))
            {
                maxSSE = SseList[j];
                clusterToSplit = j;
            }
        }

        // Repeat for a set number of iterations
        for (size_t j = 0; j < bisectingIterations; ++j)
        {
            ClusteringResult curr =
                tentativeSplitterForBisecting(dataPoints, &membership, clusterToSplit, maxIterations, tracker, rng);

            if (logLevel >= 3) printf("(Bisecting) Cluster %zu, try %zu: SSE %.0f\n", clusterToSplit, j + 1, curr.sse);

            // If the result is better than the best result so far, update the best result
            if (curr.sse < bestSse)
            {
                bestSse = curr.sse;

                // Save the two new centroids
                copyCoordinates(centroidRow(&newCentroids, 0), centroidRowConst(&curr.centroids, 0),
                                dataPoints->dimensions);
                copyCoordinates(centroidRow(&newCentroids, 1), centroidRowConst(&curr.centroids, 1),
                                dataPoints->dimensions);
            }

            freeClusteringResult(&curr);
        }

        // Replace the old centroid with the new centroid1
        copyCoordinates(centroidRow(centroids, clusterToSplit), centroidRowConst(&newCentroids, 0),
                        dataPoints->dimensions);

        // Increase the size of the centroids array and add the new centroid2
        assert(centroids->size < centroids->capacity);
        centroids->size++;
        copyCoordinates(centroidRow(centroids, centroids->size - 1), centroidRowConst(&newCentroids, 1),
                        dataPoints->dimensions);

        trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

        partitionStep(dataPoints, centroids); // partition step, vai pitaisiko tallentaa ne loopissa ja tehda tassa
                                              // sitten muutokset? (esim kaikki 0->clusterToSplit, 1->centroids->size-1)

        // Step 3: Update the SSE list
        SseList[clusterToSplit] = calculateClusterSSE(dataPoints, centroids, clusterToSplit);
        SseList[centroids->size - 1] = calculateClusterSSE(dataPoints, centroids, centroids->size - 1);

        bestSse = DBL_MAX;

        trackerRecord(tracker, iterationCount, "step", dataPoints, centroids, clusterToSplit);

        iterationCount++;
    }

    // Step 4: Run the final k-means
    double finalResultSse = runKMeans(dataPoints, maxIterations, centroids, tracker, KM_STAGE_FINAL);

    trackerRecord(tracker, iterationCount, "final", dataPoints, centroids, SIZE_MAX);

    // Cleanup
    free(SseList);
    freeClusterIndex(&membership);
    freeCentroids(&newCentroids);

    return finalResultSse;
}
