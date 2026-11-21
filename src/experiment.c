/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"
#include "experiment.h"
#include "baselines.h"
#include "metrics.h"
#include "util.h"
#include "version.h"

#include <assert.h>
#include <float.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief Aggregate statistics across runs.
 *
 * - sseSum: Sum of per-run SSE values.
 * - ciSum: Sum of per-run CI values.
 * - timeSum: Sum of durations across runs in milliseconds.
 * - successRate: Count of runs meeting success criteria (e.g., CI==0).
 */
typedef struct
{
    double sseSum;      /**< Sum of SSE values across runs. */
    size_t ciSum;       /**< Sum of Centroid Index (CI) values across runs. */
    double timeSum;     /**< Total time across runs in milliseconds. */
    double successRate; /**< Success count across runs (used as count, averaged later). */
    bool ciAvailable;   /**< False once a run had no CI (no ground truth); CI and success rate print as NA. */
} Statistics;

/**
 * @brief Resets all statistic accumulators to their baseline.
 */
static void initializeStatistics(Statistics* stats)
{
    assert(stats != NULL);

    stats->sseSum = 0.0;
    stats->ciSum = 0;
    stats->timeSum = 0.0;
    stats->successRate = 0.0; /* used as count; averaged later */
    stats->ciAvailable = true;
}

/**
 * @brief Adds one run's SSE, CI and time to the totals; a CI of CI_NA marks CI as not available.
 */
static void addRunStatistics(Statistics* stats, double sse, size_t ci, double ms)
{
    stats->sseSum += sse;
    stats->timeSum += ms;
    if (ci == CI_NA)
    {
        stats->ciAvailable = false;
        return;
    }
    stats->ciSum += ci;
    if (ci == 0) stats->successRate++;
}

/**
 * @brief Appends the summary row of one algorithm to `<outputDirectory>/<filename>.csv` (header when the file is
 * empty).
 *
 * Algorithm;Average CI;SSE;Relative CI;MS;Success Rate, averaged over `loopCount` runs (> 0); SSE divided by
 * `scaling` (> 0); CI, relative CI and success rate NA without ground truth. Fixed decimals through formatFixed
 * (decimal comma with --decimal-comma). Exits when the file cannot be opened; write errors are not checked.
 */
static void writeResultsToFile(const char* filename, Statistics stats, size_t numCentroids, const char* algorithm,
                               size_t loopCount, size_t scaling, const char* outputDirectory)
{
    char csvFileName[PATH_MAX];
    char outputFilePath[PATH_MAX];
    if (snprintf(csvFileName, sizeof(csvFileName), "%s.csv", filename) >= (int)sizeof(csvFileName) ||
        snprintf(outputFilePath, sizeof(outputFilePath), "%s%c%s", outputDirectory, PATHSEP, csvFileName) >=
            (int)sizeof(outputFilePath))
    {
        fprintf(stderr, "Buffer too small in writeResultsToFile\n");
        exit(EXIT_FAILURE);
    }

    FILE* file;
    if (FOPEN(file, outputFilePath, "a+") != 0)
    {
        handleFileError(outputFilePath);
    }

    /* Write header once when the file is empty */
    fseek(file, 0, SEEK_END);
    if (ftell(file) == 0)
    {
        fprintf(file, "Algorithm;Average CI;SSE;Relative CI;MS;Success Rate\n");
    }

    double avgCI = (double)stats.ciSum / loopCount;
    double sse = ((double)stats.sseSum / (double)loopCount);
    sse /= (double)scaling;
    double relCI = avgCI / (double)numCentroids;
    double avgTime = stats.timeSum / (double)loopCount;
    double succRate = (stats.successRate / (double)loopCount);

    char ciText[NUMBER_TEXT_SIZE];
    char sseText[NUMBER_TEXT_SIZE];
    char relText[NUMBER_TEXT_SIZE];
    char msText[NUMBER_TEXT_SIZE];
    char rateText[NUMBER_TEXT_SIZE];
    if (stats.ciAvailable)
    {
        fprintf(file, "%s;%s;%s;%s;%s;%s\n", algorithm, formatFixed(avgCI, 2, ciText), formatFixed(sse, 0, sseText),
                formatFixed(relCI, 2, relText), formatFixed(avgTime, 0, msText), formatFixed(succRate, 2, rateText));
    }
    else
    {
        fprintf(file, "%s;NA;%s;NA;%s;NA\n", algorithm, formatFixed(sse, 0, sseText), formatFixed(avgTime, 0, msText));
    }

    fclose(file);
}

/**
 * @brief Creates a new folder `outputs/<YYYY-MM-DD_HH-MM-SS>` (local time; `_2`, `_3`, ... when it exists) and writes
 * its path into `outputDirectory`.
 *
 * Creates `outputs` when missing. Exits on a path longer than `size` or a mkdir error.
 */
void createUniqueDirectory(char* outputDirectory, size_t size)
{
    time_t now = time(NULL);
    struct tm t;
    LOCALTIME(&t, &now);

    char datebuf[32];

    if (strftime(datebuf, sizeof datebuf, "%Y-%m-%d_%H-%M-%S", &t) == 0)
    {
        fprintf(stderr, "strftime failed\n");
        exit(EXIT_FAILURE);
    }

    if (MAKE_DIR("outputs") != 0 && errno != EEXIST)
    {
        perror("mkdir outputs");
        exit(EXIT_FAILURE);
    }

    // A second call in the same second gets <timestamp>_2, _3, ...: never two calls in one folder
    for (unsigned attempt = 1; attempt < 100; ++attempt)
    {
        const int length = attempt == 1 ? snprintf(outputDirectory, size, "outputs%c%s", PATHSEP, datebuf)
                                        : snprintf(outputDirectory, size, "outputs%c%s_%u", PATHSEP, datebuf, attempt);
        if (length < 0 || (size_t)length >= size)
        {
            fprintf(stderr, "Buffer too small in createUniqueDirectory\n");
            exit(EXIT_FAILURE);
        }

        if (MAKE_DIR(outputDirectory) == 0)
        {
            return;
        }
        if (errno != EEXIST)
        {
            perror("Error: Unable to create directory");
            exit(EXIT_FAILURE);
        }
    }
    fprintf(stderr, "Error: Unable to create a new folder under outputs for %s\n", datebuf);
    exit(EXIT_FAILURE);
}

/**
 * @brief Ensures a per-dataset subdirectory exists under a base path.
 *
 * Builds `<baseDirectory>/<datasetName>` and creates the directory if missing.
 * Treats pre-existing directories as success.
 *
 * @param baseDirectory Parent path under which the dataset folder is created.
 * @param datasetName Leaf folder name (dataset identifier).
 * @param datasetDirectory Output buffer receiving the combined path.
 * @param size Capacity of `datasetDirectory` in bytes.
 * @return void
 * @errors Exits on buffer overflow or mkdir error other than EEXIST.
 */
void createDatasetDirectory(const char* baseDirectory, const char* datasetName, char* datasetDirectory, size_t size)
{
    joinPath(datasetDirectory, size, baseDirectory, datasetName);

    if (MAKE_DIR(datasetDirectory) != 0 && errno != EEXIST)
    {
        perror("Error: Unable to create dataset directory");
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Prints the averages over `loopCount` runs (> 0) to stdout: CI, SSE divided by `scaling`, relative CI, time,
 * success rate.
 *
 * CI, relative CI and success rate print as NA without ground truth.
 */
static void printStatistics(const char* algorithmName, Statistics stats, size_t loopCount, size_t numCentroids,
                            size_t scaling)
{
    if (!stats.ciAvailable)
    {
        printf("(%s) Average CI: NA and SSE: %.0f\n", algorithmName, stats.sseSum / loopCount / scaling);
        printf("(%s) Relative CI: NA\n", algorithmName);
        printf("(%s) Average time taken: %.0f ms\n", algorithmName, stats.timeSum / loopCount);
        printf("(%s) Success rate: NA\n\n", algorithmName);
        return;
    }

    printf("(%s) Average CI: %.2f and SSE: %.0f\n", algorithmName, (double)stats.ciSum / loopCount,
           stats.sseSum / loopCount / scaling);
    printf("(%s) Relative CI: %.2f\n", algorithmName, (double)stats.ciSum / loopCount / numCentroids);
    printf("(%s) Average time taken: %.0f ms\n", algorithmName, stats.timeSum / loopCount);
    printf("(%s) Success rate: %.2f%%\n\n", algorithmName, stats.successRate / loopCount * 100);
}

/**
 * @brief K-means runs firstRun..loopCount-1 (0-based): initial centroids by `init`, k-means until the SSE stops
 * improving.
 *
 * Shared by the six algorithm wrappers: run i uses rng_derive_seed(seed, i); the stopwatch measures the run
 * (tracking paused); after each run one `_runs.csv` row and, for the first run with CI = 0 and with CI > 0,
 * the example snapshots; at the end the console statistics and the summary row (SSE divided by `scaling`).
 * `groundTruth` may be empty (CI is then NA); K must be 1..N (main checks it). Exits on allocation or file errors.
 * KM tracking: one row per k-means iteration (runKMeansWithTracking).
 */
void runKMeansAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations,
                        size_t firstRun, size_t loopCount, size_t scaling, const char* fileName,
                        const char* outputDirectory, const TrackOptions* track, InitMethod init, uint64_t seed)
{
    Statistics stats;
    initializeStatistics(&stats);

    Tracker tracker = trackerCreate(ALGO_KM, outputDirectory, groundTruth, track);
    double duration;
    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    printf("K-means\n");

    for (size_t i = firstRun; i < loopCount; ++i)
    {
        if (logLevel >= 2) printf("Round %zu\n", i + 1);

        const uint64_t runSeed = rng_derive_seed(seed, i);
        Rng runRng;
        rng_seed(&runRng, runSeed);
        Rng* rng = &runRng;

        resetPartitions(dataPoints);

        Centroids centroids = allocateCentroids(numCentroids, dataPoints->dimensions);

        trackerBeginRun(&tracker, i);

        initialCentroids(init, numCentroids, dataPoints, &centroids, rng);

        double resultSse = runKMeansWithTracking(dataPoints, maxIterations, &centroids, &tracker);

        duration = trackerEndRun(&tracker);

        size_t centroidIndex = centroidIndexOrNA(&centroids, groundTruth);

        addRunStatistics(&stats, resultSse, centroidIndex, duration);

        appendRunCsv(outputDirectory, "KM", i, runSeed, resultSse, centroidIndex, duration);

        saveExampleSnapshots(&centroids, dataPoints, centroidIndex, "KM", outputDirectory, &savedZeroResults,
                             &savedNonZeroResults);

        freeCentroids(&centroids);
    }

    trackerFree(&tracker);

    printStatistics("K-means", stats, loopCount - firstRun, numCentroids, scaling);
    writeResultsToFile(fileName, stats, numCentroids, "KM", loopCount - firstRun, scaling, outputDirectory);
}

/**
 * @brief Repeated k-means runs firstRun..loopCount-1 (see runKMeansAlgorithm for the shared run loop and outputs).
 *
 * Each run makes `maxRepeats` k-means from new initial centroids (by `init`) and keeps the lowest SSE. Capped
 * runs retain that repeat's partition (copied untimed); unlimited runs, and capped runs without a finite SSE,
 * relabel with the best centroids for the snapshots. Tracking: one row per repeat with the best SSE and CI so
 * far, snapshots at the improvements.
 */
void runRepeatedKMeansAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids,
                                size_t maxIterations, size_t maxRepeats, size_t firstRun, size_t loopCount,
                                size_t scaling, const char* fileName, const char* outputDirectory,
                                const TrackOptions* track, InitMethod init, uint64_t seed)
{
    Statistics stats;
    initializeStatistics(&stats);

    Tracker tracker = trackerCreate(ALGO_RKM, outputDirectory, groundTruth, track);
    double duration;

    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    printf("Repeated K-means\n");

    for (size_t i = firstRun; i < loopCount; ++i)
    {
        if (logLevel >= 2) printf("Round %zu\n", i + 1);

        const uint64_t runSeed = rng_derive_seed(seed, i);
        Rng runRng;
        rng_seed(&runRng, runSeed);
        Rng* rng = &runRng;

        double bestSse = DBL_MAX;
        size_t bestCI = CI_NA;

        Centroids bestCentroids = allocateCentroids(numCentroids, dataPoints->dimensions);
        size_t* bestPartitions = NULL;
        if (maxIterations != SIZE_MAX)
        {
            bestPartitions = malloc(dataPoints->size * sizeof(*bestPartitions));
            handleMemoryError(bestPartitions);
        }

        trackerBeginRun(&tracker, i);

        for (size_t j = 0; j < maxRepeats; ++j)
        {
            if (logLevel >= 3) printf("Repeat %zu\n", j + 1);
            Centroids centroids = allocateCentroids(numCentroids, dataPoints->dimensions);
            initialCentroids(init, numCentroids, dataPoints, &centroids, rng);
            double resultSse = runKMeans(dataPoints, maxIterations, &centroids, &tracker, KM_STAGE_NONE);

            if (resultSse < bestSse)
            {
                bestSse = resultSse;
                deepCopyCentroids(&centroids, &bestCentroids);
                if (bestPartitions != NULL)
                {
                    // Only the snapshot needs the labels; unlimited runs relabel after the timed run instead
                    trackerPause(&tracker);
                    memcpy(bestPartitions, dataPoints->partitions, dataPoints->size * sizeof(*bestPartitions));
                    trackerResume(&tracker);
                }

                // CI of the new best solution, only for the rows (stopwatch paused)
                if (tracker.logRows) bestCI = trackerCi(&tracker, &centroids);
                trackerSnapshot(&tracker, j + 1, "step", dataPoints, &centroids, SIZE_MAX);
            }

            // One row per repeat (count = repeat): the best solution so far
            trackerAddRow(&tracker, j + 1, bestSse, bestCI);

            freeCentroids(&centroids);
        }

        duration = trackerEndRun(&tracker);

        // bestPartitions holds labels only after an improvement, that is, when bestSse is below DBL_MAX
        if (bestPartitions != NULL && bestSse < DBL_MAX)
        {
            memcpy(dataPoints->partitions, bestPartitions, dataPoints->size * sizeof(*bestPartitions));
        }
        else
        {
            // The labels are from the last repeat; the snapshot needs those of the best centroids
            partitionStep(dataPoints, &bestCentroids);
        }
        free(bestPartitions);

        size_t centroidIndex = centroidIndexOrNA(&bestCentroids, groundTruth);

        addRunStatistics(&stats, bestSse, centroidIndex, duration);

        appendRunCsv(outputDirectory, "RKM", i, runSeed, bestSse, centroidIndex, duration);

        saveExampleSnapshots(&bestCentroids, dataPoints, centroidIndex, "RKM", outputDirectory, &savedZeroResults,
                             &savedNonZeroResults);

        freeCentroids(&bestCentroids);
    }

    trackerFree(&tracker);

    printStatistics("Repeated K-means", stats, loopCount - firstRun, numCentroids, scaling);
    writeResultsToFile(fileName, stats, numCentroids, "RKM", loopCount - firstRun, scaling, outputDirectory);
}

/**
 * @brief Random Swap runs firstRun..loopCount-1 (see runKMeansAlgorithm for the shared run loop and outputs).
 *
 * Each run starts from initial centroids by `init` and their optimal partition and makes `maxSwaps` trial swaps
 * (randomSwap). Tracking: the initial solution and every improvement, with its swap number.
 */
void runRandomSwapAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxSwaps,
                            size_t firstRun, size_t loopCount, size_t scaling, const char* fileName,
                            const char* outputDirectory, const TrackOptions* track, InitMethod init, uint64_t seed)
{
    Statistics stats;
    initializeStatistics(&stats);

    Tracker tracker = trackerCreate(ALGO_RS, outputDirectory, groundTruth, track);
    double duration;

    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    printf("Random swap\n");

    for (size_t i = firstRun; i < loopCount; ++i)
    {
        if (logLevel >= 2) printf("Round %zu\n", i + 1);

        const uint64_t runSeed = rng_derive_seed(seed, i);
        Rng runRng;
        rng_seed(&runRng, runSeed);
        Rng* rng = &runRng;

        Centroids centroids = allocateCentroids(numCentroids, dataPoints->dimensions);

        trackerBeginRun(&tracker, i);

        initialCentroids(init, numCentroids, dataPoints, &centroids, rng);
        partitionStep(dataPoints, &centroids); // Local repartition requires this

        double resultSse = randomSwap(dataPoints, &centroids, maxSwaps, &tracker, rng);

        duration = trackerEndRun(&tracker);

        size_t centroidIndex = centroidIndexOrNA(&centroids, groundTruth);

        addRunStatistics(&stats, resultSse, centroidIndex, duration);

        appendRunCsv(outputDirectory, "RS", i, runSeed, resultSse, centroidIndex, duration);

        saveExampleSnapshots(&centroids, dataPoints, centroidIndex, "RS", outputDirectory, &savedZeroResults,
                             &savedNonZeroResults);

        freeCentroids(&centroids);
    }

    trackerFree(&tracker);

    printStatistics("Random Swap", stats, loopCount - firstRun, numCentroids, scaling);
    writeResultsToFile(fileName, stats, numCentroids, "RS", loopCount - firstRun, scaling, outputDirectory);
}

/**
 * @brief Random Split runs firstRun..loopCount-1 (see runKMeansAlgorithm for the shared run loop and outputs).
 *
 * Each run starts from one random data point and calls runRandomSplit (a random cluster with at least 2 points
 * is split by local 2-means until K, then a final k-means).
 */
void runRandomSplitAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids,
                             size_t maxIterations, size_t firstRun, size_t loopCount, size_t scaling,
                             const char* fileName, const char* outputDirectory, const TrackOptions* track,
                             uint64_t seed)
{
    Statistics stats;
    initializeStatistics(&stats);

    Tracker tracker = trackerCreate(ALGO_SKM_RANDOM, outputDirectory, groundTruth, track);
    double duration;

    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    printf("Random Split k-means\n");

    for (size_t i = firstRun; i < loopCount; ++i)
    {
        if (logLevel >= 2) printf("Round %zu\n", i + 1);

        const uint64_t runSeed = rng_derive_seed(seed, i);
        Rng runRng;
        rng_seed(&runRng, runSeed);
        Rng* rng = &runRng;

        resetPartitions(dataPoints);

        Centroids centroids = allocateCentroids(numCentroids, dataPoints->dimensions);
        centroids.size = 1;

        trackerBeginRun(&tracker, i);

        generateRandomCentroids(centroids.size, dataPoints, &centroids, rng);

        double resultSse = runRandomSplit(dataPoints, &centroids, numCentroids, maxIterations, &tracker, rng);

        duration = trackerEndRun(&tracker);

        size_t centroidIndex = centroidIndexOrNA(&centroids, groundTruth);

        addRunStatistics(&stats, resultSse, centroidIndex, duration);

        appendRunCsv(outputDirectory, "SKM_Random", i, runSeed, resultSse, centroidIndex, duration);

        saveExampleSnapshots(&centroids, dataPoints, centroidIndex, "SKM_Random", outputDirectory, &savedZeroResults,
                             &savedNonZeroResults);

        freeCentroids(&centroids);
    }

    trackerFree(&tracker);

    printStatistics("Random Split", stats, loopCount - firstRun, numCentroids, scaling);
    writeResultsToFile(fileName, stats, numCentroids, "SKM_Random", loopCount - firstRun, scaling, outputDirectory);
}

/**
 * @brief SSE-based split k-means runs firstRun..loopCount-1 (see runKMeansAlgorithm for the shared run loop and
 * outputs).
 *
 * Each run starts from one random data point and calls runSseSplit with `splitType` (SKM-Intra, SKM-Global,
 * SKM-Global-Basic or SKM-Local).
 */
void runSseSplitAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids,
                          size_t maxIterations, size_t firstRun, size_t loopCount, size_t scaling, const char* fileName,
                          const char* outputDirectory, Algorithm splitType, const TrackOptions* track, uint64_t seed)
{
    Statistics stats;
    initializeStatistics(&stats);

    Tracker tracker = trackerCreate(splitType, outputDirectory, groundTruth, track);
    double duration;

    const char* splitTypeName = getAlgorithmName(splitType);

    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    printf("%s\n", splitTypeName);

    for (size_t i = firstRun; i < loopCount; ++i)
    {
        if (logLevel >= 2) printf("Round %zu\n", i + 1);

        const uint64_t runSeed = rng_derive_seed(seed, i);
        Rng runRng;
        rng_seed(&runRng, runSeed);
        Rng* rng = &runRng;

        resetPartitions(dataPoints);

        Centroids centroids = allocateCentroids(numCentroids, dataPoints->dimensions);
        centroids.size = 1;

        trackerBeginRun(&tracker, i);

        generateRandomCentroids(centroids.size, dataPoints, &centroids, rng);

        double resultSse = runSseSplit(dataPoints, &centroids, numCentroids, maxIterations, splitType, &tracker, rng);

        duration = trackerEndRun(&tracker);

        size_t centroidIndex = centroidIndexOrNA(&centroids, groundTruth);

        addRunStatistics(&stats, resultSse, centroidIndex, duration);

        appendRunCsv(outputDirectory, splitTypeName, i, runSeed, resultSse, centroidIndex, duration);

        saveExampleSnapshots(&centroids, dataPoints, centroidIndex, splitTypeName, outputDirectory, &savedZeroResults,
                             &savedNonZeroResults);

        freeCentroids(&centroids);
    }

    trackerFree(&tracker);

    printStatistics(splitTypeName, stats, loopCount - firstRun, numCentroids, scaling);
    writeResultsToFile(fileName, stats, numCentroids, splitTypeName, loopCount - firstRun, scaling, outputDirectory);
}

/**
 * @brief Bisecting k-means runs firstRun..loopCount-1 (see runKMeansAlgorithm for the shared run loop and outputs).
 *
 * Each run starts from one random data point and calls runBisectingKMeans with `bisectingIterations` tries per split.
 */
void runBisectingKMeansAlgorithm(Dataset* dataPoints, const Centroids* groundTruth, size_t numCentroids,
                                 size_t maxIterations, size_t firstRun, size_t loopCount, size_t scaling,
                                 const char* fileName, const char* outputDirectory, const TrackOptions* track,
                                 size_t bisectingIterations, uint64_t seed)
{
    Statistics stats;
    initializeStatistics(&stats);

    Tracker tracker = trackerCreate(ALGO_BISECTING, outputDirectory, groundTruth, track);
    double duration;

    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    printf("Bisecting k-means\n");

    for (size_t i = firstRun; i < loopCount; ++i)
    {
        if (logLevel >= 2) printf("Round %zu\n", i + 1);

        const uint64_t runSeed = rng_derive_seed(seed, i);
        Rng runRng;
        rng_seed(&runRng, runSeed);
        Rng* rng = &runRng;

        resetPartitions(dataPoints);

        Centroids centroids = allocateCentroids(numCentroids, dataPoints->dimensions);
        centroids.size = 1;

        trackerBeginRun(&tracker, i);

        generateRandomCentroids(centroids.size, dataPoints, &centroids, rng);

        double resultSse =
            runBisectingKMeans(dataPoints, &centroids, numCentroids, maxIterations, bisectingIterations, &tracker, rng);

        duration = trackerEndRun(&tracker);

        size_t centroidIndex = centroidIndexOrNA(&centroids, groundTruth);

        addRunStatistics(&stats, resultSse, centroidIndex, duration);

        appendRunCsv(outputDirectory, "Bisecting_KM", i, runSeed, resultSse, centroidIndex, duration);

        saveExampleSnapshots(&centroids, dataPoints, centroidIndex, "Bisecting_KM", outputDirectory, &savedZeroResults,
                             &savedNonZeroResults);

        freeCentroids(&centroids);
    }

    trackerFree(&tracker);

    printStatistics("Bisecting", stats, loopCount - firstRun, numCentroids, scaling);
    writeResultsToFile(fileName, stats, numCentroids, "Bisecting_KM", loopCount - firstRun, scaling, outputDirectory);
}

/**
 * @brief Writes the shared part of run_config.txt into `text`: what must agree for every call that writes to one
 * folder.
 *
 * Version, commit, compiler and flags, timer, machine, CPU, master seed, log level and the algorithm parameters.
 */
static void runConfigHeader(char* text, size_t size, const Options* opt)
{
    char machine[512];
    char cpu[256];
    char iterations[32];
    platform_machine(machine, sizeof(machine));
    platform_cpu(cpu, sizeof(cpu));
    if (opt->maxIterations == SIZE_MAX)
    {
        snprintf(iterations, sizeof(iterations), "until the SSE stops improving");
    }
    else
    {
        snprintf(iterations, sizeof(iterations), "%zu", opt->maxIterations);
    }

#ifdef NDEBUG
    const char* asserts = "off (NDEBUG)";
#else
    const char* asserts = "on";
#endif

    const int length =
        snprintf(text, size,
                 "# Split_kMeans run configuration. Every call that writes to this folder must match the lines above "
                 "\"calls:\".\n"
                 "version: %s\n"
                 "commit: %s\n"
                 "compiler: %s\n"
                 "flags: %s\n"
                 "asserts: %s\n"
                 "timer: %s\n"
                 "machine: %s\n"
                 "cpu: %s\n"
                 "seed: %" PRIu64 "\n"
                 "log-level: %d\n"
                 "max-iterations: %s\n"
                 "max-swaps: %zu\n"
                 "repeats: %zu\n"
                 "bisect-tries: %zu\n"
                 "init: %s\n"
                 "decimal-comma: %s\n"
                 "calls:\n",
                 SKM_VERSION, SKM_GIT_COMMIT, platform_compiler(), SKM_BUILD_FLAGS, asserts, platform_clock_name(),
                 machine, cpu, opt->seed, logLevel, iterations, opt->maxSwaps, opt->maxRepeats, opt->bisectTries,
                 opt->init == INIT_KMEANSPP ? "kmeans++" : "random", decimalComma ? "on" : "off");
    if (length < 0 || (size_t)length >= size)
    {
        fprintf(stderr, "Error: run_config.txt header too long\n");
        exit(EXIT_FAILURE);
    }
}

/** @brief Copies line `index` (0-based) of `text` into `line`, or "(none)". */
void textLine(const char* text, size_t index, char* line, size_t size)
{
    for (size_t i = 0; i < index && text != NULL; ++i)
    {
        text = strchr(text, '\n');
        if (text != NULL) ++text;
    }
    if (text == NULL || *text == '\0')
    {
        snprintf(line, size, "(none)");
        return;
    }
    const size_t length = strcspn(text, "\n");
    snprintf(line, size, "%.*s", (int)(length < size ? length : size - 1), text);
}

/**
 * @brief Creates or extends `<outputDirectory>/run_config.txt`; exits when the folder holds results of another
 * configuration.
 *
 * The shared header (runConfigHeader) must equal the one in the file; then one entry per call is appended:
 * the time, the command line (the program by file name, without its folder) and the resolved options
 * (mode, algorithms, runs, tracking).
 */
void writeRunConfig(const char* outputDirectory, const Options* opt, const TrackOptions* track,
                    const char* snapshotsText, const char* datasetsText, bool cliMode, int argc, char* argv[])
{
    char header[RUN_CONFIG_SIZE];
    runConfigHeader(header, sizeof(header), opt);

    char path[PATH_MAX];
    joinPath(path, sizeof(path), outputDirectory, "run_config.txt");

    const bool exists = fileExists(path);
    if (exists)
    {
        FILE* file;
        if (FOPEN(file, path, "r") != 0)
        {
            handleFileError(path);
        }
        char existing[RUN_CONFIG_SIZE];
        const size_t length = fread(existing, 1, sizeof(existing) - 1, file);
        existing[length] = '\0';
        fclose(file);

        const size_t headerLength = strlen(header);
        if (length < headerLength || memcmp(existing, header, headerLength) != 0)
        {
            size_t line = 0;
            char ours[1024];
            char theirs[1024];
            do
            {
                textLine(header, line, ours, sizeof(ours));
                textLine(existing, line, theirs, sizeof(theirs));
                ++line;
            } while (strcmp(ours, theirs) == 0 && strcmp(ours, "(none)") != 0);
            fprintf(stderr, "Error: %s belongs to another configuration; write to a new -o folder\n", path);
            fprintf(stderr, "  this call:   %s\n  that folder: %s\n", ours, theirs);
            exit(EXIT_FAILURE);
        }
    }

    FILE* file;
    if (FOPEN(file, path, exists ? "a" : "w") != 0)
    {
        handleFileError(path);
    }
    if (!exists)
    {
        fputs(header, file);
    }

    time_t now = time(NULL);
    struct tm local;
    LOCALTIME(&local, &now);
    char stamp[32];
    if (strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local) == 0)
    {
        snprintf(stamp, sizeof(stamp), "unknown time");
    }
    fprintf(file, "%s | ", stamp);
    // The program by file name only: argv[0] can be an absolute path with the user name (PowerShell passes one)
    const char* program = argc > 0 && argv[0] != NULL ? argv[0] : "split_kmeans";
    for (const char* c = program; *c != '\0'; ++c)
    {
        if (*c == '/' || *c == '\\')
        {
            program = c + 1;
        }
    }
    fputs(program, file);
    for (int i = 1; i < argc; ++i)
    {
        fprintf(file, " %s", argv[i]);
    }
    fprintf(file, "\n  mode: %s; algorithms:", cliMode ? "cli" : "batch");
    for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
    {
        if (opt->algorithms[a])
        {
            fprintf(file, " %s", algorithmOptions[a].name);
        }
    }
    fprintf(
        file,
        "; runs: %zu to %zu (RKM and RS: to %zu); datasets: %s; track-progress: %s; snapshots: %s; track-inner: %s\n",
        opt->firstRun, opt->runs, opt->runsGiven ? opt->runs : (size_t)1, datasetsText, track->rows ? "on" : "off",
        snapshotsText, track->inner ? "on" : "off");
    fclose(file);
}

/**
 * @brief Writes `<datasetDirectory>/dataset.txt` (data and ground-truth files, K, N, D; `report` reads K from it).
 *
 * Exits when the folder already describes a dataset with another K, N or D (no mixing of datasets in one folder).
 */
void writeDatasetInfo(const char* datasetDirectory, const char* dataFile, const char* gtFile, size_t k, size_t n,
                      size_t d)
{
    char path[PATH_MAX];
    joinPath(path, sizeof(path), datasetDirectory, "dataset.txt");

    char shape[128];
    snprintf(shape, sizeof(shape), "k: %zu\nn: %zu\nd: %zu\n", k, n, d);

    if (fileExists(path))
    {
        FILE* file;
        if (FOPEN(file, path, "r") != 0)
        {
            handleFileError(path);
        }
        char existing[2 * PATH_MAX + 256];
        const size_t length = fread(existing, 1, sizeof(existing) - 1, file);
        existing[length] = '\0';
        fclose(file);
        if (strstr(existing, shape) == NULL)
        {
            fprintf(
                stderr,
                "Error: %s describes another dataset or K (this call: k %zu, n %zu, d %zu); write to a new -o folder\n",
                path, k, n, d);
            exit(EXIT_FAILURE);
        }
        return;
    }

    FILE* file;
    if (FOPEN(file, path, "w") != 0)
    {
        handleFileError(path);
    }
    fprintf(file, "data: %s\nground-truth: %s\n%s", dataFile, gtFile[0] != '\0' ? gtFile : "none", shape);
    fclose(file);
}
