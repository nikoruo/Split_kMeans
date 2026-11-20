/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"
#include "dataset.h"
#include "metrics.h"
#include "rng.h"
#include "util.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(bool condition, const char* message)
{
    if (!condition)
    {
        fprintf(stderr, "test_unit: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static FILE* writeFixture(const char* path)
{
    FILE* file = fopen(path, "w");
    if (file == NULL)
    {
        perror(path);
        exit(EXIT_FAILURE);
    }
    return file;
}

static void closeFixture(FILE* file)
{
    require(fclose(file) == 0, "could not close fixture");
}

static void testNumbers(void)
{
    const char* invalid[] = {"", "1,5", "abc", "1.5x", "nan", "inf", "1e999"};
    double value = 0.0;
    require(parseFiniteDouble("-1.25", &value) && value == -1.25, "finite decimal rejected");
    require(parseFiniteDouble("5e-400", &value), "finite underflow rejected");
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        if (parseFiniteDouble(invalid[i], &value))
        {
            fprintf(stderr, "test_unit: invalid number accepted: %s\n", invalid[i]);
            exit(EXIT_FAILURE);
        }
    }
}

static void testMetrics(void)
{
    DataPoints points = allocateDataPoints(3, 1);
    Centroids centroids = allocateCentroids(2, 1);
    Centroids truth = allocateCentroids(2, 1);
    points.points[0].attributes[0] = 0.0;
    points.points[1].attributes[0] = 2.0;
    points.points[2].attributes[0] = 10.0;
    points.points[0].partition = 0;
    points.points[1].partition = 0;
    points.points[2].partition = 1;
    centroids.points[0].attributes[0] = 0.0;
    centroids.points[1].attributes[0] = 10.0;
    truth.points[0].attributes[0] = 0.0;
    truth.points[1].attributes[0] = 10.0;

    require(calculateSSE(&points, &centroids) == 4.0, "SSE is incorrect");
    require(calculateClusterSSE(&points, &centroids, 0) == 4.0 &&
                calculateClusterSSE(&points, &centroids, 1) == 0.0,
            "cluster SSE is incorrect");
    require(calculateCentroidIndex(&centroids, &truth) == 0, "matching centroids have nonzero CI");
    require(centroidIndexOrNA(&centroids, NULL) == CI_NA, "missing ground truth did not give NA");
    points.points[1].attributes[0] = 5.0;
    require(findNearestCentroid(&points.points[1], &centroids) == 0, "nearest-centroid tie did not choose first");
    centroids.points[1].attributes[0] = 1.0;
    require(calculateCentroidIndex(&centroids, &truth) == 1, "orphan centroid was not counted");

    freeCentroids(&truth);
    freeCentroids(&centroids);
    freeDataPoints(&points);
}

static void testRng(void)
{
    Rng first;
    Rng second;
    rng_seed(&first, rng_derive_seed(20261119, 3));
    rng_seed(&second, rng_derive_seed(20261119, 3));
    for (size_t i = 0; i < 256; ++i)
    {
        require(rng_u32(&first) == rng_u32(&second), "same seed did not reproduce the random stream");
        size_t a = rng_below(&first, 7);
        size_t b = rng_below(&second, 7);
        require(a == b && a < 7, "bounded random selection is not reproducible or is out of range");
    }
    require(rng_derive_seed(20261119, 3) != rng_derive_seed(20261119, 4), "run seeds are not independent");
}

static void expectReadFailure(const char* executable, const char* mode, const char* path, const char* errorPath,
                              const char* expected)
{
    char command[3 * PATH_MAX + 128];
    int length =
        snprintf(command, sizeof(command), "cd . && \"%s\" %s \"%s\" 2> \"%s\"", executable, mode, path, errorPath);
    require(length >= 0 && (size_t)length < sizeof(command), "test command too long");
    int status = system(command);
    require(status != -1 && status != 0, "invalid input did not fail");

    FILE* errors = fopen(errorPath, "r");
    require(errors != NULL, "could not read parser error");
    char output[2 * PATH_MAX + 512];
    size_t bytes = fread(output, 1, sizeof(output) - 1, errors);
    require(!ferror(errors), "could not read parser error");
    output[bytes] = '\0';
    closeFixture(errors);
    if (strstr(output, "Error:") == NULL || strstr(output, path) == NULL || strstr(output, expected) == NULL ||
        strstr(output, "Sanitizer") != NULL)
    {
        fprintf(stderr, "test_unit: unexpected parser error for %s:\n%s", path, output);
        exit(EXIT_FAILURE);
    }
}

static void testDatasetParser(const char* executable, const char* directory)
{
    char path[PATH_MAX];
    char errorPath[PATH_MAX];
    int length = snprintf(path, sizeof(path), "%s%cdata.txt", directory, PATHSEP);
    require(length >= 0 && (size_t)length < sizeof(path), "fixture path too long");
    length = snprintf(errorPath, sizeof(errorPath), "%s%cerror.txt", directory, PATHSEP);
    require(length >= 0 && (size_t)length < sizeof(errorPath), "error path too long");

    FILE* file = writeFixture(path);
    require(fputs(" \t\n1.25\t-2\n5e-400 4.5e1\n", file) >= 0, "could not write fixture");
    closeFixture(file);
    DataPoints points = readDataPoints(path);
    require(points.size == 2 && points.points[0].dimensions == 2 && points.points[0].partition == SIZE_MAX &&
                points.points[0].attributes[0] == 1.25 && points.points[0].attributes[1] == -2.0 &&
                points.points[1].attributes[1] == 45.0,
            "valid dataset parsed incorrectly");
    freeDataPoints(&points);
    Centroids centroids = readCentroids(path, 1);
    require(centroids.size == 2 && centroids.points[1].attributes[1] == 45.0, "valid centroid file parsed incorrectly");
    freeCentroids(&centroids);

    const char* invalid[] = {"1,5", "abc", "1.5x", "nan", "inf", "1e999"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        file = writeFixture(path);
        require(fprintf(file, "1 2\n3 %s\n", invalid[i]) > 0, "could not write fixture");
        closeFixture(file);
        expectReadFailure(executable, "--data", path, errorPath, "line 2, column 2");
        expectReadFailure(executable, "--centroids", path, errorPath, "line 2, column 2");
    }

    file = writeFixture(path);
    require(fputs("1 2\n3\n", file) >= 0, "could not write fixture");
    closeFixture(file);
    expectReadFailure(executable, "--data", path, errorPath, "line 2 (expected 2, got 1)");

    file = writeFixture(path);
    for (size_t i = 0; i < LINE_BUFSZ - 2; ++i)
    {
        require(fputc(' ', file) != EOF, "could not write fixture");
    }
    require(fputs("1\n", file) >= 0, "could not write fixture");
    closeFixture(file);
    points = readDataPoints(path);
    require(points.size == 1 && points.points[0].attributes[0] == 1.0, "maximum-length line rejected");
    freeDataPoints(&points);

    file = writeFixture(path);
    for (size_t i = 0; i < LINE_BUFSZ - 1; ++i)
    {
        require(fputc(' ', file) != EOF, "could not write fixture");
    }
    require(fputs("1\n", file) >= 0, "could not write fixture");
    closeFixture(file);
    expectReadFailure(executable, "--data", path, errorPath, "longer than 16383 characters");
    expectReadFailure(executable, "--centroids", path, errorPath, "longer than 16383 characters");

    require(remove(path) == 0 && remove(errorPath) == 0, "could not remove test fixtures");
}

int main(int argc, char* argv[])
{
    if (argc == 3 && strcmp(argv[1], "--data") == 0)
    {
        DataPoints points = readDataPoints(argv[2]);
        freeDataPoints(&points);
        return EXIT_SUCCESS;
    }
    if (argc == 3 && strcmp(argv[1], "--centroids") == 0)
    {
        Centroids centroids = readCentroids(argv[2], 1);
        freeCentroids(&centroids);
        return EXIT_SUCCESS;
    }
    require(argc == 2, "expected a fixture directory");
    testNumbers();
    testDatasetParser(argv[0], argv[1]);
    testMetrics();
    testRng();
    puts("test_unit: parser, metric and RNG cases passed");
    return EXIT_SUCCESS;
}
