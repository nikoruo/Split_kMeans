/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef BASELINES_H
#define BASELINES_H

#include "algorithms.h"
#include "dataset.h"
#include "rng.h"
#include "tracking.h"

void localRepartitionForRS(Dataset* dataPoints, const Centroids* centroids, size_t removed, bool* affected);
/* distinctRows is the data's distinct count capped at K+1. */
double randomSwap(Dataset* dataPoints, Centroids* centroids, size_t maxSwaps, size_t distinctRows, Tracker* tracker,
                  Rng* rng);
/* Bisecting requires centroid capacity of at least maxCentroids. */
double runBisectingKMeans(Dataset* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations,
                          size_t bisectingIterations, BisectSelect selection, Tracker* tracker, Rng* rng);

#endif /* BASELINES_H */
