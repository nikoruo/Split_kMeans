/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef BASELINES_H
#define BASELINES_H

#include "algorithms.h"
#include "dataset.h"
#include "rng.h"
#include "tracking.h"

/* Marks affected[K]; removed and every input label must be below K. */
void localRepartitionForRS(Dataset* dataPoints, const Centroids* centroids, size_t removed, bool* affected);
/* Requires assigned labels, a live tracker/RNG, and the correct distinct-row count capped at K+1.
 * The count is not generally validated; K equal to the distinct count performs no swaps. */
double randomSwap(Dataset* dataPoints, Centroids* centroids, size_t maxSwaps, size_t distinctRows, Tracker* tracker,
                  Rng* rng);
/* Start with one centroid and all labels 0; require positive iteration/try limits, a live tracker/RNG and
 * centroid and dimension-compatible scratch capacity for maxCentroids. */
double runBisectingKMeans(Dataset* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations,
                          size_t bisectingIterations, BisectSelect selection, Tracker* tracker, Rng* rng,
                          struct KMeansScratch* scratch);

#endif /* BASELINES_H */
