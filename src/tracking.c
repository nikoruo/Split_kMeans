/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"
#include "tracking.h"
#include "metrics.h"
#include "util.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Resets the stopwatch to zero and starts it. */
static void stopwatchStart(Stopwatch* stopwatch)
{
    stopwatch->elapsedTicks = 0;
    stopwatch->running = true;
    stopwatch->startTicks = platform_ticks();
}

/** @brief Stops the clock; the time until resume is not counted. No-op when already paused. */
static void stopwatchPause(Stopwatch* stopwatch)
{
    if (stopwatch->running)
    {
        stopwatch->elapsedTicks += platform_ticks() - stopwatch->startTicks;
        stopwatch->running = false;
    }
}

/** @brief Restarts the clock after a pause. No-op when running. */
static void stopwatchResume(Stopwatch* stopwatch)
{
    if (!stopwatch->running)
    {
        stopwatch->running = true;
        stopwatch->startTicks = platform_ticks();
    }
}

/** @brief Counted time in milliseconds, paused intervals excluded. */
static double stopwatchElapsedMs(const Stopwatch* stopwatch)
{
    uint64_t ticks = stopwatch->elapsedTicks;
    if (stopwatch->running)
    {
        ticks += platform_ticks() - stopwatch->startTicks;
    }
    return (double)ticks * 1000.0 / (double)platform_ticks_per_second();
}

/**
 * @brief Appends the buffered rows of one run to `filePath`: run;count;sse;ci;ms (ci is NA when not available).
 *
 * Writes the header first when the file does not exist yet. Call outside timed code.
 */
static void appendLogCsv(const char* filePath, size_t run, const TrackRow* rows, size_t rowCount)
{
    const bool writeHeader = !fileExists(filePath);

    FILE* file;
    if (FOPEN(file, filePath, "a") != 0)
    {
        handleFileError(filePath);
        return;
    }
    if (writeHeader)
    {
        fprintf(file, "run;count;sse;ci;ms\n");
    }
    char ciText[CI_TEXT_SIZE];
    char sseText[NUMBER_TEXT_SIZE];
    char msText[NUMBER_TEXT_SIZE];
    for (size_t i = 0; i < rowCount; ++i)
    {
        fprintf(file, "%zu;%zu;%s;%s;%s\n", run + 1, rows[i].count, formatFixed(rows[i].sse, 6, sseText),
                formatCi(rows[i].ci, ciText), formatFixed(rows[i].ms, 3, msText));
    }
    fclose(file);
}

/**
 * @brief Appends one row per run to `<outputDirectory>/<algorithmName>_runs.csv` (run;seed;sse;ci;ms).
 *
 * The seed reproduces the run; SSE uses %.17g so it round-trips; ci is NA without ground truth. Call outside timed
 * code.
 */
void appendRunCsv(const char* outputDirectory, const char* algorithmName, size_t run, uint64_t seed, double sse,
                  size_t ci, double ms)
{
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s%c%s_runs.csv", outputDirectory, PATHSEP, algorithmName) >= (int)sizeof(path))
    {
        fprintf(stderr, "Buffer too small in appendRunCsv\n");
        exit(EXIT_FAILURE);
    }

    const bool writeHeader = !fileExists(path);

    FILE* file;
    if (FOPEN(file, path, "a") != 0)
    {
        handleFileError(path);
    }

    if (writeHeader)
    {
        fprintf(file, "run;seed;sse;ci;ms\n");
    }
    char ciText[CI_TEXT_SIZE];
    fprintf(file, "%zu;%" PRIu64 ";%.17g;%s;%.17g\n", run + 1, seed, sse, formatCi(ci, ciText), ms);
    fclose(file);
}

/**
 * @brief Writes centroids to `outputDirectory/filename`: one row per centroid, attributes `%f` separated by one space.
 *
 * `%f` keeps 6 decimals (snapshot files for the figures, not for exact comparisons); the program never changes
 * the locale, so the decimal separator is a point. The folder must exist. Exits when the file cannot be opened;
 * write errors are not checked.
 */
static void writeCentroidsToFile(const char* filename, const Centroids* centroids, const char* outputDirectory)
{
    char outputFilePath[PATH_MAX];
    joinPath(outputFilePath, sizeof(outputFilePath), outputDirectory, filename);

    FILE* centroidFile;
    if (FOPEN(centroidFile, outputFilePath, "w") != 0)
    {
        handleFileError(outputFilePath);
    }

    for (size_t i = 0; i < centroids->size; ++i)
    {
        const DataPoint* c = &centroids->points[i];
        for (size_t j = 0; j < c->dimensions; ++j)
        {
            if (j) fputc(' ', centroidFile);
            fprintf(centroidFile, "%f", c->attributes[j]);
        }
        fputc('\n', centroidFile);
    }

    fclose(centroidFile);
}

/**
 * @brief Writes one partition label per line to outputDirectory/filename.
 *
 * Serializes `dataPoints->points[i].partition` for all points in order, using the
 * current C locale. Labels are zero-based; unassigned points may appear as `SIZE_MAX`.
 *
 * @param filename Target file name (no path).
 * @param dataPoints Points whose partitions to serialize; size determines line count.
 * @param outputDirectory Destination directory; must exist.
 * @return void
 * @errors Exits on open failure via handleFileError. Write errors are not checked.
 * @note Output intended for debugging/postprocessing; format is line-oriented text.
 */
static void writeDataPointPartitionsToFile(const char* filename, const DataPoints* dataPoints,
                                           const char* outputDirectory)
{
    char outputFilePath[PATH_MAX];
    joinPath(outputFilePath, sizeof(outputFilePath), outputDirectory, filename);

    FILE* file;
    if (FOPEN(file, outputFilePath, "w") != 0)
    {
        handleFileError(outputFilePath);
    }

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        fprintf(file, "%zu\n", dataPoints->points[i].partition);
    }

    fclose(file);
}

/**
 * @brief Writes the path `<outputDirectory>/<AlgorithmName><suffix>` into `path`; exits when it does not fit.
 */
void algorithmFilePath(Algorithm algorithm, const char* outputDirectory, const char* suffix, char* path,
                       size_t pathSize)
{
    const char* algorithmName = getAlgorithmName(algorithm);

    if (snprintf(path, pathSize, "%s%c%s%s", outputDirectory, PATHSEP, algorithmName, suffix) >= (int)pathSize)
    {
        fprintf(stderr, "Error: Path too long: %s%c%s%s\n", outputDirectory, PATHSEP, algorithmName, suffix);
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Saves example centroid and partition files for perfect and failed runs.
 *
 * If `centroidIndex` is 0 (perfect run) and `*savedPerfect` is false, saves the
 * centroids and partitions to `<algorithmPrefix>_centroids_perfect.txt` and
 * `<algorithmPrefix>_partitions_perfect.txt`, then sets `*savedPerfect` to true.
 * If `centroidIndex` is non-zero (failed run) and `*savedFailed` is false, saves
 * to `<algorithmPrefix>_centroids_failed.txt` and
 * `<algorithmPrefix>_partitions_failed.txt`, then sets `*savedFailed` to true.
 *
 * @param centroids Centroids to save.
 * @param dataPoints Data points whose partitions to save.
 * @param centroidIndex Index of the centroid set (0 = perfect; CI_NA saves nothing).
 * @param algorithmPrefix Prefix for output file names.
 * @param outputDirectory Directory where files are saved.
 * @param savedPerfect Pointer to a bool tracking if perfect example was saved.
 * @param savedFailed Pointer to a bool tracking if failed example was saved.
 * @errors Exits when a file cannot be opened (via writeCentroidsToFile and writeDataPointPartitionsToFile).
 */
void saveExampleSnapshots(const Centroids* centroids, const DataPoints* dataPoints, size_t centroidIndex,
                          const char* algorithmPrefix, const char* outputDirectory, bool* savedPerfect,
                          bool* savedFailed)
{
    if (centroidIndex == CI_NA)
    {
        return; // No ground truth: a run cannot be classed as perfect or failed
    }

    if (centroidIndex != 0 && !(*savedFailed))
    {
        char centroidsFile[PATH_MAX];
        char partitionsFile[PATH_MAX];
        snprintf(centroidsFile, sizeof(centroidsFile), "%s_centroids_failed.txt", algorithmPrefix);
        snprintf(partitionsFile, sizeof(partitionsFile), "%s_partitions_failed.txt", algorithmPrefix);

        writeCentroidsToFile(centroidsFile, centroids, outputDirectory);
        writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
        *savedFailed = true;
    }
    else if (centroidIndex == 0 && !(*savedPerfect))
    {
        char centroidsFile[PATH_MAX];
        char partitionsFile[PATH_MAX];
        snprintf(centroidsFile, sizeof(centroidsFile), "%s_centroids_perfect.txt", algorithmPrefix);
        snprintf(partitionsFile, sizeof(partitionsFile), "%s_partitions_perfect.txt", algorithmPrefix);

        writeCentroidsToFile(centroidsFile, centroids, outputDirectory);
        writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
        *savedPerfect = true;
    }
}

/**
 * @brief Writes a snapshot: `<algo>_run<RRR>_iter<IIII>_<phase>_centroids.txt` and `..._partitions.txt` in
 * `outputDirectory`.
 *
 * `run` is 1-based and `iteration` is the count of `_log.csv`; both are zero-padded (3 and 4 digits) so the
 * names sort in order. Phases: start, split (after the two-way split), step, final (after the final k-means).
 * Centroids: one space-separated row per centroid; partitions: one 0-based label per line.
 */
static void saveIterationState(const DataPoints* dataPoints, const Centroids* centroids, size_t run, size_t iteration,
                               const char* phase, const char* outputDirectory, const char* algorithmName)
{
    char centroidsFileName[PATH_MAX];
    char partitionsFileName[PATH_MAX];

    if (snprintf(centroidsFileName, sizeof(centroidsFileName), "%s_run%03zu_iter%04zu_%s_centroids.txt", algorithmName,
                 run, iteration, phase) >= (int)sizeof(centroidsFileName) ||
        snprintf(partitionsFileName, sizeof(partitionsFileName), "%s_run%03zu_iter%04zu_%s_partitions.txt",
                 algorithmName, run, iteration, phase) >= (int)sizeof(partitionsFileName))
    {
        fprintf(stderr, "Buffer too small in saveIterationState\n");
        exit(EXIT_FAILURE);
    }

    writeCentroidsToFile(centroidsFileName, centroids, outputDirectory);
    writeDataPointPartitionsToFile(partitionsFileName, dataPoints, outputDirectory);
}

/**
 * @brief Appends one row to `<outputDirectory>/<algorithmName>_iteration_stats.txt`, with a header when the file is
 * new.
 *
 * One row per snapshot. Columns: Run;Iteration;Phase;NumCentroids;SSE;CI;SplitCluster;MS (run 1-based;
 * CI is NA without ground truth; SplitCluster is SIZE_MAX when none).
 */
static void writeIterationStats(const Centroids* centroids, size_t ci, size_t run, size_t iteration, const char* phase,
                                double sse, size_t splitCluster, double elapsedMs, const char* outputDirectory,
                                const char* algorithmName)
{
    char statsFileName[PATH_MAX];
    if (snprintf(statsFileName, sizeof(statsFileName), "%s_iteration_stats.txt", algorithmName) >=
        (int)sizeof(statsFileName))
    {
        fprintf(stderr, "Buffer too small in writeIterationStats (name)\n");
        exit(EXIT_FAILURE);
    }

    char outputFilePath[PATH_MAX];
    joinPath(outputFilePath, sizeof(outputFilePath), outputDirectory, statsFileName);

    FILE* statsFile = NULL;
    if (FOPEN(statsFile, outputFilePath, "a+") != 0)
    {
        handleFileError(outputFilePath);
    }

    /* Write header once when the file is empty */
    fseek(statsFile, 0, SEEK_END);
    if (ftell(statsFile) == 0)
    {
        fprintf(statsFile, "Run;Iteration;Phase;NumCentroids;SSE;CI;SplitCluster;MS\n");
    }

    char ciText[CI_TEXT_SIZE];
    char sseText[NUMBER_TEXT_SIZE];
    char msText[NUMBER_TEXT_SIZE];
    fprintf(statsFile, "%zu;%zu;%s;%zu;%s;%s;%zu;%s\n", run, iteration, phase, centroids->size,
            formatFixed(sse, 6, sseText), formatCi(ci, ciText), splitCluster, formatFixed(elapsedMs, 3, msText));

    fclose(statsFile);
}

/**
 * @brief Writes the iteration stats row and the snapshot files of the current state (`run` 1-based).
 */
static void trackProgressState(const DataPoints* dataPoints, const Centroids* centroids, double sse, size_t ci,
                               size_t run, size_t iteration, const char* phase, size_t clusterToSplit,
                               Algorithm splitType, const char* outputDirectory, double elapsedMs)
{
    const char* splitTypeName = getAlgorithmName(splitType);

    writeIterationStats(centroids, ci, run, iteration, phase, sse, clusterToSplit, elapsedMs, outputDirectory,
                        splitTypeName);
    saveIterationState(dataPoints, centroids, run, iteration, phase, outputDirectory, splitTypeName);
}

/** @brief True when `value` is in the selection. */
static bool selectionContains(const Selection* selection, size_t value)
{
    if (selection->all)
    {
        return true;
    }
    for (size_t i = 0; i < selection->rangeCount; ++i)
    {
        if (value >= selection->low[i] && value <= selection->high[i])
        {
            return true;
        }
    }
    return false;
}

/** @brief Name of a k-means stage in `_inner.csv`. */
static const char* kmeansStageName(KMeansStage stage)
{
    switch (stage)
    {
        case KM_STAGE_SPLIT:
            return "split";
        case KM_STAGE_TRY:
            return "try";
        case KM_STAGE_GLOBAL:
            return "global";
        case KM_STAGE_FINAL:
            return "final";
        default:
            return "none";
    }
}

/**
 * @brief Appends the `_inner.csv` rows of one run to `filePath`: run;count;stage;iteration;k;sse;ci;ms.
 *
 * Writes the header first when the file does not exist yet. Call outside timed code.
 */
static void appendInnerCsv(const char* filePath, size_t run, const InnerRow* rows, size_t rowCount)
{
    const bool writeHeader = !fileExists(filePath);

    FILE* file;
    if (FOPEN(file, filePath, "a") != 0)
    {
        handleFileError(filePath);
    }
    if (writeHeader)
    {
        fprintf(file, "run;count;stage;iteration;k;sse;ci;ms\n");
    }
    char ciText[CI_TEXT_SIZE];
    char sseText[NUMBER_TEXT_SIZE];
    char msText[NUMBER_TEXT_SIZE];
    for (size_t i = 0; i < rowCount; ++i)
    {
        const InnerRow* row = &rows[i];
        fprintf(file, "%zu;%zu;%s;%zu;%zu;%s;%s;%s\n", run + 1, row->count, kmeansStageName(row->stage), row->iteration,
                row->k, formatFixed(row->sse, 6, sseText), formatCi(row->ci, ciText), formatFixed(row->ms, 3, msText));
    }
    fclose(file);
}

/**
 * @brief Tracker for one algorithm with the switches in `options`.
 *
 * `outputDirectory`, `groundTruth` and `options` must outlive the tracker. Free with trackerFree.
 */
Tracker trackerCreate(Algorithm algorithm, const char* outputDirectory, const Centroids* groundTruth,
                      const TrackOptions* options)
{
    Tracker tracker;
    tracker.groundTruth = groundTruth;
    tracker.outputDirectory = outputDirectory;
    tracker.algorithm = algorithm;
    tracker.options = options;
    tracker.logRows = options->rows;
    tracker.inner = options->inner;
    tracker.run = 0;
    tracker.snapshots = false;
    tracker.nextCount = 0;
    tracker.stopwatch.startTicks = 0;
    tracker.stopwatch.elapsedTicks = 0;
    tracker.stopwatch.running = false;
    tracker.rows = NULL;
    tracker.rowCount = 0;
    tracker.rowCapacity = 0;
    tracker.innerRows = NULL;
    tracker.innerCount = 0;
    tracker.innerCapacity = 0;
    return tracker;
}

/** @brief Frees the row buffers. */
void trackerFree(Tracker* tracker)
{
    free(tracker->rows);
    tracker->rows = NULL;
    tracker->rowCount = 0;
    tracker->rowCapacity = 0;
    free(tracker->innerRows);
    tracker->innerRows = NULL;
    tracker->innerCount = 0;
    tracker->innerCapacity = 0;
}

/** @brief Starts run `run` (0-based): clears the rows, selects the snapshots of this run and starts the stopwatch. */
void trackerBeginRun(Tracker* tracker, size_t run)
{
    tracker->run = run;
    tracker->snapshots =
        tracker->options->snapshots.enabled && selectionContains(&tracker->options->snapshots.runs, run + 1);
    tracker->nextCount = 0;
    tracker->rowCount = 0;
    tracker->innerCount = 0;
    stopwatchStart(&tracker->stopwatch);
}

/** @brief Stops the stopwatch, appends the buffered rows to `_log.csv` and `_inner.csv` and returns the run time in ms.
 */
double trackerEndRun(Tracker* tracker)
{
    stopwatchPause(&tracker->stopwatch);
    const double ms = stopwatchElapsedMs(&tracker->stopwatch);

    char path[PATH_MAX];
    if (tracker->logRows && tracker->rowCount > 0)
    {
        algorithmFilePath(tracker->algorithm, tracker->outputDirectory, "_log.csv", path, sizeof(path));
        appendLogCsv(path, tracker->run, tracker->rows, tracker->rowCount);
    }
    if (tracker->inner && tracker->innerCount > 0)
    {
        algorithmFilePath(tracker->algorithm, tracker->outputDirectory, "_inner.csv", path, sizeof(path));
        appendInnerCsv(path, tracker->run, tracker->innerRows, tracker->innerCount);
    }
    return ms;
}

/** @brief Buffers one row; the caller has paused the stopwatch. */
static void trackerPushRow(Tracker* tracker, size_t count, double sse, size_t ci, double ms)
{
    if (tracker->rowCount == tracker->rowCapacity)
    {
        const size_t grown = tracker->rowCapacity ? tracker->rowCapacity * 2 : 64;
        TrackRow* rows = realloc(tracker->rows, grown * sizeof(TrackRow));
        handleMemoryError(rows);
        tracker->rows = rows;
        tracker->rowCapacity = grown;
    }

    TrackRow* row = &tracker->rows[tracker->rowCount++];
    row->count = count;
    row->sse = sse;
    row->ci = ci;
    row->ms = ms;
}

/** @brief Buffers a `_log.csv` row with known SSE and CI at the current run time; no-op without row logging. */
void trackerAddRow(Tracker* tracker, size_t count, double sse, size_t ci)
{
    if (!tracker->logRows)
    {
        return;
    }
    stopwatchPause(&tracker->stopwatch);
    trackerPushRow(tracker, count, sse, ci, stopwatchElapsedMs(&tracker->stopwatch));
    stopwatchResume(&tracker->stopwatch);
}

/** @brief CI of `centroids` against the ground truth, computed with the stopwatch paused; CI_NA without ground truth.
 */
size_t trackerCi(Tracker* tracker, const Centroids* centroids)
{
    if (!hasGroundTruth(tracker->groundTruth))
    {
        return CI_NA;
    }
    stopwatchPause(&tracker->stopwatch);
    const size_t ci = calculateCentroidIndex(centroids, tracker->groundTruth);
    stopwatchResume(&tracker->stopwatch);
    return ci;
}

/** @brief True when the state at `count` of the current run is selected for a snapshot. */
static bool trackerSnapshotWanted(const Tracker* tracker, size_t count)
{
    return tracker->snapshots && selectionContains(&tracker->options->snapshots.counts, count);
}

/**
 * @brief Records the state at `count`: a `_log.csv` row and, when selected, the snapshot files and iteration stats.
 *
 * SSE and CI are computed with the stopwatch paused. Returns the CI, or CI_NA when nothing is recorded or there is no
 * ground truth. `phase` names the snapshot (start, step or final); `splitCluster` is the cluster split at this count,
 * SIZE_MAX when none.
 */
size_t trackerRecord(Tracker* tracker, size_t count, const char* phase, const DataPoints* dataPoints,
                     const Centroids* centroids, size_t splitCluster)
{
    tracker->nextCount = count + 1;

    const bool snapshot = trackerSnapshotWanted(tracker, count);
    if (!tracker->logRows && !snapshot)
    {
        return CI_NA;
    }

    stopwatchPause(&tracker->stopwatch);
    const double ms = stopwatchElapsedMs(&tracker->stopwatch);
    const double sse = calculateSSE(dataPoints, centroids);
    const size_t ci = centroidIndexOrNA(centroids, tracker->groundTruth);

    if (tracker->logRows)
    {
        trackerPushRow(tracker, count, sse, ci, ms);
    }
    if (snapshot)
    {
        trackProgressState(dataPoints, centroids, sse, ci, tracker->run + 1, count, phase, splitCluster,
                           tracker->algorithm, tracker->outputDirectory, ms);
    }

    stopwatchResume(&tracker->stopwatch);
    return ci;
}

/** @brief When selected, writes the snapshot files and iteration stats of the state at `count`, without a `_log.csv`
 * row. */
void trackerSnapshot(Tracker* tracker, size_t count, const char* phase, const DataPoints* dataPoints,
                     const Centroids* centroids, size_t splitCluster)
{
    if (!trackerSnapshotWanted(tracker, count))
    {
        return;
    }

    stopwatchPause(&tracker->stopwatch);
    const double ms = stopwatchElapsedMs(&tracker->stopwatch);
    const double sse = calculateSSE(dataPoints, centroids);
    const size_t ci = centroidIndexOrNA(centroids, tracker->groundTruth);
    trackProgressState(dataPoints, centroids, sse, ci, tracker->run + 1, count, phase, splitCluster, tracker->algorithm,
                       tracker->outputDirectory, ms);
    stopwatchResume(&tracker->stopwatch);
}

/**
 * @brief With `--snapshots ...:split`, writes the state after the two-way split of `splitCluster`, before the follow-up
 * step.
 *
 * Phase "split" at the count of the split being made. NULL tracker: no-op.
 */
void trackerSplitSnapshot(Tracker* tracker, const DataPoints* dataPoints, const Centroids* centroids,
                          size_t splitCluster)
{
    if (tracker != NULL && tracker->options->snapshots.split)
    {
        trackerSnapshot(tracker, tracker->nextCount, "split", dataPoints, centroids, splitCluster);
    }
}

/**
 * @brief With --track-inner, buffers the `_inner.csv` row of iteration `iteration` of a k-means run at `stage`.
 *
 * `sse` is that run's SSE after the iteration. CI only for the global and final stages. NULL tracker or KM_STAGE_NONE:
 * no-op.
 */
void trackerInnerRow(Tracker* tracker, KMeansStage stage, size_t iteration, const Centroids* centroids, double sse)
{
    if (tracker == NULL || !tracker->inner || stage == KM_STAGE_NONE)
    {
        return;
    }

    stopwatchPause(&tracker->stopwatch);
    if (tracker->innerCount == tracker->innerCapacity)
    {
        const size_t grown = tracker->innerCapacity ? tracker->innerCapacity * 2 : 256;
        InnerRow* rows = realloc(tracker->innerRows, grown * sizeof(InnerRow));
        handleMemoryError(rows);
        tracker->innerRows = rows;
        tracker->innerCapacity = grown;
    }

    InnerRow* row = &tracker->innerRows[tracker->innerCount++];
    row->count = tracker->nextCount;
    row->stage = stage;
    row->iteration = iteration;
    row->k = centroids->size;
    row->sse = sse;
    row->ci = (stage == KM_STAGE_GLOBAL || stage == KM_STAGE_FINAL) ? centroidIndexOrNA(centroids, tracker->groundTruth)
                                                                    : CI_NA;
    row->ms = stopwatchElapsedMs(&tracker->stopwatch);
    stopwatchResume(&tracker->stopwatch);
}
