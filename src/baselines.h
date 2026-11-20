/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef BASELINES_H
#define BASELINES_H

#include "dataset.h"
#include "rng.h"
#include "tracking.h"

void localRepartitionForRS(Dataset* dataPoints, Centroids* centroids, size_t removed);
double randomSwap(Dataset* dataPoints, Centroids* centroids, size_t maxSwaps, Tracker* tracker, Rng* rng);
/* Bisecting requires centroid capacity of at least maxCentroids. */
double runBisectingKMeans(Dataset* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations,
                          size_t bisectingIterations, Tracker* tracker, Rng* rng);

#endif /* BASELINES_H */
