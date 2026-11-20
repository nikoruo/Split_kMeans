/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef ALGORITHMS_H
#define ALGORITHMS_H

/** @brief The algorithms; getAlgorithmName gives the output file prefix, `--algo` the command-line names. */
typedef enum
{
    ALGO_SKM_INTRA = 0,       /**< skm-intra: the chosen cluster is split by local 2-means only. */
    ALGO_SKM_GLOBAL = 1,      /**< skm-global: local 2-means, then global k-means (splitClusterGlobalV2). */
    ALGO_SKM_LOCAL = 2,       /**< skm-local: local 2-means, then local repartition. */
    ALGO_RS = 3,              /**< rs: Random Swap. */
    ALGO_BISECTING = 4,       /**< bisecting: bisecting k-means. */
    ALGO_SKM_RANDOM = 5,      /**< skm-random: a random cluster is split by local 2-means. */
    ALGO_KM = 6,              /**< km: k-means. */
    ALGO_RKM = 7,             /**< rkm: repeated k-means. */
    ALGO_SKM_GLOBAL_BASIC = 8 /**< skm-global-basic: two random points, then global k-means (splitClusterGlobal). */
} Algorithm;

#endif /* ALGORITHMS_H */
