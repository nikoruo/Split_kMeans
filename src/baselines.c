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
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Random Swap local repartition after one centroid moved, in two passes.
 *
 * Pass 1: every point of the swapped cluster (`removed`) moves to its nearest centroid. Pass 2: every other
 * point moves to the swapped centroid when it is closer than its own. The caller updates marked means afterwards.
 *
 * @param dataPoints Dataset with current partitions.
 * @param centroids Centroids after the swap; index `removed` denotes the modified centroid.
 * @param removed Index of the centroid whose coordinates were changed; must be < centroids->size (not checked).
 * @param affected Initially clear array of at least centroids->size flags; touched clusters are marked.
 */
void localRepartitionForRS(Dataset* dataPoints, const Centroids* centroids, size_t removed, bool* affected)
{
    affected[removed] = true;
    // from removed -> to existing
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->partitions[i] == removed)
        {
            size_t nearestCentroid = findNearestCentroid(datasetRowConst(dataPoints, i), centroids);
            if (nearestCentroid != removed) affected[nearestCentroid] = true;
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
                affected[currentPartition] = true;
                dataPoints->partitions[i] = removed;
            }
        }
    }

    // centroidStep(centroids, dataPoints); // historical call; the current API also requires scratch
}

static bool matchesCentroid(const double* point, const Centroids* centroids)
{
    for (size_t c = 0; c < centroids->size; ++c)
        if (coordinatesEqual(point, centroidRowConst(centroids, c), centroids->dimensions)) return true;
    return false;
}

static size_t randomDistinctSwapTarget(const Dataset* dataPoints, const Centroids* centroids, size_t* eligible, Rng* rng)
{
    for (size_t attempt = 0; attempt < dataPoints->size; ++attempt)
    {
        size_t point = rng_below(rng, dataPoints->size);
        if (!matchesCentroid(datasetRowConst(dataPoints, point), centroids)) return point;
    }

    size_t count = 0;
    for (size_t i = 0; i < dataPoints->size; ++i)
        if (!matchesCentroid(datasetRowConst(dataPoints, i), centroids)) eligible[count++] = i;
    if (count == 0)
    {
        fprintf(stderr, "Error: Random Swap found no distinct target\n");
        exit(EXIT_FAILURE);
    }
    return eligible[rng_below(rng, count)];
}

/**
 * @brief Random Swap: moves a random centroid to a random data point that differs from every centroid, then keeps
 * the swap if the SSE improves.
 *
 * Per swap, in the step order of Fränti 2018 p. 7: local repartition, update of the affected means, then two k-means
 * iterations (partition, then means). The rs.txt and CBModules rs.c instead run two iterations of means, then
 * partition, with no separate mean update; the 2018 order is kept on purpose (plan, Resolved). A rejected swap
 * restores the centroids and partitions. The tracker records the initial
 * solution (count 0) and every improvement (count = swap number); CI is computed only for
 * the tracker and the level-2 log, with the stopwatch paused.
 *
 * @param dataPoints Dataset; its partitions must belong to `centroids` on entry.
 * @param centroids Initial centroids; updated in place to the best solution.
 * @param maxSwaps Number of trial swaps.
 * @param distinctRows Number of distinct rows, capped at K+1.
 * @return The best SSE found.
 * @errors Exits on allocation failure, a non-finite initial SSE or a `distinctRows` that does not match the data.
 */
double randomSwap(Dataset* dataPoints, Centroids* centroids, size_t maxSwaps, size_t distinctRows, Tracker* tracker,
                  Rng* rng)
{
    double bestSse = calculateSSE(dataPoints, centroids);
    if (!isfinite(bestSse))
    {
        fprintf(stderr, "Error: Random Swap initial SSE is not finite\n");
        exit(EXIT_FAILURE);
    }
    size_t kMeansIterations = 2;
    size_t numCentroids = centroids->size;
    size_t dimensions = centroids->dimensions;
    assert(dataPoints->dimensions == dimensions);
    assert(distinctRows >= numCentroids);

    size_t totalAttributes = numCentroids * dimensions;
    size_t bestCI = trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);
    if (distinctRows == numCentroids)
    {
        if (bestSse != 0.0)
        {
            fprintf(stderr, "Error: Random Swap initial centroids do not represent all distinct data points\n");
            exit(EXIT_FAILURE);
        }
        return bestSse;
    }

    double* backupAttributes = malloc(totalAttributes * sizeof(double));
    handleMemoryError(backupAttributes);

    size_t* backupPartitions = malloc(dataPoints->size * sizeof(size_t));
    handleMemoryError(backupPartitions);
    bool* affected = calloc(numCentroids, sizeof(*affected));
    handleMemoryError(affected);
    size_t* eligible = malloc(dataPoints->size * sizeof(*eligible));
    handleMemoryError(eligible);
    ClusterIndex membership = allocateClusterIndex(dataPoints->size, numCentroids);
    KMeansScratch scratch = allocateKMeansScratch(numCentroids, dimensions);

    for (size_t i = 0; i < maxSwaps; ++i)
    {
        if (logLevel >= 3) printf("Swap %zu\n", i + 1);

        // Backup
        memcpy(backupAttributes, centroids->coordinates, totalAttributes * sizeof(double));
        memcpy(backupPartitions, dataPoints->partitions, dataPoints->size * sizeof(size_t));

        // Swap
        size_t randomCentroidId = rng_below(rng, centroids->size);
        size_t randomDataPointId = randomDistinctSwapTarget(dataPoints, centroids, eligible, rng);

        copyCoordinates(centroidRow(centroids, randomCentroidId), datasetRowConst(dataPoints, randomDataPointId),
                        dimensions);

        memset(affected, 0, numCentroids * sizeof(*affected));
        localRepartitionForRS(dataPoints, centroids, randomCentroidId, affected);
        updateAffectedCentroids(dataPoints, &membership, centroids, affected);

        // K-means
        double resultSse = runKMeans(dataPoints, kMeansIterations, centroids, tracker, KM_STAGE_NONE, &scratch);
        if (!isfinite(resultSse))
        {
            trackerPause(tracker);
            fprintf(stderr, "Warning: Random Swap rejected swap %zu with non-finite SSE\n", i + 1);
            trackerResume(tracker);
        }

        // If 1) SSE improves, we keep the change
        // if not, 2) we reverse the swap
        if (isfinite(resultSse) && resultSse < bestSse)
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
    free(affected);
    free(eligible);
    freeClusterIndex(&membership);
    freeKMeansScratch(&scratch);

    return bestSse;
}

/**
 * @brief Bisecting k-means: grows K by splitting the cluster with the largest SSE or row count.
 *
 * Each split picks the largest eligible cluster by the selected rule and applies the best of
 * `bisectingIterations` local 2-means tries. Only that cluster's labels change. A final global k-means follows.
 *
 * @param dataPoints Dataset; partitions updated in-place across splits.
 * @param centroids Global centroids; grown in-place up to maxCentroids.
 * @param maxCentroids Target number of clusters (K); K=1 skips the split.
 * @param maxIterations Max iterations for the local k-means and final global k-means.
 * @param bisectingIterations Local 2-means tries per split; at least 1.
 * @param selection Select by SSE or assigned row count.
 * @param tracker Records each split (count = split number) and the final k-means; carries the ground truth for CI.
 * @return SSE after the final k-means.
 * @errors Exits on allocation failure, an invalid selection mode, zero iterations or tries, a non-finite try SSE,
 *         no finite final SSE, or when no cluster with two distinct points is left to split.
 */
double runBisectingKMeans(Dataset* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations,
                          size_t bisectingIterations, BisectSelect selection, Tracker* tracker, Rng* rng,
                          KMeansScratch* scratch)
{
    assert(centroids->capacity >= maxCentroids);
    if (selection != BISECT_SELECT_SSE && selection != BISECT_SELECT_SIZE)
    {
        fprintf(stderr, "Error: Invalid bisecting selection mode: %d\n", (int)selection);
        exit(EXIT_FAILURE);
    }
    if (maxIterations == 0 || bisectingIterations == 0)
    {
        fprintf(stderr, "Error: Bisecting k-means requires positive iterations and tries\n");
        exit(EXIT_FAILURE);
    }
    double* SseList = malloc((maxCentroids < 2 ? 2 : maxCentroids) * sizeof(double));
    handleMemoryError(SseList);
    ClusterIndex membership = {0};
    if (maxCentroids > 1) membership = allocateClusterIndex(dataPoints->size, maxCentroids);

    Centroids trialCentroids = {0};
    Centroids bestSplit = {0};
    SplitScratch splitScratch = {0};
    size_t* trialLabels = NULL;
    if (maxCentroids > 1)
    {
        trialCentroids = allocateCentroids(2, dataPoints->dimensions);
        bestSplit = allocateCentroids(2, dataPoints->dimensions);
        splitScratch = allocateSplitScratch(scratch);
        trialLabels = malloc(dataPoints->size * sizeof(*trialLabels));
        handleMemoryError(trialLabels);
        SseList[0] = calculateClusterSSE(dataPoints, centroids, 0);
    }
    size_t* bestLabels = malloc(dataPoints->size * sizeof(*bestLabels));
    handleMemoryError(bestLabels);
    trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);

    size_t iterationCount = 1;
    while (centroids->size < maxCentroids)
    {
        size_t splittable = rebuildClusterIndex(&membership, dataPoints, centroids->size);
        requireSplittableCluster(splittable, centroids->size, ALGO_BISECTING);
        size_t clusterToSplit = SIZE_MAX;

        for (size_t j = 0; j < centroids->size; ++j)
        {
            if (membership.eligible[j] &&
                (clusterToSplit == SIZE_MAX ||
                 (selection == BISECT_SELECT_SIZE ? membership.sizes[j] > membership.sizes[clusterToSplit]
                                                  : SseList[j] > SseList[clusterToSplit])))
            {
                clusterToSplit = j;
            }
        }

        double bestSse = DBL_MAX;
        for (size_t j = 0; j < bisectingIterations; ++j)
        {
            double sse = runBisectingTrial(dataPoints, &membership, clusterToSplit, maxIterations, &trialCentroids,
                                           trialLabels, tracker, rng, &splitScratch);

            if (logLevel >= 3) printf("(Bisecting) Cluster %zu, try %zu: SSE %.0f\n", clusterToSplit, j + 1, sse);

            if (sse < bestSse)
            {
                bestSse = sse;
                copyCoordinates(bestSplit.coordinates, trialCentroids.coordinates, 2 * dataPoints->dimensions);
                for (size_t i = membership.offsets[clusterToSplit]; i < membership.offsets[clusterToSplit + 1]; ++i)
                {
                    size_t point = membership.indices[i];
                    bestLabels[point] = trialLabels[point];
                }
            }
        }

        applyTentativeSplit(dataPoints, &membership, centroids, clusterToSplit, &bestSplit, bestLabels);
        trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

        splitClusterSSEs(dataPoints, &membership, centroids, clusterToSplit, &SseList[clusterToSplit],
                         &SseList[centroids->size - 1]);

        trackerRecord(tracker, iterationCount, "step", dataPoints, centroids, clusterToSplit);
        ++iterationCount;
    }

    freeSplitScratch(&splitScratch);
    double finalResultSse = runKMeans(dataPoints, maxIterations, centroids, tracker, KM_STAGE_FINAL, scratch);
    if (finalResultSse == DBL_MAX || !isfinite(finalResultSse))
    {
        fprintf(stderr, "Error: Bisecting k-means produced no finite final SSE\n");
        exit(EXIT_FAILURE);
    }

    trackerRecord(tracker, iterationCount, "final", dataPoints, centroids, SIZE_MAX);

    free(SseList);
    freeClusterIndex(&membership);
    freeCentroids(&trialCentroids);
    freeCentroids(&bestSplit);
    free(trialLabels);
    free(bestLabels);

    return finalResultSse;
}
