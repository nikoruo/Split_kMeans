/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef EXPERIMENT_H
#define EXPERIMENT_H

#include "algorithms.h"
#include "kmeans.h"
#include "tracking.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RUN_CONFIG_SIZE 8192

typedef struct
{
    uint64_t seed;
    size_t runs;
    bool runsGiven;
    size_t maxIterations;
    size_t maxSwaps;
    size_t maxRepeats;
    size_t bisectTries;
    BisectSelect bisectSelect;
    InitMethod init;
    RepartitionMode repartition;
    size_t firstRun;
    bool algorithms[ALGORITHM_COUNT];
} Options;

void createUniqueDirectory(char* outputDirectory, size_t size);
/* Existing paths must be directories. */
void createDatasetDirectory(const char* baseDirectory, const char* datasetName, char* datasetDirectory, size_t size);
void checkRunConfig(const char* outputDirectory, const Options* opt);
void writeRunConfig(const char* outputDirectory, const Options* opt, const TrackOptions* track,
                    const char* snapshotsText, const char* datasetsText, bool cliMode, int argc, char* argv[]);
void textLine(const char* text, size_t index, char* line, size_t size);

/* Wrappers use the zero-based range [firstRun, loopCount), with firstRun < loopCount and scaling > 0.
 * Data must be nonempty with 1 <= K <= distinct rows; track must be non-null and groundTruth may be NULL or empty.
 * Iteration limits, RKM repeats and bisecting tries must be positive where used. */
void runKMeansAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations,
                        size_t firstRun, size_t loopCount, size_t scaling, const char* fileName,
                        const char* outputDirectory, const TrackOptions* track, InitMethod init, uint64_t seed);
/* Returns false if a run has no finite repeat; earlier runs remain on disk and current labels are not a result. */
bool runRepeatedKMeansAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids,
                                size_t maxIterations, size_t maxRepeats, size_t firstRun, size_t loopCount,
                                size_t scaling, const char* fileName, const char* outputDirectory,
                                const TrackOptions* track, InitMethod init, uint64_t seed);
void runRandomSwapAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxSwaps,
                            size_t firstRun, size_t loopCount, size_t scaling, const char* fileName,
                            const char* outputDirectory, const TrackOptions* track, InitMethod init,
                            size_t distinctRows, uint64_t seed);
void runRandomSplitAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids,
                             size_t maxIterations, size_t firstRun, size_t loopCount, size_t scaling,
                             const char* fileName, const char* outputDirectory, const TrackOptions* track,
                             uint64_t seed);
void runSseSplitAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids,
                          size_t maxIterations, size_t firstRun, size_t loopCount, size_t scaling, const char* fileName,
                          const char* outputDirectory, Algorithm splitType, RepartitionMode repartition,
                          const TrackOptions* track, uint64_t seed);
void runBisectingKMeansAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids,
                                 size_t maxIterations, size_t firstRun, size_t loopCount, size_t scaling,
                                 const char* fileName, const char* outputDirectory, const TrackOptions* track,
                                 size_t bisectingIterations, BisectSelect selection, uint64_t seed);

#endif /* EXPERIMENT_H */
