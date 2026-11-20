/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"

#include "metrics.h"
#include "util.h"

#include <math.h>
#include <stdlib.h>

/**
   * @brief Euclidean distance between two points of the same dimensionality.
   *
   * No call site; kept (Unused code review).
   */
  double calculateEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
  {
      return sqrt(calculateSquaredEuclideanDistance(point1, point2));
  }

/**
   * @brief Sums squared distances from each point to its assigned centroid.
   *
   * Assumes every point has a valid `partition` in `[0, centroids->size)` (asserted).
   *
   * @param dataPoints Dataset whose partitions are already assigned.
   * @param centroids Centroid set referenced by partitions.
   * @return Total SSE across all points.
   */
  double calculateSSE(const DataPoints* dataPoints, const Centroids* centroids)
  {
      double sse = 0.0;

      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          size_t cIndex = dataPoints->points[i].partition;

          assert(cIndex < centroids->size);

          sse += calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[cIndex]);
      }

      return sse;
  }

/**
   * @brief SSE divided by the number of values (N·D). Requires a non-empty dataset (an empty one is read out of bounds).
   *
   * No call site; kept (Unused code review). The thesis reports SSE and CI only.
   */
  double calculateMSE(const DataPoints* dataPoints, const Centroids* centroids)
  {
      const double sse = calculateSSE(dataPoints, centroids);

      const double mse = sse / ((double)dataPoints->size * (double)dataPoints->points[0].dimensions);

      return mse;
  }

/**
   * @brief Sums squared distances for points assigned to a specific cluster.
   *
   * Accumulates SSE over points with `partition == clusterLabel`. Returns 0.0 if the
   * cluster has no points.
   *
   * @param dataPoints Dataset with assigned partitions.
   * @param centroids Centroid set; `clusterLabel` must be < `centroids->size` (not checked).
   * @param clusterLabel Target cluster id.
   * @return SSE for the specified cluster.
   */
  double calculateClusterSSE(const DataPoints* dataPoints, const Centroids* centroids, size_t clusterLabel)
  {
      double sse = 0.0;

      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterLabel)
          {
              sse += calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[clusterLabel]);
          }
      }

      return sse;
  }

/**
   * @brief Counts centroids in `centroids2` with no nearest centroid from `centroids1`.
   *
   * For each centroid in `centroids1`, marks its nearest neighbor in `centroids2`.
   * Returns how many centroids in `centroids2` were never marked. Both sets must be
   * non-empty and of the same dimensionality (neither is checked; callers check at the input boundary).
   *
   * @param centroids1 Reference set used to mark nearest neighbors.
   * @param centroids2 Target set whose unmarked count is returned.
   * @return Number of orphan centroids in `centroids2`.
   */
  size_t countOrphans(const Centroids* centroids1, const Centroids* centroids2)
  {
      size_t countFrom1to2 = 0;
      bool* hasClosest = calloc(centroids2->size, sizeof(bool));
      handleMemoryError(hasClosest);

      for (size_t i = 0; i < centroids1->size; ++i)
      {
          size_t closestIndex = findNearestCentroid(&centroids1->points[i], centroids2);
          hasClosest[closestIndex] = true;
      }

      for (size_t i = 0; i < centroids2->size; ++i)
      {
          if (!hasClosest[i])
          {
              countFrom1to2++;
          }
      }

      free(hasClosest);

      return countFrom1to2;
  }

/**
   * @brief Centroid Index (CI) as max orphan count between two centroid sets.
   *
   * CI = max(orphans(centroids1 → centroids2), orphans(centroids2 → centroids1)),
   * where orphans(X→Y) is the number of centroids in Y with no centroid from X
   * selecting them as nearest. Both sets must be non-empty and of the same dimensionality
   * (not checked here). O(K^2·D).
   */
  size_t calculateCentroidIndex(const Centroids* centroids1, const Centroids* centroids2)
  {
      if (logLevel >= 3)
      {
          printf("centroids1: %zu AND 2: %zu\n\n", centroids1->size, centroids2->size);
          if (centroids1->size > 0 && centroids2->size > 0)
          {
              printf("centroids1: %f AND 2: %f\n\n", centroids1->points->attributes[0], centroids2->points->attributes[0]);
          }
      }

      size_t countFrom1to2 = countOrphans(centroids1, centroids2);
      size_t countFrom2to1 = countOrphans(centroids2, centroids1);

      if (logLevel >= 3)
      {
          printf("Count from 1 to 2: %zu AND 2 to 1: %zu\n\n", countFrom1to2, countFrom2to1);
      }

      return (countFrom1to2 > countFrom2to1) ? countFrom1to2 : countFrom2to1;
  }

/** @brief True when ground-truth centroids are loaded, so CI can be computed. */
  bool hasGroundTruth(const Centroids* groundTruth)
  {
      return groundTruth != NULL && groundTruth->size > 0;
  }

/** @brief CI against the ground truth, or CI_NA when there is none. */
  size_t centroidIndexOrNA(const Centroids* centroids, const Centroids* groundTruth)
  {
      return hasGroundTruth(groundTruth) ? calculateCentroidIndex(centroids, groundTruth) : CI_NA;
  }
