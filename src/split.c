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
  * @brief Frees the centroid array of a clustering result (including each centroid's attributes) and resets the pointer.
  *
  * Does not free the `ClusteringResult` object itself. `numCentroids` is the number of centroids in `result->centroids`.
  */
 void freeClusteringResult(ClusteringResult* result, size_t numCentroids)
 {
     if (result == NULL) return;

     if (result->centroids != NULL)
     {
         freeDataPointArray(result->centroids, numCentroids);
         result->centroids = NULL;
     }
 }

/**
   * @brief A clustering result with SSE DBL_MAX and `numCentroids` zeroed centroids of `dimensions` attributes.
   */
  ClusteringResult allocateClusteringResult(size_t numCentroids, size_t dimensions)
  {
      assert(numCentroids > 0 && dimensions > 0);

      ClusteringResult result;
      result.sse = DBL_MAX;
      result.centroids = malloc(numCentroids * sizeof(DataPoint));
      handleMemoryError(result.centroids);
      for (size_t i = 0; i < numCentroids; ++i)
      {
          result.centroids[i] = allocateDataPoint(dimensions);
      }

      return result;
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
   * @param tracker Optional: `_inner.csv` rows of the local 2-means (stage split) and the ground truth for the level-3 log.
   * @errors Exits on allocation failure. Asserts that the cluster has at least 2 points.
   */
  void splitClusterIntraCluster(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t localMaxIterations, Tracker* tracker, Rng* rng)
  {
      size_t clusterSize = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterSize++;
          }
      }

      // The selection only picks clusters with at least 2 points
      assert(clusterSize >= 2);

      // Collect indices of points in the cluster
      size_t* clusterIndices = malloc(clusterSize * sizeof(size_t));
      handleMemoryError(clusterIndices);

      size_t index = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterIndices[index++] = i;
          }
      }

      // Random centroids
      size_t c1 = rng_below(rng, clusterSize);
      size_t c2 = c1;
      while (c2 == c1)
      {
          c2 = rng_below(rng, clusterSize);
      }

      size_t datapoint1 = clusterIndices[c1];
      size_t datapoint2 = clusterIndices[c2];

      // Initialize local centroids
      Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);

      deepCopyDataPoint(&localCentroids.points[0], &dataPoints->points[datapoint1]);
      deepCopyDataPoint(&localCentroids.points[1], &dataPoints->points[datapoint2]);

      // Prepare data points in the cluster
      DataPoints pointsInCluster;
      pointsInCluster.size = clusterSize;
      pointsInCluster.points = malloc(clusterSize * sizeof(DataPoint));
      handleMemoryError(pointsInCluster.points);
      for (size_t i = 0; i < clusterSize; ++i)
      {
          pointsInCluster.points[i] = dataPoints->points[clusterIndices[i]];
      }

      //partition step
      partitionStep(&pointsInCluster, &localCentroids);

      // Run local k-means
      runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, tracker, KM_STAGE_SPLIT);

      // Update partitions
      for (size_t i = 0; i < clusterSize; ++i)
      {
          size_t originalIndex = clusterIndices[i];
          dataPoints->points[originalIndex].partition = (pointsInCluster.points[i].partition == 0) ? clusterToSplit : centroids->size;
      }

      // Update centroids
      //#1
      deepCopyDataPoint(&centroids->points[clusterToSplit], &localCentroids.points[0]);

      //#2
      centroids->size++;
      centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint));
      handleMemoryError(centroids->points);
      centroids->points[centroids->size - 1] = allocateDataPoint(dataPoints->points[0].dimensions);
      deepCopyDataPoint(&centroids->points[centroids->size - 1], &localCentroids.points[1]);


      // Cleanup
      free(clusterIndices);
      free(pointsInCluster.points);
      freeCentroids(&localCentroids);
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
   * @param tracker Optional: the after-split snapshot, the `_inner.csv` rows (stages split and global) and the ground truth for the level-3 log.
   * @return SSE after the global k-means.
   */
  double splitClusterGlobalV2(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t localMaxIterations, Tracker* tracker, Rng* rng)
  {
      size_t clusterSize = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterSize++;
          }
      }

      // The selection only picks clusters with at least 2 points
      assert(clusterSize >= 2);

      // Collect indices of points in the cluster
      size_t* clusterIndices = malloc(clusterSize * sizeof(size_t));
      handleMemoryError(clusterIndices);

      size_t index = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterIndices[index++] = i;
          }
      }

      // Random centroids
      size_t c1 = rng_below(rng, clusterSize);
      size_t c2 = c1;
      while (c2 == c1)
      {
          c2 = rng_below(rng, clusterSize);
      }

      size_t datapoint1 = clusterIndices[c1];
      size_t datapoint2 = clusterIndices[c2];

      // Initialize local centroids
      Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);

      deepCopyDataPoint(&localCentroids.points[0], &dataPoints->points[datapoint1]);
      deepCopyDataPoint(&localCentroids.points[1], &dataPoints->points[datapoint2]);

      // Prepare data points in the cluster
      DataPoints pointsInCluster;
      pointsInCluster.size = clusterSize;
      pointsInCluster.points = malloc(clusterSize * sizeof(DataPoint));
      handleMemoryError(pointsInCluster.points);
      for (size_t i = 0; i < clusterSize; ++i)
      {
          pointsInCluster.points[i] = dataPoints->points[clusterIndices[i]];
      }

      // Run local k-means
      runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, tracker, KM_STAGE_SPLIT);

      // Update partitions
      for (size_t i = 0; i < clusterSize; ++i)
      {
          size_t originalIndex = clusterIndices[i];
          dataPoints->points[originalIndex].partition = (pointsInCluster.points[i].partition == 0) ? clusterToSplit : centroids->size;
      }

      // Update centroids
      //#1
      deepCopyDataPoint(&centroids->points[clusterToSplit], &localCentroids.points[0]);

      //#2
      centroids->size++;
      centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint));
      handleMemoryError(centroids->points);
      centroids->points[centroids->size - 1] = allocateDataPoint(dataPoints->points[0].dimensions);
      deepCopyDataPoint(&centroids->points[centroids->size - 1], &localCentroids.points[1]);

      trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

      double sse = runKMeans(dataPoints, localMaxIterations, centroids, tracker, KM_STAGE_GLOBAL);


      // Cleanup
      free(clusterIndices);
      free(pointsInCluster.points);
      freeCentroids(&localCentroids);

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
   * @param tracker Optional: the after-split snapshot, the `_inner.csv` rows (stage global) and the ground truth for the level-3 log.
   * @return SSE after global refinement.
   * @errors Exits on allocation failure. Asserts that the cluster has at least 2 points.
   * @note Runs as --algo skm-global-basic, the basic comparison for SKM-Global: global k-means starts right after
   *       the two random seeds, without the local 2-means. Whether this gives worse results is measured in 6.7.
   */
  double splitClusterGlobal(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t globalMaxIterations, Tracker* tracker, Rng* rng)
  {
      size_t clusterSize = 0;

      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterSize++;
          }
      }

          // The selection only picks clusters with at least 2 points
          assert(clusterSize >= 2);

      // Collect the indices of the points in the selected cluster
      size_t* clusterIndices = malloc(clusterSize * sizeof(size_t));
      handleMemoryError(clusterIndices);

      size_t index = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterIndices[index++] = i;
          }
      }

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
      deepCopyDataPoint(&centroids->points[clusterToSplit], &dataPoints->points[datapoint1]);

      // Add the second new centroid to the global centroids list
      centroids->size++;
      centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint));
      handleMemoryError(centroids->points);
      centroids->points[centroids->size - 1] = allocateDataPoint(dataPoints->points[0].dimensions);
      deepCopyDataPoint(&centroids->points[centroids->size - 1], &dataPoints->points[datapoint2]);

      trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

      // Run global k-means, this time including the new centroids
      double resultSse = runKMeans(dataPoints, globalMaxIterations, centroids, tracker, KM_STAGE_GLOBAL);

      // Cleanup
      free(clusterIndices);

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
  void localRepartition(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, bool* clustersAffected)
  {
      size_t newClusterIndex = centroids->size - 1;

      // new clusters -> old clusters
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit || dataPoints->points[i].partition == newClusterIndex)
          {
              size_t nearestCentroid = findNearestCentroid(&dataPoints->points[i], centroids);

              if (dataPoints->points[i].partition != nearestCentroid)
              {
                  clustersAffected[dataPoints->points[i].partition] = true; // Mark the old cluster as affected
                  clustersAffected[nearestCentroid] = true;                 // Mark the new cluster as affected
                  dataPoints->points[i].partition = nearestCentroid;
              }
          }
      }

      // old clusters -> new clusters
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          size_t currentCluster = dataPoints->points[i].partition;

          // skip new cluster -> old cluster
          if (currentCluster == clusterToSplit || currentCluster == newClusterIndex)
              continue;

          // Calculate distance to the current centroid
          double currentDistance = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[currentCluster]);

          // Calculate distance to the cluster to be split
          double distanceToSplit = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[clusterToSplit]);

          // Calculate distance to the new cluster
          double distanceToNew = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[newClusterIndex]);

          if (distanceToSplit < currentDistance || distanceToNew < currentDistance)
          {
              clustersAffected[currentCluster] = true;    // Mark the old cluster as affected
              dataPoints->points[i].partition = distanceToSplit <= distanceToNew ? clusterToSplit : newClusterIndex;
          }
      }
  }

/**
   * @brief Estimates SSE improvement from splitting one cluster with local k-means.
   *
   * Copies the points of `clusterLabel`, initializes two seeds from that set,
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
  double tentativeSseDrop(DataPoints* dataPoints, size_t clusterLabel, size_t localMaxIterations, double originalClusterSSE, Rng* rng)
  {
      size_t clusterSize = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterLabel)
          {
              clusterSize++;
          }
      }

      if (clusterSize < 2)
      {
          return 0.0;
      }

      DataPoints pointsInCluster = allocateDataPoints(clusterSize, dataPoints->points[0].dimensions);

      size_t index = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterLabel)
          {
              deepCopyDataPoint(&pointsInCluster.points[index], &dataPoints->points[i]);
              index++;
          }
      }

      // Random centroids
      size_t c1 = rng_below(rng, clusterSize);
      size_t c2 = c1;
      while (c2 == c1)
      {
          c2 = rng_below(rng, clusterSize);
      }

      Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);

      deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[c1]);
      deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[c2]);

      // k-means
      double resultSse = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, NULL, KM_STAGE_NONE);

      double sseDrop = originalClusterSSE - resultSse;

      freeDataPoints(&pointsInCluster);
      freeCentroids(&localCentroids);

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
   * @return ClusteringResult with `centroids[2]` and `sse` for the cluster’s points.
   * @errors Exits on allocation failure. Asserts that the cluster has at least 2 points.
   */
  ClusteringResult tentativeSplitterForBisecting(DataPoints* dataPoints, size_t clusterLabel, size_t localMaxIterations, Tracker* tracker, Rng* rng)
  {
      size_t clusterSize = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterLabel)
          {
              clusterSize++;
          }
      }

      // The selection only picks clusters with at least 2 points
      assert(clusterSize >= 2);

      DataPoints pointsInCluster = allocateDataPoints(clusterSize, dataPoints->points[0].dimensions);

      size_t index = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterLabel)
          {
              deepCopyDataPoint(&pointsInCluster.points[index], &dataPoints->points[i]);
              index++;
          }
      }

      // Random centroids
      size_t c1 = rng_below(rng, clusterSize);
      size_t c2 = c1;
      while (c2 == c1)
      {
          c2 = rng_below(rng, clusterSize);
      }

      Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);
      deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[c1]);
      deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[c2]);

      ClusteringResult localResult = allocateClusteringResult(2, dataPoints->points[0].dimensions);

      // k-means
      localResult.sse = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, tracker, KM_STAGE_TRY);

      deepCopyDataPoint(&localResult.centroids[0], &localCentroids.points[0]);
      deepCopyDataPoint(&localResult.centroids[1], &localCentroids.points[1]);

      freeDataPoints(&pointsInCluster);
      freeCentroids(&localCentroids);;

      return localResult;
  }

/**
   * @brief Counts the points of clusters 0..numClusters-1 into `sizes`; returns how many have at least 2 points.
   */
  size_t countClusterSizes(const DataPoints* dataPoints, size_t numClusters, size_t* sizes)
  {
      memset(sizes, 0, numClusters * sizeof(size_t));
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          assert(dataPoints->points[i].partition < numClusters);
          sizes[dataPoints->points[i].partition]++;
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
