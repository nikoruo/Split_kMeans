/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"

#include "kmeans.h"
#include "metrics.h"
#include "util.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Picks K coordinate-distinct data points by a partial Fisher-Yates shuffle.
 *
 * A row equal to an already selected centroid is removed from the remaining pool.
 *
 * @param numCentroids Number of centroids to select (K); must be <= dataPoints->size and > 0.
 * @param dataPoints Source dataset; `dataPoints->size` must be > 0.
 * @param centroids Destination centroid container; must have at least K elements.
 * @return void
 * @errors Asserts valid K and destination size; exits on allocation failure.
 * @note Collision-free draws match the original partial Fisher-Yates shuffle.
 */
void generateRandomCentroids(size_t numCentroids, const Dataset* dataPoints, Centroids* centroids, Rng* rng)
{
    const size_t N = dataPoints->size;

    assert(numCentroids > 0 && numCentroids <= N);
    assert(centroids->size >= numCentroids);
    assert(centroids->dimensions == dataPoints->dimensions);

    /* Overflow guard for allocation */
    if (N > SIZE_MAX / sizeof(size_t))
    {
        handleMemoryError(NULL);
    }

    size_t* indices = (size_t*)malloc(N * sizeof(size_t));
    handleMemoryError(indices);

    /* init indices 0..N-1 */
    for (size_t i = 0; i < N; ++i)
    {
        indices[i] = i;
    }

    size_t poolEnd = N;
    for (size_t i = 0; i < numCentroids; ++i)
    {
        for (;;)
        {
            if (poolEnd == i)
            {
                free(indices);
                fprintf(stderr, "Error: K (%zu) exceeds the number of distinct data points\n", numCentroids);
                exit(EXIT_FAILURE);
            }
            size_t j = i + rng_below(rng, poolEnd - i);
            const double* candidate = datasetRowConst(dataPoints, indices[j]);
            bool duplicate = false;
            for (size_t c = 0; c < i; ++c)
            {
                if (coordinatesEqual(candidate, centroidRowConst(centroids, c), dataPoints->dimensions))
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
            {
                size_t temp = indices[--poolEnd];
                indices[poolEnd] = indices[j];
                indices[j] = temp;
                continue;
            }
            size_t temp = indices[i];
            indices[i] = indices[j];
            indices[j] = temp;
            copyCoordinates(centroidRow(centroids, i), candidate, dataPoints->dimensions);
            break;
        }
    }

    free(indices);
}

/**
 * @brief k-means++ seeding (--init kmeans++): the first centroid uniformly, each next one with probability
 * proportional to the squared distance to the nearest chosen centroid.
 *
 * A chosen coordinate has zero weight. Reports insufficient distinct points or non-representable distances.
 * Requires 0 < K <= N and room for K centroids. O(K·N·D) with a distance cache.
 */
void generateKMeansPlusPlusCentroids(size_t numCentroids, const Dataset* dataPoints, Centroids* centroids, Rng* rng)
{
    assert(numCentroids > 0 && numCentroids <= dataPoints->size);
    assert(centroids->size >= numCentroids);
    assert(centroids->dimensions == dataPoints->dimensions);

    /* 1) Choose the first centroid at random */
    size_t firstIndex = rng_below(rng, dataPoints->size);
    copyCoordinates(centroidRow(centroids, 0), datasetRowConst(dataPoints, firstIndex), dataPoints->dimensions);

    /* Distance cache: squared distance to the nearest chosen centroid so far */
    double* dist2 = malloc(sizeof(double) * dataPoints->size);
    handleMemoryError(dist2);

    // Initialise the cache with distances to the first centroid
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        dist2[i] = calculateSquaredEuclideanDistance(datasetRowConst(dataPoints, i), centroidRowConst(centroids, 0),
                                                     dataPoints->dimensions);
    }

    size_t chosen = 1;
    while (chosen < numCentroids)
    {
        // 2. Compute the normalising constant distSum
        double distSum = 0.0;
        for (size_t i = 0; i < dataPoints->size; ++i)
        {
            distSum += dist2[i];
        }

        if (!isfinite(distSum))
        {
            free(dist2);
            fprintf(stderr, "Error: k-means++ squared-distance sum exceeds the numeric range\n");
            exit(EXIT_FAILURE);
        }
        if (distSum == 0.0)
        {
            size_t distinct = countDistinctRowsUpTo(dataPoints, numCentroids);
            free(dist2);
            if (distinct < numCentroids)
                fprintf(stderr, "Error: K (%zu) exceeds the number of distinct data points (%zu)\n",
                        numCentroids, distinct);
            else
                fprintf(stderr, "Error: k-means++ squared distances underflow for distinct data points\n");
            exit(EXIT_FAILURE);
        }

        // 3. Select a new centroid with probability r
        double r = rng_unit(rng) * distSum;
        double cumulative = 0.0;
        size_t picked = SIZE_MAX;

        for (size_t i = 0; i < dataPoints->size; ++i)
        {
            if (dist2[i] == 0.0) continue;
            cumulative += dist2[i];
            picked = i;
            if (cumulative > r) break;
        }
        assert(picked != SIZE_MAX);

        // 4. Add the selected point as the next centroid
        copyCoordinates(centroidRow(centroids, chosen), datasetRowConst(dataPoints, picked), dataPoints->dimensions);
        ++chosen;

        /* 5. Update the distance cache:
         *    for every data point, keep the minimum distance to any
         *    centroid selected so far.  Only values that improve
         *    (i.e. become smaller) are updated. */
        for (size_t i = 0; i < dataPoints->size; ++i)
        {
            double d = calculateSquaredEuclideanDistance(datasetRowConst(dataPoints, i),
                                                         centroidRowConst(centroids, chosen - 1), dataPoints->dimensions);
            if (d < dist2[i])
            {
                dist2[i] = d;
            }
        }
    }

    free(dist2);
}

/** @brief Initial centroids of KM, RKM and RS: K distinct random data points, or k-means++ with --init kmeans++. */
void initialCentroids(InitMethod init, size_t numCentroids, const Dataset* dataPoints, Centroids* centroids,
                      Rng* rng)
{
    if (init == INIT_KMEANSPP)
    {
        generateKMeansPlusPlusCentroids(numCentroids, dataPoints, centroids, rng);
    }
    else
    {
        generateRandomCentroids(numCentroids, dataPoints, centroids, rng);
    }
}

/**
 * @brief Assigns each data point to its nearest centroid.
 *
 * Computes nearest-centroid labels for all points using squared Euclidean distance.
 * Requires at least one centroid.
 *
 * @param dataPoints Points to label; updated in-place.
 * @param centroids Current centroids; `size` must be > 0.
 * @return void
 * @errors Asserts that points and centroids exist.
 */
void partitionStep(Dataset* dataPoints, const Centroids* centroids)
{
    assert(dataPoints->size > 0 && centroids->size > 0);
    assert(dataPoints->dimensions == centroids->dimensions);

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        size_t nearestCentroidId = findNearestCentroid(datasetRowConst(dataPoints, i), centroids);
        dataPoints->partitions[i] = nearestCentroidId;
    }
}

static inline void accumulatePoint(const double* point, size_t clusterLabel, size_t dimensions, double* sums,
                                   size_t* counts)
{
    for (size_t dim = 0; dim < dimensions; ++dim)
    {
        sums[clusterLabel * dimensions + dim] += point[dim];
    }
    counts[clusterLabel]++;
}

static void updateCentroids(Centroids* centroids, const double* sums, const size_t* counts)
{
    for (size_t clusterLabel = 0; clusterLabel < centroids->size; ++clusterLabel)
    {
        if (counts[clusterLabel] > 0)
        {
            for (size_t dim = 0; dim < centroids->dimensions; ++dim)
            {
                centroidRow(centroids, clusterLabel)[dim] =
                    sums[clusterLabel * centroids->dimensions + dim] / counts[clusterLabel];
            }
        }
        else if (logLevel >= 3)
        {
            fprintf(stderr, "Warning: Cluster %zu has no points assigned.\n", clusterLabel);
        }
    }
}

/**
 * @brief Recomputes centroids as means of their assigned points.
 *
 * Accumulates per-cluster coordinate sums and divides by counts. Skips clusters with
 * zero assignments (they keep their previous coordinates; level-3 warning). Partitions and
 * dimensionality are asserted (debug builds only).
 *
 * @param centroids Centroids to update in-place; `size` must be > 0.
 * @param dataPoints Dataset with assigned partitions.
 * @return void
 * @errors Asserts valid partitions and matching dimensionality.
 * @note O(N·D + K·D) work; uses a single contiguous buffer for sums.
 */
void centroidStep(Centroids* centroids, const Dataset* dataPoints)
{
    const size_t numClusters = centroids->size;
    const size_t dimensions = dataPoints->dimensions;
    assert(centroids->dimensions == dimensions);

    // Use a single allocation for the sums
    double* sums = calloc(numClusters * dimensions, sizeof(double));
    size_t* counts = calloc(numClusters, sizeof(size_t));
    handleMemoryError(sums);
    handleMemoryError(counts);

    // Accumulate sums and counts for each cluster
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        const double* point = datasetRowConst(dataPoints, i);
        const size_t clusterLabel = dataPoints->partitions[i];
        assert(clusterLabel < numClusters);

        accumulatePoint(point, clusterLabel, dimensions, sums, counts);
    }

    // Update the centroids
    updateCentroids(centroids, sums, counts);

    free(sums);
    free(counts);
}

/* Separate indexed kernels keep view checks out of the dense hot loops. */
static void partitionView(DatasetView* view, const Centroids* centroids)
{
    for (size_t i = 0; i < view->size; ++i)
    {
        view->partitions[i] = findNearestCentroid(datasetViewRow(view, i), centroids);
    }
}

static void centroidStepView(Centroids* centroids, const DatasetView* view)
{
    const size_t numClusters = centroids->size;
    const size_t dimensions = view->source->dimensions;
    double* sums = calloc(numClusters * dimensions, sizeof(double));
    size_t* counts = calloc(numClusters, sizeof(size_t));
    handleMemoryError(sums);
    handleMemoryError(counts);

    for (size_t i = 0; i < view->size; ++i)
    {
        size_t label = view->partitions[i];
        assert(label < numClusters);
        accumulatePoint(datasetViewRow(view, i), label, dimensions, sums, counts);
    }
    updateCentroids(centroids, sums, counts);
    free(sums);
    free(counts);
}

static double calculateSSEView(const DatasetView* view, const Centroids* centroids)
{
    double sse = 0.0;
    for (size_t i = 0; i < view->size; ++i)
    {
        size_t label = view->partitions[i];
        assert(label < centroids->size);
        sse += calculateSquaredEuclideanDistance(datasetViewRow(view, i), centroidRowConst(centroids, label),
                                                 view->source->dimensions);
    }
    return sse;
}

static bool updateKMeansBest(double sse, double* bestSse, size_t iteration, const Centroids* centroids,
                             const Centroids* groundTruth)
{
    if (logLevel >= 3)
    {
        if (hasGroundTruth(groundTruth) && centroids->size == groundTruth->size)
        {
            size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
            printf("(runKMeans)After iteration %zu: CI = %zu and SSE = %.0f\n", iteration + 1, centroidIndex, sse);
        }
        else
        {
            printf("(runKMeans)After iteration %zu: SSE = %.0f\n", iteration + 1, sse);
        }
    }
    if (sse < *bestSse)
    {
        if (logLevel >= 3) printf("Best SSE so far: %.5f\n", sse);
        *bestSse = sse;
        return true;
    }
    return false;
}

/**
 * @brief k-means: partition and centroid steps until the SSE does not strictly decrease or after `iterations`
 * iterations.
 *
 * @param dataPoints Points to cluster; partitions updated in place.
 * @param centroids Initial centroids; updated in place.
 * @param tracker Optional (NULL for trial splits): ground truth for the level-3 CI, and the `_inner.csv` rows of
 * `stage`.
 * @param stage Which k-means this is for --track-inner; KM_STAGE_NONE logs no rows.
 * @return Best SSE observed during the run.
 */
static double runKMeansImpl(Dataset* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker,
                            KMeansStage stage, Centroids* bestCentroids, size_t* bestLabels, double* lastSse)
{
    assert((bestCentroids == NULL) == (bestLabels == NULL));
    assert(bestCentroids == NULL || (bestCentroids->size == centroids->size &&
                                     bestCentroids->dimensions == centroids->dimensions));
    double bestSse = DBL_MAX;
    double sse = DBL_MAX;
    const Centroids* groundTruth = tracker != NULL ? tracker->groundTruth : NULL;

    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        partitionStep(dataPoints, centroids);
        centroidStep(centroids, dataPoints);

        sse = calculateSSE(dataPoints, centroids);
        if (lastSse != NULL) *lastSse = sse;

        trackerInnerRow(tracker, stage, iteration + 1, centroids, sse);

        if (!updateKMeansBest(sse, &bestSse, iteration, centroids, groundTruth)) break;
        if (bestCentroids != NULL)
        {
            copyCoordinates(bestCentroids->coordinates, centroids->coordinates,
                            centroids->size * centroids->dimensions);
            memcpy(bestLabels, dataPoints->partitions, dataPoints->size * sizeof(*bestLabels));
        }
    }

    return bestSse;
}

double runKMeans(Dataset* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker, KMeansStage stage)
{
    return runKMeansImpl(dataPoints, iterations, centroids, tracker, stage, NULL, NULL, NULL);
}

double runKMeansBest(Dataset* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker, KMeansStage stage,
                     Centroids* bestCentroids, size_t* bestLabels)
{
    assert(bestCentroids != NULL && bestLabels != NULL);
    double sse = runKMeansImpl(dataPoints, iterations, centroids, tracker, stage, bestCentroids, bestLabels, NULL);
    if (sse == DBL_MAX)
    {
        fprintf(stderr, "Error: K-means produced no finite best SSE\n");
        exit(EXIT_FAILURE);
    }
    copyCoordinates(centroids->coordinates, bestCentroids->coordinates, centroids->size * centroids->dimensions);
    memcpy(dataPoints->partitions, bestLabels, dataPoints->size * sizeof(*bestLabels));
    return sse;
}

double runKMeansLast(Dataset* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker, KMeansStage stage)
{
    if (iterations == 0)
    {
        fprintf(stderr, "Error: K-means requires at least one iteration\n");
        exit(EXIT_FAILURE);
    }
    double sse = DBL_MAX;
    (void)runKMeansImpl(dataPoints, iterations, centroids, tracker, stage, NULL, NULL, &sse);
    return sse;
}

static double runKMeansViewImpl(DatasetView* view, size_t iterations, Centroids* centroids, Tracker* tracker,
                                KMeansStage stage, bool prePartition, Centroids* bestCentroids, size_t* bestLabels)
{
    assert(view->size > 0 && centroids->size > 0 && view->source->dimensions == centroids->dimensions);
    assert((bestCentroids == NULL) == (bestLabels == NULL));
    assert(bestCentroids == NULL || (bestCentroids->size == centroids->size &&
                                     bestCentroids->dimensions == centroids->dimensions));
    double bestSse = DBL_MAX;
    const Centroids* groundTruth = tracker != NULL ? tracker->groundTruth : NULL;
    if (prePartition) partitionView(view, centroids);

    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        partitionView(view, centroids);
        centroidStepView(centroids, view);
        double sse = calculateSSEView(view, centroids);
        trackerInnerRow(tracker, stage, iteration + 1, centroids, sse);
        if (!updateKMeansBest(sse, &bestSse, iteration, centroids, groundTruth)) break;
        if (bestCentroids != NULL)
        {
            copyCoordinates(bestCentroids->coordinates, centroids->coordinates,
                            centroids->size * centroids->dimensions);
            for (size_t i = 0; i < view->size; ++i) bestLabels[view->indices[i]] = view->partitions[i];
        }
    }
    return bestSse;
}

double runKMeansView(DatasetView* view, size_t iterations, Centroids* centroids, Tracker* tracker, KMeansStage stage,
                     bool prePartition)
{
    return runKMeansViewImpl(view, iterations, centroids, tracker, stage, prePartition, NULL, NULL);
}

double runKMeansViewBest(DatasetView* view, size_t iterations, Centroids* centroids, Tracker* tracker,
                         KMeansStage stage, bool prePartition, Centroids* bestCentroids, size_t* bestLabels)
{
    assert(bestCentroids != NULL && bestLabels != NULL);
    return runKMeansViewImpl(view, iterations, centroids, tracker, stage, prePartition, bestCentroids, bestLabels);
}

/**
 * @brief k-means that records the state after every iteration with the tracker (count = iteration, 1-based).
 *
 * Runs the same steps as `runKMeans`, so the result is identical; stops when the SSE does not
 * strictly decrease or after `iterations` iterations.
 *
 * @param dataPoints Dataset to cluster; partitions updated in place.
 * @param centroids Centroid set updated in place.
 * @param tracker Rows, snapshots and the ground truth for the level-3 CI.
 * @return Best SSE observed during the run.
 */
double runKMeansWithTracking(Dataset* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker)
{
    double bestSse = DBL_MAX;
    double sse = DBL_MAX;
    const Centroids* groundTruth = tracker->groundTruth;

    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        partitionStep(dataPoints, centroids);
        centroidStep(centroids, dataPoints);

        trackerRecord(tracker, iteration + 1, "step", dataPoints, centroids, SIZE_MAX);

        sse = calculateSSE(dataPoints, centroids);

        if (logLevel >= 3)
        {
            if (hasGroundTruth(groundTruth) && centroids->size == groundTruth->size)
            {
                size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
                printf("(runKMeans)After iteration %zu: CI = %zu and SSE = %.0f\n", iteration + 1, centroidIndex, sse);
            }
            else
            {
                printf("(runKMeans)After iteration %zu: SSE = %.0f\n", iteration + 1, sse);
            }
        }

        if (sse < bestSse)
        {
            if (logLevel >= 3) printf("Best SSE so far: %.5f\n", sse);
            bestSse = sse;
        }
        else
        {
            break; // Exit the loop if the SSE does not improve
        }
    }

    return bestSse;
}
