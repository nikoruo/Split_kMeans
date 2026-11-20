/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef METRICS_H
#define METRICS_H

#include "dataset.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define CI_NA SIZE_MAX

/**
 * @brief Squared Euclidean distance between two points.
 */
static inline double calculateSquaredEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
{
    const size_t dims = point1->dimensions;
    const double* a = point1->attributes;
    const double* b = point2->attributes;

    double sum = 0.0;
    for (size_t i = 0; i < dims; ++i)
    {
        const double diff = a[i] - b[i];
        sum += diff * diff;
    }
    return sum;
}

/**
 * @brief Index of the nearest centroid by squared Euclidean distance; the first one on ties.
 *
 * Requires at least one centroid (asserted) and the same dimensionality as the point (not checked). O(K·D).
 */
static inline size_t findNearestCentroid(const DataPoint* queryPoint, const Centroids* targetCentroids)
{
    assert(targetCentroids->size > 0);

    size_t nearestCentroidId = 0;
    double minDistance = calculateSquaredEuclideanDistance(queryPoint, &targetCentroids->points[0]);

    for (size_t i = 1; i < targetCentroids->size; ++i)
    {
        double d = calculateSquaredEuclideanDistance(queryPoint, &targetCentroids->points[i]);

        if (d < minDistance)
        {
            minDistance = d;
            nearestCentroidId = i;
        }
    }

    return nearestCentroidId;
}

double calculateEuclideanDistance(const DataPoint* point1, const DataPoint* point2);
double calculateSSE(const DataPoints* dataPoints, const Centroids* centroids);
double calculateMSE(const DataPoints* dataPoints, const Centroids* centroids);
double calculateClusterSSE(const DataPoints* dataPoints, const Centroids* centroids, size_t clusterLabel);
size_t countOrphans(const Centroids* centroids1, const Centroids* centroids2);
size_t calculateCentroidIndex(const Centroids* centroids1, const Centroids* centroids2);
bool hasGroundTruth(const Centroids* groundTruth);
size_t centroidIndexOrNA(const Centroids* centroids, const Centroids* groundTruth);

#endif /* METRICS_H */
