/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

// Introduction
/**
 * Project: Split K-Means (version: SKM_VERSION in version.h; v1.0.0 released 26-10-2025)
 * Author: Niko Ruohonen
 *
 * DESCRIPTION:
 * k-means, repeated k-means, Random Swap and split-based k-means variants for the
 * thesis comparison (SSE and Centroid Index, success rate, time).
 *
 * ALGORITHMS (--algo):
 * - km, rkm, rs
 * - skm-intra, skm-global, skm-global-basic, skm-local (SSE-based split k-means)
 * - skm-random (random split), bisecting
 *
 * USAGE (all options: -h, README.md):
 * 1. Batch mode (no data file): every *.txt in data/, gt/ and centroids/ (K, one
 *    integer per file), paired by sorted file name (ASCII case-insensitive, the same
 *    on every OS); --datasets selects a subset.
 * 2. CLI mode: split_kmeans [options] -k <K> <data.txt> [gt.txt]; without gt.txt,
 *    CI and success rate are NA.
 * 3. Subcommands: ci, sse, gt-from-partition (score finished results); report (tables
 *    and curves from finished run folders).
 *
 * OUTPUT (outputs/<timestamp>/ or -o <dir>):
 *    run_config.txt                      # build, machine, seed, parameters; one entry per call
 *    <dataset>/
 *       ├─ dataset.txt                   # input paths and SHA-256, K, N, D (read by report)
 *       ├─ <dataset>.csv                 # summary row per algorithm (Excel, --decimal-comma)
 *       ├─ <Algorithm>_runs.csv          # one row per run: run;seed;sse;ci;ms (%.17g)
 *       ├─ <Algorithm>_log.csv           # tracked states: run;count;sse;ci;ms
 *       ├─ <Algorithm>_inner.csv         # --track-inner
 *       ├─ <Algorithm>_iteration_stats.txt and <Algorithm>_run<R>_iter<I>_<phase>_*.txt  # --snapshots
 *       └─ <Algorithm>_{centroids,partitions}_{perfect,failed}.txt  # first run with CI = 0 and CI > 0
 *
 * REQUIREMENTS:
 * - Input files: space/tab-separated finite numbers with a decimal point, the same
 *   column count on every row; the program never changes the locale
 * - K files: a single positive integer
 * - Ground truth: the same dimensionality as the data
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"
#include "cli.h"
#include "algorithms.h"
#include "dataset.h"
#include "experiment.h"
#include "input_identity.h"
#include "kmeans.h"
#include "metrics.h"
#include "report.h"
#include "tracking.h"
#include "util.h"
#include "version.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief Builds ground-truth centroids from data and a partition file (the gt-from-partition subcommand).
 *
 * Reads points (uniform dimensionality), then one 1-based cluster label per point (separated by spaces,
 * tabs or line breaks), and writes the cluster means (`%f` with a trailing space). K is the
 * largest label; an empty cluster keeps a zero centroid with its 0-based index in the stderr warning.
 *
 * @param dataFileName Path to data points (space/tab-separated doubles, one point per line).
 * @param partitionFileName Path to labels (one integer 1..N per data point, separated by spaces, tabs or line breaks).
 * @param outputFileName Path to write centroids (space-separated doubles per row).
 * @return 0 on success; 1 for empty data, a partition/output open failure, an invalid label or a label-count mismatch.
 * @errors File read, write and close failures, overlong lines and allocation failures exit.
 */
int generateGroundTruthCentroids(const char* dataFileName, const char* partitionFileName, const char* outputFileName)
{
    // Read the data points
    Dataset dataPoints = readDataset(dataFileName);

    if (dataPoints.size == 0)
    {
        fprintf(stderr, "Error: No data points read from file %s\n", dataFileName);
        return 1;
    }

    printf("Read %zu data points from %s\n", dataPoints.size, dataFileName);

    // Read the partition labels: integers 1..N, exactly one per data point
    FILE* partitionFile;

    if (FOPEN(partitionFile, partitionFileName, "r") != 0)
    {
        fprintf(stderr, "Error: Unable to open partition file '%s'\n", partitionFileName);
        freeDataset(&dataPoints);
        return 1;
    }

    size_t maxPartitionIndex = 0;
    size_t labelCount = 0;
    bool valid = true;
    char line[LINE_BUFSZ];
    for (size_t lineNumber = 1;
         valid && readDataLine(line, sizeof(line), partitionFile, partitionFileName, lineNumber) != NULL; ++lineNumber)
    {
        char* context = NULL;
        for (char* token = STRTOK(line, " \t\r\n", &context); token != NULL; token = STRTOK(NULL, " \t\r\n", &context))
        {
            uint64_t label = 0;
            if (!parseUint64(token, &label) || label < 1 || label > dataPoints.size)
            {
                fprintf(stderr, "Error: '%s' line %zu: '%s' is not a cluster label from 1 to %zu\n", partitionFileName,
                        lineNumber, token, dataPoints.size);
                valid = false;
                break;
            }
            if (labelCount == dataPoints.size)
            {
                fprintf(stderr, "Error: '%s' has more labels than the %zu data points of '%s'\n", partitionFileName,
                        dataPoints.size, dataFileName);
                valid = false;
                break;
            }
            const size_t clusterIndex = (size_t)(label - 1);
            dataPoints.partitions[labelCount++] = clusterIndex;
            if (clusterIndex > maxPartitionIndex)
            {
                maxPartitionIndex = clusterIndex;
            }
        }
    }
    closeInputFile(partitionFile, partitionFileName);

    if (valid && labelCount != dataPoints.size)
    {
        fprintf(stderr, "Error: '%s' has %zu labels for the %zu data points of '%s'\n", partitionFileName, labelCount,
                dataPoints.size, dataFileName);
        valid = false;
    }
    if (!valid)
    {
        freeDataset(&dataPoints);
        return 1;
    }

    printf("Read %zu labels, clusters 1 to %zu\n", labelCount, maxPartitionIndex + 1);

    // The number of centroids is maxPartitionIndex + 1 (assuming 0-based indexing)
    size_t numCentroids = maxPartitionIndex + 1;
    printf("Will calculate %zu centroids\n", numCentroids);

    // Allocate memory for the centroids
    Centroids centroids = allocateCentroids(numCentroids, dataPoints.dimensions);

    // Initialize count array to track how many points are in each cluster
    size_t* clusterCounts = calloc(numCentroids, sizeof(size_t));
    handleMemoryError(clusterCounts);

    // Sum up the attribute values for each cluster
    for (size_t i = 0; i < dataPoints.size; ++i)
    {
        size_t clusterIndex = dataPoints.partitions[i];

        // Every label was checked against 1..N and K is the largest one
        assert(clusterIndex < numCentroids);

        // Add the attribute values to the centroid sum
        for (size_t j = 0; j < dataPoints.dimensions; ++j)
        {
            centroidRow(&centroids, clusterIndex)[j] += datasetRowConst(&dataPoints, i)[j];
        }

        clusterCounts[clusterIndex]++;
    }

    // Calculate the mean values (centroids)
    for (size_t i = 0; i < numCentroids; ++i)
    {
        if (clusterCounts[i] > 0)
        {
            for (size_t j = 0; j < centroids.dimensions; ++j)
            {
                centroidRow(&centroids, i)[j] /= clusterCounts[i];
            }
            printf("Cluster %zu has %zu points\n", i, clusterCounts[i]);
        }
        else
        {
            fprintf(stderr, "Warning: Cluster %zu has no points assigned to it\n", i);
        }
    }

    // Write the centroids to the output file
    FILE* outputFile;
    if (FOPEN(outputFile, outputFileName, "w") != 0)
    {
        fprintf(stderr, "Error: Unable to open output file '%s'\n", outputFileName);
        free(clusterCounts);
        freeCentroids(&centroids);
        freeDataset(&dataPoints);
        return 1;
    }

    for (size_t i = 0; i < numCentroids; ++i)
    {
        for (size_t j = 0; j < centroids.dimensions; ++j)
        {
            checkFileWrite(outputFile, outputFileName, fprintf(outputFile, "%f ", centroidRow(&centroids, i)[j]));
        }
        checkFileWrite(outputFile, outputFileName, fprintf(outputFile, "\n"));
    }

    closeOutputFile(outputFile, outputFileName);

    printf("Successfully wrote %zu centroids to %s\n", numCentroids, outputFileName);

    // Clean up
    free(clusterCounts);
    freeCentroids(&centroids);
    freeDataset(&dataPoints);

    return 0;
}

/**
 * @brief Prints the Centroid Index between a centroid file and a ground-truth file, with the file names and sizes.
 *
 * Used by runDebuggery. Does not check that the files are non-empty or have the same dimensionality;
 * the ci subcommand checks both.
 */
void debugCalculateCI(const char* debugCentroidsFile, const char* groundTruthFile)
{
    printf("Debugging CI calculation between:\n");
    printf("  Debug file: %s\n", debugCentroidsFile);
    printf("  Ground truth: %s\n", groundTruthFile);

    // Read centroids from both files
    Centroids debugCentroids = readCentroids(debugCentroidsFile);
    Centroids groundTruth = readCentroids(groundTruthFile);

    printf("Debug centroids: %zu with %zu dimensions\n", debugCentroids.size, debugCentroids.dimensions);
    printf("Ground truth centroids: %zu with %zu dimensions\n", groundTruth.size, groundTruth.dimensions);

    // Calculate Centroid Index
    size_t ci = calculateCentroidIndex(&debugCentroids, &groundTruth);
    printf("Centroid Index (CI): %zu\n\n", ci);

    // Clean up
    freeCentroids(&debugCentroids);
    freeCentroids(&groundTruth);
}

/**
 * @brief Prints the SSE of a data file with every point at its nearest centroid of a centroid file.
 *
 * Used by runDebuggery. Does not check that the files are non-empty or have the same dimensionality;
 * the sse subcommand checks both.
 */
void debugCalculateSSE(const char* debugCentroidsFile, const char* dataFile)
{
    printf("Debugging SSE calculation:\n");
    printf("  Centroids file: %s\n", debugCentroidsFile);
    printf("  Data file: %s\n", dataFile);

    // Read centroids and data points
    Centroids centroids = readCentroids(debugCentroidsFile);
    Dataset dataPoints = readDataset(dataFile);

    printf("Centroids: %zu with %zu dimensions\n", centroids.size, centroids.dimensions);
    printf("Data points: %zu with %zu dimensions\n", dataPoints.size, dataPoints.dimensions);

    // Assign each data point to the nearest centroid
    partitionStep(&dataPoints, &centroids);

    // Calculate SSE
    double sse = calculateSSE(&dataPoints, &centroids);
    printf("Sum of Squared Errors (SSE): %.2f\n\n", sse);

    // Clean up
    freeCentroids(&centroids);
    freeDataset(&dataPoints);
}

/**
 * @brief Runs a quick CI and SSE sanity check with fixed test files.
 */
void runDebuggery(void)
{
    debugCalculateCI("debuggery/output_worms_64d.txt", "gt/worms_64d-gt.txt");
    debugCalculateSSE("debuggery/output_worms_64d.txt", "data/worms_64d.txt");
}

/** @brief Exits unless both point sets are non-empty and have the same number of dimensions. */
static void requireCompatible(size_t firstCount, size_t firstDimensions, const char* firstFile, size_t secondCount,
                              size_t secondDimensions, const char* secondFile)
{
    if (firstCount == 0 || secondCount == 0)
    {
        fprintf(stderr, "Error: %s has no rows\n", firstCount == 0 ? firstFile : secondFile);
        exit(EXIT_FAILURE);
    }
    if (firstDimensions != secondDimensions)
    {
        fprintf(stderr, "Error: %s has %zu dimensions, %s has %zu\n", firstFile, firstDimensions, secondFile,
                secondDimensions);
        exit(EXIT_FAILURE);
    }
}

/** @brief `ci <centroids.txt> <ground-truth.txt>`: prints the Centroid Index of the centroids against the ground truth.
 */
static int commandCi(int argc, char* argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: split_kmeans ci <centroids.txt> <ground-truth.txt>\n");
        return EXIT_FAILURE;
    }
    Centroids centroids = readCentroids(argv[0]);
    Centroids truth = readCentroids(argv[1]);
    requireCompatible(centroids.size, centroids.dimensions, argv[0], truth.size, truth.dimensions, argv[1]);

    printf("%zu\n", calculateCentroidIndex(&centroids, &truth));

    freeCentroids(&centroids);
    freeCentroids(&truth);
    return EXIT_SUCCESS;
}

/** @brief Prints nearest-centroid SSE (%.17g), or fails if the score is non-finite. */
static int commandSse(int argc, char* argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: split_kmeans sse <data.txt> <centroids.txt>\n");
        return EXIT_FAILURE;
    }
    Dataset dataPoints = readDataset(argv[0]);
    Centroids centroids = readCentroids(argv[1]);
    requireCompatible(dataPoints.size, dataPoints.dimensions, argv[0], centroids.size, centroids.dimensions, argv[1]);

    partitionStep(&dataPoints, &centroids);
    const double sse = calculateSSE(&dataPoints, &centroids);
    int status = EXIT_SUCCESS;
    if (!isfinite(sse))
    {
        fprintf(stderr, "Error: sse produced a non-finite SSE\n");
        status = EXIT_FAILURE;
    }
    else
    {
        printf("%.17g\n", sse);
    }

    freeDataset(&dataPoints);
    freeCentroids(&centroids);
    return status;
}

/** @brief `gt-from-partition <data.txt> <partition.txt> <out.txt>`: writes means from labels 1..N separated by
 * spaces, tabs or line breaks. */
static int commandGtFromPartition(int argc, char* argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: split_kmeans gt-from-partition <data.txt> <partition.txt> <out.txt>\n");
        return EXIT_FAILURE;
    }
    return generateGroundTruthCentroids(argv[0], argv[1], argv[2]) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/** @brief True when the algorithm named `name` (an --algo name) is selected. */
static bool algorithmSelected(const Options* opt, const char* name)
{
    for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
    {
        if (strcmp(algorithmOptions[a].name, name) == 0)
        {
            return opt->algorithms[a];
        }
    }
    return false;
}

/** @brief Parses a comma-separated `--algo` list; false on an empty list or an unknown name. */
static bool parseAlgorithmList(const char* list, bool selected[ALGORITHM_COUNT])
{
    char buffer[256];
    if (strlen(list) >= sizeof(buffer))
    {
        return false;
    }
    STRCPY(buffer, sizeof(buffer), list);

    for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
    {
        selected[a] = false;
    }

    bool any = false;
    char* context = NULL;
    for (char* token = STRTOK(buffer, ",", &context); token != NULL; token = STRTOK(NULL, ",", &context))
    {
        size_t a = 0;
        while (a < ALGORITHM_COUNT && strcmp(token, algorithmOptions[a].name) != 0)
        {
            ++a;
        }
        if (a == ALGORITHM_COUNT)
        {
            return false;
        }
        selected[a] = true;
        any = true;
    }
    return any;
}

/** @brief Parses `all` or a comma-separated list of numbers and ranges such as `1,3-5` (no empty items); false on
 * anything else. */
static bool parseSelection(const char* text, Selection* selection)
{
    selection->all = false;
    selection->rangeCount = 0;
    if (strcmp(text, "all") == 0)
    {
        selection->all = true;
        return true;
    }

    char buffer[256];
    if (text[0] == '\0' || strlen(text) >= sizeof(buffer))
    {
        return false;
    }
    STRCPY(buffer, sizeof(buffer), text);

    char* item = buffer;
    while (item != NULL)
    {
        char* next = strchr(item, ',');
        if (next != NULL)
        {
            *next++ = '\0';
        }
        if (selection->rangeCount == MAX_RANGES)
        {
            return false;
        }

        uint64_t low = 0;
        uint64_t high = 0;
        char* dash = strchr(item, '-');
        if (dash != NULL)
        {
            *dash = '\0';
            if (!parseUint64(item, &low) || !parseUint64(dash + 1, &high) || high < low)
            {
                return false;
            }
        }
        else if (!parseUint64(item, &low))
        {
            return false;
        }
        else
        {
            high = low;
        }
        if ((uint64_t)(size_t)high != high)
        {
            return false;
        }

        selection->low[selection->rangeCount] = (size_t)low;
        selection->high[selection->rangeCount] = (size_t)high;
        selection->rangeCount++;
        item = next;
    }
    return true;
}

/**
 * @brief Parses --snapshots: `none`, or RUNS[:ITERATIONS][:split].
 *
 * RUNS (1-based) and ITERATIONS (the counts of `_log.csv`) are `all` or lists such as `1,3-5`; ITERATIONS defaults
 * to all; `split` adds the state after each two-way split. False on anything else.
 */
static bool parseSnapshotSpec(const char* text, SnapshotSpec* spec)
{
    spec->enabled = false;
    spec->split = false;
    spec->runs.all = true;
    spec->runs.rangeCount = 0;
    spec->counts.all = true;
    spec->counts.rangeCount = 0;
    if (strcmp(text, "none") == 0)
    {
        return true;
    }

    char buffer[256];
    if (strlen(text) >= sizeof(buffer))
    {
        return false;
    }
    STRCPY(buffer, sizeof(buffer), text);

    char* parts[4] = {NULL, NULL, NULL, NULL};
    size_t partCount = 0;
    for (char* part = buffer; part != NULL && partCount < 4; ++partCount)
    {
        parts[partCount] = part;
        char* next = strchr(part, ':');
        if (next != NULL)
        {
            *next++ = '\0';
        }
        part = next;
    }
    if (partCount > 3 || !parseSelection(parts[0], &spec->runs))
    {
        return false;
    }
    for (size_t r = 0; r < spec->runs.rangeCount; ++r)
    {
        if (spec->runs.low[r] == 0)
        {
            return false; // runs are 1-based
        }
    }

    bool countsGiven = false;
    for (size_t i = 1; i < partCount; ++i)
    {
        if (strcmp(parts[i], "split") == 0 && i == partCount - 1)
        {
            spec->split = true;
        }
        else if (!countsGiven && parseSelection(parts[i], &spec->counts))
        {
            countsGiven = true;
        }
        else
        {
            return false;
        }
    }

    spec->enabled = true;
    return true;
}

static void printUsage(const char* program)
{
    printf("Usage: %s [options] [-k <K> <data.txt> [gt.txt]]\n", program);
    printf("       %s ci <centroids.txt> <ground-truth.txt>       prints the Centroid Index\n", program);
    printf("       %s sse <data.txt> <centroids.txt>              prints the SSE (nearest centroids)\n", program);
    printf("       %s gt-from-partition <data.txt> <partition.txt> <out.txt>\n", program);
    printf("                                writes means from 1-based labels separated by spaces, tabs or line breaks\n");
    printf("       %s report [-o dir] [--names file] [--curve-run i] [--decimal-comma] [--no-time] <run-folder>...\n",
           program);
    printf(
        "                                merges run folders into summary.csv, the seminar tables and the curves\n\n");
    printf("Split k-means clustering experiments, version %s.\n", SKM_VERSION);
    printf("Without a data file, runs batch mode over data/, gt/ and centroids/.\n\n");

    printf("Single dataset:\n");
    printf("  -k <K>              Number of clusters, at most the distinct data points (required with a data file)\n");
    printf("  <data.txt>          Input data file (space/tab-separated doubles, one point per line)\n");
    printf("  [gt.txt]            Ground truth centroids for CI (optional; without it CI and success rate are NA)\n\n");

    printf("Batch mode:\n");
    printf("  --batch             Batch mode explicitly (also the default without a data file)\n");
    printf("  --datasets <list>   Only these datasets: comma-separated data file names without .txt\n\n");

    printf("Options (each at most once):\n");
    printf("  --algo <list>       Comma-separated algorithms (default: skm-local):\n");
    printf("                      km, rkm, rs, skm-random, skm-intra, skm-global,\n");
    printf("                      skm-global-basic, skm-local, bisecting\n");
    printf("  --seed <N>          Master seed (default: time-based, printed at start)\n");
    printf("  -r, --runs <N>      Last run per dataset (default: 100; RKM and RS: 1)\n");
    printf("  --first-run <i>     Start at run i (default 1) with the same derived seeds, to resume or split a job\n");
    printf("  --max-iterations <N>\n");
    printf("                      Positive per-call k-means cap (default: until SSE stops improving). Capped\n");
    printf("                      runs may not converge; not a wall-clock timeout; RS keeps at most 2 k-means\n");
    printf("                      iterations per swap\n");
    printf("  --max-swaps <N>     Random Swap trial swaps per run (default: 5000)\n");
    printf("  --repeats <N>       Repeated k-means repeats per run (default: 1000)\n");
    printf("  --bisect-tries <N>  Bisecting k-means tries per split (default: 5)\n");
    printf("  --bisect-select <mode>\n");
    printf("                      Bisecting cluster selection: sse (default) or size\n");
    printf("  --init <method>     Initial centroids of KM, RKM and RS: random (default) or kmeans++\n");
    printf("  --repartition <mode>\n");
    printf("                      SKM-Local: both (default), inward, or inward-nonchild\n");
    printf("  --track-progress    _log.csv rows of every run (default in batch mode)\n");
    printf("  --snapshots <spec>  Snapshot files: none, or RUNS[:ITERATIONS][:split] (default: batch mode 1,\n");
    printf("                      CLI none). RUNS (1-based) and ITERATIONS (_log.csv counts) are all or lists\n");
    printf("                      such as 1,3-5; split adds the state after each two-way split\n");
    printf("  --track-inner       _inner.csv: every iteration of the k-means runs inside the split algorithms\n");
    printf("  --no-track          No tracking rows or snapshots, also in batch mode\n");
    printf("  --decimal-comma     Decimal comma in the Excel-facing files (summary, _log.csv, _inner.csv,\n");
    printf("                      _iteration_stats.txt) for a Finnish Excel; _runs.csv keeps the point\n");
    printf("  --log-level <1-3>   1 basic (default), 2 progress per run, 3 debug; use 1 for timings\n");
    printf("  -o <dir>            Output folder, created with its parents (default: outputs/<timestamp>)\n");
    printf("  --version           Version, commit and compiler\n");
    printf("  -h, --help          Display this help\n\n");

    printf("Examples:\n");
    printf("  %s\n", program);
    printf("  %s -k 15 data/s1.txt gt/s1-cb.txt\n", program);
    printf("  %s --algo skm-local,rs --seed 42 -r 10\n", program);
}

/**
 * @brief Program entry: subcommands (ci, sse, gt-from-partition, report), batch mode without a data file, CLI mode with
 * one.
 *
 * Batch mode enumerates `data/`, `gt/`, `centroids/` and pairs files by sorted order.
 * CLI mode needs `-k <K> <data.txt>`; the ground truth file is optional.
 * Options (see printUsage) apply to both modes; all of them are validated before any output folder exists.
 *
 * @return 0 on success or a subcommand's failure status.
 * @errors Most option and input errors exit; some subcommand argument errors return failure.
 */
int cliMain(int argc, char* argv[])
{
    // Subcommands score finished results, for example the CBModules Split-PCA centroids (they replace runDebuggery)
    if (argc >= 2 && strcmp(argv[1], "ci") == 0)
    {
        return commandCi(argc - 2, argv + 2);
    }
    if (argc >= 2 && strcmp(argv[1], "sse") == 0)
    {
        return commandSse(argc - 2, argv + 2);
    }
    if (argc >= 2 && strcmp(argv[1], "gt-from-partition") == 0)
    {
        return commandGtFromPartition(argc - 2, argv + 2);
    }
    if (argc >= 2 && strcmp(argv[1], "report") == 0)
    {
        return commandReport(argc - 2, argv + 2);
    }

    Options opt;
    opt.seed = (uint64_t)time(NULL);
    opt.runs = 100;
    opt.runsGiven = false;
    opt.maxIterations = SIZE_MAX;
    opt.maxSwaps = 5000;
    opt.maxRepeats = 1000;
    opt.bisectTries = 5;
    opt.bisectSelect = BISECT_SELECT_SSE;
    opt.init = INIT_RANDOM;
    opt.repartition = REPARTITION_BOTH;
    opt.firstRun = 1;
    for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
    {
        opt.algorithms[a] = (strcmp(algorithmOptions[a].name, "skm-local") == 0);
    }

    char outputDirectory[PATH_MAX];
    const char* outputOption = NULL;

    const char* cliDataFile = NULL;
    const char* cliGtFile = NULL;
    size_t cliNumCentroids = 0;
    bool batchGiven = false;
    bool trackProgressGiven = false;
    bool noTrack = false;
    bool trackInner = false;
    const char* snapshotsOption = NULL;
    const char* datasetsOption = NULL;

    size_t dataCount = 0;
    char** dataNames = NULL;
    char** gtNames = NULL;
    char** kNames = NULL;

    // ============================================================
    // Parse arguments; every option at most once; a data file selects CLI mode
    // ============================================================
    const char* given[64];
    size_t givenCount = 0;
    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        if (arg[0] == '-')
        {
            const char* name = strcmp(arg, "--runs") == 0 ? "-r" : (strcmp(arg, "--help") == 0 ? "-h" : arg);
            for (size_t g = 0; g < givenCount; ++g)
            {
                if (strcmp(given[g], name) == 0)
                {
                    fprintf(stderr, "Error: %s given more than once\n", arg);
                    exit(EXIT_FAILURE);
                }
            }
            if (givenCount < sizeof(given) / sizeof(given[0]))
            {
                given[givenCount++] = name;
            }
        }

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
        {
            printUsage(argv[0]);
            exit(EXIT_SUCCESS);
        }
        else if (strcmp(arg, "--version") == 0)
        {
            printf("split_kmeans %s (commit %s, %s)\n", SKM_VERSION, SKM_GIT_COMMIT, platform_compiler());
            exit(EXIT_SUCCESS);
        }
        else if (strcmp(arg, "-k") == 0)
        {
            cliNumCentroids = countOption(argc, argv, &i, 1);
        }
        else if (strcmp(arg, "-r") == 0 || strcmp(arg, "--runs") == 0)
        {
            opt.runs = countOption(argc, argv, &i, 1);
            opt.runsGiven = true;
        }
        else if (strcmp(arg, "--max-iterations") == 0)
        {
            opt.maxIterations = countOption(argc, argv, &i, 1);
        }
        else if (strcmp(arg, "--max-swaps") == 0)
        {
            opt.maxSwaps = countOption(argc, argv, &i, 1);
        }
        else if (strcmp(arg, "--repeats") == 0)
        {
            opt.maxRepeats = countOption(argc, argv, &i, 1);
        }
        else if (strcmp(arg, "--bisect-tries") == 0)
        {
            opt.bisectTries = countOption(argc, argv, &i, 1);
        }
        else if (strcmp(arg, "--bisect-select") == 0)
        {
            const char* mode = textOption(argc, argv, &i);
            if (strcmp(mode, "sse") == 0)
                opt.bisectSelect = BISECT_SELECT_SSE;
            else if (strcmp(mode, "size") == 0)
                opt.bisectSelect = BISECT_SELECT_SIZE;
            else
            {
                fprintf(stderr, "Error: --bisect-select requires sse or size\n");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(arg, "--first-run") == 0)
        {
            opt.firstRun = countOption(argc, argv, &i, 1);
        }
        else if (strcmp(arg, "--seed") == 0)
        {
            if (!parseUint64(textOption(argc, argv, &i), &opt.seed))
            {
                fprintf(stderr, "Error: --seed requires a non-negative integer\n");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(arg, "--init") == 0)
        {
            const char* method = textOption(argc, argv, &i);
            if (strcmp(method, "random") == 0)
            {
                opt.init = INIT_RANDOM;
            }
            else if (strcmp(method, "kmeans++") == 0)
            {
                opt.init = INIT_KMEANSPP;
            }
            else
            {
                fprintf(stderr, "Error: --init requires random or kmeans++\n");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(arg, "--repartition") == 0)
        {
            const char* mode = textOption(argc, argv, &i);
            if (strcmp(mode, "inward") == 0)
                opt.repartition = REPARTITION_INWARD;
            else if (strcmp(mode, "inward-nonchild") == 0)
                opt.repartition = REPARTITION_INWARD_NONCHILD;
            else if (strcmp(mode, "both") == 0)
                opt.repartition = REPARTITION_BOTH;
            else
            {
                fprintf(stderr, "Error: --repartition requires inward, inward-nonchild or both\n");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(arg, "--algo") == 0)
        {
            if (!parseAlgorithmList(textOption(argc, argv, &i), opt.algorithms))
            {
                fprintf(stderr,
                        "Error: --algo requires a comma-separated list of: "
                        "km, rkm, rs, skm-random, skm-intra, skm-global, skm-global-basic, skm-local, bisecting\n");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(arg, "--log-level") == 0)
        {
            const size_t level = countOption(argc, argv, &i, 1);
            if (level > 3)
            {
                fprintf(stderr, "Error: --log-level requires 1, 2 or 3\n");
                exit(EXIT_FAILURE);
            }
            logLevel = (int)level;
        }
        else if (strcmp(arg, "-o") == 0)
        {
            outputOption = textOption(argc, argv, &i);
            if (outputOption[0] == '\0' || strlen(outputOption) >= sizeof(outputDirectory))
            {
                fprintf(stderr, "Error: -o requires an output folder path shorter than %d characters\n", PATH_MAX);
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(arg, "--datasets") == 0)
        {
            datasetsOption = textOption(argc, argv, &i);
        }
        else if (strcmp(arg, "--batch") == 0)
        {
            batchGiven = true;
        }
        else if (strcmp(arg, "--track-progress") == 0)
        {
            trackProgressGiven = true;
        }
        else if (strcmp(arg, "--no-track") == 0)
        {
            noTrack = true;
        }
        else if (strcmp(arg, "--track-inner") == 0)
        {
            trackInner = true;
        }
        else if (strcmp(arg, "--decimal-comma") == 0)
        {
            decimalComma = true;
        }
        else if (strcmp(arg, "--snapshots") == 0)
        {
            SnapshotSpec check;
            snapshotsOption = textOption(argc, argv, &i);
            if (!parseSnapshotSpec(snapshotsOption, &check))
            {
                fprintf(
                    stderr,
                    "Error: --snapshots requires none or RUNS[:ITERATIONS][:split], for example 1 or 1:all:split\n");
                exit(EXIT_FAILURE);
            }
        }
        else if (arg[0] == '-')
        {
            fprintf(stderr, "Error: Unknown option: %s\n", arg);
            fprintf(stderr, "Use -h for help\n");
            exit(EXIT_FAILURE);
        }
        else if (!cliDataFile)
        {
            cliDataFile = arg;
        }
        else if (!cliGtFile)
        {
            cliGtFile = arg;
        }
        else
        {
            fprintf(stderr, "Error: Unexpected argument: %s\n", arg);
            fprintf(stderr, "Use -h for help\n");
            exit(EXIT_FAILURE);
        }
    }

    // ============================================================
    // Validate CLI options and mode before creating an output folder; input contents are read later
    // ============================================================
    const bool cliMode = (cliDataFile != NULL);

    if (batchGiven && (cliMode || cliNumCentroids != 0))
    {
        fprintf(stderr, "Error: --batch takes no data file and no -k\n");
        exit(EXIT_FAILURE);
    }
    if (cliMode && cliNumCentroids == 0)
    {
        fprintf(stderr, "Error: K must be specified with -k <number>\n");
        fprintf(stderr, "Use -h for help\n");
        exit(EXIT_FAILURE);
    }
    if (!cliMode && cliNumCentroids != 0)
    {
        fprintf(stderr, "Error: Data file required\n");
        exit(EXIT_FAILURE);
    }
    if (cliMode && datasetsOption != NULL)
    {
        fprintf(stderr, "Error: --datasets selects batch-mode datasets; CLI mode takes one data file\n");
        exit(EXIT_FAILURE);
    }
    if (opt.firstRun > opt.runs)
    {
        fprintf(stderr, "Error: --first-run %zu is after the last run %zu (set the last run with -r)\n", opt.firstRun,
                opt.runs);
        exit(EXIT_FAILURE);
    }
    if (opt.firstRun > 1 && !opt.runsGiven && (algorithmSelected(&opt, "rkm") || algorithmSelected(&opt, "rs")))
    {
        fprintf(stderr, "Error: --first-run with rkm or rs needs -r (they run once by default)\n");
        exit(EXIT_FAILURE);
    }
    if (noTrack && (trackProgressGiven || trackInner || snapshotsOption != NULL))
    {
        fprintf(stderr, "Error: --no-track cannot be combined with --track-progress, --track-inner or --snapshots\n");
        exit(EXIT_FAILURE);
    }
    if (cliMode && !fileExists(cliDataFile))
    {
        fprintf(stderr, "Error: Data file %s not found\n", cliDataFile);
        exit(EXIT_FAILURE);
    }
    if (cliGtFile != NULL && !fileExists(cliGtFile))
    {
        fprintf(stderr, "Error: Ground-truth file %s not found\n", cliGtFile);
        exit(EXIT_FAILURE);
    }

    bool* selectedSets = NULL;
    if (cliMode)
    {
        // Create single-file lists for unified loop
        dataCount = 1;
        dataNames = createStringList(1);
        gtNames = createStringList(1);
        kNames = createStringList(1);

        STRCPY(dataNames[0], PATH_MAX, cliDataFile);
        if (cliGtFile)
        {
            STRCPY(gtNames[0], PATH_MAX, cliGtFile);
        }
        // K file path (empty, won't be used since we have cliNumCentroids)
        kNames[0][0] = '\0';
    }
    else
    {
        // ============================================================
        // Directory batch mode
        // ============================================================
        dataCount = list_files("data", &dataNames);
        size_t gtCount = list_files("gt", &gtNames);
        size_t kCount = list_files("centroids", &kNames);

        if (dataCount == 0 || dataCount != gtCount || dataCount != kCount)
        {
            fprintf(stderr, "Directory mismatch: data=%zu, gt=%zu, centroids=%zu\n", dataCount, gtCount, kCount);
            exit(EXIT_FAILURE);
        }
    }

    // --datasets: a subset of the batch datasets by data file name without .txt; pairing stays by sorted position
    selectedSets = malloc(dataCount * sizeof(bool));
    handleMemoryError(selectedSets);
    for (size_t i = 0; i < dataCount; ++i)
    {
        selectedSets[i] = (datasetsOption == NULL);
    }
    if (datasetsOption != NULL)
    {
        char list[1024];
        if (strlen(datasetsOption) >= sizeof(list))
        {
            fprintf(stderr, "Error: --datasets list too long\n");
            exit(EXIT_FAILURE);
        }
        STRCPY(list, sizeof(list), datasetsOption);
        char* context = NULL;
        size_t named = 0;
        for (char* name = STRTOK(list, ",", &context); name != NULL; name = STRTOK(NULL, ",", &context))
        {
            size_t j = 0;
            while (j < dataCount && strcmp(removeExtension(dataNames[j]), name) != 0)
            {
                ++j;
            }
            if (j == dataCount)
            {
                fprintf(stderr, "Error: --datasets: no data/%s.txt\n", name);
                exit(EXIT_FAILURE);
            }
            selectedSets[j] = true;
            ++named;
        }
        if (named == 0)
        {
            fprintf(stderr, "Error: --datasets: no dataset names in '%s'\n", datasetsOption);
            exit(EXIT_FAILURE);
        }
    }
    size_t selectedCount = 0;
    for (size_t i = 0; i < dataCount; ++i)
    {
        if (selectedSets[i])
        {
            const char* name = cliMode ? platform_basename(dataNames[i]) : dataNames[i];
            if (!validateCsvName(removeExtension(name), "dataset name"))
            {
                freeStringList(dataNames, dataCount);
                freeStringList(gtNames, dataCount);
                freeStringList(kNames, dataCount);
                free(selectedSets);
                return EXIT_FAILURE;
            }
            ++selectedCount;
        }
    }

    // Tracking defaults: batch mode logs the rows of every run and the snapshots of run 1; CLI mode nothing
    TrackOptions track;
    track.rows = !noTrack && (trackProgressGiven || !cliMode);
    track.inner = trackInner;
    const char* snapshotsText =
        noTrack ? "none" : (snapshotsOption != NULL ? snapshotsOption : (cliMode ? "none" : "1"));
    parseSnapshotSpec(snapshotsText, &track.snapshots);

    if (cliMode)
    {
        printf("\n=== CLI Mode ===\n");
        printf("Data: %s\n", cliDataFile);
        printf("Ground truth: %s\n", cliGtFile ? cliGtFile : "None");
        printf("K: %zu, Runs: %zu\n", cliNumCentroids, opt.runs);
    }
    else
    {
        printf("=== Directory Batch Mode: %zu datasets ===\n", selectedCount);
    }
    printf("Seed: %" PRIu64 "\n\n", opt.seed);

    if (outputOption != NULL)
    {
        STRCPY(outputDirectory, sizeof(outputDirectory), outputOption);
        if (!platform_make_directories(outputDirectory))
        {
            fprintf(stderr, "Error: Unable to create output folder '%s': %s\n", outputDirectory, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        createUniqueDirectory(outputDirectory, sizeof(outputDirectory));
    }

    checkOutputRoot(outputDirectory);
    checkRunConfig(outputDirectory, &opt);

    const size_t firstRun = opt.firstRun - 1;
    int status = EXIT_SUCCESS;
    bool configWritten = false;

    for (size_t i = 0; i < dataCount; ++i)
    {
        if (!selectedSets[i])
        {
            continue;
        }

        // Determine file paths based on mode
        char currentDataFile[PATH_MAX];
        char currentGtFile[PATH_MAX];
        char* baseName = NULL;
        size_t currentK = 0;
        size_t currentLoops = 0;
        char currentKSource[PATH_MAX];

        if (cliMode)
        {
            // ============================================================
            // CLI mode: use parsed values
            // ============================================================
            STRCPY(currentDataFile, sizeof(currentDataFile), cliDataFile);
            if (cliGtFile)
            {
                STRCPY(currentGtFile, sizeof(currentGtFile), cliGtFile);
            }
            else
            {
                currentGtFile[0] = '\0'; // No ground truth
            }

            baseName = removeExtension(platform_basename(cliDataFile));

            currentK = cliNumCentroids;
            currentLoops = opt.runs;
            snprintf(currentKSource, sizeof(currentKSource), "-k %zu", currentK);
        }
        else
        {
            // ============================================================
            // Batch mode: build paths from file lists
            // ============================================================
            joinPath(currentDataFile, sizeof(currentDataFile), "data", dataNames[i]);
            joinPath(currentGtFile, sizeof(currentGtFile), "gt", gtNames[i]);

            char kFile[PATH_MAX];
            joinPath(kFile, sizeof(kFile), "centroids", kNames[i]);

            baseName = removeExtension(dataNames[i]);
            currentK = readKFromFile(kFile);
            currentLoops = opt.runs;
            STRCPY(currentKSource, sizeof(currentKSource), kFile);
        }

        // Read data and ground truth
        InputSnapshot dataSnapshot = readInputSnapshot(currentDataFile);
        Dataset dataPoints = readDatasetSnapshot(&dataSnapshot, currentDataFile);
        char dataSha256[SHA256_HEX_SIZE];
        STRCPY(dataSha256, sizeof(dataSha256), dataSnapshot.sha256);
        freeInputSnapshot(&dataSnapshot);
        if (dataPoints.size == 0)
        {
            fprintf(stderr, "--> Skipping %s (no data points)\n", baseName);
            continue;
        }
        const size_t numDimensions = dataPoints.dimensions;

        if (currentK > dataPoints.size)
        {
            fprintf(stderr, "Error: K (%zu) exceeds the number of data points (%zu) in %s\n", currentK, dataPoints.size,
                    currentDataFile);
            exit(EXIT_FAILURE);
        }

        const size_t distinctLimit = currentK < dataPoints.size ? currentK + 1 : currentK;
        const size_t distinctRows = countDistinctRowsUpTo(&dataPoints, distinctLimit);
        if (distinctRows < currentK)
        {
            fprintf(stderr, "Error: K (%zu) exceeds the number of distinct data points (%zu) in %s\n",
                    currentK, distinctRows, currentDataFile);
            exit(EXIT_FAILURE);
        }

        Centroids groundTruth = {0};
        char groundTruthSha256[SHA256_HEX_SIZE] = "none";
        if (currentGtFile[0] != '\0')
        {
            InputSnapshot groundTruthSnapshot = readInputSnapshot(currentGtFile);
            groundTruth = readCentroidsSnapshot(&groundTruthSnapshot, currentGtFile);
            STRCPY(groundTruthSha256, sizeof(groundTruthSha256), groundTruthSnapshot.sha256);
            freeInputSnapshot(&groundTruthSnapshot);
            if (groundTruth.size > 0 && groundTruth.dimensions != dataPoints.dimensions)
            {
                fprintf(stderr, "Error: Ground truth %s has %zu dimensions, data %s has %zu\n", currentGtFile,
                        groundTruth.dimensions, currentDataFile, dataPoints.dimensions);
                exit(EXIT_FAILURE);
            }
        }

        char datasetDirectory[PATH_MAX];
        DatasetInfo info;
        makeDatasetInfo(&info, currentDataFile, currentGtFile, dataSha256, groundTruthSha256, currentK,
                        dataPoints.size, numDimensions);
        joinPath(datasetDirectory, sizeof(datasetDirectory), outputDirectory, baseName);
        checkDatasetInfo(datasetDirectory, &info);
        if (!configWritten)
        {
            writeRunConfig(outputDirectory, &opt, &track, snapshotsText, datasetsOption != NULL ? datasetsOption : "all",
                           cliMode, argc, argv);
            configWritten = true;
        }
        createDatasetDirectory(outputDirectory, baseName, datasetDirectory, sizeof(datasetDirectory));
        writeDatasetInfo(datasetDirectory, &info);

        // Batch pairing is by sorted position: show which files went together
        if (logLevel >= 2) printf("Starting process\n");
        printf("Dataset: %s (data %s, ground truth %s, K from %s)\n", baseName, currentDataFile,
               currentGtFile[0] != '\0' ? currentGtFile : "none", currentKSource);
        if (groundTruth.size > 0 && groundTruth.size != currentK)
        {
            fprintf(stderr,
                    "Warning: %s: the ground truth %s has %zu centroids but K is %zu; CI compares sets of different "
                    "sizes\n",
                    baseName, currentGtFile, groundTruth.size, currentK);
        }
        printf("Dimensions: %zu, Data points: %zu, K: %zu, Runs: %zu\n\n", numDimensions, dataPoints.size, currentK,
               currentLoops);

        // Algorithm parameters
        size_t scaling =
            1; // SSE divisor of the summary row and the console statistics (1: unscaled; the report scales SSE)
        const size_t rkmRsLoops = opt.runsGiven ? currentLoops : 1;

        for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
        {
            if (!opt.algorithms[a])
            {
                continue;
            }

            if (!configWritten)
                writeRunConfig(outputDirectory, &opt, &track, snapshotsText, datasetsOption != NULL ? datasetsOption : "all",
                               cliMode, argc, argv);

            const Algorithm algorithmId = algorithmOptions[a].id;
            switch (algorithmId)
            {
                case ALGO_KM:
                    runKMeansAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxIterations, firstRun, currentLoops,
                                       scaling, baseName, datasetDirectory, &track, opt.init, opt.seed);
                    break;
                case ALGO_RKM:
                    if (!runRepeatedKMeansAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxIterations,
                                                    opt.maxRepeats, firstRun, rkmRsLoops, scaling, baseName,
                                                    datasetDirectory, &track, opt.init, opt.seed))
                        status = EXIT_FAILURE;
                    break;
                case ALGO_RS:
                    runRandomSwapAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxSwaps, firstRun, rkmRsLoops,
                                           scaling, baseName, datasetDirectory, &track, opt.init, distinctRows, opt.seed);
                    break;
                case ALGO_SKM_RANDOM:
                    runRandomSplitAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxIterations, firstRun,
                                            currentLoops, scaling, baseName, datasetDirectory, &track, opt.seed);
                    break;
                case ALGO_BISECTING:
                    runBisectingKMeansAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxIterations, firstRun,
                                                currentLoops, scaling, baseName, datasetDirectory, &track,
                                                opt.bisectTries, opt.bisectSelect, opt.seed);
                    break;
                default: // SKM-Intra, SKM-Global, SKM-Global-Basic, SKM-Local
                    runSseSplitAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxIterations, firstRun, currentLoops,
                                         scaling, baseName, datasetDirectory, algorithmId, opt.repartition, &track,
                                         opt.seed);
                    break;
            }
            if (status != EXIT_SUCCESS) break;
        }

        // Clean up
        freeDataset(&dataPoints);
        freeCentroids(&groundTruth);
        if (status != EXIT_SUCCESS) break;
    }

    freeStringList(dataNames, dataCount);
    freeStringList(gtNames, dataCount);
    freeStringList(kNames, dataCount);
    free(selectedSets);

    return status;
}
