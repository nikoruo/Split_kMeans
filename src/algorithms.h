/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include "dataset.h"
#include "rng.h"

/** @brief The algorithms; getAlgorithmName gives the output file prefix, `--algo` the command-line names. */
typedef enum
{
    ALGO_SKM_INTRA = 0,       /**< skm-intra: the chosen cluster is split by local 2-means only. */
    ALGO_SKM_GLOBAL = 1,      /**< skm-global: the best evaluated local 2-means, then global k-means (runSseSplit). */
    ALGO_SKM_LOCAL = 2,       /**< skm-local: local 2-means, then local repartition. */
    ALGO_RS = 3,              /**< rs: Random Swap. */
    ALGO_BISECTING = 4,       /**< bisecting: bisecting k-means. */
    ALGO_SKM_RANDOM = 5,      /**< skm-random: a random cluster is split by local 2-means. */
    ALGO_KM = 6,              /**< km: k-means. */
    ALGO_RKM = 7,             /**< rkm: repeated k-means. */
    ALGO_SKM_GLOBAL_BASIC = 8, /**< skm-global-basic: two random points, then global k-means (splitClusterGlobal). */
    ALGORITHM_COUNT
} Algorithm;

typedef struct
{
    const char* name;
    Algorithm id;
} AlgorithmOption;

typedef enum
{
    REPARTITION_INWARD,
    REPARTITION_INWARD_NONCHILD,
    REPARTITION_BOTH
} RepartitionMode;

typedef enum
{
    BISECT_SELECT_SSE,
    BISECT_SELECT_SIZE
} BisectSelect;

extern const AlgorithmOption algorithmOptions[]; /* ALGORITHM_COUNT entries, checked in algorithms.c */

struct Tracker;

const char* getAlgorithmName(Algorithm algorithm);
const char* repartitionModeName(RepartitionMode mode);
const char* bisectSelectName(BisectSelect mode);
/* Split runners require positive maxIterations and centroid capacity of at least maxCentroids. */
double runRandomSplit(Dataset* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations,
                      struct Tracker* tracker, Rng* rng);
double runSseSplit(Dataset* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations,
                   Algorithm splitType, RepartitionMode repartition, struct Tracker* tracker, Rng* rng);

#endif /* ALGORITHMS_H */
