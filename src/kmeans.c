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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Picks K different data points uniformly and copies them as initial centroids.
 *
 * Uses a partial Fisher–Yates shuffle to select `numCentroids` distinct indices from
 * `dataPoints` without replacement (duplicate points in the data can still give equal
 * coordinates, 6.6), then deep-copies those points into `centroids`.
 * Selection of swap index uses rejection sampling (no modulo bias).
 *
 * @param numCentroids Number of centroids to select (K); must be <= dataPoints->size and > 0.
 * @param dataPoints Source dataset; `dataPoints->size` must be > 0.
 * @param centroids Destination centroid container; must have at least K elements.
 * @return void
 * @errors Asserts valid K and destination size; exits on allocation failure.
 * @note O(N) init + O(K) swaps.
 */
void generateRandomCentroids(size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids, Rng* rng)
{
    const size_t N = dataPoints->size;

    assert(numCentroids > 0 && numCentroids <= N);
    assert(centroids->size >= numCentroids);

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

    for (size_t i = 0; i < numCentroids; ++i)
    {
        size_t j = i + rng_below(rng, dataPoints->size - i);
        size_t temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }

    /* Copy selected points into centroids */
    for (size_t i = 0; i < numCentroids; ++i)
    {
        size_t idx = indices[i];
        deepCopyDataPoint(&centroids->points[i], &dataPoints->points[idx]);
    }

    free(indices);
}

/**
 * @brief k-means++ seeding (--init kmeans++): the first centroid uniformly, each next one with probability
 * proportional to the squared distance to the nearest chosen centroid.
 *
 * A pick at distance 0 (an already chosen coordinate) is drawn again. Exits when no point is left at a
 * positive distance, that is when the data have fewer than K distinct points (it looped forever before 3.5).
 * Requires 0 < K <= N and room for K centroids. O(K·N·D) with a distance cache.
 */
void generateKMeansPlusPlusCentroids(size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids, Rng* rng)
{
    assert(numCentroids > 0 && numCentroids <= dataPoints->size);
    assert(centroids->size >= numCentroids);

    /* 1) Choose the first centroid at random */
    size_t firstIndex = rng_below(rng, dataPoints->size);
    deepCopyDataPoint(&centroids->points[0], &dataPoints->points[firstIndex]);

    /* Distance cache: squared distance to the nearest chosen centroid so far */
    double* dist2 = malloc(sizeof(double) * dataPoints->size);
    handleMemoryError(dist2);

    // Initialise the cache with distances to the first centroid
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        dist2[i] = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[0]);
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

        // Every point coincides with a chosen centroid: no new distinct centroid exists
        if (distSum == 0.0)
        {
            fprintf(stderr, "Error: k-means++ found only %zu distinct points for K = %zu\n", chosen, numCentroids);
            exit(EXIT_FAILURE);
        }

        // 3. Select a new centroid with probability r
        double r = rng_unit(rng) * distSum;
        double cumulative = 0.0;
        size_t picked = dataPoints->size - 1; /* fallback to last point */

        for (size_t i = 0; i < dataPoints->size; ++i)
        {
            cumulative += dist2[i];
            if (cumulative >= r)
            {
                picked = i;
                break;
            }
        }

        // Debug: If the chosen point is already a centroid, skip and choose again
        if (dist2[picked] == 0.0) continue;

        // 4. Add the selected point as the next centroid
        deepCopyDataPoint(&centroids->points[chosen++], &dataPoints->points[picked]);

        /* 5. Update the distance cache:
         *    for every data point, keep the minimum distance to any
         *    centroid selected so far.  Only values that improve
         *    (i.e. become smaller) are updated. */
        for (size_t i = 0; i < dataPoints->size; ++i)
        {
            double d = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[chosen - 1]);
            if (d < dist2[i])
            {
                dist2[i] = d;
            }
        }
    }

    free(dist2);
}

/** @brief Initial centroids of KM, RKM and RS: K distinct random data points, or k-means++ with --init kmeans++. */
void initialCentroids(InitMethod init, size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids,
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
void partitionStep(DataPoints* dataPoints, const Centroids* centroids)
{
    assert(dataPoints->size > 0 && centroids->size > 0);

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        size_t nearestCentroidId = findNearestCentroid(&dataPoints->points[i], centroids);
        dataPoints->points[i].partition = nearestCentroidId;
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
void centroidStep(Centroids* centroids, const DataPoints* dataPoints)
{
    const size_t numClusters = centroids->size;
    const size_t dimensions = dataPoints->points[0].dimensions;

    // Use a single allocation for the sums
    double* sums = calloc(numClusters * dimensions, sizeof(double));
    size_t* counts = calloc(numClusters, sizeof(size_t));
    handleMemoryError(sums);
    handleMemoryError(counts);

    // Accumulate sums and counts for each cluster
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        const DataPoint* point = &dataPoints->points[i];
        const size_t clusterLabel = point->partition;
        assert(clusterLabel < numClusters && point->dimensions == dimensions);

        for (size_t dim = 0; dim < dimensions; ++dim)
        {
            sums[clusterLabel * dimensions + dim] += point->attributes[dim];
        }
        counts[clusterLabel]++;
    }

    // Update the centroids
    for (size_t clusterLabel = 0; clusterLabel < numClusters; ++clusterLabel)
    {
        if (counts[clusterLabel] > 0)
        {
            for (size_t dim = 0; dim < dimensions; ++dim)
            {
                centroids->points[clusterLabel].attributes[dim] =
                    sums[clusterLabel * dimensions + dim] / counts[clusterLabel];
            }
        }
        else if (logLevel >= 3)
        {
            fprintf(stderr, "Warning: Cluster %zu has no points assigned.\n", clusterLabel);
        }
    }

    free(sums);
    free(counts);
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
double runKMeans(DataPoints* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker, KMeansStage stage)
{
    double bestSse = DBL_MAX;
    double sse = DBL_MAX;
    const Centroids* groundTruth = tracker != NULL ? tracker->groundTruth : NULL;

    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        partitionStep(dataPoints, centroids);
        centroidStep(centroids, dataPoints);

        sse = calculateSSE(dataPoints, centroids);

        trackerInnerRow(tracker, stage, iteration + 1, centroids, sse);

        if (logLevel >= 3)
        {
            // CI only for full-size solutions: local 2-means and partial global steps do not match the ground truth
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
double runKMeansWithTracking(DataPoints* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker)
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
