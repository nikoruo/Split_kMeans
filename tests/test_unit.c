/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"
#include "baselines.h"
#include "dataset.h"
#include "kmeans.h"
#include "metrics.h"
#include "rng.h"
#include "split.h"
#include "util.h"

#include <math.h>
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

static void testFlatStorage(void)
{
    Dataset points = allocateDataset(3, 2);
    for (size_t i = 0; i < points.size; ++i)
    {
        require(points.partitions[i] == SIZE_MAX, "new dataset partition is not unassigned");
        datasetRow(&points, i)[0] = (double)(2 * i);
        datasetRow(&points, i)[1] = (double)(2 * i + 1);
    }
    require(points.coordinates[0] == 0.0 && points.coordinates[5] == 5.0,
            "dataset coordinates are not contiguous rows");

    Centroids source = allocateCentroids(3, 2);
    source.size = 2;
    centroidRow(&source, 0)[0] = 1.0;
    centroidRow(&source, 1)[1] = 4.0;
    Centroids copy = allocateCentroids(1, 1);
    deepCopyCentroids(&source, &copy);
    centroidRow(&source, 1)[1] = 9.0;
    require(copy.size == 2 && copy.capacity >= 2 && copy.dimensions == 2 && centroidRow(&copy, 1)[1] == 4.0,
            "centroid copy is not independent or has the wrong shape");
    deepCopyCentroids(&copy, &copy);
    require(centroidRow(&copy, 1)[1] == 4.0, "self-copy changed centroids");
    double* existing = copy.coordinates;
    deepCopyCentroids(&source, &copy);
    require(copy.coordinates == existing && centroidRow(&copy, 1)[1] == 9.0,
            "centroid copy did not reuse sufficient capacity");
    Centroids empty = {0};
    deepCopyCentroids(&empty, &copy);
    require(copy.size == 0 && copy.coordinates == NULL, "empty centroid copy did not clear the destination");

    freeCentroids(&copy);
    freeCentroids(&source);
    freeDataset(&points);
}

static void testMetrics(void)
{
    Dataset points = allocateDataset(3, 1);
    Centroids centroids = allocateCentroids(2, 1);
    Centroids truth = allocateCentroids(2, 1);
    datasetRow(&points, 0)[0] = 0.0;
    datasetRow(&points, 1)[0] = 2.0;
    datasetRow(&points, 2)[0] = 10.0;
    points.partitions[0] = 0;
    points.partitions[1] = 0;
    points.partitions[2] = 1;
    centroidRow(&centroids, 0)[0] = 0.0;
    centroidRow(&centroids, 1)[0] = 10.0;
    centroidRow(&truth, 0)[0] = 0.0;
    centroidRow(&truth, 1)[0] = 10.0;

    require(calculateSSE(&points, &centroids) == 4.0, "SSE is incorrect");
    require(calculateMSE(&points, &centroids) == 4.0 / 3.0, "retained MSE helper is incorrect");
    const double origin[] = {0.0, 0.0};
    const double other[] = {3.0, 4.0};
    require(calculateEuclideanDistance(origin, other, 2) == 5.0, "retained distance helper is incorrect");
    volatile double large = 100000000.0;
    const double ordered[] = {large, 1.0, 1.0, 1.0};
    const double zeros[] = {0.0, 0.0, 0.0, 0.0};
    require(calculateSquaredEuclideanDistance(ordered, zeros, 4) == 10000000000000000.0,
            "squared distance changed the summation order");
    require(calculateClusterSSE(&points, &centroids, 0) == 4.0 &&
                calculateClusterSSE(&points, &centroids, 1) == 0.0,
            "cluster SSE is incorrect");
    require(calculateCentroidIndex(&centroids, &truth) == 0, "matching centroids have nonzero CI");
    require(centroidIndexOrNA(&centroids, NULL) == CI_NA, "missing ground truth did not give NA");
    datasetRow(&points, 1)[0] = 5.0;
    require(findNearestCentroid(datasetRow(&points, 1), &centroids) == 0, "nearest-centroid tie did not choose first");
    centroidRow(&centroids, 1)[0] = 1.0;
    require(calculateCentroidIndex(&centroids, &truth) == 1, "orphan centroid was not counted");

    freeCentroids(&truth);
    freeCentroids(&centroids);
    freeDataset(&points);
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

static void testKMeans(void)
{
    Dataset points = allocateDataset(3, 1);
    Centroids centroids = allocateCentroids(3, 1);
    datasetRow(&points, 0)[0] = 0.0;
    datasetRow(&points, 1)[0] = 2.0;
    datasetRow(&points, 2)[0] = 10.0;
    centroidRow(&centroids, 0)[0] = 0.0;
    centroidRow(&centroids, 1)[0] = 10.0;
    centroidRow(&centroids, 2)[0] = 100.0;

    double sse = runKMeans(&points, 10, &centroids, NULL, KM_STAGE_NONE);
    require(sse == 2.0 && centroidRow(&centroids, 0)[0] == 1.0 &&
                centroidRow(&centroids, 1)[0] == 10.0 && centroidRow(&centroids, 2)[0] == 100.0,
            "k-means result or empty-centroid retention is incorrect");
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testKMeansView(void)
{
    const double values[][2] = {{0.1, 0.3}, {20.0, 20.0}, {1.1, 1.3}, {30.0, 30.0}, {9.9, 10.1}};
    const size_t indices[] = {0, 2, 4};
    size_t labels[] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};
    Dataset source = allocateDataset(5, 2);
    Dataset copy = allocateDataset(3, 2);
    for (size_t i = 0; i < source.size; ++i) copyCoordinates(datasetRow(&source, i), values[i], 2);
    for (size_t i = 0; i < copy.size; ++i)
        copyCoordinates(datasetRow(&copy, i), datasetRowConst(&source, indices[i]), 2);

    DatasetView view = {&source, indices, labels, 3};
    Centroids dense = allocateCentroids(2, 2);
    Centroids indexed = allocateCentroids(2, 2);
    copyCoordinates(centroidRow(&dense, 0), datasetViewRow(&view, 0), 2);
    copyCoordinates(centroidRow(&dense, 1), datasetViewRow(&view, 2), 2);
    deepCopyCentroids(&dense, &indexed);

    partitionStep(&copy, &dense);
    double expected = runKMeans(&copy, 10, &dense, NULL, KM_STAGE_NONE);
    double actual = runKMeansView(&view, 10, &indexed, NULL, KM_STAGE_NONE, true);
    require(expected == actual && memcmp(dense.coordinates, indexed.coordinates, 4 * sizeof(double)) == 0,
            "indexed k-means changed SSE or centroid coordinates");
    for (size_t i = 0; i < view.size; ++i)
        require(labels[i] == copy.partitions[i], "indexed k-means changed local partitions");
    for (size_t i = 0; i < source.size; ++i)
        require(source.partitions[i] == SIZE_MAX, "indexed k-means changed source partitions");

    freeCentroids(&indexed);
    freeCentroids(&dense);
    freeDataset(&copy);
    freeDataset(&source);
}

static void testRepartition(void)
{
    Dataset points = allocateDataset(4, 1);
    Centroids centroids = allocateCentroids(3, 1);
    const double values[] = {0.0, 4.0, 6.0, 10.0};
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = values[i];
        points.partitions[i] = i == 0 ? 0 : 1;
    }
    centroidRow(&centroids, 0)[0] = 0.0;
    centroidRow(&centroids, 1)[0] = 10.0;
    centroidRow(&centroids, 2)[0] = 5.0;

    bool affected[] = {false, false, false};
    localRepartition(&points, &centroids, 0, affected);
    require(points.partitions[0] == 0 && points.partitions[1] == 2 &&
                points.partitions[2] == 2 && points.partitions[3] == 1 && affected[1],
            "split local repartition is incorrect");
    size_t sizes[3];
    require(countClusterSizes(&points, 3, sizes) == 1 && sizes[0] == 1 && sizes[1] == 1 && sizes[2] == 2,
            "splittable cluster counts are incorrect");

    points.partitions[1] = 2;
    points.partitions[2] = 1;
    localRepartitionForRS(&points, &centroids, 2);
    require(points.partitions[0] == 0 && points.partitions[1] == 2 &&
                points.partitions[2] == 2 && points.partitions[3] == 1,
            "Random Swap local repartition is incorrect");
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testSplitCapacity(void)
{
    Dataset points = allocateDataset(4, 2);
    Centroids centroids = allocateCentroids(4, 2);
    centroids.size = 1;
    double* original = centroids.coordinates;
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = (double)i;
        datasetRow(&points, i)[1] = (double)(i % 2);
        points.partitions[i] = 0;
    }
    Rng rng;
    rng_seed(&rng, 7);
    ClusterIndex index = allocateClusterIndex(points.size, centroids.capacity);
    rebuildClusterIndex(&index, &points, centroids.size);
    splitClusterIntraCluster(&points, &index, &centroids, 0, 10, NULL, &rng);
    require(centroids.size == 2 && centroids.capacity == 4 && centroids.coordinates == original,
            "split did not use the preallocated centroid capacity");
    for (size_t i = 0; i < points.size; ++i)
    {
        require(points.partitions[i] < 2, "split wrote an invalid partition");
    }
    freeClusterIndex(&index);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testClusterIndex(void)
{
    Dataset points = allocateDataset(6, 1);
    const size_t labels[] = {2, 0, 2, 1, 0, 2};
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = (double)i;
        points.partitions[i] = labels[i];
    }

    ClusterIndex index = allocateClusterIndex(points.size, 4);
    require(rebuildClusterIndex(&index, &points, 4) == 2, "wrong splittable cluster count");
    const size_t expected[] = {1, 4, 3, 0, 2, 5};
    require(index.sizes[0] == 2 && index.sizes[1] == 1 && index.sizes[2] == 3 && index.sizes[3] == 0,
            "incorrect cluster sizes");
    for (size_t i = 0; i < points.size; ++i)
    {
        require(index.indices[i] == expected[i], "cluster membership did not preserve point order");
    }

    Rng rng;
    Rng untouched;
    rng_seed(&rng, 42);
    rng_seed(&untouched, 42);
    require(tentativeSseDrop(&points, &index, 1, 10, 1.0, &rng) == 0.0 &&
                tentativeSseDrop(&points, &index, 3, 10, 1.0, &rng) == 0.0 &&
                rng_u32(&rng) == rng_u32(&untouched),
            "empty or singleton trial changed SSE or the random stream");
    rng_seed(&rng, 91);
    rng_seed(&untouched, 91);
    double first = tentativeSseDrop(&points, &index, 2, 10, 20.0, &rng);
    double second = tentativeSseDrop(&points, &index, 2, 10, 20.0, &untouched);
    require(first == second && rng_u32(&rng) == rng_u32(&untouched),
            "reused local-label scratch changed a tentative split");
    for (size_t i = 0; i < points.size; ++i)
    {
        require(points.partitions[i] == labels[i] && datasetRow(&points, i)[0] == (double)i,
                "tentative split changed the source dataset");
    }

    points.partitions[0] = 0;
    points.partitions[3] = 0;
    require(rebuildClusterIndex(&index, &points, 4) == 2 && index.sizes[0] == 4 && index.sizes[1] == 0 &&
                index.sizes[2] == 2 && index.indices[0] == 0 && index.indices[3] == 4 &&
                index.indices[4] == 2 && index.indices[5] == 5,
            "rebuilt cluster membership is incorrect");
    freeClusterIndex(&index);
    freeDataset(&points);
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

static void testInvalidAllocations(const char* executable, const char* directory)
{
    char errorPath[PATH_MAX];
    char command[2 * PATH_MAX + 64];
    int length = snprintf(errorPath, sizeof(errorPath), "%s%cinvalid-allocation.txt", directory, PATHSEP);
    require(length >= 0 && (size_t)length < sizeof(errorPath), "invalid allocation fixture path too long");
    const char* modes[] = {"--overflow", "--index-overflow", "--zero-dataset", "--zero-centroid-dimensions"};
    const char* messages[] = {"exceeds addressable memory", "Invalid cluster index size",
                              "Invalid dataset dimensions", "Invalid dataset dimensions"};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
    {
        length = snprintf(command, sizeof(command), "cd . && \"%s\" %s 2> \"%s\"", executable, modes[i], errorPath);
        require(length >= 0 && (size_t)length < sizeof(command), "invalid allocation command too long");
        int status = system(command);
        require(status != -1 && status != 0, "invalid allocation did not fail");
        FILE* errors = fopen(errorPath, "r");
        require(errors != NULL, "could not read allocation error");
        char line[256];
        require(fgets(line, sizeof(line), errors) != NULL && strstr(line, messages[i]) != NULL,
                "invalid allocation did not report its error");
        closeFixture(errors);
    }
    require(remove(errorPath) == 0, "could not remove allocation fixture");
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
    Dataset points = readDataset(path);
    require(points.size == 2 && points.dimensions == 2 && points.partitions[0] == SIZE_MAX &&
                datasetRow(&points, 0)[0] == 1.25 && datasetRow(&points, 0)[1] == -2.0 &&
                datasetRow(&points, 1)[1] == 45.0,
            "valid dataset parsed incorrectly");
    freeDataset(&points);
    Centroids centroids = readCentroids(path);
    require(centroids.size == 2 && centroidRow(&centroids, 1)[1] == 45.0, "valid centroid file parsed incorrectly");
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
    points = readDataset(path);
    require(points.size == 1 && datasetRow(&points, 0)[0] == 1.0, "maximum-length line rejected");
    freeDataset(&points);

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

static int runCli(const char* executable, const char* arguments, const char* capture)
{
    char command[6 * PATH_MAX + 2048];
    int length = snprintf(command, sizeof(command), "cd . && \"%s\" %s > \"%s\" 2>&1", executable, arguments, capture);
    require(length >= 0 && (size_t)length < sizeof(command), "CLI test command too long");
    return system(command);
}

static void expectCli(const char* executable, const char* arguments, const char* capture, bool success)
{
    int status = runCli(executable, arguments, capture);
    if (status == -1 || (status == 0) != success)
    {
        fprintf(stderr, "test_unit: CLI command unexpectedly %s: %s\n", success ? "failed" : "succeeded", arguments);
        FILE* file = fopen(capture, "r");
        if (file != NULL)
        {
            char line[1024];
            while (fgets(line, sizeof(line), file) != NULL) fputs(line, stderr);
            fclose(file);
        }
        exit(EXIT_FAILURE);
    }
}

static void expectText(const char* path, const char* expected)
{
    FILE* file = fopen(path, "r");
    require(file != NULL, "could not open CLI test output");
    char text[8192];
    size_t length = fread(text, 1, sizeof(text) - 1, file);
    require(!ferror(file), "could not read CLI test output");
    text[length] = '\0';
    closeFixture(file);
    if (strstr(text, expected) == NULL || strstr(text, "Sanitizer") != NULL)
    {
        fprintf(stderr, "test_unit: expected '%s' in %s:\n%s\n", expected, path, text);
        exit(EXIT_FAILURE);
    }
}

static void runCapCase(const char* executable, const char* data, const char* output, const char* capture,
                       const char* algorithms, const char* cap, size_t k)
{
    char arguments[3 * PATH_MAX + 1024];
    int length = snprintf(arguments, sizeof(arguments),
                          "--seed 42 -r 1 --max-swaps 1 --repeats 1 --bisect-tries 2 --track-progress --track-inner "
                          "--algo %s %s -o \"%s\" -k %zu \"%s\"",
                          algorithms, cap, output, k, data);
    require(length >= 0 && (size_t)length < sizeof(arguments), "CLI case too long");
    expectCli(executable, arguments, capture, true);
}

static void caseFile(char* path, size_t size, const char* output, const char* name)
{
    char dataset[PATH_MAX];
    joinPath(dataset, sizeof(dataset), output, "s01_blobs2d");
    joinPath(path, size, dataset, name);
}

static FILE* openCaseFile(const char* output, const char* algorithm, const char* suffix)
{
    char name[64];
    char path[PATH_MAX];
    int length = snprintf(name, sizeof(name), "%s%s", algorithm, suffix);
    require(length >= 0 && (size_t)length < sizeof(name), "algorithm file name too long");
    caseFile(path, sizeof(path), output, name);
    FILE* file = fopen(path, "r");
    if (file == NULL)
    {
        fprintf(stderr, "test_unit: could not open %s\n", path);
        exit(EXIT_FAILURE);
    }
    return file;
}

static double caseSse(const char* output, const char* algorithm)
{
    FILE* file = openCaseFile(output, algorithm, "_runs.csv");
    char line[512];
    require(fgets(line, sizeof(line), file) != NULL && fgets(line, sizeof(line), file) != NULL,
            "missing run result");
    size_t run = 0;
    double sse = 0.0;
    require(sscanf(line, "%zu;%*[^;];%lf;", &run, &sse) == 2 && run == 1 && isfinite(sse),
            "invalid run result");
    require(fgets(line, sizeof(line), file) == NULL && !ferror(file), "unexpected extra run result");
    closeFixture(file);
    return sse;
}

/* Rows must be runs 1..runs in order. */
static void checkRunNumbers(const char* output, const char* algorithm, size_t runs)
{
    FILE* file = openCaseFile(output, algorithm, "_runs.csv");
    char line[512];
    require(fgets(line, sizeof(line), file) != NULL, "missing run results header");
    size_t rows = 0;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        size_t run = 0;
        ++rows;
        require(sscanf(line, "%zu;", &run) == 1 && run == rows, "unexpected run number in run results");
    }
    require(!ferror(file) && rows == runs, "unexpected number of run results");
    closeFixture(file);
}

/* Number of KM iterations of run 1; the log must hold only run 1 with consecutive iterations. */
static size_t kmIterations(const char* output)
{
    FILE* file = openCaseFile(output, "KM", "_log.csv");
    char line[512];
    require(fgets(line, sizeof(line), file) != NULL, "missing KM log header");
    size_t rows = 0;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        size_t run = 0;
        size_t iteration = 0;
        ++rows;
        require(sscanf(line, "%zu;%zu;", &run, &iteration) == 2 && run == 1 && iteration == rows,
                "invalid KM iteration log");
    }
    require(!ferror(file), "could not read KM iteration log");
    closeFixture(file);
    return rows;
}

enum
{
    STAGE_SPLIT = 1,
    STAGE_TRY = 2,
    STAGE_GLOBAL = 4,
    STAGE_FINAL = 8
};

static void checkInnerIterations(const char* output, const char* algorithm, size_t limit, unsigned expectedStages)
{
    FILE* file = openCaseFile(output, algorithm, "_inner.csv");
    char line[512];
    require(fgets(line, sizeof(line), file) != NULL, "missing inner log header");
    unsigned first = 0;
    unsigned second = 0;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        size_t run = 0;
        size_t count = 0;
        size_t iteration = 0;
        char stage[16];
        require(sscanf(line, "%zu;%zu;%15[^;];%zu;", &run, &count, stage, &iteration) == 4 &&
                    run == 1 && iteration >= 1 && iteration <= limit,
                "inner k-means exceeded its iteration limit");
        unsigned flag = strcmp(stage, "split") == 0    ? STAGE_SPLIT
                        : strcmp(stage, "try") == 0   ? STAGE_TRY
                        : strcmp(stage, "global") == 0 ? STAGE_GLOBAL
                        : strcmp(stage, "final") == 0  ? STAGE_FINAL
                                                       : 0;
        require(flag != 0, "unknown inner k-means stage");
        if (iteration == 1) first |= flag;
        if (iteration == 2) second |= flag;
    }
    if (ferror(file) || first != expectedStages || second != (limit == 2 ? expectedStages : 0))
    {
        fprintf(stderr, "test_unit: unexpected k-means stages in %s_inner.csv under %s\n", algorithm, output);
        exit(EXIT_FAILURE);
    }
    closeFixture(file);
}

static void testCappedRkmSnapshot(const char* directory, const char* executable, const char* data, const char* truth,
                                  const char* capture)
{
    const size_t repeats = 2;
    char output[PATH_MAX];
    char name[64];
    char bestPath[PATH_MAX];
    char finalPath[PATH_MAX];
    char centroidPath[PATH_MAX];
    char arguments[4 * PATH_MAX + 512];
    joinPath(output, sizeof(output), directory, "rkm-snapshot");
    int length = snprintf(arguments, sizeof(arguments),
                          "--algo rkm --seed 42 --repeats %zu --max-iterations 1 -r 1 --track-progress --snapshots 1 "
                          "-o \"%s\" -k 4 \"%s\" \"%s\"",
                          repeats, output, data, truth);
    require(length >= 0 && (size_t)length < sizeof(arguments), "RKM snapshot case too long");
    expectCli(executable, arguments, capture, true);

    // The snapshot of the last improvement holds the best repeat's labels
    bestPath[0] = '\0';
    for (size_t count = repeats; count >= 1 && bestPath[0] == '\0'; --count)
    {
        length = snprintf(name, sizeof(name), "RKM_run001_iter%04zu_step_partitions.txt", count);
        require(length >= 0 && (size_t)length < sizeof(name), "snapshot name too long");
        caseFile(bestPath, sizeof(bestPath), output, name);
        if (!fileExists(bestPath)) bestPath[0] = '\0';
    }
    require(bestPath[0] != '\0', "missing RKM improvement snapshot");
    caseFile(finalPath, sizeof(finalPath), output, "RKM_partitions_perfect.txt");
    caseFile(centroidPath, sizeof(centroidPath), output, "RKM_centroids_perfect.txt");
    if (!fileExists(finalPath))
    {
        caseFile(finalPath, sizeof(finalPath), output, "RKM_partitions_failed.txt");
        caseFile(centroidPath, sizeof(centroidPath), output, "RKM_centroids_failed.txt");
    }

    FILE* best = fopen(bestPath, "r");
    FILE* final = fopen(finalPath, "r");
    require(best != NULL && final != NULL, "missing RKM partition snapshots");
    Dataset points = readDataset(data);
    Centroids centroids = readCentroids(centroidPath);
    for (size_t i = 0; i < points.size; ++i)
    {
        size_t bestLabel = 0;
        size_t finalLabel = 0;
        require(fscanf(best, "%zu", &bestLabel) == 1 && fscanf(final, "%zu", &finalLabel) == 1 &&
                    bestLabel == finalLabel && finalLabel < centroids.size,
                "capped RKM example does not retain the best repeat's partition");
        points.partitions[i] = finalLabel;
    }
    size_t extra = 0;
    require(fscanf(best, "%zu", &extra) == EOF && fscanf(final, "%zu", &extra) == EOF &&
                !ferror(best) && !ferror(final),
            "unexpected RKM partition snapshot length");
    closeFixture(best);
    closeFixture(final);
    double expected = caseSse(output, "RKM");
    double actual = calculateSSE(&points, &centroids);
    require(fabs(expected - actual) <= 1e-12 * fmax(1.0, fabs(expected)),
            "capped RKM snapshot does not match its reported SSE");
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testCappedTrials(const char* directory, const char* executable, const char* data, const char* capture)
{
    char output[PATH_MAX];
    char arguments[3 * PATH_MAX + 256];
    joinPath(output, sizeof(output), directory, "trials");
    int length = snprintf(arguments, sizeof(arguments),
                          "--algo skm-intra --seed 42 -r 1 --max-iterations 1 --log-level 3 --no-track "
                          "-o \"%s\" -k 2 \"%s\"",
                          output, data);
    require(length >= 0 && (size_t)length < sizeof(arguments), "trial cap case too long");
    expectCli(executable, arguments, capture, true);
    FILE* file = fopen(capture, "r");
    require(file != NULL, "could not open trial cap trace");
    char line[1024];
    size_t calls = 0;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        const char* iterationText = strstr(line, "(runKMeans)After iteration ");
        if (iterationText != NULL)
        {
            size_t iteration = 0;
            require(sscanf(iterationText, "(runKMeans)After iteration %zu:", &iteration) == 1 && iteration == 1,
                    "a tentative or split k-means exceeded the cap");
            ++calls;
        }
    }
    require(!ferror(file) && calls >= 4, "trial cap trace did not include both tentative splits");
    closeFixture(file);
}

static void testCappedSwapTrace(const char* directory, const char* executable, const char* data, const char* capture)
{
    char output[PATH_MAX];
    char arguments[3 * PATH_MAX + 256];
    joinPath(output, sizeof(output), directory, "rs-trace");
    int length = snprintf(arguments, sizeof(arguments),
                          "--algo rs --seed 42 --max-swaps 1 --max-iterations 1 --log-level 3 --no-track "
                          "-o \"%s\" -k 4 \"%s\"",
                          output, data);
    require(length >= 0 && (size_t)length < sizeof(arguments), "Random Swap trace case too long");
    expectCli(executable, arguments, capture, true);
    FILE* file = fopen(capture, "r");
    require(file != NULL, "could not open Random Swap trace");
    char line[1024];
    size_t iterations = 0;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        const char* iterationText = strstr(line, "(runKMeans)After iteration ");
        if (iterationText != NULL)
        {
            size_t iteration = 0;
            ++iterations;
            require(sscanf(iterationText, "(runKMeans)After iteration %zu:", &iteration) == 1 &&
                        iteration == iterations && iteration <= 2,
                    "Random Swap did not use its two-iteration limit");
        }
    }
    require(!ferror(file) && iterations == 2, "Random Swap did not run both expected iterations");
    closeFixture(file);
}

static void testCliIterationLimit(const char* directory, const char* executable, const char* data, const char* truth,
                                  const char* batch)
{
    // -o appends to existing results, so every invocation writes to a new folder
    char root[PATH_MAX];
    unsigned attempt = 0;
    do
    {
        char name[32];
        require(++attempt <= 1000, "too many earlier iteration-limit test folders");
        snprintf(name, sizeof(name), "cap-%u", attempt);
        joinPath(root, sizeof(root), directory, name);
    } while (fileExists(root));
    require(platform_make_directories(root), "could not create the iteration-limit test folder");

    char one[PATH_MAX];
    char two[PATH_MAX];
    char unlimited[PATH_MAX];
    char k1[PATH_MAX];
    char k2[PATH_MAX];
    char invalid[PATH_MAX];
    char report[PATH_MAX];
    char capture[PATH_MAX];
    char path[PATH_MAX];
    char arguments[3 * PATH_MAX + 1024];
    joinPath(one, sizeof(one), root, "one");
    joinPath(two, sizeof(two), root, "two");
    joinPath(unlimited, sizeof(unlimited), root, "unlimited");
    joinPath(k1, sizeof(k1), root, "k1");
    joinPath(k2, sizeof(k2), root, "k2");
    joinPath(invalid, sizeof(invalid), root, "invalid");
    joinPath(report, sizeof(report), root, "report");
    joinPath(capture, sizeof(capture), root, "command.txt");

    require(kmIterations(batch) == 1, "batch-mode KM did not respect its iteration limit");
    joinPath(path, sizeof(path), batch, "run_config.txt");
    expectText(path, "max-iterations: 1\n");

    const char* badOptions[] = {"--max-iterations", "--max-iterations \"\"", "--max-iterations 0",
                                "--max-iterations -1", "--max-iterations +1",
                                "--max-iterations 1.5", "--max-iterations 1x", "--max-iterations nope",
                                "--max-iterations 18446744073709551616",
                                "--max-iterations 1 --max-iterations 2"};
    for (size_t i = 0; i < sizeof(badOptions) / sizeof(badOptions[0]); ++i)
    {
        int length = snprintf(arguments, sizeof(arguments), "-o \"%s\" -k 4 \"%s\" %s", invalid, data, badOptions[i]);
        require(length >= 0 && (size_t)length < sizeof(arguments), "invalid CLI case too long");
        expectCli(executable, arguments, capture, false);
        expectText(capture, "Error: --max-iterations");
        require(!fileExists(invalid), "invalid iteration limit created an output folder");
    }

    const char* algorithms = "km,rkm,rs,skm-random,skm-intra,skm-global,skm-global-basic,skm-local,bisecting";
    runCapCase(executable, data, one, capture, algorithms, "--max-iterations 1", 4);
    runCapCase(executable, data, two, capture, algorithms, "--max-iterations 2", 4);
    runCapCase(executable, data, unlimited, capture, algorithms, "", 4);
    require(kmIterations(one) == 1 && kmIterations(two) == 2, "KM did not respect its iteration limit");
    // With more than 2 iterations, iteration 2 improved, so caps 1 and 2 must give different results
    require(kmIterations(unlimited) > 2, "fixture: unlimited KM must need more than 2 iterations");
    double kmOne = caseSse(one, "KM");
    double kmTwo = caseSse(two, "KM");
    double kmUnlimited = caseSse(unlimited, "KM");
    require(kmOne > kmTwo && kmTwo >= kmUnlimited, "KM cap did not change the result as expected");
    require(caseSse(one, "RKM") == kmOne && caseSse(two, "RKM") == kmTwo && caseSse(unlimited, "RKM") == kmUnlimited,
            "RKM did not apply the iteration limit to its repeat");
    require(caseSse(one, "RS") == caseSse(unlimited, "RS") && caseSse(two, "RS") == caseSse(unlimited, "RS"),
            "iteration option changed Random Swap");
    testCappedRkmSnapshot(root, executable, data, truth, capture);
    testCappedTrials(root, executable, data, capture);
    testCappedSwapTrace(root, executable, data, capture);

    const struct
    {
        const char* name;
        unsigned stages;
    } splitCases[] = {{"SKM_Random", STAGE_SPLIT | STAGE_FINAL},
                      {"SKM_Intra", STAGE_SPLIT | STAGE_FINAL},
                      {"SKM_Global", STAGE_SPLIT | STAGE_GLOBAL},
                      {"SKM_Global_Basic", STAGE_SPLIT | STAGE_GLOBAL},
                      {"SKM_Local", STAGE_SPLIT | STAGE_FINAL},
                      {"Bisecting_KM", STAGE_SPLIT | STAGE_TRY | STAGE_FINAL}};
    for (size_t i = 0; i < sizeof(splitCases) / sizeof(splitCases[0]); ++i)
    {
        checkInnerIterations(one, splitCases[i].name, 1, splitCases[i].stages);
        checkInnerIterations(two, splitCases[i].name, 2, splitCases[i].stages);
    }
    runCapCase(executable, data, k1, capture,
               "skm-random,skm-intra,skm-global,skm-global-basic,skm-local,bisecting", "--max-iterations 1", 1);
    for (size_t i = 0; i < sizeof(splitCases) / sizeof(splitCases[0]); ++i)
        checkInnerIterations(k1, splitCases[i].name, 1, STAGE_FINAL);
    runCapCase(executable, data, k2, capture, "skm-global,skm-global-basic", "--max-iterations 1", 2);
    checkInnerIterations(k2, "SKM_Global", 1, STAGE_SPLIT | STAGE_FINAL);
    checkInnerIterations(k2, "SKM_Global_Basic", 1, STAGE_SPLIT | STAGE_FINAL);

    joinPath(path, sizeof(path), one, "run_config.txt");
    expectText(path, "max-iterations: 1\n");
    joinPath(path, sizeof(path), two, "run_config.txt");
    expectText(path, "max-iterations: 2\n");
    joinPath(path, sizeof(path), unlimited, "run_config.txt");
    expectText(path, "max-iterations: until the SSE stops improving\n");

    int length = snprintf(arguments, sizeof(arguments),
                          "--seed 42 --max-swaps 1 --repeats 1 --bisect-tries 2 --max-iterations 01 "
                          "-r 2 --first-run 2 --algo km -o \"%s\" -k 4 \"%s\"",
                          one, data);
    require(length >= 0 && (size_t)length < sizeof(arguments), "matching CLI case too long");
    expectCli(executable, arguments, capture, true);
    length = snprintf(arguments, sizeof(arguments),
                      "--seed 42 --max-swaps 1 --repeats 1 --bisect-tries 2 --max-iterations %zu "
                      "-r 2 --first-run 2 --algo km -o \"%s\" -k 4 \"%s\"",
                      (size_t)SIZE_MAX, unlimited, data);
    require(length >= 0 && (size_t)length < sizeof(arguments), "unlimited CLI case too long");
    expectCli(executable, arguments, capture, true);
    checkRunNumbers(unlimited, "KM", 2);

    length = snprintf(arguments, sizeof(arguments),
                      "--seed 42 --max-swaps 1 --repeats 1 --bisect-tries 2 --max-iterations 2 "
                      "-r 1 --algo km -o \"%s\" -k 4 \"%s\"",
                      one, data);
    require(length >= 0 && (size_t)length < sizeof(arguments), "mismatched CLI case too long");
    expectCli(executable, arguments, capture, false);
    expectText(capture, "another configuration");
    expectText(capture, "max-iterations: 1");
    expectText(capture, "max-iterations: 2");
    // The matching call appended run 2; the rejected call (run 1) must not have written a row
    checkRunNumbers(one, "KM", 2);

    length = snprintf(arguments, sizeof(arguments), "report -o \"%s\" \"%s\" \"%s\"", report, one, two);
    require(length >= 0 && (size_t)length < sizeof(arguments), "report CLI case too long");
    expectCli(executable, arguments, capture, false);
    expectText(capture, "different configurations");
    expectText(capture, "max-iterations: 1");
    expectText(capture, "max-iterations: 2");
    require(remove(capture) == 0, "could not remove CLI test capture");
}

int main(int argc, char* argv[])
{
    if (argc == 2 && strcmp(argv[1], "--overflow") == 0)
    {
        Dataset points = allocateDataset(SIZE_MAX, 2);
        freeDataset(&points);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--index-overflow") == 0)
    {
        ClusterIndex index = allocateClusterIndex(SIZE_MAX, 2);
        freeClusterIndex(&index);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--zero-dataset") == 0)
    {
        Dataset points = allocateDataset(0, 1);
        freeDataset(&points);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--zero-centroid-dimensions") == 0)
    {
        Centroids centroids = allocateCentroids(1, 0);
        freeCentroids(&centroids);
        return EXIT_SUCCESS;
    }
    if (argc == 3 && strcmp(argv[1], "--data") == 0)
    {
        Dataset points = readDataset(argv[2]);
        freeDataset(&points);
        return EXIT_SUCCESS;
    }
    if (argc == 3 && strcmp(argv[1], "--centroids") == 0)
    {
        Centroids centroids = readCentroids(argv[2]);
        freeCentroids(&centroids);
        return EXIT_SUCCESS;
    }
    require(argc == 2 || argc == 6,
            "expected a fixture directory and optional CLI executable, dataset, ground truth and batch output");
    testNumbers();
    testDatasetParser(argv[0], argv[1]);
    testInvalidAllocations(argv[0], argv[1]);
    testFlatStorage();
    testMetrics();
    testRng();
    testKMeans();
    testKMeansView();
    testRepartition();
    testSplitCapacity();
    testClusterIndex();
    if (argc == 6) testCliIterationLimit(argv[1], argv[2], argv[3], argv[4], argv[5]);
    puts("test_unit: parser, flat data, metrics, RNG, k-means, membership and repartition cases passed");
    return EXIT_SUCCESS;
}
