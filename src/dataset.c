/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"

#include "dataset.h"
#include "util.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Deinitializes a data point: frees its attributes and resets fields.
 */
void deinitDataPoint(DataPoint* point)
{
    if (point == NULL) return;

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
    if (points == NULL) return;

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
    if (dataPoints == NULL) return;
    if (dataPoints->points == NULL) return;

    freeDataPointArray(dataPoints->points, dataPoints->size);
    dataPoints->points = NULL;
    dataPoints->size = 0;
 }

/**
  * @brief Releases owned centroid array (and its elements) and resets the container.
  */
 void freeCentroids(Centroids* centroids)
 {
     if (centroids == NULL) return;
     if (centroids->points == NULL) return;

     freeDataPointArray(centroids->points, centroids->size);
     centroids->points = NULL;
     centroids->size = 0;
 }

/**
   * @brief Creates a point with `dimensions` doubles, zero-initialized.
   */
  DataPoint allocateDataPoint(size_t dimensions)
  {
      assert(dimensions > 0);

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
      assert(dimensions > 0 && size > 0);

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
   * @brief Load whitespace-separated double vectors, enforcing a consistent dimensionality per row.
   *
   * Skips whitespace-only lines. The first non-empty row defines the expected column count;
   * every subsequent non-empty row must match or the function terminates with an error.
   * Returns an empty container if the file has no data rows.
   *
   * @param filename Path to a space/tab-delimited text file of doubles.
   * @return DataPoints with size equal to the number of data rows; each point owns its attributes
   *         (length == expectedDims), dimensions set to expectedDims, and partition = SIZE_MAX.
   * @errors Exits on file open/read failures, allocation failures, inconsistent column counts,
   *         values that are not finite numbers, and lines longer than LINE_BUFSZ - 1 characters.
   * @note Parsing uses strtod and the current locale (the program never changes it, so dot decimals).
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
      size_t lineNumber = 0;
      char countBuffer[LINE_BUFSZ];

      while (readDataLine(countBuffer, sizeof(countBuffer), file, filename, ++lineNumber) != NULL)
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
                  "Error: Inconsistent column count in '%s' line %zu (expected %zu, got %zu)\n",
                  filename, lineNumber, expectedDims, tokenCount);
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
      lineNumber = 0;

      while (currentPoint < lineCount && readDataLine(line, sizeof(line), file, filename, ++lineNumber) != NULL)
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
                  "Error: Inconsistent column count in '%s' line %zu (expected %zu, got %zu)\n",
                  filename, lineNumber, expectedDims, tokenCount);
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
              if (!parseFiniteDouble(token, &point->attributes[dim]))
              {
                  fclose(file);
                  fprintf(stderr, "Error: '%s' in '%s' line %zu, column %zu is not a finite number\n",
                      token, filename, lineNumber, dim + 1);
                  freeDataPointArray(dataPoints.points, currentPoint + 1);
                  exit(EXIT_FAILURE);
              }
              ++dim;
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
   * @errors The errors of readDataPoints (open and read errors, inconsistent column counts, values that are not
   *         finite numbers, overlong lines). Prints the size at log level 2 and every centroid at level 3.
   * @note Skips whitespace-only lines; parsing uses the current C locale for `strtod`.
   */
  Centroids readCentroids(const char* filename, int logLevel)
  {
      DataPoints points = readDataPoints(filename);

      Centroids centroids;
      centroids.size = points.size;
      centroids.points = points.points;

      if (logLevel >= 2 && centroids.size > 0)
      {
          printf("Read %zu centroids from %s, each with %zu dimensions\n", centroids.size, filename, centroids.points[0].dimensions);
      }

      if (logLevel >= 3)
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
      }

      return centroids;
  }

/**
   * @brief Reads a single positive integer K from a file (strict, no extra content).
   */
  size_t readKFromFile(const char* path)
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
      if (destination == source) return;
      assert(destination != NULL && source != NULL);

      /* Resize or allocate attributes buffer */
      double* newAttrs = (double*)realloc(destination->attributes, source->dimensions * sizeof(double));
      handleMemoryError(newAttrs);

      destination->attributes = newAttrs;
      destination->dimensions = source->dimensions;
      destination->partition = source->partition;

      memcpy(destination->attributes, source->attributes, source->dimensions * sizeof(double));
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

      if (source->size == 0)
      {
          destination->points = NULL;
          destination->size = 0;
          return;
      }

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
