/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"

#include "algorithms.h"
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

const AlgorithmOption algorithmOptions[ALGORITHM_COUNT] = {
    {"km", ALGO_KM}, {"rkm", ALGO_RKM}, {"rs", ALGO_RS}, {"skm-random", ALGO_SKM_RANDOM},
    {"skm-intra", ALGO_SKM_INTRA}, {"skm-global", ALGO_SKM_GLOBAL},
    {"skm-global-basic", ALGO_SKM_GLOBAL_BASIC}, {"skm-local", ALGO_SKM_LOCAL},
    {"bisecting", ALGO_BISECTING},
};

/**
 * @brief Maps an algorithm id to a short stable name.
 */
const char* getAlgorithmName(Algorithm algorithm)
{
    switch (algorithm)
    {
        case ALGO_SKM_INTRA:
            return "SKM_Intra";
        case ALGO_SKM_GLOBAL:
            return "SKM_Global";
        case ALGO_SKM_LOCAL:
            return "SKM_Local";
        case ALGO_RS:
            return "RS";
        case ALGO_BISECTING:
            return "Bisecting_KM";
        case ALGO_SKM_RANDOM:
            return "SKM_Random";
        case ALGO_KM:
            return "KM";
        case ALGO_RKM:
            return "RKM";
        case ALGO_SKM_GLOBAL_BASIC:
            return "SKM_Global_Basic";
        default:
            fprintf(stderr, "Error: Invalid algorithm type provided: %d\n", (int)algorithm);
            return "Unknown";
    }
}

/**
 * @brief Random Split: grows K by splitting a random cluster with local k-means.
 *
 * Each split draws a cluster uniformly and draws again while it has fewer than 2 points, splits it
 * with splitClusterIntraCluster and records the state. Stops when `centroids->size == maxCentroids`,
 * then runs a final global k-means and returns its SSE.
 *
 * @param dataPoints Dataset; partitions updated in-place across splits.
 * @param centroids Global centroids; grows until `maxCentroids`.
 * @param maxCentroids Target number of centroids (K).
 * @param maxIterations Max iterations for local/final k-means refinements.
 * @param tracker Records each split (count = split number) and the final k-means; carries the ground truth for CI.
 * @return Final SSE after the concluding global k-means.
 * @errors Exits on allocation failure in underlying functions.
 */
double runRandomSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations,
                      Tracker* tracker, Rng* rng)
{
    const Centroids* groundTruth = tracker->groundTruth;
    trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);

    size_t iterationCount = 1; // Helper for tracking progress

    size_t* clusterSizes = malloc(maxCentroids * sizeof(size_t));
    handleMemoryError(clusterSizes);

    while (centroids->size < maxCentroids)
    {
        // Uniform over the clusters with at least 2 points: draw again on smaller ones
        requireSplittableCluster(countClusterSizes(dataPoints, centroids->size, clusterSizes), centroids->size,
                                 ALGO_SKM_RANDOM);
        size_t clusterToSplit = rng_below(rng, centroids->size);
        while (clusterSizes[clusterToSplit] < 2)
        {
            clusterToSplit = rng_below(rng, centroids->size);
        }

        splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, tracker, rng);

        if (logLevel >= 3)
        {
            double sse = calculateSSE(dataPoints, centroids);
            if (hasGroundTruth(groundTruth))
            {
                size_t ci = calculateCentroidIndex(centroids, groundTruth);
                printf("(RandomSplit) Number of centroids: %zu, CI: %zu, and SSE: %.0f \n", centroids->size, ci, sse);
            }
            else
            {
                printf("(RandomSplit) Number of centroids: %zu, SSE: %.0f \n", centroids->size, sse);
            }
        }

        trackerRecord(tracker, iterationCount, "step", dataPoints, centroids, clusterToSplit);

        iterationCount++;
    }

    free(clusterSizes);

    double finalResultSse = runKMeans(dataPoints, maxIterations, centroids, tracker, KM_STAGE_FINAL);

    trackerRecord(tracker, iterationCount, "final", dataPoints, centroids, SIZE_MAX);

    return finalResultSse;
}

/**
 * @brief SSE-based split k-means: grows K by splitting the cluster with the largest tentative SSE drop.
 *
 * The first split of the single starting cluster uses local 2-means for every variant. Then each split picks,
 * among the clusters with at least 2 points, the one whose tentative local 2-means lowers its SSE the most, and
 * splits it by the variant:
 *  - ALGO_SKM_INTRA: local 2-means on the cluster; the drops of the two halves are re-evaluated.
 *  - ALGO_SKM_GLOBAL: local 2-means, then global k-means (splitClusterGlobalV2); all drops re-evaluated.
 *  - ALGO_SKM_GLOBAL_BASIC: two random points, then global k-means (splitClusterGlobal); all drops re-evaluated.
 *  - ALGO_SKM_LOCAL: local 2-means, then localRepartition; the drops of the touched clusters re-evaluated
 *    (the repartition and the re-evaluation are skipped after the last split, 6.1).
 * The split itself re-runs a local 2-means instead of reusing the evaluated one (6.2).
 *
 * @param dataPoints Dataset; partitions updated in-place.
 * @param centroids Global centroids; grows until `maxCentroids`.
 * @param maxCentroids Target number of centroids (K); K=1 skips the split.
 * @param maxIterations Max iterations for local/global k-means refinements.
 * @param splitType One of the four SKM variants above.
 * @param tracker Records each split (count = split number) and the final k-means; carries the ground truth for CI.
 * @return Final SSE: of the final k-means, or for the global variants of the last global k-means (the final
 *         k-means runs there only when no global split ran, K <= 2).
 * @errors Exits on allocation failure in underlying helpers.
 */
double runSseSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations,
                   Algorithm splitType, Tracker* tracker, Rng* rng)
{
    const Centroids* groundTruth = tracker->groundTruth;
    double finalResultSse = DBL_MAX;
    bool refinedGlobally = false; // set when a global split (SKM-Global, SKM-Global-Basic) ran global k-means

    // The initial split always makes two clusters, so the arrays hold at least two
    const size_t capacity = maxCentroids < 2 ? 2 : maxCentroids;

    double* clusterSSEs = malloc(capacity * sizeof(double));
    handleMemoryError(clusterSSEs);

    double* SseDrops = calloc(capacity, sizeof(double));
    handleMemoryError(SseDrops);

    bool* clustersAffected = calloc(capacity, sizeof(bool));
    handleMemoryError(clustersAffected);

    size_t* clusterSizes = malloc(capacity * sizeof(size_t));
    handleMemoryError(clusterSizes);
    trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);

    size_t iterationCount = 1; // Helper for tracking progress

    // K=1: no split; the final k-means below refines the single centroid
    if (maxCentroids > 1)
    {
        // Only 1 cluster, so no need for decision making
        size_t initialClusterToSplit = 0;
        splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, maxIterations, tracker, rng);

        trackerRecord(tracker, 1, "step", dataPoints, centroids, initialClusterToSplit);

        for (size_t i = 0; i < centroids->size; ++i)
        {
            clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
            SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i], rng);
        }

        iterationCount = 2;
    }

    while (centroids->size < maxCentroids)
    {
        // Choose the cluster that reduces the SSE the most; only clusters with at least 2 points can split
        requireSplittableCluster(countClusterSizes(dataPoints, centroids->size, clusterSizes), centroids->size,
                                 splitType);
        size_t clusterToSplit = SIZE_MAX;
        double maxSseDrop = 0.0;

        for (size_t i = 0; i < centroids->size; ++i)
        {
            if (clusterSizes[i] >= 2 && (clusterToSplit == SIZE_MAX || SseDrops[i] > maxSseDrop))
            {
                maxSseDrop = SseDrops[i];
                clusterToSplit = i;
            }
        }

        // Split the chosen cluster by the variant (see the function comment)
        if (splitType == ALGO_SKM_INTRA)
            splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, tracker, rng);
        else if (splitType == ALGO_SKM_GLOBAL)
        {
            finalResultSse = splitClusterGlobalV2(dataPoints, centroids, clusterToSplit, maxIterations, tracker, rng);
            refinedGlobally = true;
        }
        else if (splitType == ALGO_SKM_GLOBAL_BASIC)
        {
            finalResultSse = splitClusterGlobal(dataPoints, centroids, clusterToSplit, maxIterations, tracker, rng);
            refinedGlobally = true;
        }
        else if (splitType == ALGO_SKM_LOCAL)
            splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, tracker, rng);

        if (centroids->size < maxCentroids)
        {
            if (splitType == ALGO_SKM_INTRA)
            {
                // Recalculate SSE for the affected clusters
                clusterSSEs[clusterToSplit] = calculateClusterSSE(dataPoints, centroids, clusterToSplit);
                clusterSSEs[centroids->size - 1] = calculateClusterSSE(dataPoints, centroids, centroids->size - 1);

                // Update SseDrops for the affected clusters
                SseDrops[clusterToSplit] =
                    tentativeSseDrop(dataPoints, clusterToSplit, maxIterations, clusterSSEs[clusterToSplit], rng);
                SseDrops[centroids->size - 1] = tentativeSseDrop(dataPoints, centroids->size - 1, maxIterations,
                                                                 clusterSSEs[centroids->size - 1], rng);
            }
            else if (splitType == ALGO_SKM_GLOBAL || splitType == ALGO_SKM_GLOBAL_BASIC)
            {
                for (size_t i = 0; i < centroids->size; ++i)
                {
                    clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
                    SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i], rng);
                }
            }
            else if (splitType == ALGO_SKM_LOCAL)
            {
                trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

                localRepartition(dataPoints, centroids, clusterToSplit, clustersAffected);

                clustersAffected[clusterToSplit] = true;
                clustersAffected[centroids->size - 1] = true;

                // Recalculate SseDrop for affected clusters (old and new)
                for (size_t i = 0; i < centroids->size; ++i)
                {
                    if (clustersAffected[i])
                    {
                        clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
                        SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i], rng);
                    }
                }

                memset(clustersAffected, 0, capacity * sizeof(bool));
            }
        }
        trackerRecord(tracker, iterationCount, "step", dataPoints, centroids, clusterToSplit);

        iterationCount++;

        if (logLevel >= 3)
        {
            double sse = calculateSSE(dataPoints, centroids);
            if (hasGroundTruth(groundTruth))
            {
                size_t ci = calculateCentroidIndex(centroids, groundTruth);
                printf("(%s) Number of centroids: %zu, CI: %zu, and SSE: %.0f \n", getAlgorithmName(splitType),
                       centroids->size, ci, sse);
            }
            else
            {
                printf("(%s) Number of centroids: %zu, SSE: %.0f \n", getAlgorithmName(splitType), centroids->size,
                       sse);
            }
        }
    }

    free(clusterSSEs);
    free(SseDrops);
    free(clustersAffected);
    free(clusterSizes);

    // Global splits end with global k-means; without one (K <= 2) run it here
    if (!refinedGlobally)
    {
        finalResultSse = runKMeans(dataPoints, maxIterations, centroids, tracker, KM_STAGE_FINAL);
        trackerRecord(tracker, iterationCount, "final", dataPoints, centroids, SIZE_MAX);
    }

    return finalResultSse;
}
