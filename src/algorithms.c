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
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const AlgorithmOption algorithmOptions[] = {
    {"km", ALGO_KM}, {"rkm", ALGO_RKM}, {"rs", ALGO_RS}, {"skm-random", ALGO_SKM_RANDOM},
    {"skm-intra", ALGO_SKM_INTRA}, {"skm-global", ALGO_SKM_GLOBAL},
    {"skm-global-basic", ALGO_SKM_GLOBAL_BASIC}, {"skm-local", ALGO_SKM_LOCAL},
    {"bisecting", ALGO_BISECTING},
};
_Static_assert(sizeof(algorithmOptions) / sizeof(algorithmOptions[0]) == ALGORITHM_COUNT,
               "every Algorithm needs exactly one algorithmOptions entry");

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

const char* repartitionModeName(RepartitionMode mode)
{
    switch (mode)
    {
        case REPARTITION_INWARD: return "inward";
        case REPARTITION_INWARD_NONCHILD: return "inward-nonchild";
        case REPARTITION_BOTH: return "both";
        default:
            fprintf(stderr, "Error: Invalid repartition mode: %d\n", (int)mode);
            exit(EXIT_FAILURE);
    }
}

const char* bisectSelectName(BisectSelect mode)
{
    switch (mode)
    {
        case BISECT_SELECT_SSE: return "sse";
        case BISECT_SELECT_SIZE: return "size";
        default:
            fprintf(stderr, "Error: Invalid bisecting selection mode: %d\n", (int)mode);
            exit(EXIT_FAILURE);
    }
}

static void requireFiniteKMeansSse(double sse, Algorithm algorithm)
{
    if (!isfinite(sse))
    {
        fprintf(stderr, "Error: %s produced a non-finite k-means SSE\n", getAlgorithmName(algorithm));
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Random Split: grows K by splitting a random cluster with local k-means.
 *
 * Each split draws uniformly among clusters with at least two distinct points (after as many ineligible draws as
 * there are clusters, it picks an eligible one by rank), splits it
 * with splitClusterIntraCluster and records the state. Stops when `centroids->size == maxCentroids`,
 * then runs a final global k-means and returns its SSE.
 *
 * @param dataPoints Dataset; partitions updated in-place across splits.
 * @param centroids Global centroids; grows until `maxCentroids`.
 * @param maxCentroids Target number of centroids (K).
 * @param maxIterations Max iterations for local/final k-means refinements; at least 1.
 * @param tracker Records each split (count = split number) and the final k-means; carries the ground truth for CI.
 * @return Final SSE after the concluding global k-means.
 * @errors Exits on a zero iteration limit, non-finite final SSE, allocation failure, or no splittable cluster.
 */
double runRandomSplit(Dataset* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations,
                      Tracker* tracker, Rng* rng)
{
    assert(centroids->capacity >= maxCentroids);
    if (maxIterations == 0)
    {
        fprintf(stderr, "Error: Random Split requires at least one k-means iteration\n");
        exit(EXIT_FAILURE);
    }
    const Centroids* groundTruth = tracker->groundTruth;
    trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);

    size_t iterationCount = 1; // Helper for tracking progress

    ClusterIndex membership = {0};
    if (maxCentroids > 1) membership = allocateClusterIndex(dataPoints->size, maxCentroids);

    while (centroids->size < maxCentroids)
    {
        size_t splittable = rebuildClusterIndex(&membership, dataPoints, centroids->size);
        requireSplittableCluster(splittable, centroids->size, ALGO_SKM_RANDOM);
        size_t clusterToSplit = rng_below(rng, centroids->size);
        size_t attempts = 1;
        while (!membership.eligible[clusterToSplit])
        {
            if (attempts == centroids->size)
            {
                size_t rank = rng_below(rng, splittable);
                for (clusterToSplit = 0; clusterToSplit < centroids->size; ++clusterToSplit)
                {
                    if (!membership.eligible[clusterToSplit]) continue;
                    if (rank == 0) break;
                    --rank;
                }
                assert(clusterToSplit < centroids->size);
                break;
            }
            clusterToSplit = rng_below(rng, centroids->size);
            ++attempts;
        }

        splitClusterIntraCluster(dataPoints, &membership, centroids, clusterToSplit, maxIterations, tracker, rng);

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

    freeClusterIndex(&membership);

    double finalResultSse = runKMeans(dataPoints, maxIterations, centroids, tracker, KM_STAGE_FINAL);
    requireFiniteKMeansSse(finalResultSse, ALGO_SKM_RANDOM);

    trackerRecord(tracker, iterationCount, "final", dataPoints, centroids, SIZE_MAX);

    return finalResultSse;
}

static Centroids tentativePair(Centroids* storage, size_t cluster)
{
    assert(storage->coordinates != NULL && cluster < storage->size / 2);
    Centroids pair = {
        .coordinates = storage->coordinates + 2 * cluster * storage->dimensions,
        .size = 2,
        .capacity = 2,
        .dimensions = storage->dimensions,
    };
    return pair;
}

static double evaluateSplitDrop(Dataset* dataPoints, const ClusterIndex* membership, size_t cluster,
                                size_t maxIterations, double clusterSse, Centroids* storage, size_t* labels,
                                Tracker* tracker, InnerTrace* traces, Rng* rng)
{
    if (storage->coordinates == NULL)
        return tentativeSseDrop(dataPoints, membership, cluster, maxIterations, clusterSse, rng);
    Centroids pair = tentativePair(storage, cluster);
    return cacheTentativeSplit(dataPoints, membership, cluster, maxIterations, clusterSse, &pair, labels, tracker,
                               traces != NULL ? &traces[cluster] : NULL, rng);
}

/**
 * @brief SSE-based split k-means: grows K by splitting the cluster with the largest tentative SSE drop.
 *
 * The first split of the single starting cluster uses local 2-means for every variant. Then each split picks,
 * among clusters with at least two distinct points, the one whose tentative local 2-means lowers its SSE most, and
 * splits it by the variant:
 *  - ALGO_SKM_INTRA: apply the evaluated local 2-means; the drops of the two halves are re-evaluated.
 *  - ALGO_SKM_GLOBAL: apply the evaluated local 2-means, then global k-means; all drops re-evaluated.
 *  - ALGO_SKM_GLOBAL_BASIC: two random points, then global k-means (splitClusterGlobal); all drops re-evaluated.
 *  - ALGO_SKM_LOCAL: apply the evaluated local 2-means (the first split runs a fresh one), then localRepartition;
 *    touched means and their drops are updated.
 * SKM-Global-Basic retains its separate raw-seed split instead of applying the evaluated local 2-means.
 *
 * @param dataPoints Dataset; partitions updated in-place.
 * @param centroids Global centroids; grows until `maxCentroids`.
 * @param maxCentroids Target number of centroids (K); K=1 skips the split.
 * @param maxIterations Max iterations for local/global k-means refinements; at least 1.
 * @param splitType One of the four SKM variants above.
 * @param repartition SKM-Local repartition mode (--repartition); ignored by the other variants.
 * @param tracker Records each split (count = split number) and the final k-means; carries the ground truth for CI.
 * @return Final SSE: of the final k-means, or for the global variants of the last global k-means (the final
 *         k-means runs there only when no global split ran, K <= 2).
 * @errors Exits on allocation failure, a zero iteration limit, non-finite SSE, or no splittable cluster.
 */
double runSseSplit(Dataset* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations,
                   Algorithm splitType, RepartitionMode repartition, Tracker* tracker, Rng* rng)
{
    assert(centroids->capacity >= maxCentroids);
    if (maxIterations == 0)
    {
        fprintf(stderr, "Error: SSE split requires at least one k-means iteration\n");
        exit(EXIT_FAILURE);
    }
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

    Centroids trialCentroids = {0};
    size_t* trialLabels = NULL;
    InnerTrace* trialTraces = NULL;
    if (maxCentroids > 1 && splitType != ALGO_SKM_GLOBAL_BASIC)
    {
        if (capacity > SIZE_MAX / 2)
        {
            fprintf(stderr, "Error: Tentative split capacity exceeds addressable memory\n");
            exit(EXIT_FAILURE);
        }
        trialCentroids = allocateCentroids(2 * capacity, dataPoints->dimensions);
        trialLabels = malloc(dataPoints->size * sizeof(*trialLabels));
        handleMemoryError(trialLabels);
        if (tracker->inner)
        {
            trackerPause(tracker);
            trialTraces = calloc(capacity, sizeof(*trialTraces));
            handleMemoryError(trialTraces);
            trackerResume(tracker);
        }
    }

    ClusterIndex membership = {0};
    if (maxCentroids > 1) membership = allocateClusterIndex(dataPoints->size, capacity);
    size_t splittable = 0;
    trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);

    size_t iterationCount = 1; // Helper for tracking progress

    // K=1: no split; the final k-means below refines the single centroid
    if (maxCentroids > 1)
    {
        // Only 1 cluster, so no need for decision making
        size_t initialClusterToSplit = 0;
        splittable = rebuildClusterIndex(&membership, dataPoints, centroids->size);
        requireSplittableCluster(splittable, centroids->size, splitType);
        splitClusterIntraCluster(dataPoints, &membership, centroids, initialClusterToSplit, maxIterations, tracker, rng);

        if (splitType == ALGO_SKM_LOCAL)
        {
            trackerSplitSnapshot(tracker, dataPoints, centroids, initialClusterToSplit);
            splittable = localRepartition(dataPoints, &membership, centroids, initialClusterToSplit, repartition,
                                         clustersAffected);
            memset(clustersAffected, 0, capacity * sizeof(bool));
        }

        trackerRecord(tracker, 1, "step", dataPoints, centroids, initialClusterToSplit);

        if (splitType != ALGO_SKM_LOCAL)
            splittable = rebuildClusterIndex(&membership, dataPoints, centroids->size);
        // Runs for every K >= 2: at K = 2 no later split uses these trials (kept on purpose, plan D12)
        for (size_t i = 0; i < centroids->size; ++i)
        {
            clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
            SseDrops[i] = evaluateSplitDrop(dataPoints, &membership, i, maxIterations, clusterSSEs[i],
                                            &trialCentroids, trialLabels, tracker, trialTraces, rng);
        }

        iterationCount = 2;
    }

    while (centroids->size < maxCentroids)
    {
        // Choose the eligible cluster that reduces the SSE the most
        requireSplittableCluster(splittable, centroids->size, splitType);
        size_t clusterToSplit = SIZE_MAX;
        double maxSseDrop = 0.0;

        for (size_t i = 0; i < centroids->size; ++i)
        {
            if (membership.eligible[i] && (clusterToSplit == SIZE_MAX || SseDrops[i] > maxSseDrop))
            {
                maxSseDrop = SseDrops[i];
                clusterToSplit = i;
            }
        }

        // Split the chosen cluster by the variant (see the function comment)
        if (splitType == ALGO_SKM_GLOBAL_BASIC)
        {
            finalResultSse =
                splitClusterGlobal(dataPoints, &membership, centroids, clusterToSplit, maxIterations, tracker, rng);
            requireFiniteKMeansSse(finalResultSse, splitType);
            refinedGlobally = true;
        }
        else
        {
            Centroids pair = tentativePair(&trialCentroids, clusterToSplit);
            if (trialTraces != NULL) trackerAppendTrace(tracker, &trialTraces[clusterToSplit], iterationCount);
            applyTentativeSplit(dataPoints, &membership, centroids, clusterToSplit, &pair, trialLabels);
            if (splitType == ALGO_SKM_GLOBAL)
            {
                trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);
                finalResultSse = runKMeans(dataPoints, maxIterations, centroids, tracker, KM_STAGE_GLOBAL);
                requireFiniteKMeansSse(finalResultSse, splitType);
                refinedGlobally = true;
            }
        }

        if (splitType == ALGO_SKM_LOCAL)
        {
            trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);
            splittable = localRepartition(dataPoints, &membership, centroids, clusterToSplit, repartition,
                                         clustersAffected);
        }

        if (centroids->size < maxCentroids)
        {
            if (splitType == ALGO_SKM_INTRA)
            {
                splittable = rebuildClusterIndex(&membership, dataPoints, centroids->size);
                // Recalculate SSE for the affected clusters
                clusterSSEs[clusterToSplit] = calculateClusterSSE(dataPoints, centroids, clusterToSplit);
                clusterSSEs[centroids->size - 1] = calculateClusterSSE(dataPoints, centroids, centroids->size - 1);

                // Update SseDrops for the affected clusters
                SseDrops[clusterToSplit] =
                    evaluateSplitDrop(dataPoints, &membership, clusterToSplit, maxIterations,
                                      clusterSSEs[clusterToSplit], &trialCentroids, trialLabels, tracker, trialTraces, rng);
                SseDrops[centroids->size - 1] =
                    evaluateSplitDrop(dataPoints, &membership, centroids->size - 1, maxIterations,
                                      clusterSSEs[centroids->size - 1], &trialCentroids, trialLabels, tracker,
                                      trialTraces, rng);
            }
            else if (splitType == ALGO_SKM_GLOBAL || splitType == ALGO_SKM_GLOBAL_BASIC)
            {
                splittable = rebuildClusterIndex(&membership, dataPoints, centroids->size);
                for (size_t i = 0; i < centroids->size; ++i)
                {
                    clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
                    SseDrops[i] = evaluateSplitDrop(dataPoints, &membership, i, maxIterations, clusterSSEs[i],
                                                    &trialCentroids, trialLabels, tracker, trialTraces, rng);
                }
            }
            else if (splitType == ALGO_SKM_LOCAL)
            {
                for (size_t i = 0; i < centroids->size; ++i)
                {
                    if (clustersAffected[i])
                    {
                        clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
                        SseDrops[i] = evaluateSplitDrop(dataPoints, &membership, i, maxIterations, clusterSSEs[i],
                                                        &trialCentroids, trialLabels, tracker, trialTraces, rng);
                    }
                }
            }
        }
        if (splitType == ALGO_SKM_LOCAL) memset(clustersAffected, 0, capacity * sizeof(bool));
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
    freeCentroids(&trialCentroids);
    free(trialLabels);
    if (trialTraces != NULL)
    {
        trackerPause(tracker);
        for (size_t i = 0; i < capacity; ++i) free(trialTraces[i].rows);
        free(trialTraces);
        trackerResume(tracker);
    }
    freeClusterIndex(&membership);

    // Global splits end with global k-means; without one (K <= 2) run it here
    if (!refinedGlobally)
    {
        finalResultSse = runKMeans(dataPoints, maxIterations, centroids, tracker, KM_STAGE_FINAL);
        requireFiniteKMeansSse(finalResultSse, splitType);
        trackerRecord(tracker, iterationCount, "final", dataPoints, centroids, SIZE_MAX);
    }

    return finalResultSse;
}
