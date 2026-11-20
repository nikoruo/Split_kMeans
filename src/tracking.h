/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef TRACKING_H
#define TRACKING_H

#include "algorithms.h"
#include "dataset.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Wall-clock stopwatch on the monotonic platform clock (platform_ticks).
 *
 * Paused time is left out, so tracking and snapshot I/O can pause it. Times are in ms.
 */
typedef struct
{
    uint64_t startTicks;   /**< Clock value at the last start or resume. */
    uint64_t elapsedTicks; /**< Time accumulated before the last pause. */
    bool running;
} Stopwatch;

/** @brief One `_log.csv` row, buffered during the run and written after it. */
typedef struct
{
    size_t count; /**< Split number, swap, repeat or iteration; 0 is the initial state. */
    double sse;
    size_t ci; /**< CI_NA without ground truth. */
    double ms; /**< Counted run time when the state was reached. */
} TrackRow;

/** @brief The k-means runs that --track-inner logs (`_inner.csv` stage column); KM_STAGE_NONE logs nothing. */
typedef enum
{
    KM_STAGE_NONE,   /**< Not logged: k-means of KM, RKM and RS, trial splits. */
    KM_STAGE_SPLIT,  /**< Local 2-means that splits the chosen cluster. */
    KM_STAGE_TRY,    /**< Local 2-means of one bisecting try. */
    KM_STAGE_GLOBAL, /**< Global k-means after a split (SKM-Global, SKM-Global-Basic). */
    KM_STAGE_FINAL   /**< Final k-means of a split algorithm or bisecting. */
} KMeansStage;

/** @brief One `_inner.csv` row: one iteration of a k-means run inside a split algorithm. */
typedef struct
{
    size_t count;
    KMeansStage stage;
    size_t iteration;
    size_t k;
    double sse;
    size_t ci;
    double ms;
} InnerRow;

#define MAX_RANGES 32

/** @brief A set of 1-based runs or of counts, given as `all` or a list such as `1,3-5`. */
typedef struct
{
    bool all;
    size_t rangeCount;
    size_t low[MAX_RANGES];
    size_t high[MAX_RANGES];
} Selection;

/** @brief What --snapshots writes for the selected runs and counts. */
typedef struct
{
    bool enabled;
    Selection runs;
    Selection counts;
    bool split;
} SnapshotSpec;

/** @brief Tracking switches of one invocation. */
typedef struct
{
    bool rows;
    SnapshotSpec snapshots;
    bool inner;
} TrackOptions;

/* Owns row buffers; its options and ground truth must outlive the tracker. */
typedef struct Tracker
{
    const Centroids* groundTruth;
    const char* outputDirectory;
    Algorithm algorithm;
    const TrackOptions* options;
    bool logRows;
    bool inner;
    size_t run;
    bool snapshots;
    size_t nextCount;
    Stopwatch stopwatch;
    TrackRow* rows;
    size_t rowCount;
    size_t rowCapacity;
    InnerRow* innerRows;
    size_t innerCount;
    size_t innerCapacity;
} Tracker;

Tracker trackerCreate(Algorithm algorithm, const char* outputDirectory, const Centroids* groundTruth,
                      const TrackOptions* options);
void trackerFree(Tracker* tracker);
void trackerBeginRun(Tracker* tracker, size_t run);
double trackerEndRun(Tracker* tracker);
size_t trackerRecord(Tracker* tracker, size_t count, const char* phase, const DataPoints* dataPoints,
                     const Centroids* centroids, size_t splitCluster);
void trackerSnapshot(Tracker* tracker, size_t count, const char* phase, const DataPoints* dataPoints,
                     const Centroids* centroids, size_t splitCluster);
void trackerInnerRow(Tracker* tracker, KMeansStage stage, size_t iteration, const Centroids* centroids, double sse);
void trackerAddRow(Tracker* tracker, size_t count, double sse, size_t ci);
size_t trackerCi(Tracker* tracker, const Centroids* centroids);
void trackerSplitSnapshot(Tracker* tracker, const DataPoints* dataPoints, const Centroids* centroids,
                          size_t splitCluster);
void appendRunCsv(const char* outputDirectory, const char* algorithmName, size_t run, uint64_t seed,
                  double sse, size_t ci, double ms);
void algorithmFilePath(Algorithm algorithm, const char* outputDirectory, const char* suffix, char* path, size_t pathSize);
void saveExampleSnapshots(const Centroids* centroids, const DataPoints* dataPoints, size_t centroidIndex,
                          const char* algorithmPrefix, const char* outputDirectory,
                          bool* savedPerfect, bool* savedFailed);

#endif /* TRACKING_H */
