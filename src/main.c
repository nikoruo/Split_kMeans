/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
*/

/* Update log
* --------------------------------------------------------------------
* Version 1.0.0 - 26-10-2025 by Niko Ruohonen
* - Initial release
* --------------------------------------------------------------------
* Unreleased (v2.0.0 refactor, docs/refactor-plan.md)
* - 2.3: K=1 runs without a split (SKM variants and bisecting crashed with a heap
*   overflow); the final k-means gives the dataset mean. SKM-Global and
*   SKM-Global-Basic with K=2 now run global k-means after the initial split;
*   before, they skipped it and reported SSE 1.7976931348623157e+308 (DBL_MAX).
*   Golden: e04_tiny SSE DBL_MAX -> 0, e07_k2 SSE DBL_MAX -> 107718.06 (same as
*   SKM-Intra); new edge case e01_k1 (SSE 14686.68 for every algorithm).
* - 2.4: split selection considers only clusters with 2 or more points
*   (SKM variants, Random Split, bisecting); exits with an error if none is
*   left. Fixes the hangs on one-point clusters (SKM-Local on e04_tiny with
*   K=3, bisecting on e02_k_equals_n). Golden files unchanged; bisecting
*   joins the edge runs (7 new golden files).
* - 2.5: RKM tracking rows report the CI of the best solution also when the
*   first repeat stays best (they said NA); the perfect/failed partition
*   snapshots are relabelled with the best centroids (they held the labels
*   of the last repeat). Golden files unchanged.
* - 2.6: the data and ground-truth parser rejects values that are not
*   finite numbers ("1,5", "abc", "1.5x", nan, inf, 1e999; before, strtod
*   read them as 1, 0, 1.5, NaN, Inf, Inf) and lines longer than 16383
*   characters (before, split into extra rows); errors give file, line and
*   column. getNumDimensions removed. Golden files unchanged.
* - 2.7: batch mode lists only *.txt files (folders and other files are
*   ignored) and sorts them the same way on every OS: ASCII case-insensitive,
*   then byte order (before, MSVC used _stricmp and MinGW/Linux strcmp, so
*   pairing could differ between builds). list_files moved to platform.c with
*   checked allocation; every plain dir/name join is checked (joinPath).
*   STAT, MKDIR_OK and PRAGMA_MSVC removed. Golden files unchanged.
* - 2.8: ASan and UBSan clean on all 108 regression runs (Windows, Visual
*   Studio clang 22); CI runs make asan check and make ubsan check with gcc
*   and clang, leak detection on. _Analysis_assume_ fallback and the sal.h
*   include removed (unused). No code fixes were needed.
* - Phase 3 (measurement, outputs, CLI; golden files unchanged, M1 matches M0):
*   run times from a monotonic wall clock (QueryPerformanceCounter or
*   clock_gettime(CLOCK_MONOTONIC); clock() was CPU time on Linux) that pauses
*   while tracking; _log.csv rows are buffered and written after each run, with
*   run and count columns (RS: the initial solution and each improvement only;
*   RKM: one row per repeat; KM: one row per iteration); --track-progress,
*   --snapshots, --track-inner and --no-track are separate; snapshot names give
*   run, iteration and phase; run_config.txt; --decimal-comma; a strict CLI
*   (--max-swaps, --repeats, --bisect-tries, --init, --first-run, --datasets,
*   --batch, --version); subcommands ci, sse and gt-from-partition; report
*   (summary, seminar tables and curves from finished batch folders) and
*   dataset.txt in every dataset folder.
* - Phase 3 review (golden runs unchanged, report golden files updated): report
*   ends RS at the last SSE improvement of each run (was the first CI = 0) and
*   needs its _log.csv rows; KM mean curves keep each finished run at its last
*   state (they averaged only the runs still running, so they rose); sd NA for
*   one run; gt-from-partition rejects short, long or non-integer label files
*   (it computed centroids from a partial file and exited 0); run_config.txt
*   has no host name and names the program without its folder; an empty
*   --datasets list is an error; splitClusterGlobal checks its realloc.
* --------------------------------------------------------------------
* Update 1.1...
* - ...
*/

// Introduction
/**
* Project: Split K-Means (version: SKM_VERSION below; v1.0.0 released 26-10-2025)
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
*    and curves from finished batch folders).
*
* OUTPUT (outputs/<timestamp>/ or -o <dir>):
*    run_config.txt                      # build, machine, seed, parameters; one entry per call
*    <dataset>/
*       ├─ dataset.txt                   # data and ground-truth files, K, N, D (read by report)
*       ├─ <dataset>.csv                 # summary row per algorithm (Excel, --decimal-comma)
*       ├─ <Algorithm>_runs.csv          # one row per run: run;seed;sse;ci;ms (%.17g)
*       ├─ <Algorithm>_log.csv           # tracked states: run;count;sse;ci;ms
*       ├─ <Algorithm>_inner.csv         # --track-inner
*       ├─ <Algorithm>_iteration_stats.txt and <Algorithm>_run<R>_iter<I>_<phase>_*.txt  # --snapshots
*       └─ <Algorithm>_{centroids,partitions}_{perfect,failed}.txt  # first run with CI = 0 and CI > 0
*
* REQUIREMENTS:
* - Input files: whitespace-separated finite numbers with a decimal point, the same
*   column count on every row; the program never changes the locale
* - K files: a single positive integer
* - Ground truth: the same dimensionality as the data
*/

/**
* @brief Suppresses warnings about unsafe functions in Visual Studio.
*
* This macro disables warnings for functions like `strcpy` and `sprintf`
* when using the Microsoft C compiler. It is ignored by other compilers.
*/
#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS
#endif

 // Platform-specific includes
#include "platform.h"
#include "dataset.h"
#include "util.h"

#include "rng.h"

// General includes
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <float.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stddef.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h> 
#include <limits.h>
#include <stdlib.h>

/////////////
// Macros //
///////////


/** @brief Program version: help text, --version and run_config.txt. */
#define SKM_VERSION "2.0.0-dev"

/* Build information: the Makefile and the Visual Studio project generate build_info.h with the git commit and the flags. */
#ifdef SKM_BUILD_INFO
#include "build_info.h"
#endif
#ifndef SKM_GIT_COMMIT
#define SKM_GIT_COMMIT "unknown"
#endif
#ifndef SKM_BUILD_FLAGS
#define SKM_BUILD_FLAGS "unknown"
#endif

/** @brief CI value meaning "not available": no ground truth, or not computed yet. Printed as NA. */
#define CI_NA SIZE_MAX

//////////////
// GLOBALS //
////////////

// Set by --log-level: 1 basic (default), 2 progress, 3 debug.
// Checked only per run, swap, split or iteration, never per point.
static int logLevel = 1;

// Set by --decimal-comma: the Excel-facing files (summary, _log.csv, _inner.csv, _iteration_stats.txt)
// get a decimal comma, as a Finnish Excel reads them. Numbers are formatted in the C locale first.
static bool decimalComma = false;

//////////////
// Structs //
////////////



/**
 * @brief One bisecting try: the SSE of the two-way split and its two centroids.
 *
 * Owns `centroids` (two points); free with freeClusteringResult.
 */
typedef struct
{
    double sse;           /**< SSE of the split cluster's points after the local 2-means. */
    DataPoint* centroids; /**< The two centroids. */
} ClusteringResult;

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
    double sseSum;       /**< Sum of SSE values across runs. */
    size_t ciSum;        /**< Sum of Centroid Index (CI) values across runs. */
    double timeSum;      /**< Total time across runs in milliseconds. */
    double successRate;  /**< Success count across runs (used as count, averaged later). */
    bool ciAvailable;    /**< False once a run had no CI (no ground truth); CI and success rate print as NA. */
} Statistics;

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
    size_t count;  /**< Split number, swap, repeat or iteration; 0 is the initial state. */
    double sse;
    size_t ci;     /**< CI_NA without ground truth. */
    double ms;     /**< Counted run time when the state was reached. */
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
    size_t count;      /**< Split number the k-means belongs to (as in `_log.csv`). */
    KMeansStage stage;
    size_t iteration;  /**< 1-based iteration of that k-means run. */
    size_t k;          /**< Centroids in that k-means run (2 for the local ones). */
    double sse;        /**< SSE of the points in that k-means run. */
    size_t ci;         /**< CI against the ground truth; CI_NA for local runs or without ground truth. */
    double ms;
} InnerRow;

/** @brief Most ranges in one --snapshots run or iteration list. */
#define MAX_RANGES 32

/** @brief A set of 1-based runs or of counts, given as `all` or a list such as `1,3-5`. */
typedef struct
{
    bool all;
    size_t rangeCount;
    size_t low[MAX_RANGES];  /**< Inclusive bounds. */
    size_t high[MAX_RANGES];
} Selection;

/** @brief What --snapshots writes: the selected runs and counts, and the state after each two-way split when `split` is set. */
typedef struct
{
    bool enabled;
    Selection runs;    /**< 1-based run numbers. */
    Selection counts;  /**< Counts as in `_log.csv`. */
    bool split;        /**< Also the state after the two-way split, before the follow-up step. */
} SnapshotSpec;

/** @brief Tracking switches of one invocation: `_log.csv` rows, snapshots and `_inner.csv` rows. */
typedef struct
{
    bool rows;              /**< --track-progress: `_log.csv` rows of every run. */
    SnapshotSpec snapshots; /**< --snapshots. */
    bool inner;             /**< --track-inner: per-iteration rows of the k-means runs inside the split algorithms. */
} TrackOptions;

/** @brief Initial centroids of KM, RKM and RS (--init); the split algorithms and bisecting start from one random point. */
typedef enum
{
    INIT_RANDOM,   /**< K distinct random data points (default). */
    INIT_KMEANSPP  /**< k-means++ seeding. */
} InitMethod;

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

/**
 * @brief Tracking and log context of one algorithm: ground truth, run stopwatch, `_log.csv` and `_inner.csv` rows, snapshots.
 *
 * Recording pauses the stopwatch, so tracking does not count in the run time. Owns the row buffers; free with trackerFree.
 */
typedef struct
{
    const Centroids* groundTruth; /**< Reference for CI; NULL or empty gives CI_NA. */
    const char* outputDirectory;  /**< Dataset output folder; must exist. */
    Algorithm algorithm;          /**< Names the output files (getAlgorithmName). */
    const TrackOptions* options;  /**< Must outlive the tracker. */
    bool logRows;                 /**< Buffer `_log.csv` rows in every run. */
    bool inner;                   /**< Buffer `_inner.csv` rows in every run. */
    size_t run;                   /**< Current run, 0-based. */
    bool snapshots;               /**< The current run is selected for snapshots. */
    size_t nextCount;             /**< Count after the last recorded one: the split the inner k-means runs belong to. */
    Stopwatch stopwatch;          /**< Run time; paused while tracking. */
    TrackRow* rows;               /**< `_log.csv` rows of the current run. */
    size_t rowCount;
    size_t rowCapacity;
    InnerRow* innerRows;          /**< `_inner.csv` rows of the current run. */
    size_t innerCount;
    size_t innerCapacity;
} Tracker;


///////////////
// Memories //
/////////////






 /**
  * @brief Frees the centroid array of a clustering result (including each centroid's attributes) and resets the pointer.
  *
  * Does not free the `ClusteringResult` object itself. `numCentroids` is the number of centroids in `result->centroids`.
  */
 void freeClusteringResult(ClusteringResult* result, size_t numCentroids)
 {
     if (result == NULL) return;

     if (result->centroids != NULL)
     {
         freeDataPointArray(result->centroids, numCentroids);
         result->centroids = NULL;
     }
 }






  /**
   * @brief A clustering result with SSE DBL_MAX and `numCentroids` zeroed centroids of `dimensions` attributes.
   */
  ClusteringResult allocateClusteringResult(size_t numCentroids, size_t dimensions)
  {
      assert(numCentroids > 0 && dimensions > 0);

      ClusteringResult result;
      result.sse = DBL_MAX;
      result.centroids = malloc(numCentroids * sizeof(DataPoint));
      handleMemoryError(result.centroids);
      for (size_t i = 0; i < numCentroids; ++i)
      {
          result.centroids[i] = allocateDataPoint(dimensions);
      }      

      return result;
  }

  /**
   * @brief Resets all statistic accumulators to their baseline.
   */
  void initializeStatistics(Statistics* stats)
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

  //////////////
  // Helpers //
  ////////////

/**
 * @brief Squared Euclidean distance between two points.
 */
  double calculateSquaredEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
  {
      const size_t dims = point1->dimensions;
      const double* a = point1->attributes;
      const double* b = point2->attributes;

      double sum = 0.0;
      for (size_t i = 0; i < dims; ++i)
      {
          const double diff = a[i] - b[i];
          sum += diff * diff;
      }
      return sum;
  }

  /**
   * @brief Euclidean distance between two points of the same dimensionality.
   *
   * No call site; kept (Unused code review).
   */
  double calculateEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
  {
      return sqrt(calculateSquaredEuclideanDistance(point1, point2));
  }








  /** @brief Buffer size for formatCi: 20 digits of SIZE_MAX plus the terminator. */
  #define CI_TEXT_SIZE 21

  /** @brief Buffer size for formatFixed: DBL_MAX with %.6f has 316 characters. */
  #define NUMBER_TEXT_SIZE 330

  /**
   * @brief Writes `value` with `decimals` fixed decimals into `buffer` and returns it (Excel-facing files).
   *
   * The program never calls setlocale, so printf writes a decimal point; with --decimal-comma it becomes a comma.
   */
  static const char* formatFixed(double value, int decimals, char buffer[NUMBER_TEXT_SIZE])
  {
      snprintf(buffer, NUMBER_TEXT_SIZE, "%.*f", decimals, value);
      if (decimalComma)
      {
          char* point = strchr(buffer, '.');
          if (point != NULL)
          {
              *point = ',';
          }
      }
      return buffer;
  }

  /**
   * @brief Writes `ci` as decimal text, or "NA" for CI_NA, into `buffer`; returns `buffer`.
   */
  static const char* formatCi(size_t ci, char buffer[CI_TEXT_SIZE])
  {
      if (ci == CI_NA)
      {
          snprintf(buffer, CI_TEXT_SIZE, "NA");
      }
      else
      {
          snprintf(buffer, CI_TEXT_SIZE, "%zu", ci);
      }
      return buffer;
  }


  /**
   * @brief Appends the buffered rows of one run to `filePath`: run;count;sse;ci;ms (ci is NA when not available).
   *
   * Writes the header first when the file does not exist yet. Call outside timed code.
   */
  void appendLogCsv(const char* filePath, size_t run, const TrackRow* rows, size_t rowCount)
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
   * The seed reproduces the run; SSE uses %.17g so it round-trips; ci is NA without ground truth. Call outside timed code.
   */
  void appendRunCsv(const char* outputDirectory, const char* algorithmName, size_t run, uint64_t seed, double sse, size_t ci, double ms)
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
  void writeCentroidsToFile(const char* filename, const Centroids* centroids, const char* outputDirectory)
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
  void writeDataPointPartitionsToFile(const char* filename, const DataPoints* dataPoints, const char* outputDirectory)
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
   * @brief Appends the summary row of one algorithm to `<outputDirectory>/<filename>.csv` (header when the file is empty).
   *
   * Algorithm;Average CI;SSE;Relative CI;MS;Success Rate, averaged over `loopCount` runs (> 0); SSE divided by
   * `scaling` (> 0); CI, relative CI and success rate NA without ground truth. Fixed decimals through formatFixed
   * (decimal comma with --decimal-comma). Exits when the file cannot be opened; write errors are not checked.
   */
  void writeResultsToFile(const char* filename, Statistics stats, size_t numCentroids, const char* algorithm, size_t loopCount, size_t scaling, const char* outputDirectory)
  {
      char csvFileName[PATH_MAX];
      char outputFilePath[PATH_MAX];
      if (snprintf(csvFileName, sizeof(csvFileName), "%s.csv", filename) >= (int)sizeof(csvFileName)
          || snprintf(outputFilePath, sizeof(outputFilePath), "%s%c%s", outputDirectory, PATHSEP, csvFileName) >= (int)sizeof(outputFilePath))
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
   * @brief Creates a new folder `outputs/<YYYY-MM-DD_HH-MM-SS>` (local time; `_2`, `_3`, ... when it exists) and writes its path into `outputDirectory`.
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
          const int length = attempt == 1
              ? snprintf(outputDirectory, size, "outputs%c%s", PATHSEP, datebuf)
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
   * @brief Maps an algorithm id to a short stable name.
   */
  const char* getAlgorithmName(Algorithm algorithm)
  {
      switch (algorithm)
      {
      case ALGO_SKM_INTRA: return "SKM_Intra";
      case ALGO_SKM_GLOBAL: return "SKM_Global";
      case ALGO_SKM_LOCAL: return "SKM_Local";
      case ALGO_RS: return "RS";
      case ALGO_BISECTING: return "Bisecting_KM";
      case ALGO_SKM_RANDOM: return "SKM_Random";
      case ALGO_KM: return "KM";
      case ALGO_RKM: return "RKM";
      case ALGO_SKM_GLOBAL_BASIC: return "SKM_Global_Basic";
      default:
          fprintf(stderr, "Error: Invalid algorithm type provided: %d\n", (int)algorithm);
          return "Unknown";
      }
  }

  /**
   * @brief Prints the averages over `loopCount` runs (> 0) to stdout: CI, SSE divided by `scaling`, relative CI, time, success rate.
   *
   * CI, relative CI and success rate print as NA without ground truth.
   */
  void printStatistics(const char* algorithmName, Statistics stats, size_t loopCount, size_t numCentroids, size_t scaling)
  {
      if (!stats.ciAvailable)
      {
          printf("(%s) Average CI: NA and SSE: %.0f\n", algorithmName, stats.sseSum / loopCount / scaling);
          printf("(%s) Relative CI: NA\n", algorithmName);
          printf("(%s) Average time taken: %.0f ms\n", algorithmName, stats.timeSum / loopCount);
          printf("(%s) Success rate: NA\n\n", algorithmName);
          return;
      }

      printf("(%s) Average CI: %.2f and SSE: %.0f\n", algorithmName, (double)stats.ciSum / loopCount, stats.sseSum / loopCount / scaling);
      printf("(%s) Relative CI: %.2f\n", algorithmName, (double)stats.ciSum / loopCount / numCentroids);
      printf("(%s) Average time taken: %.0f ms\n", algorithmName, stats.timeSum / loopCount);
      printf("(%s) Success rate: %.2f%%\n\n", algorithmName, stats.successRate / loopCount * 100);
  }


  /**
   * @brief Writes the path `<outputDirectory>/<AlgorithmName><suffix>` into `path`; exits when it does not fit.
   */
  static void algorithmFilePath(Algorithm algorithm, const char* outputDirectory, const char* suffix, char* path, size_t pathSize)
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
  static void saveExampleSnapshots(const Centroids* centroids, const DataPoints* dataPoints,
      size_t centroidIndex, const char* algorithmPrefix, const char* outputDirectory,
      bool* savedPerfect, bool* savedFailed)
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

  /////////////////
  // Clustering //
  ///////////////

  /**
   * @brief Picks K different data points uniformly and copies them as initial centroids.
   *
   * Uses a partial Fisher–Yates shuffle to select `numCentroids` distinct indices from
   * `dataPoints` without replacement (duplicate points in the data can still give equal
   * coordinates, 6.6), then deep-copies those points into `centroids`.
   * Selection of swap index uses rejection sampling (no modulo bias).
   *
   * @param numCentroids Number of centroids to select (K); must be <= dataPoints->size and > 0.
   * @param dataPoints Source dataset; `dataPoints->size` must be > 0.
   * @param centroids Destination centroid container; must have at least K elements.
   * @return void
   * @errors Asserts valid K and destination size; exits on allocation failure.
   * @note O(N) init + O(K) swaps.
   */
  void generateRandomCentroids(size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids, Rng* rng)
  {
      const size_t N = dataPoints->size;

      assert(numCentroids > 0 && numCentroids <= N);
      assert(centroids->size >= numCentroids);

      /* Overflow guard for allocation */
      if (N > SIZE_MAX / sizeof(size_t))
      {
          handleMemoryError(NULL);
      }

      size_t* indices = (size_t*)malloc(N * sizeof(size_t));
      handleMemoryError(indices);

      /* init indices 0..N-1 */
      for (size_t i = 0; i < N; ++i)
      {
          indices[i] = i;
      }

      for (size_t i = 0; i < numCentroids; ++i)
      {
          size_t j = i + rng_below(rng, dataPoints->size - i);
          size_t temp = indices[i];
          indices[i] = indices[j];
          indices[j] = temp;
      }

      /* Copy selected points into centroids */
      for (size_t i = 0; i < numCentroids; ++i)
      {
          size_t idx = indices[i];
          deepCopyDataPoint(&centroids->points[i], &dataPoints->points[idx]);
      }

      free(indices);
  }

  /**
   * @brief k-means++ seeding (--init kmeans++): the first centroid uniformly, each next one with probability
   * proportional to the squared distance to the nearest chosen centroid.
   *
   * A pick at distance 0 (an already chosen coordinate) is drawn again. Exits when no point is left at a
   * positive distance, that is when the data have fewer than K distinct points (it looped forever before 3.5).
   * Requires 0 < K <= N and room for K centroids. O(K·N·D) with a distance cache.
   */
  void generateKMeansPlusPlusCentroids(size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids, Rng* rng)
  {
      assert(numCentroids > 0 && numCentroids <= dataPoints->size);
      assert(centroids->size >= numCentroids);

      /* 1) Choose the first centroid at random */
      size_t firstIndex = rng_below(rng, dataPoints->size);
      deepCopyDataPoint(&centroids->points[0], &dataPoints->points[firstIndex]);

      /* Distance cache: squared distance to the nearest chosen centroid so far */
      double* dist2 = malloc(sizeof(double) * dataPoints->size);
      handleMemoryError(dist2);

      // Initialise the cache with distances to the first centroid
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          dist2[i] = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[0]);
      }

      size_t chosen = 1;
      while (chosen < numCentroids)
      {
          // 2. Compute the normalising constant distSum
          double distSum = 0.0;
          for (size_t i = 0; i < dataPoints->size; ++i)
          {
              distSum += dist2[i];
          }

          // Every point coincides with a chosen centroid: no new distinct centroid exists
          if (distSum == 0.0)
          {
              fprintf(stderr, "Error: k-means++ found only %zu distinct points for K = %zu\n", chosen, numCentroids);
              exit(EXIT_FAILURE);
          }

          // 3. Select a new centroid with probability r
          double r = rng_unit(rng) * distSum;
          double cumulative = 0.0;
          size_t picked = dataPoints->size - 1;   /* fallback to last point */

          for (size_t i = 0; i < dataPoints->size; ++i)
          {
              cumulative += dist2[i];
              if (cumulative >= r)
              {
                  picked = i;
                  break;
              }
          }

          // Debug: If the chosen point is already a centroid, skip and choose again
          if (dist2[picked] == 0.0)
              continue;

          // 4. Add the selected point as the next centroid
          deepCopyDataPoint(&centroids->points[chosen++], &dataPoints->points[picked]);

          /* 5. Update the distance cache:
           *    for every data point, keep the minimum distance to any
           *    centroid selected so far.  Only values that improve
           *    (i.e. become smaller) are updated. */
          for (size_t i = 0; i < dataPoints->size; ++i)
          {
              double d = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[chosen - 1]);
              if (d < dist2[i])
              {
                  dist2[i] = d;
              }
          }
      }

      free(dist2);
  }

  /** @brief Initial centroids of KM, RKM and RS: K distinct random data points, or k-means++ with --init kmeans++. */
  static void initialCentroids(InitMethod init, size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids, Rng* rng)
  {
      if (init == INIT_KMEANSPP)
      {
          generateKMeansPlusPlusCentroids(numCentroids, dataPoints, centroids, rng);
      }
      else
      {
          generateRandomCentroids(numCentroids, dataPoints, centroids, rng);
      }
  }

  /**
   * @brief Sums squared distances from each point to its assigned centroid.
   *
   * Assumes every point has a valid `partition` in `[0, centroids->size)` (asserted).
   *
   * @param dataPoints Dataset whose partitions are already assigned.
   * @param centroids Centroid set referenced by partitions.
   * @return Total SSE across all points.
   */
  double calculateSSE(const DataPoints* dataPoints, const Centroids* centroids)
  {
      double sse = 0.0;

      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          size_t cIndex = dataPoints->points[i].partition;

          assert(cIndex < centroids->size);

          sse += calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[cIndex]);
      }

      return sse;
  }

  /**
   * @brief SSE divided by the number of values (N·D). Requires a non-empty dataset (an empty one is read out of bounds).
   *
   * No call site; kept (Unused code review). The thesis reports SSE and CI only.
   */
  double calculateMSE(const DataPoints* dataPoints, const Centroids* centroids)
  {
      const double sse = calculateSSE(dataPoints, centroids);

      const double mse = sse / ((double)dataPoints->size * (double)dataPoints->points[0].dimensions);

      return mse;
  }

  /**
   * @brief Sums squared distances for points assigned to a specific cluster.
   *
   * Accumulates SSE over points with `partition == clusterLabel`. Returns 0.0 if the
   * cluster has no points.
   *
   * @param dataPoints Dataset with assigned partitions.
   * @param centroids Centroid set; `clusterLabel` must be < `centroids->size` (not checked).
   * @param clusterLabel Target cluster id.
   * @return SSE for the specified cluster.
   */
  double calculateClusterSSE(const DataPoints* dataPoints, const Centroids* centroids, size_t clusterLabel)
  {
      double sse = 0.0;

      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterLabel)
          {
              sse += calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[clusterLabel]);
          }
      }

      return sse;
  }

  /**
   * @brief Index of the nearest centroid by squared Euclidean distance; the first one on ties.
   *
   * Requires at least one centroid (asserted) and the same dimensionality as the point (not checked). O(K·D).
   */
  size_t findNearestCentroid(const DataPoint* queryPoint, const Centroids* targetCentroids)
  {
      assert(targetCentroids->size > 0);

      size_t nearestCentroidId = 0;
      double minDistance = calculateSquaredEuclideanDistance(queryPoint, &targetCentroids->points[0]);

      for (size_t i = 1; i < targetCentroids->size; ++i)
      {
          double d = calculateSquaredEuclideanDistance(queryPoint, &targetCentroids->points[i]);

          if (d < minDistance)
          {
              minDistance = d;
              nearestCentroidId = i;
          }
      }

      return nearestCentroidId;
  }

  /**
   * @brief Assigns each data point to its nearest centroid.
   *
   * Computes nearest-centroid labels for all points using squared Euclidean distance.
   * Requires at least one centroid.
   *
   * @param dataPoints Points to label; updated in-place.
   * @param centroids Current centroids; `size` must be > 0.
   * @return void
   * @errors Asserts that points and centroids exist.
   */
  void partitionStep(DataPoints* dataPoints, const Centroids* centroids)
  {
      assert(dataPoints->size > 0 && centroids->size > 0);

      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          size_t nearestCentroidId = findNearestCentroid(&dataPoints->points[i], centroids);
          dataPoints->points[i].partition = nearestCentroidId;
      }
  }

  /**
   * @brief Recomputes centroids as means of their assigned points.
   *
   * Accumulates per-cluster coordinate sums and divides by counts. Skips clusters with
   * zero assignments (they keep their previous coordinates; level-3 warning). Partitions and
   * dimensionality are asserted (debug builds only).
   *
   * @param centroids Centroids to update in-place; `size` must be > 0.
   * @param dataPoints Dataset with assigned partitions.
   * @return void
   * @errors Asserts valid partitions and matching dimensionality.
   * @note O(N·D + K·D) work; uses a single contiguous buffer for sums.
   */
  void centroidStep(Centroids* centroids, const DataPoints* dataPoints)
  {
      const size_t numClusters = centroids->size;
      const size_t dimensions = dataPoints->points[0].dimensions;

      // Use a single allocation for the sums
      double* sums = calloc(numClusters * dimensions, sizeof(double));
      size_t* counts = calloc(numClusters, sizeof(size_t));
      handleMemoryError(sums);
      handleMemoryError(counts);

      // Accumulate sums and counts for each cluster
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          const DataPoint* point = &dataPoints->points[i];
          const size_t clusterLabel = point->partition;
          assert(clusterLabel < numClusters && point->dimensions == dimensions);

          for (size_t dim = 0; dim < dimensions; ++dim)
          {
              sums[clusterLabel * dimensions + dim] += point->attributes[dim];
          }
          counts[clusterLabel]++;
      }

      // Update the centroids
      for (size_t clusterLabel = 0; clusterLabel < numClusters; ++clusterLabel)
      {
          if (counts[clusterLabel] > 0)
          {
              for (size_t dim = 0; dim < dimensions; ++dim)
              {
                  centroids->points[clusterLabel].attributes[dim] =
                      sums[clusterLabel * dimensions + dim] / counts[clusterLabel];
              }
          }
          else if (logLevel >= 3)
          {
              fprintf(stderr, "Warning: Cluster %zu has no points assigned.\n", clusterLabel);
          }
      }

      free(sums);
      free(counts);
  }

  /**
   * @brief Counts centroids in `centroids2` with no nearest centroid from `centroids1`.
   *
   * For each centroid in `centroids1`, marks its nearest neighbor in `centroids2`.
   * Returns how many centroids in `centroids2` were never marked. Both sets must be
   * non-empty and of the same dimensionality (neither is checked; callers check at the input boundary).
   *
   * @param centroids1 Reference set used to mark nearest neighbors.
   * @param centroids2 Target set whose unmarked count is returned.
   * @return Number of orphan centroids in `centroids2`.
   */
  size_t countOrphans(const Centroids* centroids1, const Centroids* centroids2)
  {
      size_t countFrom1to2 = 0;
      bool* hasClosest = calloc(centroids2->size, sizeof(bool));
      handleMemoryError(hasClosest);

      for (size_t i = 0; i < centroids1->size; ++i)
      {
          size_t closestIndex = findNearestCentroid(&centroids1->points[i], centroids2);
          hasClosest[closestIndex] = true;
      }

      for (size_t i = 0; i < centroids2->size; ++i)
      {
          if (!hasClosest[i])
          {
              countFrom1to2++;
          }
      }

      free(hasClosest);

      return countFrom1to2;
  }

  /**
   * @brief Centroid Index (CI) as max orphan count between two centroid sets.
   *
   * CI = max(orphans(centroids1 → centroids2), orphans(centroids2 → centroids1)),
   * where orphans(X→Y) is the number of centroids in Y with no centroid from X
   * selecting them as nearest. Both sets must be non-empty and of the same dimensionality
   * (not checked here). O(K^2·D).
   */
  size_t calculateCentroidIndex(const Centroids* centroids1, const Centroids* centroids2)
  {
      if (logLevel >= 3)
      {
          printf("centroids1: %zu AND 2: %zu\n\n", centroids1->size, centroids2->size);
          if (centroids1->size > 0 && centroids2->size > 0)
          {
              printf("centroids1: %f AND 2: %f\n\n", centroids1->points->attributes[0], centroids2->points->attributes[0]);
          }
      }

      size_t countFrom1to2 = countOrphans(centroids1, centroids2);
      size_t countFrom2to1 = countOrphans(centroids2, centroids1);

      if (logLevel >= 3)
      {
          printf("Count from 1 to 2: %zu AND 2 to 1: %zu\n\n", countFrom1to2, countFrom2to1);
      }

      return (countFrom1to2 > countFrom2to1) ? countFrom1to2 : countFrom2to1;
  }

  /** @brief True when ground-truth centroids are loaded, so CI can be computed. */
  static bool hasGroundTruth(const Centroids* groundTruth)
  {
      return groundTruth != NULL && groundTruth->size > 0;
  }

  /** @brief CI against the ground truth, or CI_NA when there is none. */
  static size_t centroidIndexOrNA(const Centroids* centroids, const Centroids* groundTruth)
  {
      return hasGroundTruth(groundTruth) ? calculateCentroidIndex(centroids, groundTruth) : CI_NA;
  }

  /**
   * @brief Writes a snapshot: `<algo>_run<RRR>_iter<IIII>_<phase>_centroids.txt` and `..._partitions.txt` in `outputDirectory`.
   *
   * `run` is 1-based and `iteration` is the count of `_log.csv`; both are zero-padded (3 and 4 digits) so the
   * names sort in order. Phases: start, split (after the two-way split), step, final (after the final k-means).
   * Centroids: one space-separated row per centroid; partitions: one 0-based label per line.
   */
  void saveIterationState(const DataPoints* dataPoints, const Centroids* centroids,
      size_t run, size_t iteration, const char* phase, const char* outputDirectory, const char* algorithmName)
  {
      char centroidsFileName[PATH_MAX];
      char partitionsFileName[PATH_MAX];

      if (snprintf(centroidsFileName, sizeof(centroidsFileName), "%s_run%03zu_iter%04zu_%s_centroids.txt",
              algorithmName, run, iteration, phase) >= (int)sizeof(centroidsFileName)
          || snprintf(partitionsFileName, sizeof(partitionsFileName), "%s_run%03zu_iter%04zu_%s_partitions.txt",
              algorithmName, run, iteration, phase) >= (int)sizeof(partitionsFileName))
      {
          fprintf(stderr, "Buffer too small in saveIterationState\n");
          exit(EXIT_FAILURE);
      }

      writeCentroidsToFile(centroidsFileName, centroids, outputDirectory);
      writeDataPointPartitionsToFile(partitionsFileName, dataPoints, outputDirectory);
  }

  /**
   * @brief Appends one row to `<outputDirectory>/<algorithmName>_iteration_stats.txt`, with a header when the file is new.
   *
   * One row per snapshot. Columns: Run;Iteration;Phase;NumCentroids;SSE;CI;SplitCluster;MS (run 1-based;
   * CI is NA without ground truth; SplitCluster is SIZE_MAX when none).
   */
  void writeIterationStats(const Centroids* centroids, size_t ci, size_t run, size_t iteration, const char* phase,
      double sse, size_t splitCluster, double elapsedMs, const char* outputDirectory, const char* algorithmName)
  {
      char statsFileName[PATH_MAX];
      if (snprintf(statsFileName, sizeof(statsFileName), "%s_iteration_stats.txt", algorithmName) >= (int)sizeof(statsFileName))
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
      size_t run, size_t iteration, const char* phase, size_t clusterToSplit, Algorithm splitType, const char* outputDirectory, double elapsedMs)
  {
      const char* splitTypeName = getAlgorithmName(splitType);

      writeIterationStats(centroids, ci, run, iteration, phase, sse, clusterToSplit, elapsedMs, outputDirectory, splitTypeName);
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
      case KM_STAGE_SPLIT: return "split";
      case KM_STAGE_TRY: return "try";
      case KM_STAGE_GLOBAL: return "global";
      case KM_STAGE_FINAL: return "final";
      default: return "none";
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
          fprintf(file, "%zu;%zu;%s;%zu;%zu;%s;%s;%s\n", run + 1, row->count, kmeansStageName(row->stage),
              row->iteration, row->k, formatFixed(row->sse, 6, sseText), formatCi(row->ci, ciText), formatFixed(row->ms, 3, msText));
      }
      fclose(file);
  }

  /**
   * @brief Tracker for one algorithm with the switches in `options`.
   *
   * `outputDirectory`, `groundTruth` and `options` must outlive the tracker. Free with trackerFree.
   */
  static Tracker trackerCreate(Algorithm algorithm, const char* outputDirectory, const Centroids* groundTruth, const TrackOptions* options)
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
  static void trackerFree(Tracker* tracker)
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
  static void trackerBeginRun(Tracker* tracker, size_t run)
  {
      tracker->run = run;
      tracker->snapshots = tracker->options->snapshots.enabled && selectionContains(&tracker->options->snapshots.runs, run + 1);
      tracker->nextCount = 0;
      tracker->rowCount = 0;
      tracker->innerCount = 0;
      stopwatchStart(&tracker->stopwatch);
  }

  /** @brief Stops the stopwatch, appends the buffered rows to `_log.csv` and `_inner.csv` and returns the run time in ms. */
  static double trackerEndRun(Tracker* tracker)
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
  static void trackerAddRow(Tracker* tracker, size_t count, double sse, size_t ci)
  {
      if (!tracker->logRows)
      {
          return;
      }
      stopwatchPause(&tracker->stopwatch);
      trackerPushRow(tracker, count, sse, ci, stopwatchElapsedMs(&tracker->stopwatch));
      stopwatchResume(&tracker->stopwatch);
  }

  /** @brief CI of `centroids` against the ground truth, computed with the stopwatch paused; CI_NA without ground truth. */
  static size_t trackerCi(Tracker* tracker, const Centroids* centroids)
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
   * SSE and CI are computed with the stopwatch paused. Returns the CI, or CI_NA when nothing is recorded or there is no ground truth.
   * `phase` names the snapshot (start, step or final); `splitCluster` is the cluster split at this count, SIZE_MAX when none.
   */
  static size_t trackerRecord(Tracker* tracker, size_t count, const char* phase, const DataPoints* dataPoints, const Centroids* centroids, size_t splitCluster)
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

  /** @brief When selected, writes the snapshot files and iteration stats of the state at `count`, without a `_log.csv` row. */
  static void trackerSnapshot(Tracker* tracker, size_t count, const char* phase, const DataPoints* dataPoints, const Centroids* centroids, size_t splitCluster)
  {
      if (!trackerSnapshotWanted(tracker, count))
      {
          return;
      }

      stopwatchPause(&tracker->stopwatch);
      const double ms = stopwatchElapsedMs(&tracker->stopwatch);
      const double sse = calculateSSE(dataPoints, centroids);
      const size_t ci = centroidIndexOrNA(centroids, tracker->groundTruth);
      trackProgressState(dataPoints, centroids, sse, ci, tracker->run + 1, count, phase, splitCluster,
          tracker->algorithm, tracker->outputDirectory, ms);
      stopwatchResume(&tracker->stopwatch);
  }

  /**
   * @brief With `--snapshots ...:split`, writes the state after the two-way split of `splitCluster`, before the follow-up step.
   *
   * Phase "split" at the count of the split being made. NULL tracker: no-op.
   */
  static void trackerSplitSnapshot(Tracker* tracker, const DataPoints* dataPoints, const Centroids* centroids, size_t splitCluster)
  {
      if (tracker != NULL && tracker->options->snapshots.split)
      {
          trackerSnapshot(tracker, tracker->nextCount, "split", dataPoints, centroids, splitCluster);
      }
  }

  /**
   * @brief With --track-inner, buffers the `_inner.csv` row of iteration `iteration` of a k-means run at `stage`.
   *
   * `sse` is that run's SSE after the iteration. CI only for the global and final stages. NULL tracker or KM_STAGE_NONE: no-op.
   */
  static void trackerInnerRow(Tracker* tracker, KMeansStage stage, size_t iteration, const Centroids* centroids, double sse)
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
      row->ci = (stage == KM_STAGE_GLOBAL || stage == KM_STAGE_FINAL) ? centroidIndexOrNA(centroids, tracker->groundTruth) : CI_NA;
      row->ms = stopwatchElapsedMs(&tracker->stopwatch);
      stopwatchResume(&tracker->stopwatch);
  }

  /**
   * @brief Random Swap local repartition after one centroid moved, in two passes.
   *
   * Pass 1: every point of the swapped cluster (`removed`) moves to its nearest centroid. Pass 2: every other
   * point moves to the swapped centroid when it is closer than its own. Other points keep their labels.
   *
   * @param dataPoints Dataset with current partitions.
   * @param centroids Centroids after the swap; index `removed` denotes the modified centroid.
   * @param removed Index of the centroid whose coordinates were changed; must be < centroids->size (not checked).
   * @note O(N·D). The centroids are not updated here (the step-4 update is commented out, 6.4); the two k-means iterations follow.
   */
  void localRepartitionForRS(DataPoints* dataPoints, Centroids* centroids, size_t removed)
  {
      // from removed -> to existing
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == removed)
          {
              size_t nearestCentroid = findNearestCentroid(&dataPoints->points[i], centroids);
              dataPoints->points[i].partition = nearestCentroid;
          }
      }

      // from existing -> to created
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition != removed)
          {
              size_t currentPartition = dataPoints->points[i].partition;

              // Calculate distance to the current centroid
              double distanceToCurrent = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[currentPartition]);

              // Calculate distance to the removed centroid
              double distanceToRemoved = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[removed]);

              // Assign to the closer centroid
              if (distanceToRemoved < distanceToCurrent)
              {
                  dataPoints->points[i].partition = removed;
              }
          }
      }

      //centroidStep(centroids, dataPoints);
  }

  /**
   * @brief k-means: partition and centroid steps until the SSE does not strictly decrease or after `iterations` iterations.
   *
   * @param dataPoints Points to cluster; partitions updated in place.
   * @param centroids Initial centroids; updated in place.
   * @param tracker Optional (NULL for trial splits): ground truth for the level-3 CI, and the `_inner.csv` rows of `stage`.
   * @param stage Which k-means this is for --track-inner; KM_STAGE_NONE logs no rows.
   * @return Best SSE observed during the run.
   */
  double runKMeans(DataPoints* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker, KMeansStage stage)
  {
      double bestSse = DBL_MAX;
      double sse = DBL_MAX;
      const Centroids* groundTruth = tracker != NULL ? tracker->groundTruth : NULL;

      for (size_t iteration = 0; iteration < iterations; ++iteration)
      {
          partitionStep(dataPoints, centroids);
          centroidStep(centroids, dataPoints);

          sse = calculateSSE(dataPoints, centroids);

          trackerInnerRow(tracker, stage, iteration + 1, centroids, sse);

          if (logLevel >= 3)
          {
              // CI only for full-size solutions: local 2-means and partial global steps do not match the ground truth
              if (hasGroundTruth(groundTruth) && centroids->size == groundTruth->size)
              {
                  size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
                  printf("(runKMeans)After iteration %zu: CI = %zu and SSE = %.0f\n", iteration + 1, centroidIndex, sse);
              }
              else
              {
                  printf("(runKMeans)After iteration %zu: SSE = %.0f\n", iteration + 1, sse);
              }
          }

          if (sse < bestSse)
          {
              if (logLevel >= 3) printf("Best SSE so far: %.5f\n", sse);
              bestSse = sse;
          }
          else
          {
              break; // Exit the loop if the SSE does not improve
          }
      }

      return bestSse;
  }

  /**
   * @brief k-means that records the state after every iteration with the tracker (count = iteration, 1-based).
   *
   * Runs the same steps as `runKMeans`, so the result is identical; stops when the SSE does not
   * strictly decrease or after `iterations` iterations.
   *
   * @param dataPoints Dataset to cluster; partitions updated in place.
   * @param centroids Centroid set updated in place.
   * @param tracker Rows, snapshots and the ground truth for the level-3 CI.
   * @return Best SSE observed during the run.
   */
  double runKMeansWithTracking(DataPoints* dataPoints, size_t iterations, Centroids* centroids, Tracker* tracker)
  {
      double bestSse = DBL_MAX;
      double sse = DBL_MAX;
      const Centroids* groundTruth = tracker->groundTruth;

      for (size_t iteration = 0; iteration < iterations; ++iteration)
      {
          partitionStep(dataPoints, centroids);
          centroidStep(centroids, dataPoints);

          trackerRecord(tracker, iteration + 1, "step", dataPoints, centroids, SIZE_MAX);

          sse = calculateSSE(dataPoints, centroids);

          if (logLevel >= 3)
          {
              if (hasGroundTruth(groundTruth) && centroids->size == groundTruth->size)
              {
                  size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
                  printf("(runKMeans)After iteration %zu: CI = %zu and SSE = %.0f\n", iteration + 1, centroidIndex, sse);
              }
              else
              {
                  printf("(runKMeans)After iteration %zu: SSE = %.0f\n", iteration + 1, sse);
              }
          }

          if (sse < bestSse)
          {
              if (logLevel >= 3) printf("Best SSE so far: %.5f\n", sse);
              bestSse = sse;
          }
          else
          {
              break; // Exit the loop if the SSE does not improve
          }
      }

      return bestSse;
  }

  /**
   * @brief Random Swap: moves a random centroid to a random data point, runs two k-means iterations and keeps the swap if the SSE improves.
   *
   * A rejected swap restores the centroids and partitions. The tracker records the initial
   * solution (count 0) and every improvement (count = swap number); CI is computed only for
   * the tracker and the level-2 log, with the stopwatch paused.
   *
   * @param dataPoints Dataset; its partitions must belong to `centroids` on entry.
   * @param centroids Initial centroids; updated in place to the best solution.
   * @param maxSwaps Number of trial swaps.
   * @return The best SSE found.
   */
  double randomSwap(DataPoints* dataPoints, Centroids* centroids, size_t maxSwaps, Tracker* tracker, Rng* rng)
  {
      double bestSse = DBL_MAX;
      size_t kMeansIterations = 2;
      size_t numCentroids = centroids->size;
      size_t dimensions = centroids->points[0].dimensions;

      size_t totalAttributes = numCentroids * dimensions;

      double* backupAttributes = malloc(totalAttributes * sizeof(double));
      handleMemoryError(backupAttributes);

      size_t* backupPartitions = malloc(dataPoints->size * sizeof(size_t));
      handleMemoryError(backupPartitions);

      size_t bestCI = trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);

      for (size_t i = 0; i < maxSwaps; ++i)
      {
          if (logLevel >= 3) printf("Swap %zu\n", i + 1);

          //Backup
          size_t offset = 0;
          for (size_t j = 0; j < numCentroids; ++j)
          {
              memcpy(&backupAttributes[offset], centroids->points[j].attributes, dimensions * sizeof(double));
              offset += dimensions;
          }


          for (size_t j = 0; j < dataPoints->size; ++j)
          {
              backupPartitions[j] = dataPoints->points[j].partition;
          }

          //Swap
          size_t randomCentroidId = rng_below(rng, centroids->size);
          size_t randomDataPointId = rng_below(rng, dataPoints->size);

          memcpy(centroids->points[randomCentroidId].attributes, dataPoints->points[randomDataPointId].attributes, dimensions * sizeof(double));

          localRepartitionForRS(dataPoints, centroids, randomCentroidId);

          //K-means
          double resultSse = runKMeans(dataPoints, kMeansIterations, centroids, tracker, KM_STAGE_NONE);

          //If 1) SSE improves, we keep the change
          //if not, 2) we reverse the swap
          if (resultSse < bestSse)
          {
              bestSse = resultSse;

              // Row and snapshot for each improvement, with its swap number (decided 2026-11-19)
              bestCI = trackerRecord(tracker, i + 1, "step", dataPoints, centroids, SIZE_MAX);

              // Alternative: the row without the snapshot files
              //trackerAddRow(tracker, i + 1, resultSse, trackerCi(tracker, centroids));

              if (logLevel >= 2)
              {
                  if (bestCI == CI_NA) bestCI = trackerCi(tracker, centroids);
                  char ciText[CI_TEXT_SIZE];
                  printf("(RS) Swap %zu: Best CI: %s and Best SSE: %.5f\n", i + 1, formatCi(bestCI, ciText), resultSse);
              }
          }
          else
          {
              offset = 0;
              for (size_t j = 0; j < numCentroids; ++j)
              {
                  memcpy(centroids->points[j].attributes, &backupAttributes[offset], dimensions * sizeof(double));
                  offset += dimensions;
              }

              for (size_t j = 0; j < dataPoints->size; ++j)
              {
                  dataPoints->points[j].partition = backupPartitions[j];
              }

              // Alternative (v1): one row per swap; a rejected swap repeats the best solution so far
              //trackerAddRow(tracker, i + 1, bestSse, bestCI);

              // Alternative: row and snapshot also for a rejected swap (the restored best solution)
              //trackerRecord(tracker, i + 1, "step", dataPoints, centroids, SIZE_MAX);
          }
      }

      free(backupAttributes);
      free(backupPartitions);

      return bestSse;
  }

  /**
   * @brief Splits one cluster by running a local k-means within that cluster.
   *
   * Picks two different points of the target cluster as seeds (different indices; equal coordinates
   * are possible, 6.6), runs k-means (K=2) on that cluster's points only, then replaces the old centroid
   * with the first local centroid and appends the second as a new global centroid. Partitions for the
   * affected points are rewritten accordingly.
   *
   * @param dataPoints All points with current partitions.
   * @param centroids Global centroids; updated in-place (size increases by one).
   * @param clusterToSplit Index of the cluster to split.
   * @param localMaxIterations Max iterations for the local k-means refinement.
   * @param tracker Optional: `_inner.csv` rows of the local 2-means (stage split) and the ground truth for the level-3 log.
   * @errors Exits on allocation failure. Asserts that the cluster has at least 2 points.
   */
  void splitClusterIntraCluster(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t localMaxIterations, Tracker* tracker, Rng* rng)
  {
      size_t clusterSize = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterSize++;
          }
      }

      // The selection only picks clusters with at least 2 points
      assert(clusterSize >= 2);

      // Collect indices of points in the cluster
      size_t* clusterIndices = malloc(clusterSize * sizeof(size_t));
      handleMemoryError(clusterIndices);

      size_t index = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterIndices[index++] = i;
          }
      }

      // Random centroids
      size_t c1 = rng_below(rng, clusterSize);
      size_t c2 = c1;
      while (c2 == c1)
      {
          c2 = rng_below(rng, clusterSize);
      }

      size_t datapoint1 = clusterIndices[c1];
      size_t datapoint2 = clusterIndices[c2];

      // Initialize local centroids
      Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);

      deepCopyDataPoint(&localCentroids.points[0], &dataPoints->points[datapoint1]);
      deepCopyDataPoint(&localCentroids.points[1], &dataPoints->points[datapoint2]);

      // Prepare data points in the cluster
      DataPoints pointsInCluster;
      pointsInCluster.size = clusterSize;
      pointsInCluster.points = malloc(clusterSize * sizeof(DataPoint));
      handleMemoryError(pointsInCluster.points);
      for (size_t i = 0; i < clusterSize; ++i)
      {
          pointsInCluster.points[i] = dataPoints->points[clusterIndices[i]];
      }

      //partition step
      partitionStep(&pointsInCluster, &localCentroids);

      // Run local k-means
      runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, tracker, KM_STAGE_SPLIT);

      // Update partitions
      for (size_t i = 0; i < clusterSize; ++i)
      {
          size_t originalIndex = clusterIndices[i];
          dataPoints->points[originalIndex].partition = (pointsInCluster.points[i].partition == 0) ? clusterToSplit : centroids->size;
      }

      // Update centroids
      //#1
      deepCopyDataPoint(&centroids->points[clusterToSplit], &localCentroids.points[0]);

      //#2
      centroids->size++;
      centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint));
      handleMemoryError(centroids->points);
      centroids->points[centroids->size - 1] = allocateDataPoint(dataPoints->points[0].dimensions);
      deepCopyDataPoint(&centroids->points[centroids->size - 1], &localCentroids.points[1]);


      // Cleanup
      free(clusterIndices);
      free(pointsInCluster.points);
      freeCentroids(&localCentroids);
  }

  /**
   * @brief SKM-Global split: local 2-means within the chosen cluster, then global k-means over all data.
   *
   * The two local seeds are two different points of the cluster (different indices; equal coordinates are
   * possible, 6.6). The local result replaces the old centroid and is appended as the new one; global k-means
   * then runs until the SSE stops improving (localMaxIterations is SIZE_MAX by default).
   *
   * @param dataPoints All points with current partitions.
   * @param centroids Global centroids; updated in place (size increases by one).
   * @param clusterToSplit Index of the cluster to split; it has at least 2 points (asserted).
   * @param localMaxIterations Max iterations for both the local and the global k-means.
   * @param tracker Optional: the after-split snapshot, the `_inner.csv` rows (stages split and global) and the ground truth for the level-3 log.
   * @return SSE after the global k-means.
   */
  double splitClusterGlobalV2(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t localMaxIterations, Tracker* tracker, Rng* rng)
  {
      size_t clusterSize = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterSize++;
          }
      }

      // The selection only picks clusters with at least 2 points
      assert(clusterSize >= 2);

      // Collect indices of points in the cluster
      size_t* clusterIndices = malloc(clusterSize * sizeof(size_t));
      handleMemoryError(clusterIndices);

      size_t index = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterIndices[index++] = i;
          }
      }

      // Random centroids
      size_t c1 = rng_below(rng, clusterSize);
      size_t c2 = c1;
      while (c2 == c1)
      {
          c2 = rng_below(rng, clusterSize);
      }

      size_t datapoint1 = clusterIndices[c1];
      size_t datapoint2 = clusterIndices[c2];

      // Initialize local centroids
      Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);

      deepCopyDataPoint(&localCentroids.points[0], &dataPoints->points[datapoint1]);
      deepCopyDataPoint(&localCentroids.points[1], &dataPoints->points[datapoint2]);

      // Prepare data points in the cluster
      DataPoints pointsInCluster;
      pointsInCluster.size = clusterSize;
      pointsInCluster.points = malloc(clusterSize * sizeof(DataPoint));
      handleMemoryError(pointsInCluster.points);
      for (size_t i = 0; i < clusterSize; ++i)
      {
          pointsInCluster.points[i] = dataPoints->points[clusterIndices[i]];
      }

      // Run local k-means
      runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, tracker, KM_STAGE_SPLIT);

      // Update partitions
      for (size_t i = 0; i < clusterSize; ++i)
      {
          size_t originalIndex = clusterIndices[i];
          dataPoints->points[originalIndex].partition = (pointsInCluster.points[i].partition == 0) ? clusterToSplit : centroids->size;
      }

      // Update centroids
      //#1
      deepCopyDataPoint(&centroids->points[clusterToSplit], &localCentroids.points[0]);

      //#2
      centroids->size++;
      centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint));
      handleMemoryError(centroids->points);
      centroids->points[centroids->size - 1] = allocateDataPoint(dataPoints->points[0].dimensions);
      deepCopyDataPoint(&centroids->points[centroids->size - 1], &localCentroids.points[1]);

      trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

      double sse = runKMeans(dataPoints, localMaxIterations, centroids, tracker, KM_STAGE_GLOBAL);


      // Cleanup
      free(clusterIndices);
      free(pointsInCluster.points);
      freeCentroids(&localCentroids);

      return sse;
  }

  /**
   * @brief SKM-Global-Basic split: two random points of the chosen cluster as centroids, then global k-means.
   *
   * Selects two different points of the cluster (different indices), overwrites the original centroid
   * with the first, appends the second as a new centroid, then runs global k-means for
   * `globalMaxIterations` to refine assignments over all data.
   *
   * @param dataPoints All points with current partitions.
   * @param centroids Global centroids; updated in-place (size increases by one).
   * @param clusterToSplit Index of the cluster to split.
   * @param globalMaxIterations Max iterations for the global refinement.
   * @param tracker Optional: the after-split snapshot, the `_inner.csv` rows (stage global) and the ground truth for the level-3 log.
   * @return SSE after global refinement.
   * @errors Exits on allocation failure. Asserts that the cluster has at least 2 points.
   * @note Runs as --algo skm-global-basic, the basic comparison for SKM-Global: global k-means starts right after
   *       the two random seeds, without the local 2-means. Whether this gives worse results is measured in 6.7.
   */
  double splitClusterGlobal(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t globalMaxIterations, Tracker* tracker, Rng* rng)
  {
      size_t clusterSize = 0;

      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterSize++;
          }
      }

          // The selection only picks clusters with at least 2 points
          assert(clusterSize >= 2);

      // Collect the indices of the points in the selected cluster
      size_t* clusterIndices = malloc(clusterSize * sizeof(size_t));
      handleMemoryError(clusterIndices);

      size_t index = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterIndices[index++] = i;
          }
      }

      // Select two random points from the cluster to be the new centroids
      size_t c1 = rng_below(rng, clusterSize);
      size_t c2 = c1;
      while (c2 == c1)
      {
          c2 = rng_below(rng, clusterSize);
      }

      // Get the indices of the randomly selected data points
      size_t datapoint1 = clusterIndices[c1];
      size_t datapoint2 = clusterIndices[c2];

      // Add the first new centroid (overwrite the current centroid at clusterToSplit)
      deepCopyDataPoint(&centroids->points[clusterToSplit], &dataPoints->points[datapoint1]);

      // Add the second new centroid to the global centroids list
      centroids->size++;
      centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint));
      handleMemoryError(centroids->points);
      centroids->points[centroids->size - 1] = allocateDataPoint(dataPoints->points[0].dimensions);
      deepCopyDataPoint(&centroids->points[centroids->size - 1], &dataPoints->points[datapoint2]);

      trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

      // Run global k-means, this time including the new centroids
      double resultSse = runKMeans(dataPoints, globalMaxIterations, centroids, tracker, KM_STAGE_GLOBAL);

      // Cleanup
      free(clusterIndices);

      return resultSse;
  }

  /**
   * @brief SKM-Local repartition after a split, in two passes over the same centroids.
   *
   * Pass 1: every point of the split cluster and of the new cluster moves to its nearest centroid among all
   * centroids. Pass 2: every other point moves to the split or the new centroid when one of them is closer than
   * its own (the nearer of the two). Pass 1 marks both clusters of a moved point in `clustersAffected`, pass 2
   * the cluster it left (the caller marks the split and the new cluster). No centroid is recomputed here (6.1).
   *
   * @param dataPoints Dataset with current partitions.
   * @param centroids Global centroids; the new centroid is at index `centroids->size - 1`.
   * @param clusterToSplit Index of the cluster that was split.
   * @param clustersAffected Output array (size >= number of clusters) marking touched clusters.
   * @note O(N·D); indices are not checked.
   */
  void localRepartition(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, bool* clustersAffected)
  {
      size_t newClusterIndex = centroids->size - 1;

      // new clusters -> old clusters
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit || dataPoints->points[i].partition == newClusterIndex)
          {
              size_t nearestCentroid = findNearestCentroid(&dataPoints->points[i], centroids);

              if (dataPoints->points[i].partition != nearestCentroid)
              {
                  clustersAffected[dataPoints->points[i].partition] = true; // Mark the old cluster as affected
                  clustersAffected[nearestCentroid] = true;                 // Mark the new cluster as affected
                  dataPoints->points[i].partition = nearestCentroid;
              }
          }
      }

      // old clusters -> new clusters
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          size_t currentCluster = dataPoints->points[i].partition;

          // skip new cluster -> old cluster
          if (currentCluster == clusterToSplit || currentCluster == newClusterIndex)
              continue;

          // Calculate distance to the current centroid
          double currentDistance = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[currentCluster]);

          // Calculate distance to the cluster to be split
          double distanceToSplit = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[clusterToSplit]);

          // Calculate distance to the new cluster
          double distanceToNew = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[newClusterIndex]);

          if (distanceToSplit < currentDistance || distanceToNew < currentDistance)
          {
              clustersAffected[currentCluster] = true;    // Mark the old cluster as affected
              dataPoints->points[i].partition = distanceToSplit <= distanceToNew ? clusterToSplit : newClusterIndex;
          }
      }
  }

  /**
   * @brief Estimates SSE improvement from splitting one cluster with local k-means.
   *
   * Copies the points of `clusterLabel`, initializes two seeds from that set,
   * runs k-means with K=2 locally, and returns the decrease relative to the
   * original single-cluster SSE (`originalClusterSSE - localSseAfterSplit`).
   *
   * @param dataPoints Dataset with current partitions; used to extract the cluster’s points.
   * @param clusterLabel Cluster id to test a tentative split for.
   * @param localMaxIterations Max iterations for the local k-means refinement.
   * @param originalClusterSSE Current SSE of the cluster before splitting.
   * @return SSE drop (non-negative when the split helps); 0 for a cluster with fewer than 2 points.
   * @errors Exits on allocation failure.
   */
  double tentativeSseDrop(DataPoints* dataPoints, size_t clusterLabel, size_t localMaxIterations, double originalClusterSSE, Rng* rng)
  {
      size_t clusterSize = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterLabel)
          {
              clusterSize++;
          }
      }

      if (clusterSize < 2)
      {
          return 0.0;
      }

      DataPoints pointsInCluster = allocateDataPoints(clusterSize, dataPoints->points[0].dimensions);

      size_t index = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterLabel)
          {
              deepCopyDataPoint(&pointsInCluster.points[index], &dataPoints->points[i]);
              index++;
          }
      }

      // Random centroids
      size_t c1 = rng_below(rng, clusterSize);
      size_t c2 = c1;
      while (c2 == c1)
      {
          c2 = rng_below(rng, clusterSize);
      }

      Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);

      deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[c1]);
      deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[c2]);

      // k-means
      double resultSse = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, NULL, KM_STAGE_NONE);

      double sseDrop = originalClusterSSE - resultSse;

      freeDataPoints(&pointsInCluster);
      freeCentroids(&localCentroids);

      return sseDrop;
  }

  /**
   * @brief One bisecting try: local k-means with K=2 on the points of `clusterLabel`.
   *
   * The seeds are two different points of the cluster (different indices, drawn uniformly). Returns the two
   * centroids and the local SSE; the caller keeps the try with the lowest SSE and frees each result.
   *
   * @param dataPoints Dataset with current partitions; used to extract the cluster’s points.
   * @param clusterLabel Cluster id to bisect.
   * @param localMaxIterations Max iterations for the local k-means refinement.
   * @param tracker Optional: `_inner.csv` rows of the try (stage try) and the ground truth for the level-3 log.
   * @return ClusteringResult with `centroids[2]` and `sse` for the cluster’s points.
   * @errors Exits on allocation failure. Asserts that the cluster has at least 2 points.
   */
  ClusteringResult tentativeSplitterForBisecting(DataPoints* dataPoints, size_t clusterLabel, size_t localMaxIterations, Tracker* tracker, Rng* rng)
  {
      size_t clusterSize = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterLabel)
          {
              clusterSize++;
          }
      }

      // The selection only picks clusters with at least 2 points
      assert(clusterSize >= 2);

      DataPoints pointsInCluster = allocateDataPoints(clusterSize, dataPoints->points[0].dimensions);

      size_t index = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterLabel)
          {
              deepCopyDataPoint(&pointsInCluster.points[index], &dataPoints->points[i]);
              index++;
          }
      }

      // Random centroids
      size_t c1 = rng_below(rng, clusterSize);
      size_t c2 = c1;
      while (c2 == c1)
      {
          c2 = rng_below(rng, clusterSize);
      }

      Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);
      deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[c1]);
      deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[c2]);

      ClusteringResult localResult = allocateClusteringResult(2, dataPoints->points[0].dimensions);

      // k-means
      localResult.sse = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, tracker, KM_STAGE_TRY);

      deepCopyDataPoint(&localResult.centroids[0], &localCentroids.points[0]);
      deepCopyDataPoint(&localResult.centroids[1], &localCentroids.points[1]);

      freeDataPoints(&pointsInCluster);
      freeCentroids(&localCentroids);;

      return localResult;
  }

  /**
   * @brief Counts the points of clusters 0..numClusters-1 into `sizes`; returns how many have at least 2 points.
   */
  static size_t countClusterSizes(const DataPoints* dataPoints, size_t numClusters, size_t* sizes)
  {
      memset(sizes, 0, numClusters * sizeof(size_t));
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          assert(dataPoints->points[i].partition < numClusters);
          sizes[dataPoints->points[i].partition]++;
      }

      size_t splittable = 0;
      for (size_t c = 0; c < numClusters; ++c)
      {
          if (sizes[c] >= 2) splittable++;
      }
      return splittable;
  }

  /**
   * @brief Exits when no cluster has 2 or more points. Cannot happen while K <= N; reports a bug instead of hanging.
   */
  static void requireSplittableCluster(size_t splittable, size_t numClusters, Algorithm splitType)
  {
      if (splittable == 0)
      {
          fprintf(stderr, "Error: %s found no cluster with 2 or more points to split (K=%zu)\n",
              getAlgorithmName(splitType), numClusters);
          exit(EXIT_FAILURE);
      }
  }

  /**
   * @brief Random Split: grows K by splitting a random cluster with local k-means.
   *
   * Each split draws a cluster uniformly and draws again while it has fewer than 2 points, splits it
   * with splitClusterIntraCluster and records the state. Stops when `centroids->size == maxCentroids`,
   * then runs a final global k-means and returns its SSE.
   *
   * @param dataPoints Dataset; partitions updated in-place across splits.
   * @param centroids Global centroids; grows until `maxCentroids`.
   * @param maxCentroids Target number of centroids (K).
   * @param maxIterations Max iterations for local/final k-means refinements.
   * @param tracker Records each split (count = split number) and the final k-means; carries the ground truth for CI.
   * @return Final SSE after the concluding global k-means.
   * @errors Exits on allocation failure in underlying functions.
   */
  double runRandomSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, Tracker* tracker, Rng* rng)
  {
      const Centroids* groundTruth = tracker->groundTruth;
      trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);

      size_t iterationCount = 1; //Helper for tracking progress

      size_t* clusterSizes = malloc(maxCentroids * sizeof(size_t));
      handleMemoryError(clusterSizes);

      while (centroids->size < maxCentroids)
      {
          // Uniform over the clusters with at least 2 points: draw again on smaller ones
          requireSplittableCluster(countClusterSizes(dataPoints, centroids->size, clusterSizes), centroids->size, ALGO_SKM_RANDOM);
          size_t clusterToSplit = rng_below(rng, centroids->size);
          while (clusterSizes[clusterToSplit] < 2)
          {
              clusterToSplit = rng_below(rng, centroids->size);
          }

          splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, tracker, rng);

          if (logLevel >= 3)
          {
              double sse = calculateSSE(dataPoints, centroids);
              if (hasGroundTruth(groundTruth))
              {
                  size_t ci = calculateCentroidIndex(centroids, groundTruth);
                  printf("(RandomSplit) Number of centroids: %zu, CI: %zu, and SSE: %.0f \n", centroids->size, ci, sse);
              }
              else
              {
                  printf("(RandomSplit) Number of centroids: %zu, SSE: %.0f \n", centroids->size, sse);
              }
          }

          trackerRecord(tracker, iterationCount, "step", dataPoints, centroids, clusterToSplit);

          iterationCount++;
      }

      free(clusterSizes);

      double finalResultSse = runKMeans(dataPoints, maxIterations, centroids, tracker, KM_STAGE_FINAL);

      trackerRecord(tracker, iterationCount, "final", dataPoints, centroids, SIZE_MAX);

      return finalResultSse;
  }

  /**
   * @brief SSE-based split k-means: grows K by splitting the cluster with the largest tentative SSE drop.
   *
   * The first split of the single starting cluster uses local 2-means for every variant. Then each split picks,
   * among the clusters with at least 2 points, the one whose tentative local 2-means lowers its SSE the most, and
   * splits it by the variant:
   *  - ALGO_SKM_INTRA: local 2-means on the cluster; the drops of the two halves are re-evaluated.
   *  - ALGO_SKM_GLOBAL: local 2-means, then global k-means (splitClusterGlobalV2); all drops re-evaluated.
   *  - ALGO_SKM_GLOBAL_BASIC: two random points, then global k-means (splitClusterGlobal); all drops re-evaluated.
   *  - ALGO_SKM_LOCAL: local 2-means, then localRepartition; the drops of the touched clusters re-evaluated
   *    (the repartition and the re-evaluation are skipped after the last split, 6.1).
   * The split itself re-runs a local 2-means instead of reusing the evaluated one (6.2).
   *
   * @param dataPoints Dataset; partitions updated in-place.
   * @param centroids Global centroids; grows until `maxCentroids`.
   * @param maxCentroids Target number of centroids (K); K=1 skips the split.
   * @param maxIterations Max iterations for local/global k-means refinements.
   * @param splitType One of the four SKM variants above.
   * @param tracker Records each split (count = split number) and the final k-means; carries the ground truth for CI.
   * @return Final SSE: of the final k-means, or for the global variants of the last global k-means (the final
   *         k-means runs there only when no global split ran, K <= 2).
   * @errors Exits on allocation failure in underlying helpers.
   */
  double runSseSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, Algorithm splitType, Tracker* tracker, Rng* rng)
  {
      const Centroids* groundTruth = tracker->groundTruth;
      double finalResultSse = DBL_MAX;
      bool refinedGlobally = false; // set when a global split (SKM-Global, SKM-Global-Basic) ran global k-means

      // The initial split always makes two clusters, so the arrays hold at least two
      const size_t capacity = maxCentroids < 2 ? 2 : maxCentroids;

      double* clusterSSEs = malloc(capacity * sizeof(double));
      handleMemoryError(clusterSSEs);

      double* SseDrops = calloc(capacity, sizeof(double));
      handleMemoryError(SseDrops);

      bool* clustersAffected = calloc(capacity, sizeof(bool));
      handleMemoryError(clustersAffected);

      size_t* clusterSizes = malloc(capacity * sizeof(size_t));
      handleMemoryError(clusterSizes);
      trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);

      size_t iterationCount = 1; //Helper for tracking progress

      // K=1: no split; the final k-means below refines the single centroid
      if (maxCentroids > 1)
      {
          //Only 1 cluster, so no need for decision making
          size_t initialClusterToSplit = 0;
          splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, maxIterations, tracker, rng);

          trackerRecord(tracker, 1, "step", dataPoints, centroids, initialClusterToSplit);

          for (size_t i = 0; i < centroids->size; ++i)
          {
              clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
              SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i], rng);
          }

          iterationCount = 2;
      }

      while (centroids->size < maxCentroids)
      {
          // Choose the cluster that reduces the SSE the most; only clusters with at least 2 points can split
          requireSplittableCluster(countClusterSizes(dataPoints, centroids->size, clusterSizes), centroids->size, splitType);
          size_t clusterToSplit = SIZE_MAX;
          double maxSseDrop = 0.0;

          for (size_t i = 0; i < centroids->size; ++i)
          {
              if (clusterSizes[i] >= 2 && (clusterToSplit == SIZE_MAX || SseDrops[i] > maxSseDrop))
              {
                  maxSseDrop = SseDrops[i];
                  clusterToSplit = i;
              }
          }

          // Split the chosen cluster by the variant (see the function comment)
          if (splitType == ALGO_SKM_INTRA) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, tracker, rng);
          else if (splitType == ALGO_SKM_GLOBAL)
          {
              finalResultSse = splitClusterGlobalV2(dataPoints, centroids, clusterToSplit, maxIterations, tracker, rng);
              refinedGlobally = true;
          }
          else if (splitType == ALGO_SKM_GLOBAL_BASIC)
          {
              finalResultSse = splitClusterGlobal(dataPoints, centroids, clusterToSplit, maxIterations, tracker, rng);
              refinedGlobally = true;
          }
          else if (splitType == ALGO_SKM_LOCAL) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, tracker, rng);

          if (centroids->size < maxCentroids)
          {
              if (splitType == ALGO_SKM_INTRA)
              {
                  // Recalculate SSE for the affected clusters
                  clusterSSEs[clusterToSplit] = calculateClusterSSE(dataPoints, centroids, clusterToSplit);
                  clusterSSEs[centroids->size - 1] = calculateClusterSSE(dataPoints, centroids, centroids->size - 1);

                  // Update SseDrops for the affected clusters
                  SseDrops[clusterToSplit] = tentativeSseDrop(dataPoints, clusterToSplit, maxIterations, clusterSSEs[clusterToSplit], rng);
                  SseDrops[centroids->size - 1] = tentativeSseDrop(dataPoints, centroids->size - 1, maxIterations, clusterSSEs[centroids->size - 1], rng);
              }
              else if (splitType == ALGO_SKM_GLOBAL || splitType == ALGO_SKM_GLOBAL_BASIC)
              {
                  for (size_t i = 0; i < centroids->size; ++i)
                  {
                      clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
                      SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i], rng);
                  }
              }
              else if (splitType == ALGO_SKM_LOCAL)
              {
                  trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

                  localRepartition(dataPoints, centroids, clusterToSplit, clustersAffected);

                  clustersAffected[clusterToSplit] = true;
                  clustersAffected[centroids->size - 1] = true;

                  // Recalculate SseDrop for affected clusters (old and new)
                  for (size_t i = 0; i < centroids->size; ++i)
                  {
                      if (clustersAffected[i])
                      {
                          clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
                          SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i], rng);
                      }
                  }

                  memset(clustersAffected, 0, capacity * sizeof(bool));
              }
          }
          trackerRecord(tracker, iterationCount, "step", dataPoints, centroids, clusterToSplit);

          iterationCount++;

          if (logLevel >= 3)
          {
              double sse = calculateSSE(dataPoints, centroids);
              if (hasGroundTruth(groundTruth))
              {
                  size_t ci = calculateCentroidIndex(centroids, groundTruth);
                  printf("(%s) Number of centroids: %zu, CI: %zu, and SSE: %.0f \n", getAlgorithmName(splitType), centroids->size, ci, sse);
              }
              else
              {
                  printf("(%s) Number of centroids: %zu, SSE: %.0f \n", getAlgorithmName(splitType), centroids->size, sse);
              }
          }
      }

      free(clusterSSEs);
      free(SseDrops);
      free(clustersAffected);
      free(clusterSizes);

      // Global splits end with global k-means; without one (K <= 2) run it here
      if (!refinedGlobally)
      {
          finalResultSse = runKMeans(dataPoints, maxIterations, centroids, tracker, KM_STAGE_FINAL);
          trackerRecord(tracker, iterationCount, "final", dataPoints, centroids, SIZE_MAX);
      }

      return finalResultSse;
  }

  /**
   * @brief Bisecting k-means: grows K by splitting the cluster with the largest SSE.
   *
   * The first split (to K=2) is one local 2-means. Then each split picks, among the clusters with at least 2
   * points, the one with the largest stored SSE, runs `bisectingIterations` local 2-means tries on it, places the
   * two centroids of the try with the lowest SSE, and reassigns every point with a global partition step. Only the
   * stored SSEs of the two new clusters are updated, although the partition step can move points of other clusters
   * too (6.3). A final k-means follows.
   *
   * @param dataPoints Dataset; partitions updated in-place across splits.
   * @param centroids Global centroids; grown in-place up to maxCentroids.
   * @param maxCentroids Target number of clusters (K); K=1 skips the split.
   * @param maxIterations Max iterations for the local k-means and final global k-means.
   * @param bisectingIterations Local 2-means tries per split; at least 1 (main checks it).
   * @param tracker Records each split (count = split number) and the final k-means; carries the ground truth for CI.
   * @return SSE after the final k-means.
   * @errors Exits on allocation failures via helpers.
   */
  double runBisectingKMeans(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, size_t bisectingIterations, Tracker* tracker, Rng* rng)
  {
      // The initial split always makes two clusters, so the list holds at least two
      double* SseList = malloc((maxCentroids < 2 ? 2 : maxCentroids) * sizeof(double));
      handleMemoryError(SseList);
      size_t* clusterSizes = malloc((maxCentroids < 2 ? 2 : maxCentroids) * sizeof(size_t));
      handleMemoryError(clusterSizes);
      double bestSse = DBL_MAX;

      DataPoint newCentroid1 = allocateDataPoint(dataPoints->points[0].dimensions);
      DataPoint newCentroid2 = allocateDataPoint(dataPoints->points[0].dimensions);
      trackerRecord(tracker, 0, "start", dataPoints, centroids, SIZE_MAX);

      size_t iterationCount = 1; //Helper for tracking progress

      // K=1: no split; the final k-means below refines the single centroid
      if (maxCentroids > 1)
      {
          //Step 0: Only 1 cluster, so no need for decision making
          size_t initialClusterToSplit = 0;
          splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, maxIterations, tracker, rng);

          SseList[0] = calculateClusterSSE(dataPoints, centroids, 0);
          SseList[1] = calculateClusterSSE(dataPoints, centroids, 1);

          trackerRecord(tracker, 1, "step", dataPoints, centroids, initialClusterToSplit);

          iterationCount = 2;
      }

      //Repeat until we have K clusters
      while (centroids->size < maxCentroids)
      {
          //Step 1: Choose the cluster to split (the highest SSE among clusters with at least 2 points)
          requireSplittableCluster(countClusterSizes(dataPoints, centroids->size, clusterSizes), centroids->size, ALGO_BISECTING);
          size_t clusterToSplit = SIZE_MAX;
          double maxSSE = 0.0;

          for (size_t j = 0; j < centroids->size; ++j)
          {
              if (clusterSizes[j] >= 2 && (clusterToSplit == SIZE_MAX || SseList[j] > maxSSE))
              {
                  maxSSE = SseList[j];
                  clusterToSplit = j;
              }
          }

          //Repeat for a set number of iterations
          for (size_t j = 0; j < bisectingIterations; ++j)
          {
              ClusteringResult curr = tentativeSplitterForBisecting(dataPoints, clusterToSplit, maxIterations, tracker, rng);

              if (logLevel >= 3) printf("(Bisecting) Cluster %zu, try %zu: SSE %.0f\n", clusterToSplit, j + 1, curr.sse);

              // If the result is better than the best result so far, update the best result
              if (curr.sse < bestSse)
              {
                  bestSse = curr.sse;

                  // Save the two new centroids
                  deepCopyDataPoint(&newCentroid1, &curr.centroids[0]);
                  deepCopyDataPoint(&newCentroid2, &curr.centroids[1]);
              }

              freeClusteringResult(&curr, 2);
          }

          // Replace the old centroid with the new centroid1
          deepCopyDataPoint(&centroids->points[clusterToSplit], &newCentroid1);

          // Increase the size of the centroids array and add the new centroid2
          centroids->points = realloc(centroids->points, (centroids->size + 1) * sizeof(DataPoint));
          handleMemoryError(centroids->points);
          centroids->points[centroids->size] = allocateDataPoint(newCentroid2.dimensions);
          deepCopyDataPoint(&centroids->points[centroids->size], &newCentroid2);
          centroids->size++;

          trackerSplitSnapshot(tracker, dataPoints, centroids, clusterToSplit);

          partitionStep(dataPoints, centroids); //partition step, vai pitaisiko tallentaa ne loopissa ja tehda tassa sitten muutokset? (esim kaikki 0->clusterToSplit, 1->centroids->size-1)

          //Step 3: Update the SSE list
          SseList[clusterToSplit] = calculateClusterSSE(dataPoints, centroids, clusterToSplit);
          SseList[centroids->size - 1] = calculateClusterSSE(dataPoints, centroids, centroids->size - 1);

          bestSse = DBL_MAX;

          trackerRecord(tracker, iterationCount, "step", dataPoints, centroids, clusterToSplit);

          iterationCount++;
      }

      //Step 4: Run the final k-means
      double finalResultSse = runKMeans(dataPoints, maxIterations, centroids, tracker, KM_STAGE_FINAL);

      trackerRecord(tracker, iterationCount, "final", dataPoints, centroids, SIZE_MAX);

      // Cleanup
      free(SseList);
      free(clusterSizes);
      deinitDataPoint(&newCentroid1);
      deinitDataPoint(&newCentroid2);

      return finalResultSse;
  }

  /**
   * @brief K-means runs firstRun..loopCount-1 (0-based): initial centroids by `init`, k-means until the SSE stops improving.
   *
   * Shared by the six algorithm wrappers: run i uses rng_derive_seed(seed, i); the stopwatch measures the run
   * (tracking paused); after each run one `_runs.csv` row and, for the first run with CI = 0 and with CI > 0,
   * the example snapshots; at the end the console statistics and the summary row (SSE divided by `scaling`).
   * `groundTruth` may be empty (CI is then NA); K must be 1..N (main checks it). Exits on allocation or file errors.
   * KM tracking: one row per k-means iteration (runKMeansWithTracking).
   */
  void runKMeansAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t firstRun, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, const TrackOptions* track, InitMethod init, uint64_t seed)
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

          Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

          trackerBeginRun(&tracker, i);

          initialCentroids(init, numCentroids, dataPoints, &centroids, rng);

          double resultSse = runKMeansWithTracking(dataPoints, maxIterations, &centroids, &tracker);

          duration = trackerEndRun(&tracker);

          size_t centroidIndex = centroidIndexOrNA(&centroids, groundTruth);

          addRunStatistics(&stats, resultSse, centroidIndex, duration);

          appendRunCsv(outputDirectory, "KM", i, runSeed, resultSse, centroidIndex, duration);

          saveExampleSnapshots(&centroids, dataPoints, centroidIndex, "KM",
              outputDirectory, &savedZeroResults, &savedNonZeroResults);

          freeCentroids(&centroids);
      }

      trackerFree(&tracker);

      printStatistics("K-means", stats, loopCount - firstRun, numCentroids, scaling);
      writeResultsToFile(fileName, stats, numCentroids, "KM", loopCount - firstRun, scaling, outputDirectory);
  }

  /**
   * @brief Repeated k-means runs firstRun..loopCount-1 (see runKMeansAlgorithm for the shared run loop and outputs).
   *
   * Each run makes `maxRepeats` k-means from new initial centroids (by `init`) and keeps the lowest SSE; the points
   * are then relabelled with the best centroids for the snapshots. Tracking: one row per repeat with the best SSE
   * and CI so far (CI only when tracking), snapshots at the improvements.
   */
  void runRepeatedKMeansAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t maxRepeats, size_t firstRun, size_t loopCount, size_t scaling,
      const char* fileName, const char* outputDirectory, const TrackOptions* track, InitMethod init, uint64_t seed)
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

          Centroids bestCentroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

          trackerBeginRun(&tracker, i);

          for (size_t j = 0; j < maxRepeats; ++j)
          {
              if (logLevel >= 3) printf("Repeat %zu\n", j + 1);
              Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);
              initialCentroids(init, numCentroids, dataPoints, &centroids, rng);
              double resultSse = runKMeans(dataPoints, maxIterations, &centroids, &tracker, KM_STAGE_NONE);

              if (resultSse < bestSse)
              {
                  bestSse = resultSse;
                  deepCopyCentroids(&centroids, &bestCentroids);

                  // CI of the new best solution, only for the rows (stopwatch paused)
                  if (tracker.logRows) bestCI = trackerCi(&tracker, &centroids);
                  trackerSnapshot(&tracker, j + 1, "step", dataPoints, &centroids, SIZE_MAX);
              }

              // One row per repeat (count = repeat): the best solution so far
              trackerAddRow(&tracker, j + 1, bestSse, bestCI);

              freeCentroids(&centroids);
          }

          duration = trackerEndRun(&tracker);

          // The labels are from the last repeat; the snapshot needs those of the best centroids
          partitionStep(dataPoints, &bestCentroids);

          size_t centroidIndex = centroidIndexOrNA(&bestCentroids, groundTruth);

          addRunStatistics(&stats, bestSse, centroidIndex, duration);

          appendRunCsv(outputDirectory, "RKM", i, runSeed, bestSse, centroidIndex, duration);

          saveExampleSnapshots(&bestCentroids, dataPoints, centroidIndex, "RKM",
              outputDirectory, &savedZeroResults, &savedNonZeroResults);

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
  void runRandomSwapAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxSwaps, size_t firstRun, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, const TrackOptions* track, InitMethod init, uint64_t seed)
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

          Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

          trackerBeginRun(&tracker, i);

          initialCentroids(init, numCentroids, dataPoints, &centroids, rng);
          partitionStep(dataPoints, &centroids); //Local repartition requires this

          double resultSse = randomSwap(dataPoints, &centroids, maxSwaps, &tracker, rng);

          duration = trackerEndRun(&tracker);

          size_t centroidIndex = centroidIndexOrNA(&centroids, groundTruth);

          addRunStatistics(&stats, resultSse, centroidIndex, duration);

          appendRunCsv(outputDirectory, "RS", i, runSeed, resultSse, centroidIndex, duration);

          saveExampleSnapshots(&centroids, dataPoints, centroidIndex, "RS",
              outputDirectory, &savedZeroResults, &savedNonZeroResults);

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
  void runRandomSplitAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t firstRun, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, const TrackOptions* track, uint64_t seed)
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

          Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

          trackerBeginRun(&tracker, i);

          generateRandomCentroids(centroids.size, dataPoints, &centroids, rng);

          double resultSse = runRandomSplit(dataPoints, &centroids, numCentroids, maxIterations, &tracker, rng);

          duration = trackerEndRun(&tracker);

          size_t centroidIndex = centroidIndexOrNA(&centroids, groundTruth);

          addRunStatistics(&stats, resultSse, centroidIndex, duration);

          appendRunCsv(outputDirectory, "SKM_Random", i, runSeed, resultSse, centroidIndex, duration);

          saveExampleSnapshots(&centroids, dataPoints, centroidIndex, "SKM_Random",
              outputDirectory, &savedZeroResults, &savedNonZeroResults);

          freeCentroids(&centroids);
      }

      trackerFree(&tracker);

      printStatistics("Random Split", stats, loopCount - firstRun, numCentroids, scaling);
      writeResultsToFile(fileName, stats, numCentroids, "SKM_Random", loopCount - firstRun, scaling, outputDirectory);
  }


  /**
   * @brief SSE-based split k-means runs firstRun..loopCount-1 (see runKMeansAlgorithm for the shared run loop and outputs).
   *
   * Each run starts from one random data point and calls runSseSplit with `splitType` (SKM-Intra, SKM-Global,
   * SKM-Global-Basic or SKM-Local).
   */
  void runSseSplitAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t firstRun, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, Algorithm splitType, const TrackOptions* track, uint64_t seed)
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

          Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

          trackerBeginRun(&tracker, i);

          generateRandomCentroids(centroids.size, dataPoints, &centroids, rng);

          double resultSse = runSseSplit(dataPoints, &centroids, numCentroids, maxIterations, splitType, &tracker, rng);

          duration = trackerEndRun(&tracker);

          size_t centroidIndex = centroidIndexOrNA(&centroids, groundTruth);

          addRunStatistics(&stats, resultSse, centroidIndex, duration);

          appendRunCsv(outputDirectory, splitTypeName, i, runSeed, resultSse, centroidIndex, duration);

          saveExampleSnapshots(&centroids, dataPoints, centroidIndex, splitTypeName,
              outputDirectory, &savedZeroResults, &savedNonZeroResults);

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
  void runBisectingKMeansAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t firstRun, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, const TrackOptions* track, size_t bisectingIterations, uint64_t seed)
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

          Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

          trackerBeginRun(&tracker, i);

          generateRandomCentroids(centroids.size, dataPoints, &centroids, rng);

          double resultSse = runBisectingKMeans(dataPoints, &centroids, numCentroids, maxIterations, bisectingIterations, &tracker, rng);

          duration = trackerEndRun(&tracker);

          size_t centroidIndex = centroidIndexOrNA(&centroids, groundTruth);

          addRunStatistics(&stats, resultSse, centroidIndex, duration);

          appendRunCsv(outputDirectory, "Bisecting_KM", i, runSeed, resultSse, centroidIndex, duration);

          saveExampleSnapshots(&centroids, dataPoints, centroidIndex, "Bisecting_KM",
              outputDirectory, &savedZeroResults, &savedNonZeroResults);

          freeCentroids(&centroids);
      }

      trackerFree(&tracker);

      printStatistics("Bisecting", stats, loopCount - firstRun, numCentroids, scaling);
      writeResultsToFile(fileName, stats, numCentroids, "Bisecting_KM", loopCount - firstRun, scaling, outputDirectory);
  }

  /////////////
  // Random //
  ///////////

  static bool parseUint64(const char* text, uint64_t* out);

/**
 * @brief Builds ground-truth centroids from data and a partition file (the gt-from-partition subcommand).
 *
 * Reads points (uniform dimensionality), then one 1-based cluster label per point (whitespace-separated
 * integers, usually one per line), and writes the cluster means (`%f` with a trailing space). K is the
 * largest label; an empty cluster keeps a zero centroid with a warning.
 *
 * @param dataFileName Path to data points (whitespace-separated doubles).
 * @param partitionFileName Path to partition labels (integers 1..K, one per data point).
 * @param outputFileName Path to write centroids (space-separated doubles per row).
 * @return 0 on success; 1 when the data file has no rows, a file cannot be opened, a label is not an integer from
 *         1 to N, or the partition file has fewer or more labels than the data file has points.
 * @errors The data file is read by readDataPoints, which exits on its errors; lines over LINE_BUFSZ - 1 characters
 *         and allocation failures exit.
 */
  int generateGroundTruthCentroids(const char* dataFileName, const char* partitionFileName, const char* outputFileName)
  {
      // Read the data points
      DataPoints dataPoints = readDataPoints(dataFileName);

      if (dataPoints.size == 0)
      {
          printf("Error: No data points read from file %s\n", dataFileName);
          return 1;
      }

      printf("Read %zu data points from %s\n", dataPoints.size, dataFileName);

      // Read the partition labels: integers 1..N, exactly one per data point
      FILE* partitionFile;

      if (FOPEN(partitionFile, partitionFileName, "r") != 0)
      {
          fprintf(stderr, "Error: Unable to open partition file '%s'\n", partitionFileName);
          freeDataPoints(&dataPoints);
          return 1;
      }

      size_t maxPartitionIndex = 0;
      size_t labelCount = 0;
      bool valid = true;
      char line[LINE_BUFSZ];
      for (size_t lineNumber = 1; valid && readDataLine(line, sizeof(line), partitionFile, partitionFileName, lineNumber) != NULL; ++lineNumber)
      {
          char* context = NULL;
          for (char* token = STRTOK(line, " \t\r\n", &context); token != NULL; token = STRTOK(NULL, " \t\r\n", &context))
          {
              uint64_t label = 0;
              if (!parseUint64(token, &label) || label < 1 || label > dataPoints.size)
              {
                  fprintf(stderr, "Error: '%s' line %zu: '%s' is not a cluster label from 1 to %zu\n",
                      partitionFileName, lineNumber, token, dataPoints.size);
                  valid = false;
                  break;
              }
              if (labelCount == dataPoints.size)
              {
                  fprintf(stderr, "Error: '%s' has more labels than the %zu data points of '%s'\n",
                      partitionFileName, dataPoints.size, dataFileName);
                  valid = false;
                  break;
              }
              const size_t clusterIndex = (size_t)(label - 1);
              dataPoints.points[labelCount++].partition = clusterIndex;
              if (clusterIndex > maxPartitionIndex)
              {
                  maxPartitionIndex = clusterIndex;
              }
          }
      }
      fclose(partitionFile);

      if (valid && labelCount != dataPoints.size)
      {
          fprintf(stderr, "Error: '%s' has %zu labels for the %zu data points of '%s'\n",
              partitionFileName, labelCount, dataPoints.size, dataFileName);
          valid = false;
      }
      if (!valid)
      {
          freeDataPoints(&dataPoints);
          return 1;
      }

      printf("Read %zu labels, clusters 1 to %zu\n", labelCount, maxPartitionIndex + 1);

      // The number of centroids is maxPartitionIndex + 1 (assuming 0-based indexing)
      size_t numCentroids = maxPartitionIndex + 1;
      printf("Will calculate %zu centroids\n", numCentroids);

      // Allocate memory for the centroids
      Centroids centroids = allocateCentroids(numCentroids, dataPoints.points[0].dimensions);

      // Initialize count array to track how many points are in each cluster
      size_t* clusterCounts = calloc(numCentroids, sizeof(size_t));
      handleMemoryError(clusterCounts);

      // Sum up the attribute values for each cluster
      for (size_t i = 0; i < dataPoints.size; ++i)
      {
          size_t clusterIndex = dataPoints.points[i].partition;

          // Every label was checked against 1..N and K is the largest one
          assert(clusterIndex < numCentroids);

          // Add the attribute values to the centroid sum
          for (size_t j = 0; j < dataPoints.points[i].dimensions; ++j)
          {
              centroids.points[clusterIndex].attributes[j] += dataPoints.points[i].attributes[j];
          }

          clusterCounts[clusterIndex]++;
      }

      // Calculate the mean values (centroids)
      for (size_t i = 0; i < numCentroids; ++i)
      {
          if (clusterCounts[i] > 0)
          {
              for (size_t j = 0; j < centroids.points[i].dimensions; ++j)
              {
                  centroids.points[i].attributes[j] /= clusterCounts[i];
              }
              printf("Cluster %zu has %zu points\n", i, clusterCounts[i]);
          }
          else
          {
              printf("Warning: Cluster %zu has no points assigned to it\n", i);
          }
      }

      // Write the centroids to the output file
      FILE* outputFile;
      if (FOPEN(outputFile, outputFileName, "w") != 0)
      {
          fprintf(stderr, "Error: Unable to open output file '%s'\n", outputFileName);
          free(clusterCounts);
          freeCentroids(&centroids);
          freeDataPoints(&dataPoints);
          return 1;
      }

      for (size_t i = 0; i < numCentroids; ++i)
      {
          for (size_t j = 0; j < centroids.points[i].dimensions; ++j)
          {
              fprintf(outputFile, "%f ", centroids.points[i].attributes[j]);
          }
          fprintf(outputFile, "\n");
      }

      fclose(outputFile);

      printf("Successfully wrote %zu centroids to %s\n", numCentroids, outputFileName);

      // Clean up
      free(clusterCounts);
      freeCentroids(&centroids);
      freeDataPoints(&dataPoints);

      return 0;
  }

  /**
   * @brief Prints the Centroid Index between a centroid file and a ground-truth file, with the file names and sizes.
   *
   * Used by runDebuggery. Does not check that the files are non-empty or have the same dimensionality
   * (an empty file is read out of bounds); the ci subcommand checks both.
   */
  void debugCalculateCI(const char* debugCentroidsFile, const char* groundTruthFile)
  {
      printf("Debugging CI calculation between:\n");
      printf("  Debug file: %s\n", debugCentroidsFile);
      printf("  Ground truth: %s\n", groundTruthFile);

      // Read centroids from both files
      Centroids debugCentroids = readCentroids(debugCentroidsFile, logLevel);
      Centroids groundTruth = readCentroids(groundTruthFile, logLevel);

      printf("Debug centroids: %zu with %zu dimensions\n",
          debugCentroids.size, debugCentroids.points[0].dimensions);
      printf("Ground truth centroids: %zu with %zu dimensions\n",
          groundTruth.size, groundTruth.points[0].dimensions);

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
      Centroids centroids = readCentroids(debugCentroidsFile, logLevel);
      DataPoints dataPoints = readDataPoints(dataFile);

      printf("Centroids: %zu with %zu dimensions\n",
          centroids.size, centroids.points[0].dimensions);
      printf("Data points: %zu with %zu dimensions\n",
          dataPoints.size, dataPoints.points[0].dimensions);

      // Assign each data point to the nearest centroid
      partitionStep(&dataPoints, &centroids);

      // Calculate SSE
      double sse = calculateSSE(&dataPoints, &centroids);
      printf("Sum of Squared Errors (SSE): %.2f\n\n", sse);

      // Clean up
      freeCentroids(&centroids);
      freeDataPoints(&dataPoints);
  }

  /**
 * @brief Runs a quick CI and SSE sanity check with fixed test files.
 */
  void runDebuggery(void)
  {
      debugCalculateCI("debuggery/output_worms_64d.txt", "gt/worms_64d-gt.txt");
      debugCalculateSSE("debuggery/output_worms_64d.txt", "data/worms_64d.txt");
  }

  /////////////////
  // Subcommands //
  /////////////////

  /** @brief Exits unless both point sets are non-empty and have the same number of dimensions. */
  static void requireCompatible(const DataPoint* first, size_t firstCount, const char* firstFile,
      const DataPoint* second, size_t secondCount, const char* secondFile)
  {
      if (firstCount == 0 || secondCount == 0)
      {
          fprintf(stderr, "Error: %s has no rows\n", firstCount == 0 ? firstFile : secondFile);
          exit(EXIT_FAILURE);
      }
      if (first[0].dimensions != second[0].dimensions)
      {
          fprintf(stderr, "Error: %s has %zu dimensions, %s has %zu\n",
              firstFile, first[0].dimensions, secondFile, second[0].dimensions);
          exit(EXIT_FAILURE);
      }
  }

  /** @brief `ci <centroids.txt> <ground-truth.txt>`: prints the Centroid Index of the centroids against the ground truth. */
  static int commandCi(int argc, char* argv[])
  {
      if (argc != 2)
      {
          fprintf(stderr, "Usage: split_kmeans ci <centroids.txt> <ground-truth.txt>\n");
          return EXIT_FAILURE;
      }
      Centroids centroids = readCentroids(argv[0], logLevel);
      Centroids truth = readCentroids(argv[1], logLevel);
      requireCompatible(centroids.points, centroids.size, argv[0], truth.points, truth.size, argv[1]);

      printf("%zu\n", calculateCentroidIndex(&centroids, &truth));

      freeCentroids(&centroids);
      freeCentroids(&truth);
      return EXIT_SUCCESS;
  }

  /** @brief `sse <data.txt> <centroids.txt>`: prints the SSE (%.17g) of the data with every point at its nearest centroid. */
  static int commandSse(int argc, char* argv[])
  {
      if (argc != 2)
      {
          fprintf(stderr, "Usage: split_kmeans sse <data.txt> <centroids.txt>\n");
          return EXIT_FAILURE;
      }
      DataPoints dataPoints = readDataPoints(argv[0]);
      Centroids centroids = readCentroids(argv[1], logLevel);
      requireCompatible(dataPoints.points, dataPoints.size, argv[0], centroids.points, centroids.size, argv[1]);

      partitionStep(&dataPoints, &centroids);
      printf("%.17g\n", calculateSSE(&dataPoints, &centroids));

      freeDataPoints(&dataPoints);
      freeCentroids(&centroids);
      return EXIT_SUCCESS;
  }

  /** @brief `gt-from-partition <data.txt> <partition.txt> <out.txt>`: writes the mean of each cluster (labels 1..K, one per line). */
  static int commandGtFromPartition(int argc, char* argv[])
  {
      if (argc != 3)
      {
          fprintf(stderr, "Usage: split_kmeans gt-from-partition <data.txt> <partition.txt> <out.txt>\n");
          return EXIT_FAILURE;
      }
      return generateGroundTruthCentroids(argv[0], argv[1], argv[2]) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  }


  ///////////
  // Main //
  /////////

  /** @brief One `--algo` name and the algorithm id used by getAlgorithmName. */
  typedef struct
  {
      const char* name;
      Algorithm id;
  } AlgorithmOption;

  /** @brief Accepted `--algo` names, in the order the algorithms run. */
  static const AlgorithmOption algorithmOptions[] =
  {
      { "km", ALGO_KM }, { "rkm", ALGO_RKM }, { "rs", ALGO_RS }, { "skm-random", ALGO_SKM_RANDOM }, { "skm-intra", ALGO_SKM_INTRA },
      { "skm-global", ALGO_SKM_GLOBAL }, { "skm-global-basic", ALGO_SKM_GLOBAL_BASIC }, { "skm-local", ALGO_SKM_LOCAL }, { "bisecting", ALGO_BISECTING },
  };

  #define ALGORITHM_COUNT (sizeof(algorithmOptions) / sizeof(algorithmOptions[0]))

  /** @brief Command-line options. The defaults reproduce v1.0.0: SKM-Local, 100 runs, RKM and RS once, 5000 swaps, 1000 repeats, 5 tries. */
  typedef struct
  {
      uint64_t seed;          /**< Master seed; run i of every algorithm uses rng_derive_seed(seed, i). */
      size_t runs;            /**< Runs per dataset for KM, the SKM variants and bisecting. */
      bool runsGiven;         /**< RKM and RS run once unless --runs is given. */
      size_t maxIterations;   /**< k-means also stops when SSE stops improving. */
      size_t maxSwaps;        /**< Random Swap trial swaps. */
      size_t maxRepeats;      /**< Repeated k-means repeats per run. */
      size_t bisectTries;     /**< Bisecting k-means tries per split. */
      InitMethod init;        /**< Initial centroids of KM, RKM and RS. */
      size_t firstRun;        /**< First run to make, 1-based; runs firstRun..runs keep their derived seeds. */
      bool algorithms[ALGORITHM_COUNT];
  } Options;

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

  /** @brief Parses a decimal unsigned 64-bit integer; false on anything else. */
  static bool parseUint64(const char* text, uint64_t* out)
  {
      if (text[0] < '0' || text[0] > '9')
      {
          return false;
      }
      char* end = NULL;
      errno = 0;
      const unsigned long long value = strtoull(text, &end, 10);
      if (errno != 0 || *end != '\0')
      {
          return false;
      }
      *out = (uint64_t)value;
      return true;
  }

  /** @brief Reads the value after option `argv[*i]` as an integer of at least `minimum` and advances `*i`; exits otherwise. */
  static size_t countOption(int argc, char* argv[], int* i, uint64_t minimum)
  {
      const char* name = argv[*i];
      uint64_t value = 0;
      if (*i + 1 >= argc || !parseUint64(argv[*i + 1], &value) || value < minimum || (uint64_t)(size_t)value != value)
      {
          fprintf(stderr, "Error: %s requires an integer of at least %" PRIu64 "\n", name, minimum);
          fprintf(stderr, "Use -h for help\n");
          exit(EXIT_FAILURE);
      }
      ++*i;
      return (size_t)value;
  }

  /** @brief Returns the value after option `argv[*i]` and advances `*i`; exits when it is missing. */
  static const char* textOption(int argc, char* argv[], int* i)
  {
      if (*i + 1 >= argc)
      {
          fprintf(stderr, "Error: %s requires a value\n", argv[*i]);
          fprintf(stderr, "Use -h for help\n");
          exit(EXIT_FAILURE);
      }
      ++*i;
      return argv[*i];
  }

  /** @brief Creates `path` and its missing parent folders (as mkdir -p); false with errno set on failure. */
  static bool makeDirectories(const char* path)
  {
      char partial[PATH_MAX];
      const size_t length = strlen(path);
      if (length == 0 || length >= sizeof(partial))
      {
          errno = ENAMETOOLONG;
          return false;
      }
      for (size_t i = 1; i <= length; ++i)
      {
          if (i < length && path[i] != '/' && path[i] != '\\')
          {
              continue;
          }
          memcpy(partial, path, i);
          partial[i] = '\0';
          if (i == 2 && partial[1] == ':')
          {
              continue; // drive root such as C:
          }
          if (MAKE_DIR(partial) != 0 && errno != EEXIST)
          {
              return false;
          }
      }
      return true;
  }

  /** @brief Parses `all` or a comma-separated list of numbers and ranges such as `1,3-5` (no empty items); false on anything else. */
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

      char* parts[4] = { NULL, NULL, NULL, NULL };
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

  /** @brief Size of the run_config.txt header buffer. */
  #define RUN_CONFIG_SIZE 8192

  /**
   * @brief Writes the shared part of run_config.txt into `text`: what must agree for every call that writes to one folder.
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

      const int length = snprintf(text, size,
          "# Split_kMeans run configuration. Every call that writes to this folder must match the lines above \"calls:\".\n"
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
  static void textLine(const char* text, size_t index, char* line, size_t size)
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
   * @brief Creates or extends `<outputDirectory>/run_config.txt`; exits when the folder holds results of another configuration.
   *
   * The shared header (runConfigHeader) must equal the one in the file; then one entry per call is appended:
   * the time, the command line (the program by file name, without its folder) and the resolved options
   * (mode, algorithms, runs, tracking).
   */
  static void writeRunConfig(const char* outputDirectory, const Options* opt, const TrackOptions* track, const char* snapshotsText, const char* datasetsText,
      bool cliMode, int argc, char* argv[])
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
      fprintf(file, "; runs: %zu to %zu (RKM and RS: to %zu); datasets: %s; track-progress: %s; snapshots: %s; track-inner: %s\n",
          opt->firstRun, opt->runs, opt->runsGiven ? opt->runs : (size_t)1, datasetsText, track->rows ? "on" : "off", snapshotsText,
          track->inner ? "on" : "off");
      fclose(file);
  }

  /**
   * @brief Writes `<datasetDirectory>/dataset.txt` (data and ground-truth files, K, N, D; `report` reads K from it).
   *
   * Exits when the folder already describes a dataset with another K, N or D (no mixing of datasets in one folder).
   */
  static void writeDatasetInfo(const char* datasetDirectory, const char* dataFile, const char* gtFile, size_t k, size_t n, size_t d)
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
              fprintf(stderr, "Error: %s describes another dataset or K (this call: k %zu, n %zu, d %zu); write to a new -o folder\n",
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

  /////////////
  // Report //
  /////////////

  /** @brief One `_runs.csv` row or one `_log.csv` row (`count` is 0 for runs). */
  typedef struct
  {
      size_t run;    /**< 1-based. */
      size_t count;
      double sse;
      size_t ci;     /**< CI_NA when not available. */
      double ms;
      size_t order;  /**< Position in its ReportRows (set by reportPush), so sorting keeps the file order. */
  } ReportRow;

  /** @brief Growing array of report rows; free `rows`. */
  typedef struct
  {
      ReportRow* rows;
      size_t count;
      size_t capacity;
  } ReportRows;

  /** @brief The runs and tracked states of one algorithm on one dataset, merged over the batch folders. */
  typedef struct
  {
      ReportRows runs; /**< `_runs.csv`. */
      ReportRows log;  /**< `_log.csv`. */
  } ReportAlgorithm;

  /** @brief One dataset of the report. */
  typedef struct
  {
      char name[256];                              /**< Folder name. */
      char display[256];                           /**< Name in the tables (--names). */
      size_t k;                                    /**< From dataset.txt; 0 when unknown. */
      ReportAlgorithm algorithms[ALGORITHM_COUNT]; /**< Indexed like algorithmOptions. */
  } ReportDataset;

  /** @brief Means over the runs of one algorithm on one dataset. */
  typedef struct
  {
      size_t runs;
      double sseMean;
      double sseSd;
      double ciMean;
      double successPct;
      double msMean;
      double msSd;
      bool ciAvailable;  /**< False when a run has no CI. */
  } ReportSummary;

  /** @brief One curve point: the time, SSE and CI at a count (means over runs for the mean curves). */
  typedef struct
  {
      size_t count;
      double ms;
      double sse;
      double ci;
      bool ciAvailable;
  } CurvePoint;

  /** @brief Display name of an algorithm in the report tables, in the style of the seminar slides. */
  static const char* reportAlgorithmName(Algorithm algorithm)
  {
      switch (algorithm)
      {
      case ALGO_KM: return "KM";
      case ALGO_RKM: return "RKM";
      case ALGO_RS: return "RS";
      case ALGO_SKM_RANDOM: return "SKM - Random";
      case ALGO_SKM_INTRA: return "SKM - Intra";
      case ALGO_SKM_GLOBAL: return "SKM - Global";
      case ALGO_SKM_GLOBAL_BASIC: return "SKM - Global basic";
      case ALGO_SKM_LOCAL: return "SKM - Local";
      case ALGO_BISECTING: return "Bisecting KM";
      default: return "Unknown";
      }
  }

  static void reportPush(ReportRows* rows, const ReportRow* row)
  {
      if (rows->count == rows->capacity)
      {
          const size_t grown = rows->capacity ? rows->capacity * 2 : 64;
          ReportRow* resized = realloc(rows->rows, grown * sizeof(ReportRow));
          handleMemoryError(resized);
          rows->rows = resized;
          rows->capacity = grown;
      }
      rows->rows[rows->count] = *row;
      rows->rows[rows->count].order = rows->count;
      rows->count++;
  }

  /** @brief Parses a number field; accepts a decimal comma (files written with --decimal-comma). */
  static bool reportNumber(const char* text, double* value)
  {
      char buffer[NUMBER_TEXT_SIZE];
      if (strlen(text) >= sizeof(buffer))
      {
          return false;
      }
      STRCPY(buffer, sizeof(buffer), text);
      char* comma = strchr(buffer, ',');
      if (comma != NULL)
      {
          *comma = '.';
      }
      return parseFiniteDouble(buffer, value);
  }

  /**
   * @brief Appends the rows of a `_runs.csv` (run;seed;sse;ci;ms) or `_log.csv` (run;count;sse;ci;ms) file to `rows`.
   *
   * Skips the header line (a first line that is not the header is read as data: a file emptied by an interrupted
   * call gets no header on the next append); exits on a malformed line. For `_runs.csv`, a run number that is
   * already in `rows` (the same run twice, in one or two batch folders) is an error.
   */
  static void reportReadCsv(const char* path, bool isLog, ReportRows* rows)
  {
      FILE* file;
      if (FOPEN(file, path, "r") != 0)
      {
          handleFileError(path);
      }

      const char* header = isLog ? "run;count;sse;ci;ms" : "run;seed;sse;ci;ms";
      char line[1024];
      size_t lineNumber = 0;
      while (fgets(line, sizeof(line), file) != NULL)
      {
          ++lineNumber;
          line[strcspn(line, "\r\n")] = '\0';
          if ((lineNumber == 1 && strcmp(line, header) == 0) || line[0] == '\0')
          {
              continue;
          }

          char* fields[5];
          size_t fieldCount = 0;
          char* field = line;
          while (field != NULL && fieldCount < 5)
          {
              fields[fieldCount++] = field;
              char* next = strchr(field, ';');
              if (next != NULL)
              {
                  *next++ = '\0';
              }
              field = next;
          }

          uint64_t run = 0;
          uint64_t second = 0;
          ReportRow row;
          row.ci = CI_NA;
          bool ok = fieldCount == 5 && field == NULL && parseUint64(fields[0], &run) && run >= 1
              && parseUint64(fields[1], &second) && reportNumber(fields[2], &row.sse) && reportNumber(fields[4], &row.ms);
          if (ok && strcmp(fields[3], "NA") != 0)
          {
              uint64_t ci = 0;
              ok = parseUint64(fields[3], &ci);
              row.ci = (size_t)ci;
          }
          if (!ok)
          {
              fclose(file);
              fprintf(stderr, "Error: %s line %zu is not a %s row\n", path, lineNumber, header);
              exit(EXIT_FAILURE);
          }
          row.run = (size_t)run;
          row.count = isLog ? (size_t)second : 0;

          if (!isLog)
          {
              for (size_t i = 0; i < rows->count; ++i)
              {
                  if (rows->rows[i].run == row.run)
                  {
                      fclose(file);
                      fprintf(stderr, "Error: run %zu appears twice (in %s, or in it and an earlier batch folder)\n", row.run, path);
                      exit(EXIT_FAILURE);
                  }
              }
          }
          reportPush(rows, &row);
      }
      fclose(file);
  }

  /** @brief Reads the shared header of `<directory>/run_config.txt` (the lines up to "calls:") into `header`; exits when missing. */
  static void reportReadHeader(const char* directory, char* header, size_t size)
  {
      char path[PATH_MAX];
      joinPath(path, sizeof(path), directory, "run_config.txt");
      FILE* file;
      if (FOPEN(file, path, "r") != 0)
      {
          fprintf(stderr, "Error: %s not found; report needs output folders written by version 2.0.0 or later\n", path);
          exit(EXIT_FAILURE);
      }
      const size_t length = fread(header, 1, size - 1, file);
      header[length] = '\0';
      fclose(file);

      // A folder written on Windows and copied (not checked out) to Linux keeps its CRLF line ends
      size_t kept = 0;
      for (size_t i = 0; i < length; ++i)
      {
          if (header[i] != '\r')
          {
              header[kept++] = header[i];
          }
      }
      header[kept] = '\0';

      char* calls = strstr(header, "\ncalls:\n");
      if (calls == NULL)
      {
          fprintf(stderr, "Error: %s has no calls: line\n", path);
          exit(EXIT_FAILURE);
      }
      calls[strlen("\ncalls:\n")] = '\0';
  }

  /** @brief K from `<directory>/dataset.txt`, or 0 when the file is missing. */
  static size_t reportReadK(const char* directory)
  {
      char path[PATH_MAX];
      joinPath(path, sizeof(path), directory, "dataset.txt");
      FILE* file;
      if (FOPEN(file, path, "r") != 0)
      {
          return 0;
      }
      char line[PATH_MAX + 32];
      size_t k = 0;
      while (fgets(line, sizeof(line), file) != NULL)
      {
          uint64_t value = 0;
          line[strcspn(line, "\r\n")] = '\0';
          if (strncmp(line, "k: ", 3) == 0 && parseUint64(line + 3, &value))
          {
              k = (size_t)value;
          }
      }
      fclose(file);
      return k;
  }

  /** @brief Means and sample standard deviations of SSE and time, mean CI and success rate of `rows`. */
  static ReportSummary reportSummarize(const ReportRow* rows, size_t count)
  {
      ReportSummary summary;
      memset(&summary, 0, sizeof(summary));
      summary.runs = count;
      summary.ciAvailable = count > 0;
      if (count == 0)
      {
          return summary;
      }

      size_t successes = 0;
      for (size_t i = 0; i < count; ++i)
      {
          summary.sseMean += rows[i].sse;
          summary.msMean += rows[i].ms;
          if (rows[i].ci == CI_NA)
          {
              summary.ciAvailable = false;
          }
          else
          {
              summary.ciMean += (double)rows[i].ci;
              successes += rows[i].ci == 0;
          }
      }
      summary.sseMean /= (double)count;
      summary.msMean /= (double)count;
      summary.ciMean /= (double)count;
      summary.successPct = 100.0 * (double)successes / (double)count;

      if (count > 1)
      {
          for (size_t i = 0; i < count; ++i)
          {
              summary.sseSd += (rows[i].sse - summary.sseMean) * (rows[i].sse - summary.sseMean);
              summary.msSd += (rows[i].ms - summary.msMean) * (rows[i].ms - summary.msMean);
          }
          summary.sseSd = sqrt(summary.sseSd / (double)(count - 1));
          summary.msSd = sqrt(summary.msSd / (double)(count - 1));
      }
      return summary;
  }

  /** @brief Order of `_log.csv` rows: by run, then count, then file order (qsort). */
  static int compareReportRows(const void* a, const void* b)
  {
      const ReportRow* x = a;
      const ReportRow* y = b;
      if (x->run != y->run)
      {
          return (x->run > y->run) - (x->run < y->run);
      }
      if (x->count != y->count)
      {
          return (x->count > y->count) - (x->count < y->count);
      }
      return (x->order > y->order) - (x->order < y->order);
  }

  /** @brief Number of the `_log.csv` rows of `run` and, in `*first`, the index of the first; the log is sorted (reportPrepareLog). */
  static size_t reportRunRows(const ReportRows* log, size_t run, size_t* first)
  {
      size_t i = 0;
      while (i < log->count && log->rows[i].run < run)
      {
          ++i;
      }
      *first = i;
      size_t rows = 0;
      while (i + rows < log->count && log->rows[i + rows].run == run)
      {
          ++rows;
      }
      return rows;
  }

  /**
   * @brief Sorts the `_log.csv` rows of one algorithm by run and count and checks them against its `_runs.csv`; exits on a mismatch.
   *
   * Every row must belong to a run of `_runs.csv`; every run must have rows, or none may (tracked and untracked calls
   * in one report would mix the curves); RS must have them (its results end at the last improvement of each run);
   * with rows, the RS and RKM curve run must exist.
   */
  static void reportPrepareLog(const char* dataset, ReportAlgorithm* data, Algorithm algorithm, size_t curveRun)
  {
      ReportRows* log = &data->log;
      const char* name = getAlgorithmName(algorithm);
      if (log->count > 1)
      {
          qsort(log->rows, log->count, sizeof(ReportRow), compareReportRows);
      }

      for (size_t i = 0; i < log->count; ++i)
      {
          if (i > 0 && log->rows[i].run == log->rows[i - 1].run)
          {
              continue;
          }
          bool found = false;
          for (size_t r = 0; r < data->runs.count && !found; ++r)
          {
              found = (data->runs.rows[r].run == log->rows[i].run);
          }
          if (!found)
          {
              fprintf(stderr, "Error: %s %s: _log.csv has rows of run %zu, which is not in _runs.csv\n", dataset, name, log->rows[i].run);
              exit(EXIT_FAILURE);
          }
      }

      if (log->count == 0)
      {
          if (algorithm == ALGO_RS)
          {
              fprintf(stderr, "Error: %s RS has no _log.csv rows; its results end at the last SSE improvement of each run, "
                  "so run RS with tracking (batch mode or --track-progress)\n", dataset);
              exit(EXIT_FAILURE);
          }
          return;
      }

      size_t first = 0;
      for (size_t r = 0; r < data->runs.count; ++r)
      {
          if (reportRunRows(log, data->runs.rows[r].run, &first) == 0)
          {
              fprintf(stderr, "Error: %s %s: run %zu has no _log.csv rows but other runs have (calls with and without tracking are mixed)\n",
                  dataset, name, data->runs.rows[r].run);
              exit(EXIT_FAILURE);
          }
      }
      if ((algorithm == ALGO_RS || algorithm == ALGO_RKM) && reportRunRows(log, curveRun, &first) == 0)
      {
          fprintf(stderr, "Error: --curve-run %zu: %s %s has no run %zu\n", curveRun, dataset, name, curveRun);
          exit(EXIT_FAILURE);
      }
  }

  /**
   * @brief RS values at the last SSE improvement of each run: the time of the run's last `_log.csv` row with the
   * `_runs.csv` SSE and CI (Random Swap keeps its best solution, the one found at that swap).
   *
   * The log is prepared (reportPrepareLog), so every run has rows. The caller frees the rows.
   */
  static ReportRows reportRsAtLastImprovement(const ReportAlgorithm* rs)
  {
      ReportRows cut = { NULL, 0, 0 };
      for (size_t r = 0; r < rs->runs.count; ++r)
      {
          ReportRow value = rs->runs.rows[r];
          size_t first = 0;
          const size_t rows = reportRunRows(&rs->log, value.run, &first);
          assert(rows > 0);
          value.ms = rs->log.rows[first + rows - 1].ms;
          reportPush(&cut, &value);
      }
      return cut;
  }

  /** @brief Ascending order of counts (qsort). */
  static int compareCounts(const void* a, const void* b)
  {
      const size_t x = *(const size_t*)a;
      const size_t y = *(const size_t*)b;
      return (x > y) - (x < y);
  }

  /** @brief Copies a `_log.csv` or `_runs.csv` row into a curve point with `count`. */
  static void reportPoint(CurvePoint* point, const ReportRow* row, size_t count)
  {
      point->count = count;
      point->ms = row->ms;
      point->sse = row->sse;
      point->ciAvailable = row->ci != CI_NA;
      point->ci = point->ciAvailable ? (double)row->ci : 0.0;
  }

  /**
   * @brief The curve of one algorithm into `points` (capacity: the log row count); returns its length. The log is prepared.
   *
   * RS and RKM: the `_log.csv` rows of run `curveRun` (RS: the initial solution and each improvement, so the curve ends
   * at the last improvement; RKM: every repeat). Other algorithms: per count, the means over all runs of the state at
   * that count; a run that has ended keeps its last row (KM runs stop at different iterations).
   */
  static size_t reportCurve(const ReportAlgorithm* data, Algorithm algorithm, size_t curveRun, CurvePoint* points)
  {
      const ReportRows* log = &data->log;
      size_t length = 0;
      if (algorithm == ALGO_RS || algorithm == ALGO_RKM)
      {
          size_t first = 0;
          const size_t rows = reportRunRows(log, curveRun, &first);
          for (size_t i = 0; i < rows; ++i)
          {
              const ReportRow* row = &log->rows[first + i];
              reportPoint(&points[length++], row, row->count);
          }
          return length;
      }

      if (log->count == 0)
      {
          return 0;
      }

      // The rows of each run: [start, end) of the log, which is sorted by run and count; cursor = the run's state so far
      size_t* start = malloc(log->count * sizeof(size_t));
      size_t* end = malloc(log->count * sizeof(size_t));
      size_t* cursor = malloc(log->count * sizeof(size_t));
      size_t* counts = malloc(log->count * sizeof(size_t));
      handleMemoryError(start);
      handleMemoryError(end);
      handleMemoryError(cursor);
      handleMemoryError(counts);
      size_t runCount = 0;
      for (size_t i = 0; i < log->count; ++i)
      {
          if (i == 0 || log->rows[i].run != log->rows[i - 1].run)
          {
              start[runCount] = i;
              cursor[runCount] = i;
              ++runCount;
          }
          end[runCount - 1] = i + 1;
          counts[i] = log->rows[i].count;
      }
      qsort(counts, log->count, sizeof(size_t), compareCounts);

      for (size_t i = 0; i < log->count; ++i)
      {
          if (i > 0 && counts[i] == counts[i - 1])
          {
              continue;
          }
          CurvePoint* point = &points[length++];
          point->count = counts[i];
          point->ms = 0.0;
          point->sse = 0.0;
          point->ci = 0.0;
          point->ciAvailable = true;
          size_t members = 0;
          for (size_t s = 0; s < runCount; ++s)
          {
              while (cursor[s] + 1 < end[s] && log->rows[cursor[s] + 1].count <= counts[i])
              {
                  ++cursor[s];
              }
              const ReportRow* row = &log->rows[cursor[s]];
              if (row->count > counts[i])
              {
                  continue; // the run starts at a later count
              }
              ++members;
              point->ms += row->ms;
              point->sse += row->sse;
              if (row->ci == CI_NA)
              {
                  point->ciAvailable = false;
              }
              else
              {
                  point->ci += (double)row->ci;
              }
          }
          assert(members > 0);
          point->ms /= (double)members;
          point->sse /= (double)members;
          point->ci /= (double)members;
      }
      free(start);
      free(end);
      free(cursor);
      free(counts);
      return length;
  }

  /** @brief Writes `text` to `file`, then `;`, or a line end when `last`. */
  static void reportCell(FILE* file, const char* text, bool last)
  {
      fputs(text, file);
      fputc(last ? '\n' : ';', file);
  }

  /** @brief Opens `<directory>/<name>` for writing; exits on failure. */
  static FILE* reportOpen(const char* directory, const char* name)
  {
      char path[PATH_MAX];
      joinPath(path, sizeof(path), directory, name);
      FILE* file;
      if (FOPEN(file, path, "w") != 0)
      {
          handleFileError(path);
      }
      return file;
  }

  /** @brief `report` options. */
  typedef struct
  {
      size_t curveRun;   /**< Run of the RS and RKM curves (1-based). */
      bool noTime;       /**< Leave out the time columns (make check compares the rest). */
  } ReportOptions;

  /** @brief Writes `curves_<dataset>.csv`: per algorithm a block of count, time, SSE (scaled) and CI; an `end` row, then the points. */
  static void reportWriteCurves(const char* outputDirectory, const ReportDataset* dataset, const ReportSummary* summaries,
      const ReportOptions* options)
  {
      CurvePoint* curves[ALGORITHM_COUNT];
      size_t lengths[ALGORITHM_COUNT];
      size_t longest = 0;
      size_t lastAlgorithm = 0;
      double smallest = DBL_MAX;
      for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
      {
          const ReportAlgorithm* data = &dataset->algorithms[a];
          curves[a] = NULL;
          lengths[a] = 0;
          if (data->runs.count == 0)
          {
              continue;
          }
          lastAlgorithm = a;
          curves[a] = malloc((data->log.count + 1) * sizeof(CurvePoint));
          handleMemoryError(curves[a]);
          lengths[a] = reportCurve(data, algorithmOptions[a].id, options->curveRun, curves[a]);
          if (lengths[a] > longest)
          {
              longest = lengths[a];
          }
          if (summaries[a].sseMean > 0.0 && summaries[a].sseMean < smallest)
          {
              smallest = summaries[a].sseMean;
          }
          for (size_t i = 0; i < lengths[a]; ++i)
          {
              if (curves[a][i].sse > 0.0 && curves[a][i].sse < smallest)
              {
                  smallest = curves[a][i].sse;
              }
          }
      }

      // SSE in units of 10^exponent, as the v1 graph axes "SSE (10^10)"
      const int exponent = smallest < DBL_MAX ? (int)floor(log10(smallest)) : 0;
      const double scale = pow(10.0, (double)exponent);

      char name[PATH_MAX];
      if (snprintf(name, sizeof(name), "curves_%s.csv", dataset->name) >= (int)sizeof(name))
      {
          fprintf(stderr, "Error: dataset name too long: %s\n", dataset->name);
          exit(EXIT_FAILURE);
      }
      FILE* file = reportOpen(outputDirectory, name);

      char text[NUMBER_TEXT_SIZE + 64];
      for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
      {
          if (dataset->algorithms[a].runs.count == 0) continue;
          const char* display = reportAlgorithmName(algorithmOptions[a].id);
          snprintf(text, sizeof(text), "%s count", display);
          reportCell(file, text, false);
          if (!options->noTime)
          {
              snprintf(text, sizeof(text), "%s time (ms)", display);
              reportCell(file, text, false);
          }
          snprintf(text, sizeof(text), "%s SSE (10^%d)", display, exponent);
          reportCell(file, text, false);
          snprintf(text, sizeof(text), "%s CI", display);
          reportCell(file, text, a == lastAlgorithm);
      }

      // The end point: the means from _runs.csv (RS at the last improvement), the same values as the tables
      for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
      {
          if (dataset->algorithms[a].runs.count == 0) continue;
          reportCell(file, "end", false);
          if (!options->noTime)
          {
              reportCell(file, formatFixed(summaries[a].msMean, 3, text), false);
          }
          reportCell(file, formatFixed(summaries[a].sseMean / scale, 6, text), false);
          reportCell(file, summaries[a].ciAvailable ? formatFixed(summaries[a].ciMean, 4, text) : "NA", a == lastAlgorithm);
      }

      for (size_t i = 0; i < longest; ++i)
      {
          for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
          {
              if (dataset->algorithms[a].runs.count == 0) continue;
              const bool last = (a == lastAlgorithm);
              if (i >= lengths[a])
              {
                  reportCell(file, "", false);
                  if (!options->noTime) reportCell(file, "", false);
                  reportCell(file, "", false);
                  reportCell(file, "", last);
                  continue;
              }
              const CurvePoint* point = &curves[a][i];
              snprintf(text, sizeof(text), "%zu", point->count);
              reportCell(file, text, false);
              if (!options->noTime)
              {
                  reportCell(file, formatFixed(point->ms, 3, text), false);
              }
              reportCell(file, formatFixed(point->sse / scale, 6, text), false);
              reportCell(file, point->ciAvailable ? formatFixed(point->ci, 4, text) : "NA", last);
          }
      }
      fclose(file);

      for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
      {
          free(curves[a]);
      }
  }

  /** @brief The seminar tables. */
  typedef enum { TABLE_CI, TABLE_SUCCESS, TABLE_TIME, TABLE_SSE } ReportTable;

  /** @brief Writes one seminar table (algorithms × datasets in `order`) to `<outputDirectory>/<name>`. */
  static void reportWriteTable(const char* outputDirectory, const char* name, ReportTable table, const ReportDataset* datasets,
      const size_t* order, size_t datasetCount, const ReportSummary* summaries)
  {
      FILE* file = reportOpen(outputDirectory, name);
      reportCell(file, "Algorithm", datasetCount == 0);
      for (size_t d = 0; d < datasetCount; ++d)
      {
          reportCell(file, datasets[order[d]].display, d + 1 == datasetCount);
      }

      char text[NUMBER_TEXT_SIZE];
      for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
      {
          bool present = false;
          for (size_t d = 0; d < datasetCount; ++d)
          {
              present = present || summaries[order[d] * ALGORITHM_COUNT + a].runs > 0;
          }
          if (!present) continue;

          reportCell(file, reportAlgorithmName(algorithmOptions[a].id), datasetCount == 0);
          for (size_t d = 0; d < datasetCount; ++d)
          {
              const ReportSummary* summary = &summaries[order[d] * ALGORITHM_COUNT + a];
              const char* cell = "";
              if (summary->runs > 0)
              {
                  switch (table)
                  {
                  case TABLE_CI: cell = summary->ciAvailable ? formatFixed(summary->ciMean, 2, text) : "NA"; break;
                  case TABLE_SUCCESS: cell = summary->ciAvailable ? formatFixed(summary->successPct, 1, text) : "NA"; break;
                  case TABLE_TIME: cell = formatFixed(summary->msMean, 2, text); break;
                  default: cell = formatFixed(summary->sseMean, 2, text); break;
                  }
              }
              reportCell(file, cell, d + 1 == datasetCount);
          }
      }
      fclose(file);
  }

  /**
   * @brief Writes the summary.csv row of one algorithm on one dataset; `fullMs` (RS only, else NULL) is the mean time of
   * the whole runs, every swap included. The standard deviations are NA with fewer than 2 runs.
   */
  static void reportWriteSummaryRow(FILE* file, const ReportDataset* dataset, Algorithm algorithm, const ReportSummary* summary,
      const double* fullMs, bool noTime)
  {
      char text[NUMBER_TEXT_SIZE];
      const bool sd = summary->runs >= 2;
      fprintf(file, "%s;%s;%zu", dataset->display, reportAlgorithmName(algorithm), summary->runs);
      fprintf(file, ";%s", formatFixed(summary->sseMean, 6, text));
      fprintf(file, ";%s", sd ? formatFixed(summary->sseSd, 6, text) : "NA");
      fprintf(file, ";%s", summary->ciAvailable ? formatFixed(summary->ciMean, 4, text) : "NA");
      fprintf(file, ";%s", summary->ciAvailable && dataset->k > 0 ? formatFixed(summary->ciMean / (double)dataset->k, 4, text) : "NA");
      fprintf(file, ";%s", summary->ciAvailable ? formatFixed(summary->successPct, 1, text) : "NA");
      if (!noTime)
      {
          fprintf(file, ";%s", formatFixed(summary->msMean, 3, text));
          fprintf(file, ";%s", sd ? formatFixed(summary->msSd, 3, text) : "NA");
          fprintf(file, ";%s", fullMs != NULL ? formatFixed(*fullMs, 3, text) : "");
      }
      fputc('\n', file);
  }

  /** @brief Reads the datasets of every batch folder into `*datasets`; returns their number. Exits on unreadable files. */
  static size_t reportCollect(const char* const* directories, size_t directoryCount, ReportDataset** datasets)
  {
      size_t datasetCount = 0;
      for (size_t f = 0; f < directoryCount; ++f)
      {
          char** names = NULL;
          const size_t nameCount = list_dirs(directories[f], &names);
          for (size_t n = 0; n < nameCount; ++n)
          {
              char folder[PATH_MAX];
              joinPath(folder, sizeof(folder), directories[f], names[n]);

              size_t d = 0;
              while (d < datasetCount && strcmp((*datasets)[d].name, names[n]) != 0)
              {
                  ++d;
              }
              for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
              {
                  char path[PATH_MAX];
                  algorithmFilePath(algorithmOptions[a].id, folder, "_runs.csv", path, sizeof(path));
                  if (!fileExists(path))
                  {
                      continue;
                  }
                  if (d == datasetCount)
                  {
                      if (strlen(names[n]) >= sizeof((*datasets)[0].name))
                      {
                          fprintf(stderr, "Error: dataset name too long: %s\n", names[n]);
                          exit(EXIT_FAILURE);
                      }
                      ReportDataset* grown = realloc(*datasets, (datasetCount + 1) * sizeof(ReportDataset));
                      handleMemoryError(grown);
                      *datasets = grown;
                      memset(&grown[d], 0, sizeof(ReportDataset));
                      STRCPY(grown[d].name, sizeof(grown[d].name), names[n]);
                      STRCPY(grown[d].display, sizeof(grown[d].display), names[n]);
                      ++datasetCount;
                  }
                  ReportDataset* dataset = &(*datasets)[d];
                  reportReadCsv(path, false, &dataset->algorithms[a].runs);
                  algorithmFilePath(algorithmOptions[a].id, folder, "_log.csv", path, sizeof(path));
                  if (fileExists(path))
                  {
                      reportReadCsv(path, true, &dataset->algorithms[a].log);
                  }
                  const size_t k = reportReadK(folder);
                  if (dataset->k == 0)
                  {
                      dataset->k = k;
                  }
                  else if (k != 0 && k != dataset->k)
                  {
                      fprintf(stderr, "Error: %s has K = %zu, another batch folder K = %zu\n", folder, k, dataset->k);
                      exit(EXIT_FAILURE);
                  }
              }
          }
          freeStringList(names, nameCount);
      }
      return datasetCount;
  }

  /**
   * @brief Dataset order and display names: the `namesFile` lines ("<dataset> [display name]") first, the rest by name.
   *
   * Fills `order` (datasetCount entries). Unknown or repeated names in the file are warnings.
   */
  static void reportOrder(const char* namesFile, ReportDataset* datasets, size_t datasetCount, size_t* order)
  {
      bool* placed = calloc(datasetCount, sizeof(bool));
      handleMemoryError(placed);
      size_t ordered = 0;
      if (namesFile != NULL)
      {
          FILE* file;
          if (FOPEN(file, namesFile, "r") != 0)
          {
              handleFileError(namesFile);
          }
          char line[512];
          while (fgets(line, sizeof(line), file) != NULL)
          {
              line[strcspn(line, "\r\n")] = '\0';
              char* name = line + strspn(line, " \t");
              if (*name == '\0')
              {
                  continue;
              }
              char* rest = name + strcspn(name, " \t");
              if (*rest != '\0')
              {
                  *rest++ = '\0';
                  rest += strspn(rest, " \t");
              }
              size_t d = 0;
              while (d < datasetCount && strcmp(datasets[d].name, name) != 0)
              {
                  ++d;
              }
              if (d == datasetCount || placed[d])
              {
                  fprintf(stderr, "Warning: --names: %s is not a dataset of the report or is listed twice\n", name);
                  continue;
              }
              if (*rest != '\0')
              {
                  if (strlen(rest) >= sizeof(datasets[d].display))
                  {
                      fprintf(stderr, "Error: --names: the display name of %s is too long\n", name);
                      exit(EXIT_FAILURE);
                  }
                  STRCPY(datasets[d].display, sizeof(datasets[d].display), rest);
              }
              placed[d] = true;
              order[ordered++] = d;
          }
          fclose(file);
      }
      while (ordered < datasetCount)
      {
          size_t next = SIZE_MAX;
          for (size_t d = 0; d < datasetCount; ++d)
          {
              if (!placed[d] && (next == SIZE_MAX || strcmp(datasets[d].name, datasets[next].name) < 0))
              {
                  next = d;
              }
          }
          placed[next] = true;
          order[ordered++] = next;
      }
      free(placed);
  }

  /**
   * @brief `report [options] <batch dir>...`: merges batch output folders into Excel-ready tables (plan step 3.9).
   *
   * The folders' run_config.txt headers must agree. Writes summary.csv, table_ci.csv, table_success.csv,
   * table_time.csv, table_sse.csv and curves_<dataset>.csv into `-o` (default `<first dir>/report`).
   * RS results end at the last SSE improvement of each run, so RS needs its `_log.csv` rows; for every algorithm,
   * either all runs have rows or none (reportPrepareLog).
   */
  static int commandReport(int argc, char* argv[])
  {
      ReportOptions options = { 1, false };
      const char* outputOption = NULL;
      const char* namesFile = NULL;
      const char* directories[64];
      size_t directoryCount = 0;

      for (int i = 0; i < argc; ++i)
      {
          if (strcmp(argv[i], "-o") == 0)
          {
              outputOption = textOption(argc, argv, &i);
          }
          else if (strcmp(argv[i], "--names") == 0)
          {
              namesFile = textOption(argc, argv, &i);
          }
          else if (strcmp(argv[i], "--curve-run") == 0)
          {
              options.curveRun = countOption(argc, argv, &i, 1);
          }
          else if (strcmp(argv[i], "--decimal-comma") == 0)
          {
              decimalComma = true;
          }
          else if (strcmp(argv[i], "--no-time") == 0)
          {
              options.noTime = true;
          }
          else if (argv[i][0] == '-' || directoryCount == sizeof(directories) / sizeof(directories[0]))
          {
              fprintf(stderr, "Error: report: unknown option or too many folders: %s\n", argv[i]);
              return EXIT_FAILURE;
          }
          else
          {
              directories[directoryCount++] = argv[i];
          }
      }
      if (directoryCount == 0)
      {
          fprintf(stderr, "Usage: split_kmeans report [-o dir] [--names file] [--curve-run i] [--decimal-comma] [--no-time] <batch dir>...\n");
          return EXIT_FAILURE;
      }
      if (outputOption != NULL && strlen(outputOption) >= PATH_MAX)
      {
          fprintf(stderr, "Error: -o path too long\n");
          return EXIT_FAILURE;
      }

      // The folders must come from one configuration
      char first[RUN_CONFIG_SIZE];
      char other[RUN_CONFIG_SIZE];
      reportReadHeader(directories[0], first, sizeof(first));
      for (size_t f = 1; f < directoryCount; ++f)
      {
          reportReadHeader(directories[f], other, sizeof(other));
          if (strcmp(first, other) != 0)
          {
              size_t line = 0;
              char ours[1024];
              char theirs[1024];
              do
              {
                  textLine(first, line, ours, sizeof(ours));
                  textLine(other, line, theirs, sizeof(theirs));
                  ++line;
              } while (strcmp(ours, theirs) == 0 && strcmp(ours, "(none)") != 0);
              fprintf(stderr, "Error: %s and %s come from different configurations:\n  %s\n  %s\n",
                  directories[0], directories[f], ours, theirs);
              return EXIT_FAILURE;
          }
      }

      ReportDataset* datasets = NULL;
      const size_t datasetCount = reportCollect(directories, directoryCount, &datasets);
      if (datasetCount == 0)
      {
          fprintf(stderr, "Error: no <Algo>_runs.csv files in the dataset folders of %s\n", directories[0]);
          return EXIT_FAILURE;
      }
      for (size_t d = 0; d < datasetCount; ++d)
      {
          for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
          {
              if (datasets[d].algorithms[a].runs.count > 0)
              {
                  reportPrepareLog(datasets[d].name, &datasets[d].algorithms[a], algorithmOptions[a].id, options.curveRun);
              }
          }
      }

      size_t* order = malloc(datasetCount * sizeof(size_t));
      handleMemoryError(order);
      reportOrder(namesFile, datasets, datasetCount, order);

      char outputDirectory[PATH_MAX];
      if (outputOption != NULL)
      {
          STRCPY(outputDirectory, sizeof(outputDirectory), outputOption);
      }
      else
      {
          joinPath(outputDirectory, sizeof(outputDirectory), directories[0], "report");
      }
      if (!makeDirectories(outputDirectory))
      {
          // exit, as the other report errors after reading: datasets and order are still allocated
          perror("Error: Unable to create the report folder");
          exit(EXIT_FAILURE);
      }

      // summary.csv, and the summaries behind the tables and the curve end points
      ReportSummary* summaries = calloc(datasetCount * ALGORITHM_COUNT, sizeof(ReportSummary));
      handleMemoryError(summaries);
      FILE* summaryFile = reportOpen(outputDirectory, "summary.csv");
      fputs(options.noTime
          ? "dataset;algorithm;runs;sse_mean;sse_sd;ci_mean;relative_ci;success_pct\n"
          : "dataset;algorithm;runs;sse_mean;sse_sd;ci_mean;relative_ci;success_pct;time_ms_mean;time_ms_sd;full_time_ms_mean\n",
          summaryFile);
      for (size_t o = 0; o < datasetCount; ++o)
      {
          const size_t d = order[o];
          for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
          {
              const ReportAlgorithm* data = &datasets[d].algorithms[a];
              if (data->runs.count == 0) continue;

              const Algorithm algorithm = algorithmOptions[a].id;
              ReportSummary* summary = &summaries[d * ALGORITHM_COUNT + a];
              *summary = reportSummarize(data->runs.rows, data->runs.count);
              if (algorithm == ALGO_RS)
              {
                  // RS results end at the last SSE improvement of each run; full_time_ms_mean keeps the time of every swap
                  const double fullMs = summary->msMean;
                  ReportRows cut = reportRsAtLastImprovement(data);
                  *summary = reportSummarize(cut.rows, cut.count);
                  free(cut.rows);
                  reportWriteSummaryRow(summaryFile, &datasets[d], algorithm, summary, &fullMs, options.noTime);
              }
              else
              {
                  reportWriteSummaryRow(summaryFile, &datasets[d], algorithm, summary, NULL, options.noTime);
              }
          }
          reportWriteCurves(outputDirectory, &datasets[d], &summaries[d * ALGORITHM_COUNT], &options);
      }
      fclose(summaryFile);

      reportWriteTable(outputDirectory, "table_ci.csv", TABLE_CI, datasets, order, datasetCount, summaries);
      reportWriteTable(outputDirectory, "table_success.csv", TABLE_SUCCESS, datasets, order, datasetCount, summaries);
      reportWriteTable(outputDirectory, "table_sse.csv", TABLE_SSE, datasets, order, datasetCount, summaries);
      if (!options.noTime)
      {
          reportWriteTable(outputDirectory, "table_time.csv", TABLE_TIME, datasets, order, datasetCount, summaries);
      }

      printf("Report: %zu dataset%s from %zu folder%s -> %s\n", datasetCount, datasetCount == 1 ? "" : "s",
             directoryCount, directoryCount == 1 ? "" : "s", outputDirectory);

      for (size_t d = 0; d < datasetCount; ++d)
      {
          for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
          {
              free(datasets[d].algorithms[a].runs.rows);
              free(datasets[d].algorithms[a].log.rows);
          }
      }
      free(datasets);
      free(order);
      free(summaries);
      return EXIT_SUCCESS;
  }

  static void printUsage(const char* program)
  {
      printf("Usage: %s [options] [-k <K> <data.txt> [gt.txt]]\n", program);
      printf("       %s ci <centroids.txt> <ground-truth.txt>       prints the Centroid Index\n", program);
      printf("       %s sse <data.txt> <centroids.txt>              prints the SSE (nearest centroids)\n", program);
      printf("       %s gt-from-partition <data.txt> <partition.txt> <out.txt>\n", program);
      printf("                                writes the cluster means (labels 1..K, one per line)\n");
      printf("       %s report [-o dir] [--names file] [--curve-run i] [--decimal-comma] [--no-time] <batch dir>...\n", program);
      printf("                                merges batch folders into summary.csv, the seminar tables and the curves\n\n");
      printf("Split k-means clustering experiments, version %s.\n", SKM_VERSION);
      printf("Without a data file, runs batch mode over data/, gt/ and centroids/.\n\n");

      printf("Single dataset:\n");
      printf("  -k <K>              Number of clusters (required with a data file)\n");
      printf("  <data.txt>          Input data file (whitespace-separated doubles)\n");
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
      printf("  --max-swaps <N>     Random Swap trial swaps per run (default: 5000)\n");
      printf("  --repeats <N>       Repeated k-means repeats per run (default: 1000)\n");
      printf("  --bisect-tries <N>  Bisecting k-means tries per split (default: 5)\n");
      printf("  --init <method>     Initial centroids of KM, RKM and RS: random (default) or kmeans++\n");
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
 * @brief Program entry: subcommands (ci, sse, gt-from-partition, report), batch mode without a data file, CLI mode with one.
 *
 * Batch mode enumerates `data/`, `gt/`, `centroids/` and pairs files by sorted order.
 * CLI mode needs `-k <K> <data.txt>`; the ground truth file is optional.
 * Options (see printUsage) apply to both modes; all of them are validated before any output folder exists.
 *
 * @return 0 on success
 * @errors Exits on invalid arguments, file I/O errors, missing K parameter, or validation failures.
 */
  int main(int argc, char* argv[])
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
      opt.init = INIT_RANDOM;
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
          else if (strcmp(arg, "--algo") == 0)
          {
              if (!parseAlgorithmList(textOption(argc, argv, &i), opt.algorithms))
              {
                  fprintf(stderr, "Error: --algo requires a comma-separated list of: "
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
                  fprintf(stderr, "Error: --snapshots requires none or RUNS[:ITERATIONS][:split], for example 1 or 1:all:split\n");
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
      // Validate everything before any output folder is created
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
          fprintf(stderr, "Error: --first-run %zu is after the last run %zu (set the last run with -r)\n", opt.firstRun, opt.runs);
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
              fprintf(stderr, "Directory mismatch: data=%zu, gt=%zu, centroids=%zu\n",
                  dataCount, gtCount, kCount);
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
              ++selectedCount;
          }
      }

      // Tracking defaults: batch mode logs the rows of every run and the snapshots of run 1; CLI mode nothing
      TrackOptions track;
      track.rows = !noTrack && (trackProgressGiven || !cliMode);
      track.inner = trackInner;
      const char* snapshotsText = noTrack ? "none" : (snapshotsOption != NULL ? snapshotsOption : (cliMode ? "none" : "1"));
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
          if (!makeDirectories(outputDirectory))
          {
              perror("Error: Unable to create output folder");
              exit(EXIT_FAILURE);
          }
      }
      else
      {
          createUniqueDirectory(outputDirectory, sizeof(outputDirectory));
      }

      writeRunConfig(outputDirectory, &opt, &track, snapshotsText, datasetsOption != NULL ? datasetsOption : "all", cliMode, argc, argv);

      const size_t firstRun = opt.firstRun - 1;

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
                  currentGtFile[0] = '\0';  // No ground truth
              }

              baseName = removeExtension(cliDataFile);
              char* baseNameOnly = strrchr(baseName, '/');
              if (!baseNameOnly) baseNameOnly = strrchr(baseName, '\\');
              if (baseNameOnly) baseNameOnly++; else baseNameOnly = baseName;
              baseName = baseNameOnly;

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

          // Create dataset subdirectory
          char datasetDirectory[PATH_MAX];
          createDatasetDirectory(outputDirectory, baseName, datasetDirectory, sizeof(datasetDirectory));

          // Read data and ground truth
          DataPoints dataPoints = readDataPoints(currentDataFile);
          if (dataPoints.size == 0)
          {
              fprintf(stderr, "--> Skipping %s (no data points)\n", baseName);
              continue;
          }
          const size_t numDimensions = dataPoints.points[0].dimensions;

          if (currentK > dataPoints.size)
          {
              fprintf(stderr, "Error: K (%zu) exceeds the number of data points (%zu) in %s\n",
                  currentK, dataPoints.size, currentDataFile);
              exit(EXIT_FAILURE);
          }

          Centroids groundTruth = { NULL, 0 };
          if (currentGtFile[0] != '\0')
          {
              groundTruth = readCentroids(currentGtFile, logLevel);
              if (groundTruth.size > 0 && groundTruth.points[0].dimensions != dataPoints.points[0].dimensions)
              {
                  fprintf(stderr, "Error: Ground truth %s has %zu dimensions, data %s has %zu\n",
                      currentGtFile, groundTruth.points[0].dimensions, currentDataFile, dataPoints.points[0].dimensions);
                  exit(EXIT_FAILURE);
              }
          }

          writeDatasetInfo(datasetDirectory, currentDataFile, currentGtFile, currentK, dataPoints.size, numDimensions);

          // Batch pairing is by sorted position: show which files went together
          if (logLevel >= 2) printf("Starting process\n");
          printf("Dataset: %s (data %s, ground truth %s, K from %s)\n", baseName, currentDataFile,
              currentGtFile[0] != '\0' ? currentGtFile : "none", currentKSource);
          if (groundTruth.size > 0 && groundTruth.size != currentK)
          {
              fprintf(stderr, "Warning: %s: the ground truth %s has %zu centroids but K is %zu; CI compares sets of different sizes\n",
                  baseName, currentGtFile, groundTruth.size, currentK);
          }
          printf("Dimensions: %zu, Data points: %zu, K: %zu, Runs: %zu\n\n",
              numDimensions, dataPoints.size, currentK, currentLoops);
          
          // Algorithm parameters
          size_t scaling = 1; // SSE divisor of the summary row and the console statistics (1: unscaled; the report scales SSE)
          const size_t rkmRsLoops = opt.runsGiven ? currentLoops : 1;

          for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
          {
              if (!opt.algorithms[a])
              {
                  continue;
              }

              const Algorithm algorithmId = algorithmOptions[a].id;
              switch (algorithmId)
              {
              case ALGO_KM:
                  runKMeansAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxIterations, firstRun, currentLoops, scaling, baseName, datasetDirectory, &track, opt.init, opt.seed);
                  break;
              case ALGO_RKM:
                  runRepeatedKMeansAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxIterations, opt.maxRepeats, firstRun, rkmRsLoops, scaling, baseName, datasetDirectory, &track, opt.init, opt.seed);
                  break;
              case ALGO_RS:
                  runRandomSwapAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxSwaps, firstRun, rkmRsLoops, scaling, baseName, datasetDirectory, &track, opt.init, opt.seed);
                  break;
              case ALGO_SKM_RANDOM:
                  runRandomSplitAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxIterations, firstRun, currentLoops, scaling, baseName, datasetDirectory, &track, opt.seed);
                  break;
              case ALGO_BISECTING:
                  runBisectingKMeansAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxIterations, firstRun, currentLoops, scaling, baseName, datasetDirectory, &track, opt.bisectTries, opt.seed);
                  break;
              default: // SKM-Intra, SKM-Global, SKM-Global-Basic, SKM-Local
                  runSseSplitAlgorithm(&dataPoints, &groundTruth, currentK, opt.maxIterations, firstRun, currentLoops, scaling, baseName, datasetDirectory, algorithmId, &track, opt.seed);
                  break;
              }
          }

          // Clean up
          freeDataPoints(&dataPoints);
          freeCentroids(&groundTruth);
      }

      freeStringList(dataNames, dataCount);
      freeStringList(gtNames, dataCount);
      freeStringList(kNames, dataCount);
      free(selectedSets);

      return 0;
  }
