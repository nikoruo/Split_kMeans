/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
*/

/* Update log
* --------------------------------------------------------------------
* Version 1.0.0 - 22-10-2025 by Niko Ruohonen TODO
* - Initial release
* --------------------------------------------------------------------
* Update 1.1...
* - ...
*/

// Introduction
/**
* Project Name: Split_kMeans
*
* Description:
* This project focuses on the development and implementation of various clustering algorithms.
* The primary goal was to create a novel clustering algorithm, the SSE Split Algorithm,
* and also to implement existing algorithms. All algorithms were designed and optimized
* to ensure maximum efficiency and effectiveness when applied to multi-dimensional data points.
*
* Author: Niko Ruohonen
* Date: 22-10-2025
* Version: 1.0.0
*
* Details:
* - Implements multiple clustering algorithms:
        K-means
        Repeated K-means
        Random Swap
        Random Split
        SSE Split (Intra-cluster, Global, Local Repartition)
        Bisecting K-means.
* - Provides detailed logging options for debugging and performance analysis. (<- commented off by default for performance)
* - Includes memory management functions to handle dynamic allocation and deallocation of data structures.
* - Supports reading and writing data points and centroids from/to files.
* - Calculates various metrics such as Sum of Squared Errors (SSE) and Centroid Index (CI) to evaluate clustering performance.
* - Provides two centroid initialization methods:
        Random centroids (default)
        K-means++ (KMeans++) seeding (available)
*   Current builds seed with random centroids. To enable K-means++ for seeding,
*   replace the call to generateRandomCentroids with generateKMeansPlusPlusCentroids
*   in the algorithm setup where initial centroids are chosen.
*
* Usage:
* 1. Directory Batch Mode (no command-line arguments):
*    The project can be run by executing the main function, which initializes datasets, ground truth files, and clustering parameters.
*    It then runs different clustering algorithms on each dataset and writes the results to output files.
*
*    Structure of the project directory:
*    ProjectRoot/
        ├─ data/
        │  ├─ datasetA.txt               # points: one data point per line; all rows same dimensionality
        │  └─ datasetB.txt
        ├─ gt/
        │  ├─ datasetA-gt.txt            # ground truth centroids: one centroid per line; dimensions must match the corresponding data file
        │  └─ datasetB-gt.txt
        └─ centroids/
            ├─ datasetA_k.txt                 # contains ONLY a single positive integer: the number of clusters K (no extra text/BOM)
            └─ datasetB_k.txt
*
*   Pairing and file counts:
*   - Input files are discovered at runtime via directory enumeration (list_files) from "data/", "gt/", and "centroids/".
*   - The number of files in these three folders must match; otherwise the program exits with an error.
*   - Files are paired by sorted filename order. To avoid mismatches, keep base names aligned across folders,
*       e.g., "datasetA.txt" <-> "datasetA-gt.txt" <-> "datasetA_k.txt".
*
* 2. CLI Mode (with command-line arguments):
*    - Run the executable with arguments to process a single dataset.
*    - Syntax: split.exe -k <K> [-r <runs>] [--track-progress] [--track-time] <data.txt> [gt.txt]
*    - Required: data file and K value (via -k flag)
*    - Optional: ground truth file, number of runs, progress tracking, and time tracking
*    - K must be explicitly specified via -k; there is no fallback to .txt files in CLI mode
*    - When runs=1, both --track-progress and --track-time are automatically enabled
*    - Files can be located anywhere; full or relative paths are accepted
*    - Examples:
*        split.exe mydata.txt -k 5
*        split.exe C:\datasets\mydata.txt -k 5 -r 50 --track-progress C:\datasets\groundtruth.txt
*        split.exe -h (displays help)
*
*   CLI Options:
*       -k <K>              (Required) Number of clusters
*       -r, --runs <N>      Number of independent runs (default: 100)
*       --track-progress    Enable per-iteration statistics and snapshot files
*       --track-time        Record elapsed times per iteration
*       -h, --help          Display usage information
*       [gt.txt]            Optional ground truth file for CI calculation; if omitted, CI uses empty set*
*
* Results:
*  ProjectRoot/
    └─ outputs/
    └─ 2025-09-29_14-32-10/       # created automatically
        ├─ datasetA/
        │  ├─ RKM_log.csv
        │  ├─ RKM_times.txt
        │  ├─ RKM_iteration_stats.txt
        │  ├─ repeatedKMeans_centroids.txt
        │  ├─ repeatedKMeans_partitions.txt
        │  ├─ kMeans_centroids_perfect.txt
        │  ├─ kMeans_partitions_perfect.txt
        │  ├─ RandomSwap_centroids_failed.txt
        │  ├─ RandomSwap_partitions_failed.txt
        │  └─ ... (depends on which algorithms you run)
        └─ datasetB/
            └─ ...
*
* Notes:
*   Batch Mode: 
*   - Ensure that the "data/", "gt/", and "centroids/" folders contain the same number of files, with matching base names,
*       so files pair correctly across folders.
*   - Each data file must be space-separated doubles; all rows must have the same number of dimensions (columns).
*   - Each ground-truth file (gt/\*.txt) must use the same dimensionality as its corresponding data file.
*   - Each K file (centroids/\*.txt) must contain a single positive integer K and nothing else.
* - Outputs are written under outputs/<timestamp>/<dataset-base-name>/.
* - CSV files use semicolons (;) as separators.
* - Numeric formatting in some outputs depends on the current C locale. The program sets fi_FI at startup for Excel compatibility on Finnish systems, (<- commented off by default for compatibility)
*   and some files are intentionally written using the "C" locale for dot decimal separators.
* - Future plans include adding more clustering algorithms and improving the performance of existing ones.
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

// Language-specific includes
#include "locale_utils.h"

// General includes
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
#include <limits.h>
#include <stdlib.h>

/////////////
// Macros //
///////////

#define LINE_BUFSZ 16384 /* tweak if needed */

/**
* @brief Converts clock ticks to milliseconds.
*
* Divide the result of (end - start) by this value to get milliseconds.
*/
#define CLOCKS_PER_MS ((double)CLOCKS_PER_SEC / 1000.0)

//////////////
// GLOBALS //
////////////

// For logging level
// 1 = none, 2 = debug, 3 = everything
// Most LOGGING lines are commented out for performance
static const size_t LOGGING = 1;

//////////////
// Structs //
////////////

/**
 * @brief Represents a single data point in a multi-dimensional space.
 *
 * The point owns its `attributes` array. `partition` is a zero-based cluster label;
 * `SIZE_MAX` indicates "unassigned".
 */
typedef struct
{
    double* attributes;  /**< Coordinates of the data point; length == dimensions. */
    size_t dimensions;   /**< Number of dimensions (length of the attributes array). */
    size_t partition;    /**< Zero-based cluster label; SIZE_MAX means unassigned. */
} DataPoint;

/**
 * @brief A contiguous collection of data points.
 *
 * Ownership: the container owns the array and each element's `attributes`.
 * Use `freeDataPoints` to release.
 */
typedef struct
{
    DataPoint* points;   /**< Array of DataPoint structures (size elements). */
    size_t size;         /**< Number of data points. */
} DataPoints;

/**
 * @brief A collection of centroids (cluster representatives).
 *
 * Ownership: the container owns the array and each element's `attributes`.
 * Use `freeCentroids` to release.
 */
typedef struct
{
    DataPoint* points;   /**< Array of centroid points (size elements). */
    size_t size;         /**< Number of centroids (K). */
} Centroids;

/**
 * @brief Aggregates a single clustering outcome.
 *
 * - sse: Sum of Squared Errors over all points (not mean).
 * - partition: length-N array of zero-based cluster labels.
 * - centroids: array of K centroid points.
 * - centroidIndex: CI metric for comparing to a reference (optional).
 *
 * Ownership: `partition` and `centroids` are owned; free via `freeClusteringResult`.
 */
typedef struct
{
    double sse;           /**< Sum of Squared Errors (SSE) for the clustering result. */
    size_t* partition;    /**< Array of length N with zero-based cluster labels. */
    DataPoint* centroids; /**< Array of K centroid points. */
    size_t centroidIndex; /**< Centroid Index (CI) value (optional). */
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
} Statistics;


///////////////
// Memories //
/////////////

/**
 * @brief Aborts the process if an allocation returned NULL.
 *
 * Centralizes out-of-memory handling so callers remain branchless.
 * Only use after allocating a non-zero number of bytes; for size 0,
 * malloc/calloc/realloc may legally return NULL.
 */
void handleMemoryError(const void* ptr)
{
    if (ptr == NULL)
    {
        fprintf(stderr, "Error: Unable to allocate memory\n");
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Deinitializes a data point: frees its attributes and resets fields.
 */
void deinitDataPoint(DataPoint* point)
{
    //if (point == NULL) return;

    if (point->attributes != NULL)
    {
        free(point->attributes);
        point->attributes = NULL;
    }
    point->dimensions = 0;
    point->partition = SIZE_MAX;
}

/**
 * @brief Frees an array of DataPoint elements and the array itself.
 */
void freeDataPointArray(DataPoint* points, size_t size)
{
    //if (points == NULL) return;

    for (size_t i = 0; i < size; ++i)
    {
        deinitDataPoint(&points[i]);
    }
    free(points);
}

/**
 * @brief Releases owned `points` array (and its elements) and resets the container.
 */
 void freeDataPoints(DataPoints* dataPoints)
 {
    //if (dataPoints == NULL) return;
    //if (dataPoints->points == NULL) return;

    freeDataPointArray(dataPoints->points, dataPoints->size);
    dataPoints->points = NULL;
    dataPoints->size = 0;
 }

 /**
  * @brief Releases owned centroid array (and its elements) and resets the container.
  */
 void freeCentroids(Centroids* centroids)
 {
     //if (centroids == NULL) return;
     //if (centroids->points == NULL) return;

     freeDataPointArray(centroids->points, centroids->size);
     centroids->points = NULL;
     centroids->size = 0;
 }

 /**
  * @brief Releases owned arrays of a clustering outcome and resets pointers.
  *
  * Frees the `partition` array and the centroid array (including each centroid’s attributes).
  * Does not free the `ClusteringResult` object itself. The caller must pass the number of
  * centroids contained in `result->centroids`.
  */
 void freeClusteringResult(ClusteringResult* result, size_t numCentroids)
 {
     //if (result == NULL) return;

     if (result->partition != NULL)
     {
         free(result->partition);
         result->partition = NULL;
     }

     if (result->centroids != NULL)
     {
         freeDataPointArray(result->centroids, numCentroids);
         result->centroids = NULL;
     }
 }

 /**
  * @brief Allocates an array of `size` writable PATH_MAX strings, zero-initialized.
  */
 char** createStringList(size_t size)
  {
     // if (size == 0) return NULL;

     char** list = calloc(size, sizeof(char*));
     handleMemoryError(list);

     const size_t stringSize = PATH_MAX;

     for (size_t i = 0; i < size; ++i)
     {
         list[i] = calloc(stringSize, sizeof(char));
         handleMemoryError(list[i]);
     }
     
     return list;
  }

 /**
  * @brief Frees each string then the list container.
  */
  void freeStringList(char** list, size_t size)
  {
      //if (list == NULL) return;

      for (size_t i = 0; i < size; ++i)
      {
          free(list[i]);
      }
      free(list);
  }

  /**
   * @brief Creates a point with `dimensions` doubles, zero-initialized.
   */
  DataPoint allocateDataPoint(size_t dimensions)
  {
      /*if (dimensions == 0)
      {
          fprintf(stderr, "Error: allocateDataPoint requires dimensions > 0\n");
          exit(EXIT_FAILURE);
      }*/

      DataPoint point;
      point.attributes = calloc(dimensions, sizeof(double)); /* zero-init for safe += use */
      handleMemoryError(point.attributes);
      point.dimensions = dimensions;
      point.partition = SIZE_MAX; // Initialize partition to default value, here SIZE_MAX. Cant use -1 as its size_t

      return point;
  }

  /**
   * @brief Allocates an array of `size` points, each with `dimensions` zeroed attributes.
   */
  DataPoints allocateDataPoints(size_t size, size_t dimensions)
  {
      DataPoints dataPoints;
      dataPoints.points = malloc(size * sizeof(DataPoint));
      handleMemoryError(dataPoints.points);
      dataPoints.size = size;

      for (size_t i = 0; i < size; ++i)
      {
          dataPoints.points[i] = allocateDataPoint(dimensions);
      }

      return dataPoints;
  }

  /**
   * @brief Allocates a centroid array of `size` points with `dimensions` attributes each.
   */
  Centroids allocateCentroids(size_t size, size_t dimensions)
  {
      /*if (dimensions == 0 || size == 0)
      {
          fprintf(stderr, "Error: allocateCentroids requires dimensions AND size > 0\n");
          exit(EXIT_FAILURE);
      }*/

      Centroids centroids;
      centroids.size = size;
      centroids.points = malloc(size * sizeof(DataPoint));
      handleMemoryError(centroids.points);

      for (size_t i = 0; i < size; ++i)
      {
          centroids.points[i] = allocateDataPoint(dimensions);
      }

      return centroids;
  }

  /**
   * @brief Builds a clustering result with partition buffer and K centroids.
   */
  ClusteringResult allocateClusteringResult(size_t numDataPoints, size_t numCentroids, size_t dimensions)
  {
      /*if (numCentroids == 0 || dimensions == 0)
      {
          fprintf(stderr, "Error: allocateClusteringResult requires dimensions > 0 and numCentroids > 0\n");
          exit(EXIT_FAILURE);
      }*/

      ClusteringResult result;
      result.sse = DBL_MAX;
      result.centroidIndex = SIZE_MAX;
      result.partition = malloc(numDataPoints * sizeof(size_t));
      handleMemoryError(result.partition);

      for (size_t i = 0; i < numDataPoints; ++i)
      {
          result.partition[i] = SIZE_MAX;
      }
      
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
      /*if (stats == NULL)
      {
          fprintf(stderr, "Error: Null pointer passed to initializeStatistics\n");
          exit(EXIT_FAILURE);
      }*/

      stats->sseSum = 0.0;
      stats->ciSum = 0;
      stats->timeSum = 0.0;
      stats->successRate = 0.0; /* used as count; averaged later */
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
   * @brief Calculates the Euclidean distance between two data points.
   * 
   * @note (NOT USED)
   */
  double calculateEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
  {
      return sqrt(calculateSquaredEuclideanDistance(point1, point2));
  }

  /**
   * @brief Handles file opening errors and terminates the program.
   */
  void handleFileError(const char* filename)
  {
      fprintf(stderr, "Error: Unable to open file '%s'\n", filename);
      exit(EXIT_FAILURE);
  }

  /**
   * @brief Handles file read errors and terminates the program.
   */
  void handleFileReadError(const char* filename)
  {
      fprintf(stderr, "Error: Unable to read from file '%s'\n", filename);
      exit(EXIT_FAILURE);
  }

  /**
   * @brief Count whitespace-separated values on the first non-empty line.
   *
   * Scans the file from the beginning and counts tokens on the first line that
   * contains any non-whitespace. Tokens are sequences of non-isspace characters
   * separated by spaces/tabs. Ignores CR characters. Returns 0 if no tokens are
   * found (e.g., empty file or only blank lines).
   *
   * @param filename Path to the data file.
   * @return Number of columns (dimensions) detected, or 0 if none.
   * @errors Exits on open/read errors (uses handleFileError/handleFileReadError).
   * @note Reads character-by-character to avoid fixed-line-buffer truncation.
   */
  size_t getNumDimensions(const char* filename)
  {
      FILE* file;
      if (FOPEN(file, filename, "r") != 0)
      {
          handleFileError(filename);
      }

      // Inform static analyzer: 'file' is non-NULL on success
      _Analysis_assume_(file != NULL);

      size_t dimensions = 0;
      bool inToken = false;
      bool foundNonWhitespaceOnLine = false;

      for (;;)
      {
          int ch = fgetc(file);

          if (ch == EOF)
          {
              if (ferror(file))
              {
                  handleFileReadError(filename);
              }
              // EOF: return whatever was counted (0 if nothing found)
              break;
          }

          if (ch == '\r')
          {
              // Ignore CR to handle CRLF seamlessly
              continue;
          }

          if (ch == '\n')
          {
              if (foundNonWhitespaceOnLine)
              {
                  // Finished the first non-empty line
                  break;
              }
              // Blank/whitespace-only line; reset state and continue to next line
              inToken = false;
              dimensions = 0;
              foundNonWhitespaceOnLine = false;
              continue;
          }

          if (!isspace((unsigned char)ch))
          {
              foundNonWhitespaceOnLine = true;
              if (!inToken)
              {
                  ++dimensions;
                  inToken = true;
              }
          }
          else
          {
              inToken = false;
          }
      }

      fclose(file);

      return dimensions;
  }

  /**
   * @brief Load whitespace-separated double vectors, enforcing a consistent dimensionality per row.
   *
   * Skips whitespace-only lines. The first non-empty row defines the expected column count;
   * every subsequent non-empty row must match or the function terminates with an error.
   * Returns an empty container if the file has no data rows.
   *
   * @param filename Path to a space/tab-delimited text file of doubles.
   * @return DataPoints with size equal to the number of data rows; each point owns its attributes
   *         (length == expectedDims), dimensions set to expectedDims, and partition = SIZE_MAX.
   * @errors Exits on file open/read failures, allocation failures, or inconsistent column counts.
   * @note Parsing uses strtod and the current locale; prefer LC_NUMERIC="C" for dot decimals.
   */
  DataPoints readDataPoints(const char* filename)
  {
      FILE* file;
      if (FOPEN(file, filename, "r") != 0)
      {
          handleFileError(filename);
      }

      // First pass: count data rows and establish expected dimensionality
      size_t lineCount = 0;
      size_t expectedDims = SIZE_MAX;
      char countBuffer[LINE_BUFSZ];

      while (fgets(countBuffer, sizeof(countBuffer), file) != NULL)
      {
          // Determine if the line contains any non-whitespace characters
          bool hasTokenChar = false;
          for (const unsigned char* p = (const unsigned char*)countBuffer; *p; ++p)
          {
              if (!isspace(*p))
              {
                  hasTokenChar = true;
                  break;
              }
          }
          if (!hasTokenChar)
          {
              continue; // whitespace-only line
          }

          // Count tokens to validate dimensionality
          char tmp[LINE_BUFSZ];
          STRCPY(tmp, sizeof(tmp), countBuffer);
          size_t tokenCount = 0;
          char* ctx = NULL;
          char* tok = STRTOK(tmp, " \t\r\n", &ctx);
          while (tok != NULL)
          {
              ++tokenCount;
              tok = STRTOK(NULL, " \t\r\n", &ctx);
          }

          if (tokenCount == 0)
          {
              continue; // defensive: no tokens after tokenization
          }

          if (expectedDims == SIZE_MAX)
          {
              expectedDims = tokenCount;
          }
          else if (tokenCount != expectedDims)
          {
              fclose(file);
              fprintf(stderr,
                  "Error: Inconsistent column count in '%s' (expected %zu, got %zu)\n",
                  filename, expectedDims, tokenCount);
              exit(EXIT_FAILURE);
          }

          ++lineCount;
      }

      // No data rows -> return empty container
      if (lineCount == 0)
      {
          fclose(file);
          DataPoints empty = { NULL, 0 };
          return empty;
      }

      // Second pass: parse into structures
      rewind(file);

      DataPoints dataPoints;
      dataPoints.size = lineCount;
      dataPoints.points = malloc(lineCount * sizeof(DataPoint));
      handleMemoryError(dataPoints.points);

      char line[LINE_BUFSZ];
      size_t currentPoint = 0;

      while (fgets(line, sizeof(line), file) && currentPoint < lineCount)
      {
          // Skip whitespace-only lines
          bool hasTokenChar = false;
          for (const unsigned char* p = (const unsigned char*)line; *p; ++p)
          {
              if (!isspace(*p))
              {
                  hasTokenChar = true;
                  break;
              }
          }
          if (!hasTokenChar)
          {
              continue;
          }

          // Count tokens to confirm dimensionality
          size_t tokenCount = 0;
          char lineCopy[LINE_BUFSZ];
          STRCPY(lineCopy, sizeof(lineCopy), line);
          char* countContext = NULL;
          for (char* ct = STRTOK(lineCopy, " \t\r\n", &countContext);
              ct != NULL;
              ct = STRTOK(NULL, " \t\r\n", &countContext))
          {
              ++tokenCount;
          }

          if (tokenCount == 0)
          {
              continue; // defensive
          }
          if (expectedDims != SIZE_MAX && tokenCount != expectedDims)
          {
              fclose(file);
              freeDataPointArray(dataPoints.points, currentPoint);
              fprintf(stderr,
                  "Error: Inconsistent column count in '%s' (expected %zu, got %zu)\n",
                  filename, expectedDims, tokenCount);
              exit(EXIT_FAILURE);
          }

          // Initialize point and allocate attributes
          DataPoint* point = &dataPoints.points[currentPoint];
          point->attributes = malloc(expectedDims * sizeof(double));
          handleMemoryError(point->attributes);
          point->dimensions = expectedDims;
          point->partition = SIZE_MAX;

          // Parse values
          char* context = NULL;
          char* token = STRTOK(line, " \t\r\n", &context);
          size_t dim = 0;
          while (token != NULL && dim < expectedDims)
          {
              point->attributes[dim++] = strtod(token, NULL);
              token = STRTOK(NULL, " \t\r\n", &context);
          }

          // Final sanity check
          if (dim != expectedDims)
          {
              fclose(file);
              freeDataPointArray(dataPoints.points, currentPoint + 1);
              fprintf(stderr,
                  "Error: Row truncated in '%s' (expected %zu values, got %zu)\n",
                  filename, expectedDims, dim);
              exit(EXIT_FAILURE);
          }

          ++currentPoint;
      }

      fclose(file);

      if (currentPoint != lineCount)
      {
          fprintf(stderr,
              "Error: Expected to read %zu points but read %zu points\n",
              lineCount, currentPoint);
          freeDataPointArray(dataPoints.points, currentPoint);
          exit(EXIT_FAILURE);
      }

      return dataPoints;
  }

  /**
   * @brief Reads centroids from a whitespace-delimited text file.
   *
   * Treats each non-empty row as one centroid; columns are coordinates. Reuses the
   * allocation from `readDataPoints` and returns it as `Centroids` (ownership transfers
   * to the returned object; free with `freeCentroids`).
   *
   * @param filename Path to a space/tab-separated text file of doubles.
   * @return Centroids with `size == rows` and each centroid’s `dimensions == columns`.
   * @errors Exits on open/read errors or inconsistent column counts (via readDataPoints).
   * @note Skips whitespace-only lines; parsing uses the current C locale for `strtod`.
   */
  Centroids readCentroids(const char* filename)
  {
      DataPoints points = readDataPoints(filename);

      Centroids centroids;
      centroids.size = points.size;
      centroids.points = points.points;

      //printf("Read %zu centroids from %s, each with %zu dimensions\n", centroids.size, filename, centroids.points[0].dimensions);

      /*if (LOGGING >= 3)
      {
          for (size_t i = 0; i < centroids.size; ++i)
          {
              printf("Centroid %zu (dimensions: %zu) attributes: ", i, points.points[i].dimensions);
              for (size_t j = 0; j < points.points[i].dimensions; ++j)
              {
                  printf("%.0f ", points.points[i].attributes[j]);
              }
              printf("\n");
          }
      }*/

      return centroids;
  }

  /**
   * @brief Reads a single positive integer K from a file (strict, no extra content).
   */
  static size_t readKFromFile(const char* path)
  {
      FILE* fp;
      if (FOPEN(fp, path, "rb") != 0)
      {
          handleFileError(path);
      }

      /* Skip UTF-8 BOM if present */
      int c1 = fgetc(fp);
      if (c1 == 0xEF)
      {
          int c2 = fgetc(fp);
          int c3 = fgetc(fp);
          if (!(c2 == 0xBB && c3 == 0xBF))
          {
              if (c3 != EOF) ungetc(c3, fp);
              if (c2 != EOF) ungetc(c2, fp);
              ungetc(c1, fp);
          }
      }
      else if (c1 != EOF)
      {
          ungetc(c1, fp);
      }

      long k = 0;
      if (fscanf(fp, "%ld", &k) != 1 || k <= 0)
      {
          fclose(fp);
          fprintf(stderr, "Bad K in %s\n", path);
          exit(EXIT_FAILURE);
      }

      /* Ensure no extra non-whitespace content remains */
      int ch;
      do { ch = fgetc(fp); } while (ch != EOF && isspace((unsigned char)ch));
      if (ch != EOF)
      {
          fclose(fp);
          fprintf(stderr, "Bad K (extra content) in %s\n", path);
          exit(EXIT_FAILURE);
      }

      fclose(fp);
      return (size_t)k;
  }

  /**
   * @brief Appends a CSV row: ci;iteration;sse.
   */
  void appendLogCsv(const char* filePath, size_t iteration, size_t ci, double sse)
  {
      FILE* file;
      if (FOPEN(file, filePath, "a") != 0)
      {
          handleFileError(filePath);
          return;
      }
      /* Columns: ci;iteration;sse */
      fprintf(file, "%zu;%zu;%.0f\n", ci, iteration, sse);
      fclose(file);
  }

  /**
   * @brief Returns true if the path exists (file or directory).
   */
  bool fileExists(const char* filePath)
  {
      struct stat buffer;
      return (stat(filePath, &buffer) == 0);
  }

  /**
   * @brief Writes centroids as space-separated rows to outputDirectory/filename.
   *
   * One centroid per line; attributes separated by a single space. Uses the current
   * process numeric locale (does not modify LC_NUMERIC). The destination directory
   * must exist.
   *
   * @param filename Target file name (no path).
   * @param centroids Centroids to write; writes exactly `centroids->size` rows.
   * @param outputDirectory Destination directory; must exist.
   * @return void
   * @errors Exits on open failure via handleFileError. Ignores write errors.
   * @note Output format relies on the current locale’s decimal separator.
   */
  void writeCentroidsToFile(const char* filename, const Centroids* centroids, const char* outputDirectory)
  {
      char outputFilePath[PATH_MAX];
      snprintf(outputFilePath, sizeof(outputFilePath), "%s%c%s", outputDirectory, PATHSEP, filename);

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
      snprintf(outputFilePath, sizeof(outputFilePath), "%s%c%s", outputDirectory, PATHSEP, filename);

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
   * @brief Deep-copies coordinates and metadata from source into destination.
   *
   * Resizes `destination->attributes` as needed and copies `source->attributes`,
   * `dimensions`, and `partition`. Safe for repeated calls on the same destination.
   *
   * @param destination Target point; must be a valid object. `attributes` may be NULL or allocated.
   * @param source Source point to copy from.
   * @return void
   * @errors Exits on allocation failure via handleMemoryError.
   * @note No-op if `destination == source`. Uses `realloc` to minimize churn.
   */
  void deepCopyDataPoint(DataPoint* destination, const DataPoint* source)
  {
      //if (destination == source) return;
      /*if(destination == NULL || source == NULL)
      {
          fprintf(stderr, "Error: Null pointer passed to deepCopyDataPoint\n");
          exit(EXIT_FAILURE);
      }*/

      /* Resize or allocate attributes buffer */
      double* newAttrs = (double*)realloc(destination->attributes, source->dimensions * sizeof(double));
      handleMemoryError(newAttrs);

      destination->attributes = newAttrs;
      destination->dimensions = source->dimensions;
      destination->partition = source->partition;

      memcpy(destination->attributes, source->attributes, source->dimensions * sizeof(double));
  }

  /**
   * @brief Deep-copies an array of points element-by-element.
   */
  void deepCopyDataPoints(DataPoint* destination, const DataPoint* source, size_t size)
  {
      /*if (destination == NULL || source == NULL)
      {
          fprintf(stderr, "Error: Null pointer passed to deepCopyDataPoints\n");
          exit(EXIT_FAILURE);
      }*/

      for (size_t i = 0; i < size; ++i)
      {
          deepCopyDataPoint(&destination[i], &source[i]);
      }
  }

  /**
   * @brief Replaces destination with a deep copy of source centroids.
   *
   * Frees any existing centroids in `destination`, then allocates and copies all
   * centroid points and their attributes from `source`. Supports empty `source`.
   *
   * @param source Centroid set to copy from.
   * @param destination Target centroid set to overwrite.
   * @return void
   * @errors Exits on allocation failure via handleMemoryError.
   * @note `destination` assumes ownership of newly allocated memory.
   */
  void deepCopyCentroids(const Centroids* source, Centroids* destination)
  {
      if (destination->points != NULL)
      {
          freeDataPointArray(destination->points, destination->size);
          destination->points = NULL;
          destination->size = 0;
      }

      /*if (source->size == 0)
      {
          destination->points = NULL;
          destination->size = 0;
          return;
      }*/

      destination->size = source->size;

      size_t dimensions = source->points[0].dimensions;
      destination->points = allocateDataPoints(destination->size, dimensions).points;

      for (size_t i = 0; i < destination->size; ++i)
      {
          deepCopyDataPoint(&destination->points[i], &source->points[i]);
      }
  }

  /**
   * @brief Sets all data point partitions to 0.
   */
  void resetPartitions(DataPoints* dataPoints)
  {
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          dataPoints->points[i].partition = 0;
      }
  }

  /**
   * @brief Appends a single CSV summary row with aggregate metrics.
   *
   * Opens/creates `<outputDirectory>/<filename>.csv`, writes a header once when empty,
   * then appends averages across `loopCount` runs. Keeps output append-only to preserve history.
   *
   * @param filename Base name (without extension) used to build the CSV file path.
   * @param stats Accumulated sums (CI, SSE, time, successes) across runs.
   * @param numCentroids K for the algorithm to compute relative CI.
   * @param algorithm Human-readable algorithm label written to the first column.
   * @param loopCount Number of runs included in stats (must be > 0).
   * @param scaling Divisor for SSE to adjust units (must be > 0).
   * @param outputDirectory Destination directory; must exist.
   * @return void
   * @errors Exits on open failure via handleFileError. Write errors are not checked.
   * @note Uses the process locale for numeric formatting; CSV uses semicolons as separators.
   */
  void writeResultsToFile(const char* filename, Statistics stats, size_t numCentroids, const char* algorithm, size_t loopCount, size_t scaling, const char* outputDirectory)
  {
      char csvFileName[PATH_MAX];
      snprintf(csvFileName, sizeof(csvFileName), "%s.csv", filename);
      char outputFilePath[PATH_MAX];
      snprintf(outputFilePath, sizeof(outputFilePath), "%s%c%s", outputDirectory, PATHSEP, csvFileName);

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

      fprintf(file, "%s;%.2f;%.0f;%.2f;%.0f;%.2f\n", algorithm, avgCI, sse, relCI, avgTime, succRate);

      fclose(file);
  }

  /**
   * @brief Creates a timestamped output directory under "outputs".
   *
   * Ensures the base folder "outputs" exists, then creates `outputs/<YYYY-MM-DD_HH-MM-SS>`.
   * On success, writes the created path into `outputDirectory`.
   *
   * @param outputDirectory Buffer that receives the created path.
   * @param size Buffer capacity in bytes; must be large enough for the path.
   * @return void
   * @errors Exits on failure to format time, buffer overflow, or mkdir error (other than EEXIST).
   * @note Uses local time. Does not remove or clean existing directories.
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

      if (snprintf(outputDirectory, size, "outputs%c%s", PATHSEP, datebuf) >= (int)size)
      {
          fprintf(stderr, "Buffer too small in createUniqueDirectory\n");
          exit(EXIT_FAILURE);
      }

      if (MAKE_DIR(outputDirectory) != 0 && errno != EEXIST)
      {
          perror("Error: Unable to create directory");
          exit(EXIT_FAILURE);
      }
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
      if (snprintf(datasetDirectory, size, "%s%c%s", baseDirectory, PATHSEP, datasetName) >= (int)size)
      {
          fprintf(stderr, "Buffer too small in createDatasetDirectory\n");
          exit(EXIT_FAILURE);
      }

      if (MAKE_DIR(datasetDirectory) != 0 && errno != EEXIST)
      {
          perror("Error: Unable to create dataset directory");
          exit(EXIT_FAILURE);
      }
  }

  /**
   * @brief Maps an algorithm id to a short stable name.
   */
  const char* getAlgorithmName(size_t algorithmId)
  {
      switch (algorithmId)
      {
      case 0: return "SKM-Intra";
      case 1: return "SKM-Global";
      case 2: return "SKM-Local";
      case 3: return "RS";
      case 4: return "BisectingKM";
      case 5: return "SKM-Random";
      case 6: return "KM";
      case 7: return "RKM";
      default:
          fprintf(stderr, "Error: Invalid algorithm type provided: %zu\n", algorithmId);
          return "Unknown";
      }
  }

  /**
   * @brief Prints a human-readable summary of aggregate metrics.
   *
   * Reports averages across `loopCount` runs and a success rate in percent. Intended for
   * console diagnostics alongside CSV output.
   *
   * @param algorithmName Human-readable label to prefix each line.
   * @param stats Accumulated sums across runs.
   * @param loopCount Number of runs included (must be > 0).
   * @param numCentroids K for relative CI (must be > 0 to avoid divide-by-zero).
   * @param scaling Divisor applied to SSE in CSV (not used here; retained for parity).
   * @param dataSize Dataset size; currently printed for context.
   * @return void
   * @errors None (writes to stdout).
   * @note Uses the process locale for numeric formatting.
   */
  void printStatistics(const char* algorithmName, Statistics stats, size_t loopCount, size_t numCentroids, size_t scaling, size_t dataSize)
  {
      (void)scaling; /* not used in this textual summary */

      printf("(%s) Average CI: %.2f and SSE: %.0f\n", algorithmName, (double)stats.ciSum / loopCount, stats.sseSum / loopCount / scaling);
      printf("(%s) Relative CI: %.2f\n", algorithmName, (double)stats.ciSum / loopCount / numCentroids);
      printf("(%s) Average time taken: %.0f ms\n", algorithmName, stats.timeSum / loopCount);
      printf("(%s) Success rate: %.2f%%\n\n", algorithmName, stats.successRate / loopCount * 100);
  }

  /**
   * @brief Returns a copy of filename without its trailing extension.
   *
   * Uses an internal static buffer. Removes the last '.' only if it appears
   * after the last path separator; preserves leading-dot names (e.g., ".env").
   * Not thread-safe.
   */
  char* removeExtension(const char* filename)
  {
      static char baseFileName[PATH_MAX];

      /* Copy input with guaranteed NUL-termination */
      STRCPY(baseFileName, sizeof(baseFileName), filename);

      /* Find last path separator (support both styles) */
      char* lastSep = strrchr(baseFileName, '/');
      char* lastSep2 = strrchr(baseFileName, '\\');
      if (lastSep2 && (!lastSep || lastSep2 > lastSep))
          lastSep = lastSep2;

      /* Find last dot and ensure it is within the basename and not the very first basename char */
      char* lastDot = strrchr(baseFileName, '.');
      char* basenameStart = lastSep ? lastSep + 1 : baseFileName;

      if (lastDot && lastDot > basenameStart)
      {
          *lastDot = '\0';
      }

      return baseFileName;
  }

  /**
   * @brief Ensures a per-algorithm CSV log exists and returns its path.
   *
   * Builds `<outputDirectory>/<AlgorithmName>_log.csv`. If the file does not exist,
   * creates it and writes the header line. The resolved path is written to `csvFilePath`.
   *
   * @param splitType Algorithm id mapped via getAlgorithmName.
   * @param outputDirectory Directory where the CSV should reside.
   * @param csvFilePath Output buffer receiving the full CSV path.
   * @param csvFilePathSize Capacity of `csvFilePath` in bytes.
   * @return void
   * @errors Exits on buffer overflow or file open failure (via handleFileError).
   * @note Header schema: "ci;iteration;sse".
   */
  void initializeCsvFile(size_t splitType, const char* outputDirectory, char* csvFilePath, size_t csvFilePathSize)
  {
      const char* algorithmName = getAlgorithmName(splitType);

      if (snprintf(csvFilePath, csvFilePathSize, "%s%c%s_log.csv", outputDirectory, PATHSEP, algorithmName) >= (int)csvFilePathSize)
      {
          fprintf(stderr, "Buffer too small in initializeCsvFile\n");
          exit(EXIT_FAILURE);
      }

      if (!fileExists(csvFilePath))
      {
          FILE* csvFile;
          if (FOPEN(csvFile, csvFilePath, "w") != 0)
          {
              handleFileError(csvFilePath);
          }
          fprintf(csvFile, "%s\n", "ci;iteration;sse");
          fclose(csvFile);
      }
  }

  /**
   * @brief Writes per-iteration durations to a text file (one value per line).
   *
   * Creates `<outputDirectory>/<AlgorithmName>_times.txt` and writes `timeIndex` values
   * from `timeList` as integer milliseconds (rounded). Intended for post-run analysis.
   *
   * @param outputDirectory Destination directory; must exist.
   * @param splitType Algorithm id used to name the file via getAlgorithmName.
   * @param timeList Array of elapsed times in milliseconds.
   * @param timeIndex Number of valid entries in `timeList`.
   * @return void
   * @errors Exits on open failure via handleFileError. Write errors are not checked.
   * @note Uses text mode; each line is a single integer-like value (%.0f).
   */
  void writeTimeTrackingData(const char* outputDirectory, size_t splitType, const double* timeList, size_t timeIndex)
  {
      //if (timeIndex == 0 || timeList == NULL) return;

      const char* algorithmName = getAlgorithmName(splitType);

      char timesFile[PATH_MAX];
      if (snprintf(timesFile, sizeof(timesFile), "%s%c%s_times.txt", outputDirectory, PATHSEP, algorithmName) >= (int)sizeof(timesFile))
      {
          fprintf(stderr, "Buffer too small in writeTimeTrackingData\n");
          exit(EXIT_FAILURE);
      }

      FILE* timesFilePtr;
      if (FOPEN(timesFilePtr, timesFile, "w") != 0)
      {
          handleFileError(timesFile);
      }

      for (size_t i = 0; i < timeIndex; ++i)
      {
          fprintf(timesFilePtr, "%.0f\n", timeList[i]);
      }

      fclose(timesFilePtr);
  }

  /////////////////
  // Clustering //
  ///////////////

  /**
   * @brief Picks K unique data points uniformly and copies them as initial centroids.
   *
   * Uses a partial Fisher–Yates shuffle to select `numCentroids` distinct indices from
   * `dataPoints` without replacement, then deep-copies those points into `centroids`.
   * Selection of swap index uses rejection sampling (no modulo bias).
   *
   * @param numCentroids Number of centroids to select (K); must be <= dataPoints->size and > 0.
   * @param dataPoints Source dataset; `dataPoints->size` must be > 0.
   * @param centroids Destination centroid container; must have at least K elements.
   * @return void
   * @errors Exits if K is invalid, destination too small, or on allocation failure.
   * @note O(N) init + O(K) swaps. Thread-unsafe due to RANDOMIZE.
   */
  void generateRandomCentroids(size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids)
  {
      const size_t N = dataPoints->size;

      /*if (numCentroids == 0 || N == 0 || numCentroids > N)
      {
          fprintf(stderr, "Error: Invalid K (%zu) for dataset size (%zu)\n", numCentroids, N);
          exit(EXIT_FAILURE);
      }
      if (centroids->size < numCentroids)
      {
          fprintf(stderr, "Error: centroids array too small (%zu < %zu)\n",
              centroids->size, numCentroids);
          exit(EXIT_FAILURE);
      }*/

      /* Overflow guard for allocation */
      /*if (N > SIZE_MAX / sizeof(size_t))
      {
          handleMemoryError(NULL);
      }*/

      size_t* indices = (size_t*)malloc(N * sizeof(size_t));
      handleMemoryError(indices);

      /* init indices 0..N-1 */
      for (size_t i = 0; i < N; ++i)
      {
          indices[i] = i;
      }

      unsigned int randomValue;
      for (size_t i = 0; i < numCentroids; ++i)
      {
          RANDOMIZE(randomValue);
          size_t j = i + randomValue % (dataPoints->size - i);
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
   * @brief Seeds centroids using the KMeans++ strategy with a robust fallback.
   *
   * Chooses the first centroid uniformly at random; each subsequent centroid is chosen
   * with probability proportional to the squared distance to the nearest chosen centroid.
   * In degenerate cases where all distances are zero, fills remaining centroids by random picks.
   *
   * @param numCentroids Number of centroids to choose (K); must be <= dataPoints->size.
   * @param dataPoints Input dataset (N points).
   * @param centroids Output container for K centroids (preallocated).
   * @return void
   * @errors Exits on invalid K or allocation failure (via handleMemoryError).
   * @note Maintains a distance cache for O(KN) seeding cost. (NOT-USED)
   */
  void generateKMeansPlusPlusCentroids(size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids)
  {
      /*if (numCentroids == 0 || dataPoints->size == 0 || numCentroids > dataPoints->size)
      {
          fprintf(stderr, "Error: Invalid K (%zu) for dataset size (%zu)\n", numCentroids, dataPoints->size);
          exit(EXIT_FAILURE);
      }*/

      /* 1) Choose the first centroid at random */
      unsigned int randomValue;
      RANDOMIZE(randomValue);
      size_t firstIndex = (size_t)(randomValue % dataPoints->size);
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

          // 3. Select a new centroid with probability r
          RANDOMIZE(randomValue);
          double r = (double)randomValue / ((double)UINT_MAX + 1.0) * distSum;
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

  /**
   * @brief Sums squared distances from each point to its assigned centroid.
   *
   * Assumes every point has a valid `partition` in `[0, centroids->size)`. Exits if an
   * invalid index is encountered to surface upstream assignment errors.
   *
   * @param dataPoints Dataset whose partitions are already assigned.
   * @param centroids Centroid set referenced by partitions.
   * @return Total SSE across all points.
   * @errors Exits on invalid partition index.
   */
  double calculateSSE(const DataPoints* dataPoints, const Centroids* centroids)
  {
      double sse = 0.0;

      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          size_t cIndex = dataPoints->points[i].partition;

          /*if (cIndex >= centroids->size)
          {
              fprintf(stderr, "Error: Invalid partition index %zu for data point %zu\n", cIndex, i);
              exit(EXIT_FAILURE);
          }*/

          sse += calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[cIndex]);
      }

      return sse;
  }

  /**
   * @brief Returns SSE normalized by number of values (N·D).
   *
   * Computes MSE = SSE / (N·D). Returns 0.0 when the dataset is empty or dimensions is 0
   * to avoid divide-by-zero.
   *
   * @param dataPoints Dataset used for the denominator and distances.
   * @param centroids Centroids referenced by point partitions.
   * @return Mean squared error (MSE).
   * @errors Exits on invalid partition indices via calculateSSE.
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
   * @param centroids Centroid set; `clusterLabel` must be < `centroids->size`.
   * @param clusterLabel Target cluster id.
   * @return SSE for the specified cluster.
   * @errors Exits if `clusterLabel` is out of range.
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
   * @brief Returns the index of the nearest centroid by squared Euclidean distance.
   *
   * Requires at least one centroid and matching dimensionality between the query point
   * and centroids. Fails fast on invalid inputs to surface upstream issues.
   *
   * @param queryPoint Data point to classify; `dimensions` must match centroid dimensionality.
   * @param targetCentroids Centroids to compare against; `size` must be > 0.
   * @return Zero-based index of the nearest centroid.
   * @errors Exits if no centroids exist or if dimensionality mismatches.
   * @note O(K·D) per call; distance squared avoids sqrt.
   */
  size_t findNearestCentroid(const DataPoint* queryPoint, const Centroids* targetCentroids)
  {
      /*if (targetPoints->size == 0)
      {
          fprintf(stderr, "Error: Cannot find nearest centroid in an empty set of data\n");
          exit(EXIT_FAILURE);
      }*/

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
   * Requires at least one centroid; exits when none exist.
   *
   * @param dataPoints Points to label; updated in-place.
   * @param centroids Current centroids; `size` must be > 0.
   * @return void
   * @errors Exits if no centroids exist.
   */
  void partitionStep(DataPoints* dataPoints, const Centroids* centroids)
  {
      /*if (dataPoints->size == 0 || centroids->size == 0)
      {
          fprintf(stderr, "Error: Cannot perform optimal partition with empty data or centroids\n");
          exit(EXIT_FAILURE);
      }*/

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
   * zero assignments (keeps previous coordinates). Validates partitions and dimensionality.
   *
   * @param centroids Centroids to update in-place; `size` must be > 0.
   * @param dataPoints Dataset with assigned partitions.
   * @return void
   * @errors Exits if a point has an out-of-range partition or dimensionality mismatch.
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
          /*else
          {
              if(LOGGING >= 3) fprintf(stderr, "Warning: Cluster %zu has no points assigned.\n", clusterLabel);
          }*/
      }

      free(sums);
      free(counts);
  }

  /**
   * @brief Counts centroids in `centroids2` with no nearest centroid from `centroids1`.
   *
   * For each centroid in `centroids1`, marks its nearest neighbor in `centroids2`.
   * Returns how many centroids in `centroids2` were never marked. If `centroids2` is empty,
   * returns 0. If `centroids1` is empty, returns `centroids2->size`.
   *
   * @param centroids1 Reference set used to mark nearest neighbors.
   * @param centroids2 Target set whose unmarked count is returned.
   * @return Number of orphan centroids in `centroids2`.
   * @errors Exits via `findNearestCentroid` if dimensionality mismatches.
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
   * selecting them as nearest. Returns 0 when both sets are empty; equals the size
   * of the non-empty set when compared against an empty one.
   *
   * @param centroids1 First centroid set.
   * @param centroids2 Second centroid set.
   * @return CI value (non-negative integer).
   * @errors Exits via underlying functions on dimensionality mismatch.
   * @note O(K^2·D) due to nearest-centroid queries across sets.
   */
  size_t calculateCentroidIndex(const Centroids* centroids1, const Centroids* centroids2)
  {
      /*if (LOGGING >= 2)
      {
          printf("centroids1: %zu AND 2: %zu\n\n", centroids1->size, centroids2->size);
          printf("centroids1: %f AND 2: %f\n\n", centroids1->points->attributes[0], centroids2->points->attributes[0]);
      }*/

      size_t countFrom1to2 = countOrphans(centroids1, centroids2);
      size_t countFrom2to1 = countOrphans(centroids2, centroids1);

      /*if (LOGGING >= 2)
      {
          printf("Count from 1 to 2: %zu AND 2 to 1: %zu\n\n", countFrom1to2, countFrom2to1);
      }*/

      return (countFrom1to2 > countFrom2to1) ? countFrom1to2 : countFrom2to1;
  }

  /**
   * @brief Persists a snapshot of centroids and partitions for a given iteration.
   *
   * Produces two files in `outputDirectory`: `<algo>_centroids_iter_<n>.txt`
   * and `<algo>_partitions_iter_<n>.txt`. Supports post-run visualization and debugging.
   *
   * @param dataPoints Dataset to serialize partitions from.
   * @param centroids Centroids to serialize as space-separated rows.
   * @param iteration Zero-based iteration counter used in filenames.
   * @param outputDirectory Destination directory; must exist.
   * @param algorithmName Short algorithm tag used as filename prefix.
   * @return void
   * @errors Exits on buffer overflow in path formatting or file open failure via sub-writers.
   * @note File format matches other writers in this module.
   */
  void saveIterationState(const DataPoints* dataPoints, const Centroids* centroids,
      size_t iteration, const char* outputDirectory, const char* algorithmName)
  {
      char centroidsFileName[PATH_MAX];
      char partitionsFileName[PATH_MAX];

    if (snprintf(centroidsFileName, sizeof(centroidsFileName),
                 "%s_centroids_iter_%zu.txt", algorithmName, iteration) >= (int)sizeof(centroidsFileName))
    {
        fprintf(stderr, "Buffer too small in saveIterationState (centroids file)\n");
        exit(EXIT_FAILURE);
    }

    if (snprintf(partitionsFileName, sizeof(partitionsFileName),
                 "%s_partitions_iter_%zu.txt", algorithmName, iteration) >= (int)sizeof(partitionsFileName))
    {
        fprintf(stderr, "Buffer too small in saveIterationState (partitions file)\n");
        exit(EXIT_FAILURE);
    }

      writeCentroidsToFile(centroidsFileName, centroids, outputDirectory);
      writeDataPointPartitionsToFile(partitionsFileName, dataPoints, outputDirectory);
  }

  /**
   * @brief Appends per-iteration metrics to `<outputDirectory>/<algo>_iteration_stats.txt`.
   *
   * Writes a header line once when the file is empty, then appends a row with
   * iteration, K, SSE, CI, and split cluster id. Keeps a single growing log across runs.
   *
   * @param dataPoints Dataset used to derive metrics (SSE via caller).
   * @param centroids Centroids at this iteration (for K and CI).
   * @param groundTruth Reference centroids for CI.
   * @param iteration Zero-based iteration number.
   * @param sse Sum of squared errors at this iteration.
   * @param splitCluster Cluster id split this iteration or SIZE_MAX when N/A.
   * @param outputDirectory Destination directory; must exist.
   * @param algorithmName Algorithm tag used for the log filename.
   * @return void
   * @errors Exits on path overflow or open failure.
   * @note Semicolon-separated CSV-like text; numeric formatting uses current locale.
   */
  void writeIterationStats(const DataPoints* dataPoints, const Centroids* centroids,
      const Centroids* groundTruth, size_t iteration, double sse,
      size_t splitCluster, const char* outputDirectory, const char* algorithmName)
  {
      (void)dataPoints; /* not used directly; retained for signature symmetry */

      char statsFileName[PATH_MAX];
      if (snprintf(statsFileName, sizeof(statsFileName), "%s_iteration_stats.txt", algorithmName) >= (int)sizeof(statsFileName))
      {
          fprintf(stderr, "Buffer too small in writeIterationStats (name)\n");
          exit(EXIT_FAILURE);
      }

      char outputFilePath[PATH_MAX];
      if (snprintf(outputFilePath, sizeof(outputFilePath), "%s%c%s", outputDirectory, PATHSEP, statsFileName) >= (int)sizeof(outputFilePath))
      {
          fprintf(stderr, "Buffer too small in writeIterationStats (path)\n");
          exit(EXIT_FAILURE);
      }

      FILE* statsFile = NULL;
      if (FOPEN(statsFile, outputFilePath, "a+") != 0)
      {
          handleFileError(outputFilePath);
      }

      /* Write header once when the file is empty */
      fseek(statsFile, 0, SEEK_END);
      if (ftell(statsFile) == 0)
      {
          fprintf(statsFile, "Iteration;NumCentroids;SSE;CI;SplitCluster\n");
      }

      size_t ci = calculateCentroidIndex(centroids, groundTruth);
      fprintf(statsFile, "%zu;%zu;%.0f;%zu;%zu\n",
          iteration, centroids->size, sse, ci, splitCluster);

      fclose(statsFile);
  }

  /**
   * @brief Writes iteration stats and snapshot files for the current state.
   */
  static void trackProgressState(const DataPoints* dataPoints, const Centroids* centroids, const Centroids* groundTruth, size_t iteration, size_t clusterToSplit, size_t splitType, const char* outputDirectory)
  {
      const char* splitTypeName = getAlgorithmName(splitType);
      double currentSse = calculateSSE(dataPoints, centroids);
      writeIterationStats(dataPoints, centroids, groundTruth, iteration, currentSse, clusterToSplit, outputDirectory, splitTypeName);
      saveIterationState(dataPoints, centroids, iteration, outputDirectory, splitTypeName);
  }

  /**
   * @brief Appends elapsed milliseconds since start into `timeList` at `*timeIndex`.
   */
  static void updateTimeTracking(bool trackTime, clock_t start, double* timeList, size_t* timeIndex)
  {
      (void)trackTime; /* decision handled by caller */

      clock_t iterEnd = clock();
      double iterDuration = ((double)(iterEnd - start)) / CLOCKS_PER_MS;
      timeList[(*timeIndex)++] = iterDuration;
  }

  /**
   * @brief Appends CI and SSE for the current iteration to the CSV file.
   */
  static void updateCsvLogging(bool createCsv, const DataPoints* dataPoints, const Centroids* centroids, const Centroids* groundTruth, const char* csvFile, size_t iterationNumber)
  {
      (void)createCsv; /* guarded by caller */

      size_t currentCi = calculateCentroidIndex(centroids, groundTruth);
      double currentSse = calculateSSE(dataPoints, centroids);
      appendLogCsv(csvFile, iterationNumber, currentCi, currentSse);
  }

  /**
   * @brief Coordinates optional time tracking, iteration snapshots, and CSV appends.
   *
   * Applies the three logging actions depending on the provided flags. Intended to
   * centralize progress reporting logic and keep algorithm loops clean.
   *
   * @param trackTime Whether to record elapsed time into `timeList`.
   * @param start Clock taken at the beginning of the enclosing operation.
   * @param timeList Output array for elapsed times in ms.
   * @param timeIndex In/out index into `timeList`.
   * @param trackProgress Whether to write iteration stats and snapshot files.
   * @param dataPoints Current dataset state (partitions used for stats/snapshots).
   * @param centroids Current centroid state.
   * @param groundTruth Reference centroids for CI calculation.
   * @param iterationCount Iteration number to log.
   * @param outputDirectory Destination directory for files.
   * @param createCsv Whether to append a line to the CSV log.
   * @param csvFile Path to the CSV file (valid if `createCsv` is true).
   * @param clusterToSplit Cluster id split at this iteration or SIZE_MAX when N/A.
   * @param splitType Algorithm id used for labeling outputs.
   * @return void
   * @errors Exits on I/O failures via underlying helpers.
   */
  static void handleLoggingAndTracking(bool trackTime, clock_t start, double* timeList, size_t* timeIndex, bool trackProgress, const DataPoints* dataPoints,
      const Centroids* centroids, const Centroids* groundTruth, size_t iterationCount, const char* outputDirectory, bool createCsv, const char* csvFile,
      size_t clusterToSplit, size_t splitType)
  {
      if (trackTime)
      {
          updateTimeTracking(trackTime, start, timeList, timeIndex);
      }

      //NOTE: tama tehdaan nyt vain iteraatiolle 0. Halutaanko tehda esim niin monta kertaa etta CI=0 tmv? Tai kommentoida kokonaan pois?
      if (trackProgress)
      {
          trackProgressState(dataPoints, centroids, groundTruth, iterationCount, clusterToSplit, splitType, outputDirectory);
      }

      if (createCsv)
      {
          updateCsvLogging(createCsv, dataPoints, centroids, groundTruth, csvFile, iterationCount);
      }
  }

  /**
   * @brief Locally reassigns points after modifying one centroid (Random Swap helper).
   *
   * Performs a two-phase local repartition: (1) reassigns points currently in the
   * modified cluster (`removed`) to their nearest centroid; (2) compares all other
   * points’ current centroid against the modified centroid and moves points that
   * are now closer to it. Leaves other clusters unchanged.
   *
   * @param dataPoints Dataset with current partitions.
   * @param centroids Centroids after the swap; index `removed` denotes the modified centroid.
   * @param removed Index of the centroid whose coordinates were changed.
   * @return void
   * @errors Exits if `removed` is out of range.
   * @note O(N·D). A full centroid update happens later via k-means iterations.
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
   * @brief Iterates assignment and update steps until SSE no longer improves.
   *
   * Repeats partition and centroid steps up to `iterations` times, tracking the best
   * observed SSE and stopping early when the SSE does not strictly decrease.
   *
   * @param dataPoints Dataset to cluster; partitions updated in-place.
   * @param iterations Maximum iterations to perform (stop earlier on no improvement).
   * @param centroids Centroid set updated in-place.
   * @param groundTruth Optional reference (unused here).
   * @return Best SSE observed during the run.
   * @errors Exits if dataset is empty or K == 0.
   * @note Uses strict improvement; add a tolerance if convergence stalls due to rounding.
   */
  double runKMeans(DataPoints* dataPoints, size_t iterations, Centroids* centroids, const Centroids* groundTruth)
  {
      (void)groundTruth;

      double bestSse = DBL_MAX;
      double sse = DBL_MAX;

      for (size_t iteration = 0; iteration < iterations; ++iteration)
      {
          partitionStep(dataPoints, centroids);
          centroidStep(centroids, dataPoints);

          sse = calculateSSE(dataPoints, centroids);

          /*if (LOGGING >= 3)
          {
              size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
              printf("(runKMeans)After iteration %zu: CI = %zu and SSE = %.0f\n", iteration + 1, centroidIndex, sse);
          }*/

          if (sse < bestSse)
          {
              //if (LOGGING >= 3) printf("Best SSE so far: %.5f\n", sse);
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
   * @brief k-means with optional per-iteration logging and timing.
   *
   * Runs the assignment/update loop like `runKMeans`, optionally recording elapsed time,
   * writing snapshots/CI, and appending CSV lines. Increments `*iterationCount` only
   * when SSE improves.
   *
   * @param dataPoints Dataset to cluster; partitions updated in-place.
   * @param iterations Maximum iterations to perform.
   * @param centroids Centroid set updated in-place.
   * @param groundTruth Optional reference for CI in logs.
   * @param outputDirectory Destination for snapshot/stat files when enabled.
   * @param trackProgress Whether to write iteration snapshot/stat files.
   * @param timeList Output array of elapsed times (ms); valid if `trackTime`.
   * @param timeIndex In/out index into `timeList`.
   * @param start Clock at start of outer operation.
   * @param trackTime Whether to record elapsed time into `timeList`.
   * @param createCsv Whether to append per-iteration CSV entries.
   * @param iterationCount In/out iteration number used in logs; incremented on improvements.
   * @param firstRun Whether this is the first run in a multi-run context (affects logging cadence).
   * @param csvFile CSV file path; valid if `createCsv`.
   * @return Best SSE observed during the run.
   * @errors Exits if dataset is empty or K == 0.
   */
  double runKMeansWithTracking(DataPoints* dataPoints, size_t iterations, Centroids* centroids, const Centroids* groundTruth, const char* outputDirectory,
      bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv, size_t* iterationCount, bool firstRun, const char* csvFile)
  {
      double bestSse = DBL_MAX;
      double sse = DBL_MAX;

      for (size_t iteration = 0; iteration < iterations; ++iteration)
      {
          partitionStep(dataPoints, centroids);
          centroidStep(centroids, dataPoints);

          if (firstRun)
          {
              handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
                  dataPoints, centroids, groundTruth, *iterationCount, outputDirectory, createCsv, csvFile, SIZE_MAX, 7);
          }

          sse = calculateSSE(dataPoints, centroids);

          /*if (LOGGING >= 3)
          {
              size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
              printf("(runKMeans)After iteration %zu: CI = %zu and SSE = %.0f\n", iteration + 1, centroidIndex, sse);
          }*/

          if (sse < bestSse)
          {
              //if (LOGGING >= 3) printf("Best SSE so far: %.5f\n", sse);
              bestSse = sse;
          }
          else
          {
              break; // Exit the loop if the SSE does not improve
          }

          (*iterationCount)++;
      }

      return bestSse;
  }

  /**
   * @brief Performs random swaps of centroids and evaluates the resulting clustering using k-means.
   *
   * This function performs random swaps of centroids with data points, runs k-means on the modified centroids,
   * and keeps the changes if the mean squared error (SSE) improves. If the SSE does not improve, it reverses the swap.
   * The function returns the best mean squared error (SSE) obtained during the swaps.
   *
   * @param dataPoints A pointer to the DataPoints structure containing the data points.
   * @param centroids A pointer to the Centroids structure containing the centroids.
   * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
   * @param maxSwaps The maximum number of attempted swaps.
   * @return The best mean squared error (SSE) obtained during the swaps.
   */
  double randomSwap(DataPoints* dataPoints, Centroids* centroids, size_t maxSwaps, const Centroids* groundTruth, const char* outputDirectory,
      bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv)
  {
      double bestSse = DBL_MAX;
      size_t bestCI = SIZE_MAX;
      size_t kMeansIterations = 2;
      size_t numCentroids = centroids->size;
      size_t dimensions = centroids->points[0].dimensions;

      size_t totalAttributes = numCentroids * dimensions;

      double* backupAttributes = malloc(totalAttributes * sizeof(double));
      handleMemoryError(backupAttributes);

      size_t* backupPartitions = malloc(dataPoints->size * sizeof(size_t));
      handleMemoryError(backupPartitions);

      char csvFile[PATH_MAX];
      if (createCsv)
      {
          initializeCsvFile(3, outputDirectory, csvFile, sizeof(csvFile));
      }

      handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
          dataPoints, centroids, groundTruth, 0, outputDirectory, createCsv, csvFile, SIZE_MAX, 3);

      size_t iterationCount = 1; //Helper for tracking progress

      for (size_t i = 0; i < maxSwaps; ++i)
      {
          printf("Swap %zu\n", i + 1);

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
          unsigned int randomValue;

          RANDOMIZE(randomValue);
          size_t randomCentroidId = randomValue % centroids->size;

          RANDOMIZE(randomValue);
          size_t randomDataPointId = randomValue % dataPoints->size;

          memcpy(centroids->points[randomCentroidId].attributes, dataPoints->points[randomDataPointId].attributes, dimensions * sizeof(double));

          localRepartitionForRS(dataPoints, centroids, randomCentroidId);

          //K-means
          double resultSse = runKMeans(dataPoints, kMeansIterations, centroids, groundTruth);

          //If 1) SSE improves, we keep the change
          //if not, 2) we reverse the swap
          if (resultSse < bestSse)
          {
              /*if (LOGGING >= 3)
              {
                  size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
                  printf("(RS) Round %zu: Best CI: %zu and Best SSE: %.5f\n", i + 1, centroidIndex, resultSse);
              }*/

              bestSse = resultSse;

              size_t currentCi = calculateCentroidIndex(centroids, groundTruth);
              bestCI = currentCi;

			  //For all swaps in RS, we want to track progress p1/2
              appendLogCsv(csvFile, iterationCount, currentCi, resultSse);
              updateTimeTracking(trackTime, start, timeList, timeIndex);

			  //For all iterations in RS, we want to track progress
              //handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
                //  dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, SIZE_MAX, 3); 
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

              //For all swaps in RS, we want to track progress p2/2
              appendLogCsv(csvFile, iterationCount, bestCI, bestSse);
              updateTimeTracking(trackTime, start, timeList, timeIndex);
          }

          iterationCount++;
      }

      free(backupAttributes);
      free(backupPartitions);

      return bestSse;
  }

  /**
   * @brief Splits one cluster by running a local k-means within that cluster.
   *
   * Picks two distinct points from the target cluster as seeds, runs k-means (K=2) on
   * that cluster’s points only, then updates the original clustering by replacing the
   * old centroid with one local result and appending the second as a new global centroid.
   * Partitions for the affected points are rewritten accordingly.
   *
   * @param dataPoints All points with current partitions.
   * @param centroids Global centroids; updated in-place (size increases by one).
   * @param clusterToSplit Index of the cluster to split.
   * @param localMaxIterations Max iterations for the local k-means refinement.
   * @param groundTruth Optional reference centroids (unused in computation).
   * @return void
   * @errors Exits on allocation failure.
   */
  void splitClusterIntraCluster(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t localMaxIterations, const Centroids* groundTruth)
  {
      size_t clusterSize = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterSize++;
          }
      }

      // Random split will break without this
      if (clusterSize < 2)
      {
          return;
      }

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
      unsigned int randomValue;
      RANDOMIZE(randomValue);
      size_t c1 = randomValue % clusterSize;
      size_t c2 = c1;
      while (c2 == c1)
      {
          RANDOMIZE(randomValue);
          c2 = randomValue % clusterSize;
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

      //trackProgressState(&pointsInCluster, &localCentroids, groundTruth, 0, clusterToSplit, splitType, outputDirectory);

      // Run local k-means
      runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, groundTruth);

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
 * @brief Splits a cluster locally (k-means) and then refines globally.
 *
 * Runs local k-means within the target cluster to produce two centroids, merges them
 * into the global set (replace + append), then runs a short global k-means refinement
 * over all data to settle assignments.
 *
 * @param dataPoints All points with current partitions.
 * @param centroids Global centroids; updated in-place (size increases by one).
 * @param clusterToSplit Index of the cluster to split.
 * @param localMaxIterations Max iterations for both local and global refinement.
 * @param groundTruth Optional reference (unused in computation).
 * @param splitType For labeling/logging (unused here).
 * @param outputDirectory Output directory for optional logging (unused here).
 * @param trackProgress/timeList/timeIndex/start/trackTime/createCsv/iteration For logging (unused here).
 * @return SSE after the global refinement, or current SSE if the cluster is too small to split.
 * @errors Exits on allocation failure.
 */
  double splitClusterGlobalV2(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t localMaxIterations, const Centroids* groundTruth, size_t splitType, const char* outputDirectory,
      bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv, size_t iteration)
  {
      (void)splitType; (void)outputDirectory; (void)trackProgress; (void)timeList; (void)timeIndex; (void)start; (void)trackTime; (void)createCsv; (void)iteration;

      size_t clusterSize = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterSize++;
          }
      }

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
      unsigned int randomValue;
      RANDOMIZE(randomValue);
      size_t c1 = randomValue % clusterSize;
      size_t c2 = c1;
      while (c2 == c1)
      {
          RANDOMIZE(randomValue);
          c2 = randomValue % clusterSize;
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
      runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, groundTruth);

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

      //saveIterationState(dataPoints, centroids, iteration, outputDirectory, "Global_test");

      double sse = runKMeans(dataPoints, localMaxIterations, centroids, groundTruth);


      // Cleanup
      free(clusterIndices);
      free(pointsInCluster.points);
      freeCentroids(&localCentroids);

      return sse;
  }

  /**
   * @brief Replaces one cluster with two random seeds and runs global refinement.
   *
   * Selects two distinct points from the chosen cluster, overwrites the original centroid
   * with the first, appends the second as a new centroid, then runs global k-means for
   * `globalMaxIterations` to refine assignments over all data.
   *
   * @param dataPoints All points with current partitions.
   * @param centroids Global centroids; updated in-place (size increases by one).
   * @param clusterToSplit Index of the cluster to split.
   * @param globalMaxIterations Max iterations for the global refinement.
   * @param groundTruth Optional reference (unused in computation).
   * @return SSE after global refinement, or current SSE if the cluster is too small to split.
   * @errors Exits on allocation failure.
   */
  double splitClusterGlobal(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t globalMaxIterations, const Centroids* groundTruth, size_t splitType, const char* outputDirectory,
      bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv)
  {
      (void)splitType; (void)outputDirectory; (void)trackProgress; (void)timeList; (void)timeIndex; (void)start; (void)trackTime; (void)createCsv;

      size_t clusterSize = 0;

      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterToSplit)
          {
              clusterSize++;
          }
      }

      /*if (clusterSize < 2)
      {
          return;
      }*/

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
      unsigned int randomValue;
      RANDOMIZE(randomValue);
      size_t c1 = randomValue % clusterSize;
      size_t c2 = c1;
      while (c2 == c1)
      {
          RANDOMIZE(randomValue);
          c2 = randomValue % clusterSize;
      }

      // Get the indices of the randomly selected data points
      size_t datapoint1 = clusterIndices[c1];
      size_t datapoint2 = clusterIndices[c2];

      // Add the first new centroid (overwrite the current centroid at clusterToSplit)
      deepCopyDataPoint(&centroids->points[clusterToSplit], &dataPoints->points[datapoint1]);

      // Add the second new centroid to the global centroids list
      centroids->size++;
      centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint));
      centroids->points[centroids->size - 1] = allocateDataPoint(dataPoints->points[0].dimensions);
      deepCopyDataPoint(&centroids->points[centroids->size - 1], &dataPoints->points[datapoint2]);

      // Run global k-means, this time including the new centroids
      double resultSse = runKMeans(dataPoints, globalMaxIterations, centroids, groundTruth);

      // Cleanup
      free(clusterIndices);

      return resultSse;
  }

  /**
   * @brief Locally reassigns points to either the split cluster or the new cluster.
   *
   * Compares each point currently assigned to “other” clusters against the split
   * centroid and the newly created centroid. If either is closer than the point’s
   * current centroid, moves the point and marks the original cluster as affected.
   * Keeps assignments in the split and new clusters unchanged.
   *
   * @param dataPoints Dataset with current partitions.
   * @param centroids Global centroids; newest centroid is at index `centroids->size - 1`.
   * @param clusterToSplit Index of the cluster that was split.
   * @param clustersAffected Output array (size >= number of clusters) marking touched clusters.
   * @return void
   * @errors Exits if preconditions are violated.
   * @note O(N·D). Intended to be followed by centroid recomputation.
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
   * @return SSE drop (non-negative when the split helps).
   * @errors Exits on allocation failure.
   */
  double tentativeSseDrop(DataPoints* dataPoints, size_t clusterLabel, size_t localMaxIterations, double originalClusterSSE)
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
      unsigned int randomValue;
      RANDOMIZE(randomValue);
      size_t c1 = randomValue % clusterSize;
      size_t c2 = c1;
      while (c2 == c1)
      {
          RANDOMIZE(randomValue);
          c2 = randomValue % clusterSize;
      }

      Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);

      deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[c1]);
      deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[c2]);

      // k-means
      double resultSse = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, NULL);

      double sseDrop = originalClusterSSE - resultSse;

      freeDataPoints(&pointsInCluster);
      freeCentroids(&localCentroids);

      return sseDrop;
  }

  /**
   * @brief Produces a k-means candidate for a given cluster (for bisecting).
   *
   * Runs local k-means with K=2 on the points of `clusterLabel` using unbiased seeding
   * and returns two candidate centroids with the achieved local SSE. Caller compares
   * SSEs across attempts and picks the best pair.
   *
   * @param dataPoints Dataset with current partitions; used to extract the cluster’s points.
   * @param clusterLabel Cluster id to bisect.
   * @param localMaxIterations Max iterations for the local k-means refinement.
   * @param groundTruth Optional reference (unused in computation).
   * @return ClusteringResult with `centroids[2]` and `sse` for the cluster’s points.
   * @errors Exits on allocation failure.
   * @note Allocates `partition` sized to the cluster for symmetry; it is not populated here.
   */
  ClusteringResult tentativeSplitterForBisecting(DataPoints* dataPoints, size_t clusterLabel, size_t localMaxIterations, const Centroids* groundTruth)
  {
      size_t clusterSize = 0;
      for (size_t i = 0; i < dataPoints->size; ++i)
      {
          if (dataPoints->points[i].partition == clusterLabel)
          {
              clusterSize++;
          }
      }

      /*if (clusterSize < 2)
      {
          printf("Not enough points to split the cluster\n");
          exit(EXIT_FAILURE);
      }*/


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
      unsigned int randomValue;
      RANDOMIZE(randomValue);
      size_t c1 = randomValue % clusterSize;
      size_t c2 = c1;
      while (c2 == c1)
      {
          RANDOMIZE(randomValue);
          c2 = randomValue % clusterSize;
      }

      Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);
      deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[c1]);
      deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[c2]);

      ClusteringResult localResult = allocateClusteringResult(dataPoints->size, 2, dataPoints->points[0].dimensions);

      // k-means
      localResult.sse = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, groundTruth);

      deepCopyDataPoint(&localResult.centroids[0], &localCentroids.points[0]);
      deepCopyDataPoint(&localResult.centroids[1], &localCentroids.points[1]);

      freeDataPoints(&pointsInCluster);
      freeCentroids(&localCentroids);;

      return localResult;
  }

  /**
   * @brief Grows K by repeatedly splitting a random cluster with local k-means.
   *
   * Starts from current centroids, repeatedly selects a random cluster, performs an
   * intra-cluster split (local k-means), updates global centroids and partitions,
   * and logs progress if enabled. Stops when `centroids->size == maxCentroids`,
   * then runs a final global k-means and returns its SSE.
   *
   * @param dataPoints Dataset; partitions updated in-place across splits.
   * @param centroids Global centroids; grows until `maxCentroids`.
   * @param maxCentroids Target number of centroids (K).
   * @param maxIterations Max iterations for local/final k-means refinements.
   * @param groundTruth Optional reference for logging (CI).
   * @param outputDirectory Destination directory for logs (when enabled).
   * @param trackProgress/timeList/timeIndex/start/trackTime/createCsv Logging/timing controls and buffers.
   * @return Final SSE after the concluding global k-means.
   * @errors Exits on allocation failure in underlying functions.
   */
  double runRandomSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, const Centroids* groundTruth, const char* outputDirectory,
      bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv)
  {
      char csvFile[PATH_MAX];
      if (createCsv)
      {
          initializeCsvFile(5, outputDirectory, csvFile, sizeof(csvFile));
      }

      handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
          dataPoints, centroids, groundTruth, 0, outputDirectory, createCsv, csvFile, SIZE_MAX, 5);

      size_t iterationCount = 1; //Helper for tracking progress

      unsigned int randomValue;

      while (centroids->size < maxCentroids)
      {
          RANDOMIZE(randomValue);
          size_t clusterToSplit = randomValue % centroids->size;

          splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, groundTruth);

          /*if (LOGGING >= 3)
          {
              size_t ci = calculateCentroidIndex(centroids, groundTruth);
              double sse = SSE(dataPoints, centroids);
              printf("(RandomSplit) Number of centroids: %zu, CI: %zu, and SSE: %.0f \n", centroids->size, ci, sse);
          }*/

          handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
              dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, clusterToSplit, 5);

          iterationCount++;
      }

      double finalResultSse = runKMeans(dataPoints, maxIterations, centroids, groundTruth);

      handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
          dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, SIZE_MAX, 5);

      return finalResultSse;
  }

  /**
   * @brief Iteratively grows K by splitting the cluster that maximizes expected SSE drop.
   *
   * Computes per-cluster SSE drops via tentative local k-means, repeatedly splits the
   * cluster with the highest drop using the selected strategy:
   *  - 0: Intra-cluster split (local k-means on the cluster).
   *  - 1: Global split (local k-means, then global refinement).
   *  - 2: Local repartition (reassigns neighbors to split/new centroids).
   * Logs progress/timing when enabled. Returns the final SSE (from the last refinement).
   *
   * @param dataPoints Dataset; partitions updated in-place.
   * @param centroids Global centroids; grows until `maxCentroids`.
   * @param maxCentroids Target number of centroids (K).
   * @param maxIterations Max iterations for local/global k-means refinements.
   * @param groundTruth Optional reference centroids for logging (CI).
   * @param splitType Strategy selector: 0=Intra-cluster, 1=Global, 2=LocalRepartition.
   * @param outputDirectory Destination directory for logs.
   * @param trackProgress/timeList/timeIndex/start/trackTime/createCsv Logging/timing controls and buffers.
   * @return Final SSE after all splits and final refinement.
   * @errors Exits on allocation failure in underlying helpers.
   */
  double runSseSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, const Centroids* groundTruth, size_t splitType, const char* outputDirectory,
      bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv)
  {
      double finalResultSse = DBL_MAX;

      double* clusterSSEs = malloc(maxCentroids * sizeof(double));
      handleMemoryError(clusterSSEs);

      double* SseDrops = calloc(maxCentroids, sizeof(double));
      handleMemoryError(SseDrops);

      bool* clustersAffected = calloc(maxCentroids, sizeof(bool));
      handleMemoryError(clustersAffected);

      char csvFile[PATH_MAX];
      if (createCsv)
      {
          initializeCsvFile(splitType, outputDirectory, csvFile, sizeof(csvFile));
      }

      handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
          dataPoints, centroids, groundTruth, 0, outputDirectory, createCsv, csvFile, SIZE_MAX, splitType);

      //Only 1 cluster, so no need for decision making
      size_t initialClusterToSplit = 0;
      splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, maxIterations, groundTruth);

      handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
          dataPoints, centroids, groundTruth, 1, outputDirectory, createCsv, csvFile, initialClusterToSplit, splitType);

      for (size_t i = 0; i < centroids->size; ++i)
      {
          clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
          SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i]);
      }

      size_t iterationCount = 2; //Helper for tracking progress

      while (centroids->size < maxCentroids)
      {
          // Choose the cluster that reduces the SSE the most
          size_t clusterToSplit = 0;
          double maxSseDrop = SseDrops[0];

          for (size_t i = 1; i < centroids->size; ++i)
          {
              if (SseDrops[i] > maxSseDrop)
              {
                  maxSseDrop = SseDrops[i];
                  clusterToSplit = i;
              }
          }

          if (splitType == 0) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, groundTruth);
          else if (splitType == 1) finalResultSse = splitClusterGlobalV2(dataPoints, centroids, clusterToSplit, maxIterations, groundTruth, splitType, outputDirectory,
              trackProgress, timeList, timeIndex, start, trackTime, createCsv, iterationCount);
          else if (splitType == 2) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, groundTruth);

          if (centroids->size < maxCentroids)
          {
              if (splitType == 0) // Intra-cluster
              {
                  // Recalculate SSE for the affected clusters
                  clusterSSEs[clusterToSplit] = calculateClusterSSE(dataPoints, centroids, clusterToSplit);
                  clusterSSEs[centroids->size - 1] = calculateClusterSSE(dataPoints, centroids, centroids->size - 1);

                  // Update SseDrops for the affected clusters
                  SseDrops[clusterToSplit] = tentativeSseDrop(dataPoints, clusterToSplit, maxIterations, clusterSSEs[clusterToSplit]);
                  SseDrops[centroids->size - 1] = tentativeSseDrop(dataPoints, centroids->size - 1, maxIterations, clusterSSEs[centroids->size - 1]);
              }
              else if (splitType == 1) // Global
              {
                  for (size_t i = 0; i < centroids->size; ++i)
                  {
                      clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
                      SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i]);
                  }
              }
              else if (splitType == 2) // Local repartition
              {
                  localRepartition(dataPoints, centroids, clusterToSplit, clustersAffected);

                  clustersAffected[clusterToSplit] = true;
                  clustersAffected[centroids->size - 1] = true;

                  // Recalculate SseDrop for affected clusters (old and new)
                  for (size_t i = 0; i < centroids->size; ++i)
                  {
                      if (clustersAffected[i])
                      {
                          clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
                          SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i]);
                      }
                  }

                  memset(clustersAffected, 0, (maxCentroids) * sizeof(bool));
              }
          }
          handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
              dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, clusterToSplit, splitType);

          iterationCount++;

          /*if (LOGGING >= 3 && splitType == 2)
          {
              size_t ci = calculateCentroidIndex(centroids, groundTruth);
              double sse = calculateSSE(dataPoints, centroids);
              printf("(SseSplit) Number of centroids: %zu, CI: %zu, and SSE: %.0f \n", centroids->size, ci, sse);
          }*/
      }

      free(clusterSSEs);
      free(SseDrops);
      free(clustersAffected);

      if (splitType != 1)
      {
          finalResultSse = runKMeans(dataPoints, maxIterations, centroids, groundTruth);
          handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
              dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, SIZE_MAX, splitType);
      }

      return finalResultSse;
  }

  /**
   * @brief Grows K by repeatedly bisecting the cluster with the largest SSE.
   *
   * Starts from the current centroids, performs an initial split to reach K=2,
   * then repeatedly picks the cluster with the highest current SSE, runs several
   * local k-means attempts on that cluster, and applies the best candidate split.
   * Reassigns points globally after each accepted split and updates per-cluster SSEs.
   * Stops when centroids->size == maxCentroids or no cluster has ≥2 points.
   *
   * @param dataPoints Dataset; partitions updated in-place across splits.
   * @param centroids Global centroids; grown in-place up to maxCentroids.
   * @param maxCentroids Target number of clusters (K); must be ≥ 2.
   * @param maxIterations Max iterations for the local k-means and final global k-means.
   * @param groundTruth Optional reference centroids for CI in logs (may be NULL).
   * @param outputDirectory Destination directory for optional iteration logs/snapshots.
   * @param trackProgress/timeList/timeIndex/start/trackTime/createCsv Logging/timing controls.
   * @param bisectingIterations Number of local k-means attempts per split; 0 treated as 1.
   * @return Final SSE after the concluding refinement step.
   * @errors Exits on allocation failures via helpers. Validates K≥2; skips unsplittable clusters.
   * @note Recomputes per-cluster SSE after each global reassignment for robust selection.
   */
  double runBisectingKMeans(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, const Centroids* groundTruth, const char* outputDirectory,
      bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv, size_t bisectingIterations)
  {
      double* SseList = malloc(maxCentroids * sizeof(double));
      handleMemoryError(SseList);
      double bestSse = DBL_MAX;

      DataPoint newCentroid1 = allocateDataPoint(dataPoints->points[0].dimensions);
      DataPoint newCentroid2 = allocateDataPoint(dataPoints->points[0].dimensions);

      char csvFile[PATH_MAX];
      if (createCsv)
      {
          initializeCsvFile(4, outputDirectory, csvFile, sizeof(csvFile));
      }

      handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
          dataPoints, centroids, groundTruth, 0, outputDirectory, createCsv, csvFile, SIZE_MAX, 4);

      //Step 0: Only 1 cluster, so no need for decision making
      size_t initialClusterToSplit = 0;
      splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, maxIterations, groundTruth);

      SseList[0] = calculateClusterSSE(dataPoints, centroids, 0);
      SseList[1] = calculateClusterSSE(dataPoints, centroids, 1);

      handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
          dataPoints, centroids, groundTruth, 1, outputDirectory, createCsv, csvFile, initialClusterToSplit, 4);

      size_t iterationCount = 2; //Helper for tracking progress

      //Repeat until we have K clusters
      for (size_t i = 2; centroids->size < maxCentroids; ++i)
      {
          size_t clusterToSplit = 0;
          double maxSSE = SseList[0];

          //Step 1: Choose the cluster to split (the highest SSE)
          for (size_t j = 1; j < centroids->size; ++j)
          {
              if (SseList[j] > maxSSE)
              {
                  maxSSE = SseList[j];
                  clusterToSplit = j;
              }
          }

          //Repeat for a set number of iterations
          for (size_t j = 0; j < bisectingIterations; ++j)
          {
              ClusteringResult curr = tentativeSplitterForBisecting(dataPoints, clusterToSplit, maxIterations, groundTruth);

              //if (LOGGING >= 3) printf("(RKM) Round %d: Latest CI: %zu and Latest SSE: %.0f\n", repeat, result1.centroidIndex, result1.sse);

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

          partitionStep(dataPoints, centroids); //partition step, vai pitaisiko tallentaa ne loopissa ja tehda tassa sitten muutokset? (esim kaikki 0->clusterToSplit, 1->centroids->size-1)

          //Step 3: Update the SSE list
          SseList[clusterToSplit] = calculateClusterSSE(dataPoints, centroids, clusterToSplit);
          SseList[centroids->size - 1] = calculateClusterSSE(dataPoints, centroids, centroids->size - 1);

          bestSse = DBL_MAX;

          handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
              dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, clusterToSplit, 4);

          iterationCount++;
      }

      //Step 4: Run the final k-means
      double finalResultSse = runKMeans(dataPoints, maxIterations, centroids, groundTruth);

      handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
          dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, SIZE_MAX, 4);

      // Cleanup
      free(SseList);
      deinitDataPoint(&newCentroid1);
      deinitDataPoint(&newCentroid2);

      return finalResultSse;
  }

  /**
   * @brief Runs baseline k-means multiple times and aggregates CI/SSE/time.
   *
   * For each run, seeds K centroids uniformly from the data, performs k-means with
   * early stopping on non-improving SSE, then records SSE, CI vs. ground truth, and
   * elapsed time. Writes one “perfect” and one “failed” example to disk for inspection.
   *
   * @param dataPoints Dataset clustered in-place; partitions get reassigned every run.
   * @param groundTruth Reference centroids for CI; dimensions must match data (may be NULL).
   * @param numCentroids Target K; must be 1..N.
   * @param maxIterations Maximum k-means iterations (early stop on no improvement).
   * @param loopCount Number of independent k-means runs (with new random seeds).
   * @param scaling SSE divisor for CSV (kept for parity; not used in printing).
   * @param fileName Base name for CSV summary file (without extension).
   * @param outputDirectory Destination directory for outputs.
   * @return void
   * @errors Exits on allocation failures; validates basic inputs to prevent invalid K.
   * @note Uses uniform seeding; switch to KMeans++ by replacing the seeding call.
   */
  void runKMeansAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory)
  {
      Statistics stats;
      initializeStatistics(&stats);

      clock_t start, end;
      double duration;
      bool savedZeroResults = false;
      bool savedNonZeroResults = false;

      printf("K-means\n");

      for (size_t i = 0; i < loopCount; ++i)
      {
          printf("Round %zu\n", i + 1);

          resetPartitions(dataPoints);

          Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

          start = clock();

          generateRandomCentroids(numCentroids, dataPoints, &centroids);
          //generateKMeansPlusPlusCentroids(numCentroids, dataPoints, &centroids);

          double resultSse = runKMeans(dataPoints, maxIterations, &centroids, groundTruth);

          end = clock();
          duration = ((double)(end - start)) / CLOCKS_PER_MS;

          size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

          stats.sseSum += resultSse;
          stats.ciSum += centroidIndex;
          stats.timeSum += duration;
          if (centroidIndex == 0) stats.successRate++;

          if (centroidIndex != 0 && savedNonZeroResults == false)
          {
              writeCentroidsToFile("kMeans_centroids_failed.txt", &centroids, outputDirectory);
              writeDataPointPartitionsToFile("kMeans_partitions_failed.txt", dataPoints, outputDirectory);
              savedNonZeroResults = true;
          }
          else if (centroidIndex == 0 && savedZeroResults == false)
          {
              writeCentroidsToFile("kMeans_centroids_perfect.txt", &centroids, outputDirectory);
              writeDataPointPartitionsToFile("kMeans_partitions_perfect.txt", dataPoints, outputDirectory);
              savedZeroResults = true;
          }

          freeCentroids(&centroids);
      }

      printStatistics("K-means", stats, loopCount, numCentroids, scaling, dataPoints->size);
      writeResultsToFile(fileName, stats, numCentroids, "K-means", loopCount, scaling, outputDirectory);
  }

  /**
   * @brief Runs the repeated k-means algorithm on the given data points and centroids.
   *
   * This function iterates through partition and centroid steps, calculates the SSE,
   * and returns the best SSE obtained during the iterations.
   *
   * @param dataPoints A pointer to the DataPoints structure containing the data points.
   * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
   * @param numCentroids The number of centroids to generate.
   * @param maxIterations The maximum number of iterations for the k-means algorithm.
   * @param maxRepeats The maximum number of repeats for the k-means algorithm.
   * @param loopCount The number of loops to run the k-means algorithm.
   * @param scaling A scaling factor for the SSE values.
   * @param fileName The name of the file to write the results to.
   * @param outputDirectory The directory where the results file is located.
   */
  void runRepeatedKMeansAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t maxRepeats, size_t loopCount, size_t scaling,
      const char* fileName, const char* outputDirectory, bool trackProgress, bool trackTime)
  {
      Statistics stats;
      initializeStatistics(&stats);

      clock_t start, end;
      double duration;

	  size_t totalIterations = loopCount * maxRepeats * 5 + 100; //Estimated upper bound for tracking
      double* timeList = malloc(totalIterations * sizeof(double));

      handleMemoryError(timeList);
      size_t timeIndex = 0;

      char csvFile[PATH_MAX];
      if (trackProgress)
      {
          initializeCsvFile(7, outputDirectory, csvFile, sizeof(csvFile));

      }

      printf("Repeated K-means\n");

      for (size_t i = 0; i < loopCount; ++i)
      {
          printf("Round %zu\n", i + 1);

          double bestSse = DBL_MAX;
          size_t bestCI = SIZE_MAX;

          size_t iterationCount = 0; //Helper for tracking progress
          bool firstRun = true;

          Centroids bestCentroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

          start = clock();

          for (size_t j = 0; j < maxRepeats; ++j)
          {
              printf("Repeat %zu\n", j + 1);
              Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);
              generateRandomCentroids(numCentroids, dataPoints, &centroids);
              //generateKMeansPlusPlusCentroids(numCentroids, dataPoints, &centroids);
              double resultSse = runKMeansWithTracking(dataPoints, maxIterations, &centroids, groundTruth, outputDirectory, (i == 0 && trackProgress), timeList, &timeIndex, start, trackTime, trackProgress, &iterationCount, firstRun, csvFile);

              if (resultSse < bestSse)
              {
                  bestSse = resultSse;
                  deepCopyCentroids(&centroids, &bestCentroids);

                  if (!firstRun)
                  {
                      size_t currentCi = calculateCentroidIndex(&centroids, groundTruth);
                      bestCI = currentCi;

                      appendLogCsv(csvFile, iterationCount, currentCi, resultSse);
                      updateTimeTracking(trackTime, start, timeList, &timeIndex);
                  }
                  else
                  {
                      firstRun = false;
                  }
              }
              else
              {
                  appendLogCsv(csvFile, iterationCount, bestCI, bestSse);
                  updateTimeTracking(trackTime, start, timeList, &timeIndex);
              }

              freeCentroids(&centroids);
          }

          end = clock();
          duration = ((double)(end - start)) / CLOCKS_PER_MS;

          size_t centroidIndex = calculateCentroidIndex(&bestCentroids, groundTruth);

          stats.sseSum += bestSse;
          stats.ciSum += centroidIndex;
          stats.timeSum += duration;
          if (centroidIndex == 0) stats.successRate++;

          if (i == 0)
          {
              writeCentroidsToFile("repeatedKMeans_centroids.txt", &bestCentroids, outputDirectory);
              writeDataPointPartitionsToFile("repeatedKMeans_partitions.txt", dataPoints, outputDirectory);
          }

          freeCentroids(&bestCentroids);
      }

      printStatistics("Repeated K-means", stats, loopCount, numCentroids, scaling, dataPoints->size);
      writeResultsToFile(fileName, stats, numCentroids, "Repeated K-means", loopCount, scaling, outputDirectory);

      if (trackTime)
      {
          writeTimeTrackingData(outputDirectory, 7, timeList, timeIndex);
      }

      free(timeList);
  }

  /**
   * @brief Runs the repeated k-means algorithm on the given data points and centroids.
   *
   * This function iterates through partition and centroid steps, calculates the SSE,
   * and returns the best SSE obtained during the iterations.
   *
   * @param dataPoints A pointer to the DataPoints structure containing the data points.
   * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
   * @param numCentroids The number of centroids to generate.
   * @param maxIterations The maximum number of iterations for the k-means algorithm.
   * @param maxRepeats The maximum number of repeats for the k-means algorithm.
   * @param loopCount The number of loops to run the k-means algorithm.
   * @param scaling A scaling factor for the SSE values.
   * @param fileName The name of the file to write the results to.
   * @param outputDirectory The directory where the results file is located.
   */
  void runRandomSwapAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxSwaps, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, bool trackProgress, bool trackTime)
  {
      Statistics stats;
      initializeStatistics(&stats);

      clock_t start, end;
      double duration;

      //Tracker helpers
      size_t totalIterations = loopCount * maxSwaps + loopCount;
      double* timeList = malloc(totalIterations * sizeof(double));
      handleMemoryError(timeList);
      size_t timeIndex = 0;

      bool savedZeroResults = false;
      bool savedNonZeroResults = false;

      printf("Random swap\n");

      for (size_t i = 0; i < loopCount; ++i)
      {
          printf("Round %zu\n", i + 1);

          Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

          start = clock();

          generateRandomCentroids(numCentroids, dataPoints, &centroids);
          //generateKMeansPlusPlusCentroids(numCentroids, dataPoints, &centroids);
          partitionStep(dataPoints, &centroids); //Local repartition requires this

          double resultSse = randomSwap(dataPoints, &centroids, maxSwaps, groundTruth, outputDirectory, (i == 0 && trackProgress), timeList, &timeIndex, start, trackTime, trackProgress);

          end = clock();
          duration = ((double)(end - start)) / CLOCKS_PER_MS;

          size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

          stats.sseSum += resultSse;
          stats.ciSum += centroidIndex;
          stats.timeSum += duration;
          if (centroidIndex == 0) stats.successRate++;

          if (centroidIndex != 0 && savedNonZeroResults == false)
          {
              char centroidsFile[PATH_MAX];
              char partitionsFile[PATH_MAX];
              snprintf(centroidsFile, sizeof(centroidsFile), "RandomSwap_centroids_failed.txt");
              snprintf(partitionsFile, sizeof(partitionsFile), "RandomSwap_partitions_failed.txt");

              writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
              writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
              savedNonZeroResults = true;
          }
          else if (centroidIndex == 0 && savedZeroResults == false)
          {
              char centroidsFile[PATH_MAX];
              char partitionsFile[PATH_MAX];
              snprintf(centroidsFile, sizeof(centroidsFile), "RandomSwap_centroids_perfect.txt");
              snprintf(partitionsFile, sizeof(partitionsFile), "RandomSwap_partitions_perfect.txt");

              writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
              writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
              savedZeroResults = true;
          }

          freeCentroids(&centroids);
      }

      printStatistics("Random Swap", stats, loopCount, numCentroids, scaling, dataPoints->size);
      writeResultsToFile(fileName, stats, numCentroids, "Random swap", loopCount, scaling, outputDirectory);

      if (trackTime)
      {
          writeTimeTrackingData(outputDirectory, 3, timeList, timeIndex);
      }

      free(timeList);
  }

  /**
   * @brief Runs Random Split k-means and aggregates CI/SSE/time across runs.
   *
   * Seeds centroids, grows K by intra-cluster k-means splits via `runRandomSplit`,
   * and accumulates SSE, CI vs. ground truth, and elapsed time over `loopCount` runs.
   * Writes one “perfect” and one “failed” snapshot for inspection.
   *
   * @param dataPoints Dataset clustered in-place; partitions updated per run.
   * @param groundTruth Reference centroids for CI (may be NULL if CI not needed).
   * @param numCentroids Target K; must satisfy 1 <= K <= N.
   * @param maxIterations Upper bound for local/global refinement iterations.
   * @param loopCount Number of independent runs (new seeds each run).
   * @param scaling SSE divisor for CSV summary (consistency with other wrappers).
   * @param fileName Base name for the CSV summary file.
   * @param outputDirectory Destination directory for outputs.
   * @param trackProgress When true, enables per-iteration logging in the first run.
   * @param trackTime When true, collects and writes elapsed times per iteration.
   * @return void
   * @errors Exits on allocation failures via helpers; validates inputs to preempt invalid runs.
   * @note `runRandomSplit` may fail to progress if no cluster has ≥2 points; consider a retry budget there.
   */
  void runRandomSplitAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, bool trackProgress, bool trackTime)
  {
      Statistics stats;
      initializeStatistics(&stats);

      clock_t start, end;
      double duration;

      //Tracker helpers
	  size_t totalIterations = loopCount * numCentroids * 2 + loopCount; //Estimated upper bound for tracking
      double* timeList = malloc(totalIterations * sizeof(double));
      handleMemoryError(timeList);
      size_t timeIndex = 0;

      bool savedZeroResults = false;
      bool savedNonZeroResults = false;

      printf("Random Split k-means\n");

      for (size_t i = 0; i < loopCount; ++i)
      {
          printf("Round %zu\n", i + 1);

          resetPartitions(dataPoints);

          Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

          start = clock();

          generateRandomCentroids(centroids.size, dataPoints, &centroids);

          double resultSse = runRandomSplit(dataPoints, &centroids, numCentroids, maxIterations, groundTruth, outputDirectory, (i == 0 && trackProgress), timeList, &timeIndex, start, trackTime, trackProgress);

          end = clock();
          duration = ((double)(end - start)) / CLOCKS_PER_MS;

          size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

          stats.sseSum += resultSse;
          stats.ciSum += centroidIndex;
          stats.timeSum += duration;
          if (centroidIndex == 0) stats.successRate++;

          if (centroidIndex != 0 && savedNonZeroResults == false)
          {
              char centroidsFile[PATH_MAX];
              char partitionsFile[PATH_MAX];
              snprintf(centroidsFile, sizeof(centroidsFile), "RandomSplit_centroids_failed.txt");
              snprintf(partitionsFile, sizeof(partitionsFile), "RandomSplit_partitions_failed.txt");

              writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
              writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
              savedNonZeroResults = true;
          }
          else if (centroidIndex == 0 && savedZeroResults == false)
          {
              char centroidsFile[PATH_MAX];
              char partitionsFile[PATH_MAX];
              snprintf(centroidsFile, sizeof(centroidsFile), "RandomSplit_centroids_perfect.txt");
              snprintf(partitionsFile, sizeof(partitionsFile), "RandomSplit_partitions_perfect.txt");

              writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
              writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
              savedZeroResults = true;
          }

          freeCentroids(&centroids);
      }

      printStatistics("Random Split", stats, loopCount, numCentroids, scaling, dataPoints->size);
      writeResultsToFile(fileName, stats, numCentroids, "Random Split", loopCount, scaling, outputDirectory);

      if (trackTime)
      {
          writeTimeTrackingData(outputDirectory, 5, timeList, timeIndex);
      }

      free(timeList);
  }

  /**
   * @brief Runs SSE-based Split k-means and aggregates CI/SSE/time across runs.
   *
   * Wraps `runSseSplit` for three strategies (intra-cluster, global, local repartition)
   * and reports aggregate statistics over `loopCount` runs. Produces one failed and one
   * perfect snapshot to aid diagnostics.
   *
   * @param dataPoints Dataset clustered in-place; partitions updated during runs.
   * @param groundTruth Reference centroids for CI (may be NULL).
   * @param numCentroids Target K; must satisfy 1 <= K <= N.
   * @param maxIterations Upper bound for refinements inside the split strategy.
   * @param loopCount Number of independent runs.
   * @param scaling SSE divisor for CSV summary.
   * @param fileName Base name for the CSV summary file.
   * @param outputDirectory Destination directory for outputs.
   * @param splitType Strategy: 0=IntraCluster, 1=Global, 2=LocalRepartition.
   * @param trackProgress Enable per-iteration logging in the first run.
   * @param trackTime Enable per-iteration timing output.
   * @return void
   * @errors Exits on allocation failures via helpers; validates inputs to preempt invalid runs.
   * @note Timing buffer is allocated only when requested.
   */
  void runSseSplitAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, size_t splitType, bool trackProgress, bool trackTime)
  {
      Statistics stats;
      initializeStatistics(&stats);

      clock_t start, end;
      double duration;

      const char* splitTypeName = getAlgorithmName(splitType);

      //Tracker helpers
	  size_t totalIterations = loopCount * numCentroids + loopCount; //Estimated upper bound for tracking
      double* timeList = malloc(totalIterations * sizeof(double));
      handleMemoryError(timeList);
      size_t timeIndex = 0;

      bool savedZeroResults = false;
      bool savedNonZeroResults = false;

      printf("%s\n", splitTypeName);

      for (size_t i = 0; i < loopCount; ++i)
      {
          printf("Round %zu\n", i + 1);

          resetPartitions(dataPoints);

          Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

          start = clock();

          generateRandomCentroids(centroids.size, dataPoints, &centroids);

          double resultSse = runSseSplit(dataPoints, &centroids, numCentroids, maxIterations, groundTruth, splitType, outputDirectory, (i == 0 && trackProgress), timeList, &timeIndex, start, trackTime, trackProgress);

          end = clock();
          duration = ((double)(end - start)) / CLOCKS_PER_MS;

          size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

          stats.sseSum += resultSse;
          stats.ciSum += centroidIndex;
          stats.timeSum += duration;
          if (centroidIndex == 0) stats.successRate++;

          if (centroidIndex != 0 && savedNonZeroResults == false)
          {
              char centroidsFile[PATH_MAX];
              char partitionsFile[PATH_MAX];
              snprintf(centroidsFile, sizeof(centroidsFile), "%s_centroids_failed.txt", splitTypeName);
              snprintf(partitionsFile, sizeof(partitionsFile), "%s_partitions_failed.txt", splitTypeName);

              writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
              writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
              savedNonZeroResults = true;
          }
          else if (centroidIndex == 0 && savedZeroResults == false)
          {
              char centroidsFile[PATH_MAX];
              char partitionsFile[PATH_MAX];
              snprintf(centroidsFile, sizeof(centroidsFile), "%s_centroids_perfect.txt", splitTypeName);
              snprintf(partitionsFile, sizeof(partitionsFile), "%s_partitions_perfect.txt", splitTypeName);

              writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
              writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
              savedZeroResults = true;
          }

          freeCentroids(&centroids);
      }

      printStatistics(splitTypeName, stats, loopCount, numCentroids, scaling, dataPoints->size);
      writeResultsToFile(fileName, stats, numCentroids, splitTypeName, loopCount, scaling, outputDirectory);

      if (trackTime)
      {
          writeTimeTrackingData(outputDirectory, splitType, timeList, timeIndex);
      }

      free(timeList);
  }

  /**
   * @brief Runs Bisecting k-means and aggregates CI/SSE/time across runs.
   *
   * Seeds an initial centroid, executes the bisecting loop via `runBisectingKMeans`
   * until K is reached, and aggregates SSE, CI vs. ground truth, and elapsed time
   * over `loopCount` runs. Writes one failed and one perfect snapshot for inspection.
   *
   * @param dataPoints Dataset clustered in-place; partitions updated per run.
   * @param groundTruth Reference centroids for CI (may be NULL).
   * @param numCentroids Target K; must satisfy 1 <= K <= N.
   * @param maxIterations Max iterations for local/global refinements in the core loop.
   * @param loopCount Number of independent runs.
   * @param scaling SSE divisor for CSV summary.
   * @param fileName Base name for the CSV summary file.
   * @param outputDirectory Destination directory for outputs.
   * @param trackProgress Enable per-iteration logging in the first run.
   * @param trackTime Enable per-iteration timing output.
   * @param bisectingIterations Local k-means attempts per split (clamped to ≥1).
   * @return void
   * @errors Exits on allocation failures via helpers; validates inputs to preempt invalid runs.
   * @note Timing buffer is allocated only when requested.
   */
  void runBisectingKMeansAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, bool trackProgress, bool trackTime, size_t bisectingIterations)
  {
      Statistics stats;
      initializeStatistics(&stats);

      clock_t start, end;
      double duration;

      //Tracker helpers
	  size_t totalIterations = loopCount * numCentroids + loopCount; //Estimated upper bound for tracking
      double* timeList = malloc(totalIterations * sizeof(double));
      handleMemoryError(timeList);
      size_t timeIndex = 0;

      bool savedZeroResults = false;
      bool savedNonZeroResults = false;

      printf("Bisecting k-means\n");

      for (size_t i = 0; i < loopCount; ++i)
      {
          printf("Round %zu\n", i + 1);

          resetPartitions(dataPoints);

          Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

          start = clock();

          generateRandomCentroids(centroids.size, dataPoints, &centroids);

          double resultSse = runBisectingKMeans(dataPoints, &centroids, numCentroids, maxIterations, groundTruth, outputDirectory, (i == 0 && trackProgress), timeList, &timeIndex, start, trackTime, trackProgress, bisectingIterations);

          end = clock();
          duration = ((double)(end - start)) / CLOCKS_PER_MS;

          size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

          stats.sseSum += resultSse;
          stats.ciSum += centroidIndex;
          stats.timeSum += duration;
          if (centroidIndex == 0) stats.successRate++;

          if (centroidIndex != 0 && savedNonZeroResults == false)
          {
              char centroidsFile[PATH_MAX];
              char partitionsFile[PATH_MAX];
              snprintf(centroidsFile, sizeof(centroidsFile), "Bisecting_centroids_failed.txt");
              snprintf(partitionsFile, sizeof(partitionsFile), "Bisecting_partitions_failed.txt");

              writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
              writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
              savedNonZeroResults = true;
          }
          else if (centroidIndex == 0 && savedZeroResults == false)
          {
              char centroidsFile[PATH_MAX];
              char partitionsFile[PATH_MAX];
              snprintf(centroidsFile, sizeof(centroidsFile), "Bisecting_centroids_perfect.txt");
              snprintf(partitionsFile, sizeof(partitionsFile), "Bisecting_partitions_perfect.txt");

              writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
              writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
              savedZeroResults = true;
          }

          freeCentroids(&centroids);
      }

      printStatistics("Bisecting", stats, loopCount, numCentroids, scaling, dataPoints->size);
      writeResultsToFile(fileName, stats, numCentroids, "Bisecting k-means", loopCount, scaling, outputDirectory);

      if (trackTime)
      {
          writeTimeTrackingData(outputDirectory, 4, timeList, timeIndex);
      }

      free(timeList);
  }

  /////////////
  // Random //
  ///////////

/**
 * @brief Builds ground-truth centroids from data and a partition file.
 *
 * Reads points (uniform dimensionality), then reads one 1-based partition label per point,
 * converts labels to 0-based, and computes per-cluster means. Writes centroids to a file.
 * Shrinks the dataset if the partition file has fewer entries; warns if it has more.
 *
 * @param dataFileName Path to data points (whitespace-separated doubles).
 * @param partitionFileName Path to partition labels (one integer per line, 1..K).
 * @param outputFileName Path to write centroids (space-separated doubles per row).
 * @return 0 on success; non-zero on invalid inputs, file I/O failures, or empty partitions.
 * @errors Prints diagnostics to stderr/stdout and returns non-zero; alloc failures exit via helpers.
 * @note Expects 1-based labels; converts to 0-based. Ignores extra partition lines beyond N.
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

      // Read the partition indices
      FILE* partitionFile;

      if (FOPEN(partitionFile, partitionFileName, "r") != 0)
      {
          fprintf(stderr, "Error: Unable to open partition file '%s'\n", partitionFileName);
          freeDataPoints(&dataPoints);
          return 1;
      }

      // Assign the partition indices to the data points
      size_t maxPartitionIndex = 0;
      size_t validDataPoints = 0;

      // Determine if we have a mismatch between dataPoints size and partition file entries
      for (size_t i = 0; i < dataPoints.size; ++i)
      {
          int partitionIndex;
          if (fscanf(partitionFile, "%d", &partitionIndex) != 1)
          {
              // If we can't read more partitions but still have data points, we have a mismatch
              if (i < dataPoints.size - 1)
              {
                  printf("Warning: Partition file has fewer entries (%zu) than data points (%zu).\n",
                      i, dataPoints.size);

                  // Adjust the dataPoints size to match the valid entries we read
                  dataPoints.size = i;
                  break;
              }

              // Normal end of file
              break;
          }

          // Subtract 1 to adjust for partitions starting from 1 instead of 0
          partitionIndex -= 1;

          // Make sure partition index is non-negative
          if (partitionIndex < 0)
          {
              fprintf(stderr, "Error: Negative partition index found for data point %zu\n", i);
              fclose(partitionFile);
              freeDataPoints(&dataPoints);
              return 1;
          }

          dataPoints.points[i].partition = (size_t)partitionIndex;

          if (dataPoints.points[i].partition > maxPartitionIndex)
          {
              maxPartitionIndex = dataPoints.points[i].partition;
          }

          validDataPoints++;
      }

      // Check if there are more partitions in the file than data points
      int extraPartition;
      if (fscanf(partitionFile, "%d", &extraPartition) == 1)
      {
          printf("Warning: Partition file has more entries than data points. Data file has %zu points.\n",
              dataPoints.size);

          // Count how many extra partitions there are
          size_t extraPartitions = 1;  // We already read one
          while (fscanf(partitionFile, "%d", &extraPartition) == 1)
          {
              extraPartitions++;
          }
          printf("Found %zu extra partition entries.\n", extraPartitions);
      }

      fclose(partitionFile);

      printf("Processing %zu valid data points with partitions ranging from 0 to %zu\n",
          validDataPoints, maxPartitionIndex);

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

          // Skip any invalid cluster indices (this is a safety check)
          if (clusterIndex >= numCentroids)
          {
              fprintf(stderr, "Error: Invalid cluster index %zu for data point %zu\n", clusterIndex, i);
              continue;
          }

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
   * @brief Computes Centroid Index (CI) between a debug centroid file and ground truth.
   *
   * Loads both centroid sets, validates matching dimensionality and non-empty inputs,
   * then prints the CI. Intended for quick verification of centroid files.
   *
   * @param debugCentroidsFile Path to candidate centroid file.
   * @param groundTruthFile Path to ground-truth centroid file.
   * @return void
   * @errors Prints diagnostics and returns early on file/dimension issues; allocations exit via helpers.
   * @note CI compares mutual nearest-neighbor coverage; lower is better, 0 means perfect match.
   */
  void debugCalculateCI(const char* debugCentroidsFile, const char* groundTruthFile)
  {
      printf("Debugging CI calculation between:\n");
      printf("  Debug file: %s\n", debugCentroidsFile);
      printf("  Ground truth: %s\n", groundTruthFile);

      // Read centroids from both files
      Centroids debugCentroids = readCentroids(debugCentroidsFile);
      Centroids groundTruth = readCentroids(groundTruthFile);

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
   * @brief Computes SSE for assigning points to nearest centroids from files.
   *
   * Loads centroids and data points, validates matching dimensionality and non-empty inputs,
   * assigns each point to its nearest centroid, and prints the resulting SSE.
   *
   * @param debugCentroidsFile Path to centroid file (one centroid per line).
   * @param dataFile Path to data file (one point per line).
   * @return void
   * @errors Prints diagnostics and returns early on file/dimension issues; allocations exit via helpers.
   * @note Uses squared Euclidean distance; lower SSE indicates tighter clustering.
   */
  void debugCalculateSSE(const char* debugCentroidsFile, const char* dataFile)
  {
      printf("Debugging SSE calculation:\n");
      printf("  Centroids file: %s\n", debugCentroidsFile);
      printf("  Data file: %s\n", dataFile);

      // Read centroids and data points
      Centroids centroids = readCentroids(debugCentroidsFile);
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
  void runDebuggery()
  {
      debugCalculateCI("debuggery/output_worms_64d.txt", "gt/worms_64d-gt.txt");
      debugCalculateSSE("debuggery/output_worms_64d.txt", "data/worms_64d.txt");
  }


  ///////////
  // Main //
  /////////

/**
 * @brief Program entry: supports directory batch mode or single-file CLI mode.
 *
 * **Directory mode** (no args):
 *   - Enumerates `data/`, `gt/`, `centroids/` and pairs files by sorted order.
 *   - Requires matching file counts across all three directories.
 *   - K values read from `.txt` files in `centroids/` directory.
 *
 * **CLI mode** (args provided):
 *   - `split.exe <data.txt> -k <K> [-r <runs>] [--track-progress] [--track-time] [gt.txt]`
 *   - **Required**: data file and K value via `-k <number>`.
 *   - **Optional**: ground truth file (if omitted, CI calculations use empty set).
 *   - **Optional**: `-r <runs>` or `--runs <runs>` (default: 100).
 *   - **Optional**: `--track-progress` enables per-iteration stats/snapshots.
 *   - **Optional**: `--track-time` records elapsed times per iteration.
 *   - **Auto-enable tracking**: When runs=1, both tracking flags default to true.
 *   - **Help**: `-h` or `--help` displays usage information.
 *
 * @return 0 on success
 * @errors Exits on invalid arguments, file I/O errors, missing K parameter, or validation failures.
 * @note CLI mode enforces explicit K specification; no fallback to `.txt` files.
 */
  int main(int argc, char* argv[])
  {
      //runDebuggery(); //Helper for PCA codes
      //return 0;

      //set_numeric_locale_finnish();

      char outputDirectory[PATH_MAX];
      createUniqueDirectory(outputDirectory, sizeof(outputDirectory));

      const char* cliDataFile = NULL;
      const char* cliGtFile = NULL;
      size_t cliNumCentroids = 0;
      size_t cliLoops = 100;
      bool cliTrackProgress = false;
      bool cliTrackTime = false;

      size_t dataCount = 0;
      char** dataNames = NULL;
      char** gtNames = NULL;
      char** kNames = NULL;

      // ============================================================
    // Parse CLI arguments if provided
    // ============================================================
      if (argc > 1)
      {
          for (int i = 1; i < argc; ++i)
          {
              if (strcmp(argv[i], "-k") == 0)
              {
                  if (i + 1 >= argc)
                  {
                      fprintf(stderr, "Error: -k requires a numeric argument\n");
                      fprintf(stderr, "Usage: %s <data.txt> [-k <K>] [-r <runs>] [--track-progress] [--track-time] [gt.txt]\n", argv[0]);
                      exit(EXIT_FAILURE);
                  }
                  cliNumCentroids = (size_t)atol(argv[++i]);
              }
              else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--runs") == 0)
              {
                  if (i + 1 >= argc)
                  {
                      fprintf(stderr, "Error: -r/--runs requires a numeric argument\n");
                      exit(EXIT_FAILURE);
                  }
                  cliLoops = (size_t)atol(argv[++i]);
              }
              else if (strcmp(argv[i], "--track-progress") == 0)
              {
                  cliTrackProgress = true;
              }
              else if (strcmp(argv[i], "--track-time") == 0)
              {
                  cliTrackTime = true;
              }
              else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
              {
                  printf("Usage: %s <data.txt> [options] [gt.txt]\n\n", argv[0]);
                  printf("SKM-Local Clustering Algorithm\n\n");
                  printf("Options:\n");
                  printf("  -k <K>              Number of clusters\n");
                  printf("  -r, --runs <N>      Number of runs (default: 100)\n");
                  printf("  --track-progress    Enable progress tracking\n");
                  printf("  --track-time        Enable time tracking\n");
                  printf("  -h, --help          Show this help\n");
                  exit(EXIT_SUCCESS);
              }
              else if (!cliDataFile)
              {
                  cliDataFile = argv[i];
              }
              else if (!cliGtFile)
              {
                  cliGtFile = argv[i];
              }
              else
              {
                  fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
                  fprintf(stderr, "Use -h for help\n");
                  exit(EXIT_FAILURE);
              }
          }

          // CLI mode: single dataset from command line
          if (!cliDataFile)
          {
              fprintf(stderr, "Error: Data file required\n");
              exit(EXIT_FAILURE);
          }

          // K must be explicitly specified via -k in CLI mode
          if (cliNumCentroids == 0)
          {
              fprintf(stderr, "Error: K must be specified with -k <number>\n");
              fprintf(stderr, "Usage: %s <data.txt> -k <K> [-r <runs>] [--track-progress] [--track-time] [gt.txt]\n", argv[0]);
              exit(EXIT_FAILURE);
          }

          // Enable tracking by default for single runs
          if (cliLoops == 1)
          {
              cliTrackProgress = true;
              cliTrackTime = true;
          }

          printf("\n=== CLI Mode: SKM-Local ===\n");
          printf("Data: %s\n", cliDataFile);
          printf("Ground truth: %s\n", cliGtFile ? cliGtFile : "None");
          printf("K: %zu, Runs: %zu\n\n", cliNumCentroids, cliLoops);

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

          printf("=== Directory Batch Mode: %zu datasets ===\n\n", dataCount);

          // Batch mode defaults
          cliTrackProgress = true;
          cliTrackTime = true;
    }
      

	  for (size_t i = 3; i < 4; ++i)
      {
          // Determine file paths based on mode
          char currentDataFile[PATH_MAX];
          char currentGtFile[PATH_MAX];
          char* baseName = NULL;
          size_t currentK = 0;
          size_t currentLoops = 0;
          bool currentTrackProgress = false;
          bool currentTrackTime = false;

          if (argc > 1)
          {
              // CLI mode: use parsed values
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
              currentLoops = cliLoops;
              currentTrackProgress = cliTrackProgress;
              currentTrackTime = cliTrackTime;
          }
          else
          {
              // Batch mode: build paths from file lists
              snprintf(currentDataFile, sizeof(currentDataFile), "data%c%s", PATHSEP, dataNames[i]);
              snprintf(currentGtFile, sizeof(currentGtFile), "gt%c%s", PATHSEP, gtNames[i]);

              char kFile[PATH_MAX];
              snprintf(kFile, sizeof(kFile), "centroids%c%s", PATHSEP, kNames[i]);

              baseName = removeExtension(dataNames[i]);
              currentK = readKFromFile(kFile);
              currentLoops = 100;  // Default for batch
              currentTrackProgress = cliTrackProgress;
              currentTrackTime = cliTrackTime;
          }

          // Create dataset subdirectory
          char datasetDirectory[PATH_MAX];
          createDatasetDirectory(outputDirectory, baseName, datasetDirectory, sizeof(datasetDirectory));

          // Read data and ground truth
          size_t numDimensions = getNumDimensions(currentDataFile);
          if (numDimensions == 0)
          {
              fprintf(stderr, "--> Skipping %s (couldn't read dimensions)\n", baseName);
              continue;
          }

          DataPoints dataPoints = readDataPoints(currentDataFile);
          if (dataPoints.size == 0)
          {
              fprintf(stderr, "--> Skipping %s (no data points)\n", baseName);
              continue;
          }

          Centroids groundTruth = { NULL, 0 };
          if (currentGtFile[0] != '\0')
          {
              groundTruth = readCentroids(currentGtFile);
          }

          printf("Starting process\n");
          printf("Dataset: %s\n", baseName);
          printf("Dimensions: %zu, Data points: %zu, K: %zu, Runs: %zu\n\n",
              numDimensions, dataPoints.size, currentK, currentLoops);

          // Algorithm parameters
          size_t scaling = 1;
          size_t maxIterations = SIZE_MAX;

          // Run K-means
          //runKMeansAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, baseName, datasetDirectory);

          // Run Repeated K-means
          //runRepeatedKMeansAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, maxRepeats, loopCount, scaling, baseName, datasetDirectory, trackProgress, trackTime);

          // Run Random Swap
          //runRandomSwapAlgorithm(&dataPoints, &groundTruth, numCentroids, swaps, loopCount, scaling, baseName, datasetDirectory, trackProgress, trackTime);

          // Run SKM-Random
          //runRandomSplitAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, baseName, datasetDirectory, trackProgress, trackTime);

          // Run SKM-Intra
          //runSseSplitAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, baseName, datasetDirectory, 0, trackProgress, trackTime);

          // Run SKM-Global
          //runSseSplitAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, baseName, datasetDirectory, 1, trackProgress, trackTime);

          // Run SKM-Local
          runSseSplitAlgorithm(&dataPoints, &groundTruth, currentK, maxIterations, currentLoops, scaling, baseName, datasetDirectory, 2, currentTrackProgress, currentTrackTime);

          // Run Bisecting K-means
          //runBisectingKMeansAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, baseName, datasetDirectory, trackProgress, trackTime, bisectingIterations);

          // Clean up
          freeDataPoints(&dataPoints);
          freeCentroids(&groundTruth);
      }

      for (size_t i = 0; i < dataCount; ++i)
      {
          free(dataNames[i]);
          free(gtNames[i]);
          free(kNames[i]);
      }

      free(dataNames);
      free(gtNames);
      free(kNames);

      return 0;
  }