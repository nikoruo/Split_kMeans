/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"
#include "baselines.h"
#include "dataset.h"
#include "experiment.h"
#include "kmeans.h"
#include "metrics.h"
#include "rng.h"
#include "split.h"
#include "util.h"

#include <inttypes.h>
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

static KMeansScratch unitMeans;
static SplitScratch unitSplit;
static InitScratch unitInit[2];
static bool unitScratchRegistered;

static void freeUnitScratch(void)
{
    freeSplitScratch(&unitSplit);
    freeKMeansScratch(&unitMeans);
    freeInitScratch(&unitInit[0]);
    freeInitScratch(&unitInit[1]);
}

static void registerUnitScratch(void)
{
    if (!unitScratchRegistered)
    {
        require(atexit(freeUnitScratch) == 0, "could not register scratch cleanup");
        unitScratchRegistered = true;
    }
}

static KMeansScratch* unitScratch(size_t capacity, size_t dimensions)
{
    registerUnitScratch();
    if (unitMeans.sums.coordinates == NULL || unitMeans.sums.capacity < capacity ||
        unitMeans.sums.dimensions != dimensions)
    {
        freeKMeansScratch(&unitMeans);
        unitMeans = allocateKMeansScratch(capacity, dimensions);
    }
    return &unitMeans;
}

static KMeansScratch* unitKMeansScratch(const Centroids* centroids)
{
    return unitScratch(centroids->capacity, centroids->dimensions);
}

static InitScratch* unitInitScratch(const Dataset* dataPoints, InitMethod init)
{
    registerUnitScratch();
    InitScratch* scratch = &unitInit[init == INIT_KMEANSPP ? 1 : 0];
    if (scratch->capacity < dataPoints->size)
    {
        freeInitScratch(scratch);
        *scratch = allocateInitScratch(dataPoints->size, init);
    }
    return scratch;
}

static SplitScratch* unitSplitScratch(const Dataset* dataPoints, const ClusterIndex* index)
{
    KMeansScratch* kmeans = unitScratch(index->capacity < 2 ? 2 : index->capacity, dataPoints->dimensions);
    if (unitSplit.trial.centroids.coordinates == NULL ||
        unitSplit.trial.centroids.dimensions != dataPoints->dimensions)
    {
        freeSplitScratch(&unitSplit);
        unitSplit = allocateSplitScratch(kmeans);
    }
    else
    {
        unitSplit.kmeans = kmeans;
    }
    return &unitSplit;
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

static void testDistinctInitialization(void)
{
    Dataset points = allocateDataset(32, 2);
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = (double)i;
        datasetRow(&points, i)[1] = (double)(i % 5);
    }
    Centroids centroids = allocateCentroids(6, 2);
    for (uint64_t seed = 0; seed < 32; ++seed)
    {
        size_t indices[32];
        for (size_t i = 0; i < 32; ++i) indices[i] = i;
        Rng actual, previous;
        rng_seed(&actual, seed);
        rng_seed(&previous, seed);
        generateRandomCentroids(6, &points, &centroids, &actual, unitInitScratch(&points, INIT_RANDOM));
        for (size_t i = 0; i < 6; ++i)
        {
            size_t j = i + rng_below(&previous, 32 - i);
            size_t temp = indices[i];
            indices[i] = indices[j];
            indices[j] = temp;
            require(coordinatesEqual(centroidRowConst(&centroids, i), datasetRowConst(&points, indices[i]), 2),
                    "collision-free initialization changed the Fisher-Yates draws");
        }
        require(rng_u32(&actual) == rng_u32(&previous),
                "collision-free initialization changed the random stream");
    }
    freeCentroids(&centroids);
    freeDataset(&points);

    points = allocateDataset(5, 1);
    const double values[] = {0.0, -0.0, 0.0, 9.0, 10.0};
    for (size_t i = 0; i < points.size; ++i) datasetRow(&points, i)[0] = values[i];
    require(countDistinctRowsUpTo(&points, 5) == 3 && countDistinctRowsUpTo(&points, 2) == 2,
            "distinct-coordinate count mishandled duplicates or signed zero");
    centroids = allocateCentroids(3, 1);
    size_t collisionFree = 0;
    for (uint64_t seed = 0; seed < 64; ++seed)
    {
        size_t indices[] = {0, 1, 2, 3, 4};
        Rng rng, previous;
        rng_seed(&rng, seed);
        rng_seed(&previous, seed);
        for (size_t i = 0; i < 3; ++i)
        {
            size_t j = i + rng_below(&previous, 5 - i);
            size_t temp = indices[i];
            indices[i] = indices[j];
            indices[j] = temp;
        }
        bool noCollision = values[indices[0]] != values[indices[1]] &&
                           values[indices[0]] != values[indices[2]] && values[indices[1]] != values[indices[2]];
        generateRandomCentroids(3, &points, &centroids, &rng, unitInitScratch(&points, INIT_RANDOM));
        for (size_t i = 0; i < centroids.size; ++i)
            for (size_t j = 0; j < i; ++j)
                require(!coordinatesEqual(centroidRowConst(&centroids, i), centroidRowConst(&centroids, j), 1),
                        "random initialization selected duplicate coordinates");
        if (noCollision)
        {
            ++collisionFree;
            for (size_t i = 0; i < 3; ++i)
                require(centroidRow(&centroids, i)[0] == values[indices[i]],
                        "collision-free draws changed on a dataset with repeated rows");
            require(rng_u32(&rng) == rng_u32(&previous),
                    "collision-free repeated-row initialization changed the random stream");
        }
    }
    require(collisionFree > 0, "fixture did not exercise collision-free draws with repeated rows");

    uint64_t collisionSeed;
    Rng probe;
    for (collisionSeed = 0; collisionSeed < 10000; ++collisionSeed)
    {
        rng_seed(&probe, collisionSeed);
        if (rng_below(&probe, 5) == 0 && rng_below(&probe, 4) == 0 && rng_below(&probe, 3) == 0) break;
    }
    require(collisionSeed < 10000, "could not select a duplicate initialization draw");
    Rng actual, previous;
    rng_seed(&actual, collisionSeed);
    rng_seed(&previous, collisionSeed);
    (void)rng_below(&previous, 5);
    (void)rng_below(&previous, 4);
    (void)rng_below(&previous, 3);
    generateRandomCentroids(2, &points, &centroids, &actual, unitInitScratch(&points, INIT_RANDOM));
    require(centroidRow(&centroids, 0)[0] == 0 && centroidRow(&centroids, 1)[0] == 10 &&
                rng_u32(&actual) == rng_u32(&previous),
            "duplicate initialization did not shrink the remaining Fisher-Yates pool");
    datasetRow(&points, 4)[0] = 1e-200;
    require(countDistinctRowsUpTo(&points, 5) == 3,
            "distinct-coordinate count treated a tiny nonzero coordinate as zero");
    freeCentroids(&centroids);
    freeDataset(&points);
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

    double sse = runKMeans(&points, 10, &centroids, NULL, KM_STAGE_NONE, unitKMeansScratch(&centroids));
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
    double expected = runKMeans(&copy, 10, &dense, NULL, KM_STAGE_NONE, unitKMeansScratch(&dense));
    double actual = runKMeansView(&view, 10, &indexed, NULL, KM_STAGE_NONE, unitKMeansScratch(&indexed));
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

static void testKMeansScratchReuse(void)
{
    const double values[] = {0.1, 2.1, 9.9, 12.1};
    Dataset points = allocateDataset(4, 1);
    Centroids centroids = allocateCentroids(3, 1);
    KMeansScratch scratch = allocateKMeansScratch(3, 1);
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = values[i];
        points.partitions[i] = i < 2 ? 0 : 1;
    }
    centroidRow(&centroids, 2)[0] = 99.0;
    for (size_t i = 0; i < 3; ++i)
    {
        scratch.sums.coordinates[i] = 123.0;
        scratch.counts[i] = 42;
    }
    centroidStep(&centroids, &points, &scratch);
    const double firstMean = (values[0] + values[1]) / 2;
    const double secondMean = (values[2] + values[3]) / 2;
    require(centroidRow(&centroids, 0)[0] == firstMean && centroidRow(&centroids, 1)[0] == secondMean &&
                centroidRow(&centroids, 2)[0] == 99.0,
            "dense centroid step did not clear reused sums or retain an empty centroid");

    const size_t indices[] = {0, 2, 3};
    size_t labels[] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};
    DatasetView view = {&points, indices, labels, 3};
    Centroids local = allocateCentroids(2, 1);
    centroidRow(&local, 0)[0] = values[0];
    centroidRow(&local, 1)[0] = values[2];
    double sse = runKMeansView(&view, 10, &local, NULL, KM_STAGE_NONE, &scratch);
    const double expectedSse = (values[2] - secondMean) * (values[2] - secondMean) +
                               (values[3] - secondMean) * (values[3] - secondMean);
    require(sse == expectedSse && centroidRow(&local, 0)[0] == values[0] &&
                centroidRow(&local, 1)[0] == secondMean && labels[0] == 0 && labels[1] == 1 && labels[2] == 1,
            "indexed k-means did not clear scratch reused from dense k-means");

    double sum = 0.0;
    for (size_t i = 0; i < points.size; ++i)
    {
        points.partitions[i] = 2;
        sum += values[i];
    }
    centroidStep(&centroids, &points, &scratch);
    require(centroidRow(&centroids, 0)[0] == firstMean && centroidRow(&centroids, 1)[0] == secondMean &&
                centroidRow(&centroids, 2)[0] == sum / points.size,
            "growing the active scratch range retained sums or counts from an earlier step");

    freeCentroids(&local);
    freeKMeansScratch(&scratch);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testInitScratchReuse(void)
{
    const double values[] = {0.0, 0.0, 1.0, 2.0, 3.0, 4.0};
    Dataset points = allocateDataset(6, 1);
    Centroids actual = allocateCentroids(4, 1);
    Centroids expected = allocateCentroids(4, 1);
    for (size_t i = 0; i < points.size; ++i) datasetRow(&points, i)[0] = values[i];
    for (size_t method = 0; method < 2; ++method)
    {
        InitMethod init = method == 0 ? INIT_RANDOM : INIT_KMEANSPP;
        InitScratch reused = allocateInitScratch(points.size, init);
        for (uint64_t seed = 0; seed < 16; ++seed)
        {
            InitScratch fresh = allocateInitScratch(points.size, init);
            Rng actualRng, expectedRng;
            rng_seed(&actualRng, seed);
            rng_seed(&expectedRng, seed);
            initialCentroids(init, 4, &points, &actual, &actualRng, &reused);
            initialCentroids(init, 4, &points, &expected, &expectedRng, &fresh);
            require(memcmp(actual.coordinates, expected.coordinates, 4 * sizeof(double)) == 0 &&
                        rng_u32(&actualRng) == rng_u32(&expectedRng),
                    "reused initialization scratch changed centroids or the random stream");
            freeInitScratch(&fresh);
        }
        freeInitScratch(&reused);
    }
    freeCentroids(&expected);
    freeCentroids(&actual);
    freeDataset(&points);
}

static const double roundingValues[] = {
    9.999999999999998e19, 9.999999999999995e19, 1e20, 1.0000000000000005e20,
    1.0000000000000002e20, 1e20, 1e20, 9.999999999999995e19, 1.0000000000000003e20,
    1.0000000000000003e20, 1e20, 9.999999999999995e19, 9.999999999999997e19};
static const double roundingInitial[] = {1.0000000000000005e20, 9.999999999999995e19, 1e20};
enum { ROUNDING_COUNT = sizeof(roundingValues) / sizeof(roundingValues[0]) };

static Dataset roundingDataset(void)
{
    Dataset points = allocateDataset(ROUNDING_COUNT, 1);
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = roundingValues[i];
        points.partitions[i] = 0;
    }
    return points;
}

static void testLastKMeansState(void)
{
    Dataset first = roundingDataset();
    Dataset dense = roundingDataset();
    Dataset indexed = roundingDataset();
    Dataset tracked = roundingDataset();
    Centroids firstCentroids = allocateCentroids(3, 1);
    Centroids denseCentroids = allocateCentroids(3, 1);
    Centroids viewCentroids = allocateCentroids(3, 1);
    Centroids trackedCentroids = allocateCentroids(3, 1);
    copyCoordinates(firstCentroids.coordinates, roundingInitial, 3);
    copyCoordinates(denseCentroids.coordinates, roundingInitial, 3);
    copyCoordinates(viewCentroids.coordinates, roundingInitial, 3);
    copyCoordinates(trackedCentroids.coordinates, roundingInitial, 3);
    double firstSse = runKMeans(&first, 1, &firstCentroids, NULL, KM_STAGE_NONE, unitKMeansScratch(&firstCentroids));
    double denseSse = runKMeans(&dense, 10, &denseCentroids, NULL, KM_STAGE_NONE, unitKMeansScratch(&denseCentroids));
    require(firstSse == 1879048192.0 && denseSse == 2147483648.0 &&
                denseSse > firstSse && calculateSSE(&dense, &denseCentroids) == denseSse,
            "dense k-means did not return its last non-improving state");

    size_t indices[ROUNDING_COUNT], scratch[ROUNDING_COUNT];
    for (size_t i = 0; i < ROUNDING_COUNT; ++i) indices[i] = i;
    DatasetView view = {&indexed, indices, scratch, ROUNDING_COUNT};
    double viewSse =
        runKMeansView(&view, 10, &viewCentroids, NULL, KM_STAGE_NONE, unitKMeansScratch(&viewCentroids));
    for (size_t i = 0; i < indexed.size; ++i) indexed.partitions[i] = scratch[i];
    require(viewSse == denseSse && calculateSSE(&indexed, &viewCentroids) == viewSse &&
                memcmp(viewCentroids.coordinates, denseCentroids.coordinates, 3 * sizeof(double)) == 0,
            "indexed k-means did not return the same last state as dense k-means");

    TrackOptions options = {0};
    options.rows = true;
    Tracker tracker = trackerCreate(ALGO_KM, ".", &denseCentroids, &options);
    trackerBeginRun(&tracker, 0);
    double trackedSse =
        runKMeansWithTracking(&tracked, 10, &trackedCentroids, &tracker, unitKMeansScratch(&trackedCentroids));
    require(trackedSse == denseSse && calculateSSE(&tracked, &trackedCentroids) == trackedSse &&
                tracker.rowCount == 2 && tracker.rows[1].sse == trackedSse && tracker.rows[1].ci == 0 &&
                memcmp(trackedCentroids.coordinates, denseCentroids.coordinates, 3 * sizeof(double)) == 0,
            "tracked k-means did not report its last state");
    for (size_t i = 0; i < dense.size; ++i)
        require(dense.partitions[i] == indexed.partitions[i] && dense.partitions[i] == tracked.partitions[i],
                "dense, indexed and tracked k-means left different labels");
    trackerPause(&tracker);
    trackerFree(&tracker);
    freeCentroids(&trackedCentroids);
    freeCentroids(&viewCentroids);
    freeCentroids(&denseCentroids);
    freeCentroids(&firstCentroids);
    freeDataset(&tracked);
    freeDataset(&indexed);
    freeDataset(&dense);
    freeDataset(&first);
}

static void testLastTrialState(void)
{
    Dataset points = roundingDataset();
    Centroids centroids = allocateCentroids(2, 1);
    centroids.size = 1;
    centroidStep(&centroids, &points, unitKMeansScratch(&centroids));
    ClusterIndex index = allocateClusterIndex(points.size, centroids.capacity);
    rebuildClusterIndex(&index, &points, centroids.size);
    double originalSse = calculateClusterSSE(&points, &centroids, 0);
    Centroids cached = allocateCentroids(2, 1);
    Centroids bisect = allocateCentroids(2, 1);
    size_t cachedLabels[ROUNDING_COUNT], bisectLabels[ROUNDING_COUNT];
    Rng first, second;
    rng_seed(&first, 182);
    rng_seed(&second, 182);
    double drop = cacheTentativeSplit(&points, &index, 0, 10, originalSse, &cached, cachedLabels,
                                      NULL, NULL, &first, unitSplitScratch(&points, &index));
    double bisectSse = runBisectingTrial(&points, &index, 0, 10, &bisect, bisectLabels, NULL, &second,
                                         unitSplitScratch(&points, &index));
    require(bisectSse == 5905580032.0 && drop == originalSse - bisectSse &&
                memcmp(cached.coordinates, bisect.coordinates, 2 * sizeof(double)) == 0 &&
                rng_u32(&first) == rng_u32(&second),
            "tentative and bisecting trials did not score their last state");
    for (size_t i = 0; i < points.size; ++i)
        require(cachedLabels[i] < 2 && cachedLabels[i] == bisectLabels[i],
                "the next trial overwrote saved last-state labels");
    applyTentativeSplit(&points, &index, &centroids, 0, &cached, cachedLabels);
    require(calculateSSE(&points, &centroids) == bisectSse,
            "the applied tentative split does not match its last-state SSE");
    freeCentroids(&bisect);
    freeCentroids(&cached);
    freeClusterIndex(&index);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testLastStateSplitRunners(void)
{
    const struct { Algorithm algorithm; uint64_t seed; } cases[] = {
        {ALGO_SKM_RANDOM, 4}, {ALGO_SKM_INTRA, 0}, {ALGO_SKM_GLOBAL, 0},
        {ALGO_SKM_GLOBAL_BASIC, 1}, {ALGO_SKM_LOCAL, 0}, {ALGO_BISECTING, 0},
    };
    Centroids truth = allocateCentroids(3, 1);
    copyCoordinates(truth.coordinates, roundingInitial, 3);
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c)
    {
        Dataset points = roundingDataset();
        Centroids centroids = allocateCentroids(3, 1);
        centroids.size = 1;
        centroidStep(&centroids, &points, unitKMeansScratch(&centroids));
        TrackOptions options = {0};
        options.rows = true;
        options.inner = true;
        Tracker tracker = trackerCreate(cases[c].algorithm, ".", &truth, &options);
        trackerBeginRun(&tracker, 0);
        Rng rng;
        rng_seed(&rng, cases[c].seed);
        double sse;
        if (cases[c].algorithm == ALGO_SKM_RANDOM)
            sse = runRandomSplit(&points, &centroids, 3, 10, &tracker, &rng, unitKMeansScratch(&centroids));
        else if (cases[c].algorithm == ALGO_BISECTING)
            sse = runBisectingKMeans(&points, &centroids, 3, 10, 5, BISECT_SELECT_SSE, &tracker, &rng,
                                     unitKMeansScratch(&centroids));
        else
            sse = runSseSplit(&points, &centroids, 3, 10, cases[c].algorithm, REPARTITION_INWARD, &tracker, &rng,
                              unitKMeansScratch(&centroids));
        KMeansStage stage = cases[c].algorithm == ALGO_SKM_GLOBAL || cases[c].algorithm == ALGO_SKM_GLOBAL_BASIC
                                ? KM_STAGE_GLOBAL : KM_STAGE_FINAL;
        require(tracker.rowCount > 0 && tracker.innerCount > 0 && sse == calculateSSE(&points, &centroids) &&
                    tracker.rows[tracker.rowCount - 1].sse == sse &&
                    tracker.rows[tracker.rowCount - 1].ci == calculateCentroidIndex(&centroids, &truth) &&
                    tracker.innerRows[tracker.innerCount - 1].stage == stage &&
                    tracker.innerRows[tracker.innerCount - 1].sse == sse &&
                    tracker.innerRows[tracker.innerCount - 1].ci == tracker.rows[tracker.rowCount - 1].ci,
                "split runner did not report the final SSE, CI and labels from the same state");
        trackerPause(&tracker);
        trackerFree(&tracker);
        freeCentroids(&centroids);
        freeDataset(&points);
    }
    freeCentroids(&truth);
}

static void testBisectingLastTrialWins(void)
{
    Dataset expected = roundingDataset();
    Dataset actual = roundingDataset();
    Centroids expectedCentroids = allocateCentroids(2, 1);
    Centroids actualCentroids = allocateCentroids(2, 1);
    expectedCentroids.size = 1;
    actualCentroids.size = 1;
    centroidStep(&expectedCentroids, &expected, unitKMeansScratch(&expectedCentroids));
    centroidRow(&actualCentroids, 0)[0] = centroidRowConst(&expectedCentroids, 0)[0];
    ClusterIndex index = allocateClusterIndex(expected.size, expectedCentroids.capacity);
    rebuildClusterIndex(&index, &expected, expectedCentroids.size);
    Centroids first = allocateCentroids(2, 1);
    Centroids second = allocateCentroids(2, 1);
    size_t firstLabels[ROUNDING_COUNT], secondLabels[ROUNDING_COUNT];
    Rng expectedRng, actualRng;
    rng_seed(&expectedRng, 1);
    rng_seed(&actualRng, 1);
    double firstSse = runBisectingTrial(&expected, &index, 0, 10, &first, firstLabels, NULL, &expectedRng,
                                        unitSplitScratch(&expected, &index));
    double secondSse = runBisectingTrial(&expected, &index, 0, 10, &second, secondLabels, NULL, &expectedRng,
                                         unitSplitScratch(&expected, &index));
    require(firstSse == 5905580032.0 && secondSse == 4026531840.0 && firstSse > secondSse,
            "bisecting fixture did not distinguish the last scores of its tries");
    applyTentativeSplit(&expected, &index, &expectedCentroids, 0, &second, secondLabels);
    double expectedSse =
        runKMeans(&expected, 10, &expectedCentroids, NULL, KM_STAGE_FINAL, unitKMeansScratch(&expectedCentroids));

    TrackOptions options = {0};
    options.rows = true;
    options.inner = true;
    Tracker tracker = trackerCreate(ALGO_BISECTING, ".", &expectedCentroids, &options);
    trackerBeginRun(&tracker, 0);
    double actualSse = runBisectingKMeans(&actual, &actualCentroids, 2, 10, 2, BISECT_SELECT_SSE,
                                          &tracker, &actualRng, unitKMeansScratch(&actualCentroids));
    require(actualSse == expectedSse && calculateSSE(&actual, &actualCentroids) == actualSse &&
                tracker.rowCount == 3 && tracker.rows[1].sse == secondSse &&
                tracker.rows[2].sse == actualSse && tracker.rows[2].ci == 0 &&
                memcmp(actualCentroids.coordinates, expectedCentroids.coordinates, 2 * sizeof(double)) == 0 &&
                rng_u32(&actualRng) == rng_u32(&expectedRng),
            "bisecting did not apply the try with the best last-state SSE");
    for (size_t i = 0; i < actual.size; ++i)
        require(actual.partitions[i] == expected.partitions[i], "bisecting applied the wrong trial labels");
    trackerPause(&tracker);
    trackerFree(&tracker);
    freeCentroids(&second);
    freeCentroids(&first);
    freeClusterIndex(&index);
    freeCentroids(&actualCentroids);
    freeCentroids(&expectedCentroids);
    freeDataset(&actual);
    freeDataset(&expected);
}

static void checkDirectSnapshot(const char* directory, const char* algorithm, const Dataset* points,
                                const Centroids* truth, const size_t* labels, double expectedSse)
{
    char name[96], path[PATH_MAX], line[512];
    int length = snprintf(name, sizeof(name), "%s_runs.csv", algorithm);
    require(length >= 0 && (size_t)length < sizeof(name), "direct run file name too long");
    joinPath(path, sizeof(path), directory, name);
    FILE* file = fopen(path, "r");
    require(file != NULL && fgets(line, sizeof(line), file) != NULL && fgets(line, sizeof(line), file) != NULL,
            "missing direct run result");
    size_t run = 0, ci = CI_NA;
    double sse = 0.0;
    require(sscanf(line, "%zu;%*[^;];%lf;%zu;", &run, &sse, &ci) == 3 && run == 1 && ci == 0 &&
                sse == expectedSse && fgets(line, sizeof(line), file) == NULL && !ferror(file),
            "direct run reported the wrong SSE or CI");
    closeFixture(file);

    length = snprintf(name, sizeof(name), "%s_centroids_perfect.txt", algorithm);
    require(length >= 0 && (size_t)length < sizeof(name), "direct centroid snapshot name too long");
    joinPath(path, sizeof(path), directory, name);
    Centroids centroids = readCentroids(path);
    require(calculateSSE(points, &centroids) == sse && calculateCentroidIndex(&centroids, truth) == ci,
            "direct run SSE and CI do not match its saved centroids and labels");
    length = snprintf(name, sizeof(name), "%s_partitions_perfect.txt", algorithm);
    require(length >= 0 && (size_t)length < sizeof(name), "direct partition snapshot name too long");
    joinPath(path, sizeof(path), directory, name);
    file = fopen(path, "r");
    require(file != NULL, "missing direct partition snapshot");
    for (size_t i = 0; i < points->size; ++i)
    {
        size_t label = SIZE_MAX;
        require(fscanf(file, "%zu", &label) == 1 && label == labels[i] && points->partitions[i] == labels[i],
                "direct run did not retain the winning last-state labels");
    }
    size_t extra = 0;
    require(fscanf(file, "%zu", &extra) == EOF && !ferror(file), "unexpected direct snapshot labels");
    closeFixture(file);
    freeCentroids(&centroids);
}

static void testKmAndRkmLastState(const char* directory)
{
    Dataset reference = roundingDataset();
    Dataset nearest = roundingDataset();
    Centroids first = allocateCentroids(3, 1);
    Centroids second = allocateCentroids(3, 1);
    Rng rng;
    rng_seed(&rng, rng_derive_seed(4, 0));
    initialCentroids(INIT_RANDOM, 3, &reference, &first, &rng, unitInitScratch(&reference, INIT_RANDOM));
    double firstSse = runKMeans(&reference, SIZE_MAX, &first, NULL, KM_STAGE_NONE, unitKMeansScratch(&first));
    size_t winningLabels[ROUNDING_COUNT];
    memcpy(winningLabels, reference.partitions, sizeof(winningLabels));
    partitionStep(&nearest, &first);
    bool differsFromReassignment = false;
    for (size_t i = 0; i < reference.size; ++i)
        differsFromReassignment |= nearest.partitions[i] != winningLabels[i];
    initialCentroids(INIT_RANDOM, 3, &reference, &second, &rng, unitInitScratch(&reference, INIT_RANDOM));
    double secondSse = runKMeans(&reference, SIZE_MAX, &second, NULL, KM_STAGE_NONE, unitKMeansScratch(&second));
    bool differsFromLastRepeat = false;
    for (size_t i = 0; i < reference.size; ++i)
        differsFromLastRepeat |= reference.partitions[i] != winningLabels[i];
    require(firstSse == 1879048192.0 && secondSse == 2147483648.0 && firstSse < secondSse &&
                differsFromReassignment && differsFromLastRepeat,
            "RKM fixture did not distinguish the winning labels from reassignment and the last repeat");

    TrackOptions options = {0};
    options.rows = true;
    char name[64], testDirectory[PATH_MAX], output[PATH_MAX];
    int length = snprintf(name, sizeof(name), "last-state-%" PRIu64, platform_ticks());
    require(length >= 0 && (size_t)length < sizeof(name), "last-state test directory name too long");
    joinPath(testDirectory, sizeof(testDirectory), directory, name);
    require(MAKE_DIR(testDirectory) == 0, "could not create unique last-state test directory");
    joinPath(output, sizeof(output), testDirectory, "km");
    require(platform_make_directories(output), "could not create last-state KM directory");
    Dataset km = roundingDataset();
    runKMeansAlgorithm(&km, &first, 3, SIZE_MAX, 0, 1, 1, "last-state", output, &options, INIT_RANDOM, 4);
    checkDirectSnapshot(output, "KM", &km, &first, winningLabels, firstSse);

    joinPath(output, sizeof(output), testDirectory, "rkm");
    require(platform_make_directories(output), "could not create last-state RKM directory");
    Dataset rkm = roundingDataset();
    runRepeatedKMeansAlgorithm(&rkm, &first, 3, SIZE_MAX, 2, 0, 1, 1, "last-state", output,
                               &options, INIT_RANDOM, 4);
    checkDirectSnapshot(output, "RKM", &rkm, &first, winningLabels, firstSse);

    freeDataset(&rkm);
    freeDataset(&km);
    freeCentroids(&second);
    freeCentroids(&first);
    freeDataset(&nearest);
    freeDataset(&reference);
}

static void testRkmNoFiniteFallback(const char* directory)
{
    const double values[] = {1e308, -1e308, 0};
    Dataset reference = allocateDataset(3, 1);
    Dataset actual = allocateDataset(3, 1);
    for (size_t i = 0; i < reference.size; ++i)
    {
        datasetRow(&reference, i)[0] = values[i];
        datasetRow(&actual, i)[0] = values[i];
    }
    Centroids centroids = allocateCentroids(2, 1);
    Rng rng;
    rng_seed(&rng, rng_derive_seed(0, 0));
    initialCentroids(INIT_RANDOM, 2, &reference, &centroids, &rng, unitInitScratch(&reference, INIT_RANDOM));
    double trialSse = runKMeans(&reference, 1, &centroids, NULL, KM_STAGE_NONE, unitKMeansScratch(&centroids));
    require(!isfinite(trialSse) &&
                (reference.partitions[0] != 0 || reference.partitions[1] != 0 || reference.partitions[2] != 0),
            "RKM fallback fixture did not produce a non-finite trial with distinct labels");

    char name[64], output[PATH_MAX];
    int length = snprintf(name, sizeof(name), "rkm-fallback-%" PRIu64, platform_ticks());
    require(length >= 0 && (size_t)length < sizeof(name), "RKM fallback directory name too long");
    joinPath(output, sizeof(output), directory, name);
    require(MAKE_DIR(output) == 0, "could not create unique RKM fallback directory");
    TrackOptions options = {0};
    runRepeatedKMeansAlgorithm(&actual, NULL, 2, 1, 1, 0, 1, 1, "fallback", output, &options, INIT_RANDOM, 0);
    for (size_t i = 0; i < actual.size; ++i)
        require(actual.partitions[i] == 0, "RKM did not use its no-finite-SSE partition fallback");

    freeCentroids(&centroids);
    freeDataset(&actual);
    freeDataset(&reference);
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
    ClusterIndex index = allocateClusterIndex(points.size, centroids.size);
    localRepartition(&points, &index, &centroids, 0, REPARTITION_BOTH, affected);
    require(points.partitions[0] == 0 && points.partitions[1] == 2 &&
                points.partitions[2] == 2 && points.partitions[3] == 1 && affected[1],
            "split local repartition is incorrect");
    size_t sizes[3];
    require(countClusterSizes(&points, 3, sizes) == 1 && sizes[0] == 1 && sizes[1] == 1 && sizes[2] == 2,
            "splittable cluster counts are incorrect");

    points.partitions[1] = 2;
    points.partitions[2] = 1;
    bool rsAffected[] = {false, false, false};
    localRepartitionForRS(&points, &centroids, 2, rsAffected);
    require(points.partitions[0] == 0 && points.partitions[1] == 2 &&
                points.partitions[2] == 2 && points.partitions[3] == 1,
            "Random Swap local repartition is incorrect");
    freeClusterIndex(&index);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testRandomSwapRepartition(void)
{
    const double values[] = {0, 2, 9, 11, 14, 19, 21, 100, 102};
    const size_t labels[] = {0, 0, 1, 1, 1, 2, 2, 3, 3};
    const size_t expected[] = {1, 1, 1, 1, 1, 0, 2, 3, 3};
    Dataset points = allocateDataset(9, 1);
    Centroids centroids = allocateCentroids(4, 1);
    ClusterIndex index = allocateClusterIndex(points.size, centroids.size);
    bool affected[] = {false, false, false, false};
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = values[i];
        points.partitions[i] = labels[i];
    }
    centroidRow(&centroids, 0)[0] = 19;
    centroidRow(&centroids, 1)[0] = 10;
    centroidRow(&centroids, 2)[0] = 20;
    centroidRow(&centroids, 3)[0] = 100;
    localRepartitionForRS(&points, &centroids, 0, affected);
    for (size_t i = 0; i < points.size; ++i)
        require(points.partitions[i] == expected[i], "Random Swap passes did not use fixed centroids");
    require(affected[0] && affected[1] && affected[2] && !affected[3],
            "Random Swap marked the wrong affected clusters");
    updateAffectedCentroids(&points, &index, &centroids, affected);
    require(centroidRow(&centroids, 0)[0] == 19 && centroidRow(&centroids, 1)[0] == 7.2 &&
                centroidRow(&centroids, 2)[0] == 21 && centroidRow(&centroids, 3)[0] == 100,
            "Random Swap did not update only affected means");
    freeClusterIndex(&index);
    freeCentroids(&centroids);
    freeDataset(&points);

    points = allocateDataset(3, 1);
    centroids = allocateCentroids(3, 1);
    index = allocateClusterIndex(points.size, centroids.size);
    bool emptyAffected[] = {false, false, false};
    const double emptyValues[] = {0, 10, 100};
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = emptyValues[i];
        points.partitions[i] = i;
        centroidRow(&centroids, i)[0] = emptyValues[i];
    }
    centroidRow(&centroids, 2)[0] = 10;
    localRepartitionForRS(&points, &centroids, 2, emptyAffected);
    updateAffectedCentroids(&points, &index, &centroids, emptyAffected);
    require(points.partitions[2] == 1 && index.sizes[2] == 0 &&
                centroidRow(&centroids, 0)[0] == 0 && centroidRow(&centroids, 1)[0] == 55 &&
                centroidRow(&centroids, 2)[0] == 10,
            "Random Swap did not retain the swapped centroid of an empty cluster");
    freeClusterIndex(&index);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testAffectedCentroidStep(void)
{
    const double values[][2] = {{1e16, 0.1}, {10.1, -2.0}, {-1e16, 0.3}, {1.0, 1.0},
                                {1.0, 0.2}, {8.0, 5.0}, {6.5, 4.0}};
    const size_t labels[] = {0, 1, 0, 2, 0, 2, 1};
    const bool affected[] = {true, false, true, true};
    Dataset points = allocateDataset(7, 2);
    Centroids expected = allocateCentroids(4, 2);
    Centroids actual = allocateCentroids(4, 2);
    for (size_t i = 0; i < points.size; ++i)
    {
        copyCoordinates(datasetRow(&points, i), values[i], points.dimensions);
        points.partitions[i] = labels[i];
    }
    centroidRow(&actual, 1)[0] = 100.0;
    centroidRow(&actual, 1)[1] = 200.0;
    centroidRow(&actual, 3)[0] = 7.0;
    centroidRow(&actual, 3)[1] = -3.0;
    deepCopyCentroids(&actual, &expected);

    ClusterIndex index = allocateClusterIndex(points.size, actual.size);
    updateAffectedCentroids(&points, &index, &expected, affected);
    KMeansScratch scratch = allocateKMeansScratch(actual.size, actual.dimensions);
    for (size_t i = 0; i < actual.size * actual.dimensions; ++i) scratch.sums.coordinates[i] = 123.0;
    for (size_t i = 0; i < actual.size; ++i) scratch.counts[i] = 42;
    centroidStepAffected(&actual, &points, affected, &scratch);
    require(memcmp(actual.coordinates, expected.coordinates, actual.size * actual.dimensions * sizeof(double)) == 0 &&
                centroidRow(&actual, 0)[0] == 1.0 / 3.0 && centroidRow(&actual, 1)[0] == 100.0 &&
                centroidRow(&actual, 3)[0] == 7.0 && centroidRow(&actual, 3)[1] == -3.0,
            "affected means changed summation order or an empty or untouched centroid");
    for (size_t i = 0; i < points.size; ++i)
        require(points.partitions[i] == labels[i], "affected centroid update changed partition labels");

    freeKMeansScratch(&scratch);
    freeClusterIndex(&index);
    freeCentroids(&actual);
    freeCentroids(&expected);
    freeDataset(&points);
}

static uint64_t seedForSwapSequence(size_t k, size_t n, const size_t (*wanted)[2], size_t count)
{
    Rng rng;
    for (uint64_t seed = 0; seed < 1000000; ++seed)
    {
        rng_seed(&rng, seed);
        bool matches = true;
        for (size_t i = 0; i < count; ++i)
        {
            size_t centroid = rng_below(&rng, k);
            size_t point = rng_below(&rng, n);
            if (centroid != wanted[i][0] || point != wanted[i][1])
            {
                matches = false;
                break;
            }
        }
        if (matches) return seed;
    }
    require(false, "could not select Random Swap test proposals");
    return 0;
}

static void testRandomSwapAcceptance(void)
{
    const double values[] = {0, 1, 2, 50, 51, 100, 101};
    const size_t initialLabels[] = {0, 0, 0, 1, 1, 2, 2};
    const size_t proposals[][2] = {{0, 6}, {0, 2}};
    const uint64_t seed = seedForSwapSequence(3, 7, proposals, 2);
    double trackedCentroids[3] = {0};
    size_t trackedLabels[7] = {0};
    uint32_t trackedNext = 0;
    for (size_t test = 0; test < 4; ++test)
    {
        Dataset points = allocateDataset(7, 1);
        Centroids centroids = allocateCentroids(3, 1);
        for (size_t i = 0; i < points.size; ++i)
        {
            datasetRow(&points, i)[0] = values[i];
            points.partitions[i] = initialLabels[i];
        }
        centroidRow(&centroids, 0)[0] = 0;
        centroidRow(&centroids, 1)[0] = 50;
        centroidRow(&centroids, 2)[0] = 100;
        TrackOptions options = {0};
        options.rows = test != 2;
        Tracker tracker = trackerCreate(ALGO_RS, ".", NULL, &options);
        trackerBeginRun(&tracker, 0);
        Rng rng;
        rng_seed(&rng, seed);
        double sse = randomSwap(&points, &centroids, test == 3 ? 0 : (test == 0 ? 1 : 2), 4, &tracker, &rng);
        if (test == 0 || test == 3)
        {
            require(sse == 7 && tracker.rowCount == 1 && tracker.rows[0].count == 0 &&
                        tracker.rows[0].sse == 7 && centroidRow(&centroids, 0)[0] == 0 &&
                        centroidRow(&centroids, 1)[0] == 50 && centroidRow(&centroids, 2)[0] == 100,
                    "Random Swap accepted a worse first swap or did not restore its centroids");
            for (size_t i = 0; i < points.size; ++i)
                require(points.partitions[i] == initialLabels[i], "Random Swap did not restore rejected labels");
            if (test == 3)
            {
                Rng untouched;
                rng_seed(&untouched, seed);
                require(rng_u32(&rng) == rng_u32(&untouched), "zero-swap run consumed randomness");
            }
        }
        else
        {
            require(sse == 3 && calculateSSE(&points, &centroids) == sse,
                    "Random Swap did not accept the later improving swap");
            if (test == 1)
            {
                require(tracker.rowCount == 2 && tracker.rows[0].count == 0 && tracker.rows[1].count == 2 &&
                            tracker.rows[1].sse == sse,
                        "Random Swap tracked a rejected swap or missed the improvement");
                copyCoordinates(trackedCentroids, centroids.coordinates, 3);
                memcpy(trackedLabels, points.partitions, sizeof(trackedLabels));
                trackedNext = rng_u32(&rng);
            }
            else
            {
                require(memcmp(trackedCentroids, centroids.coordinates, sizeof(trackedCentroids)) == 0 &&
                            memcmp(trackedLabels, points.partitions, sizeof(trackedLabels)) == 0 &&
                            rng_u32(&rng) == trackedNext,
                        "Random Swap tracking changed the result or random stream");
            }
        }
        trackerPause(&tracker);
        trackerFree(&tracker);
        freeCentroids(&centroids);
        freeDataset(&points);
    }
}

static void testRandomSwapDistinctTargets(void)
{
    const size_t proposals[][2] = {{0, 0}, {0, 1}, {0, 2}};
    uint64_t seed;
    Rng probe;
    for (seed = 0; seed < 100000; ++seed)
    {
        rng_seed(&probe, seed);
        if (rng_below(&probe, 2) == proposals[0][0] && rng_below(&probe, 4) == proposals[0][1] &&
            rng_below(&probe, 4) == proposals[1][1] && rng_below(&probe, 4) == proposals[2][1])
            break;
    }
    require(seed < 100000, "could not select Random Swap redraw sequence");
    Dataset points = allocateDataset(4, 1);
    Centroids centroids = allocateCentroids(2, 1);
    const double values[] = {0, 10, 20, 30};
    for (size_t i = 0; i < points.size; ++i) datasetRow(&points, i)[0] = values[i];
    centroidRow(&centroids, 0)[0] = 0;
    centroidRow(&centroids, 1)[0] = 10;
    partitionStep(&points, &centroids);
    TrackOptions options = {0};
    options.rows = true;
    Tracker tracker = trackerCreate(ALGO_RS, ".", NULL, &options);
    trackerBeginRun(&tracker, 0);
    Rng actual, expected;
    rng_seed(&actual, seed);
    rng_seed(&expected, seed);
    (void)rng_below(&expected, 2);
    for (size_t i = 0; i < 3; ++i) (void)rng_below(&expected, 4);
    double sse = randomSwap(&points, &centroids, 1, 3, &tracker, &actual);
    require(sse == 100 && tracker.rowCount == 2 && rng_u32(&actual) == rng_u32(&expected),
            "Random Swap did not redraw targets equal to either existing centroid");
    trackerPause(&tracker);
    trackerFree(&tracker);
    freeCentroids(&centroids);
    freeDataset(&points);

    const size_t n = 128;
    for (seed = 0; seed < 10000; ++seed)
    {
        rng_seed(&probe, seed);
        (void)rng_below(&probe, 1);
        size_t misses = 0;
        while (misses < n && rng_below(&probe, n) != n - 1) ++misses;
        if (misses == n) break;
    }
    require(seed < 10000, "could not select Random Swap fallback sequence");
    points = allocateDataset(n, 1);
    centroids = allocateCentroids(1, 1);
    datasetRow(&points, n - 1)[0] = 9;
    resetPartitions(&points);
    tracker = trackerCreate(ALGO_RS, ".", NULL, &options);
    trackerBeginRun(&tracker, 0);
    rng_seed(&actual, seed);
    rng_seed(&expected, seed);
    (void)rng_below(&expected, 1);
    for (size_t i = 0; i < n; ++i) (void)rng_below(&expected, n);
    (void)rng_below(&expected, 1);
    sse = randomSwap(&points, &centroids, 1, 2, &tracker, &actual);
    require(sse < 81 && tracker.rowCount == 2 && rng_u32(&actual) == rng_u32(&expected),
            "Random Swap did not use the bounded eligible-row fallback");
    trackerPause(&tracker);
    trackerFree(&tracker);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testRandomSwapNoTarget(void)
{
    Dataset points = allocateDataset(4, 1);
    Centroids centroids = allocateCentroids(2, 1);
    const double values[] = {0, 0, 9, 9};
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = values[i];
        points.partitions[i] = i < 2 ? 0 : 1;
    }
    centroidRow(&centroids, 1)[0] = 9;
    TrackOptions options = {0};
    options.rows = true;
    Tracker tracker = trackerCreate(ALGO_RS, ".", NULL, &options);
    trackerBeginRun(&tracker, 0);
    Rng actual, untouched;
    rng_seed(&actual, 42);
    rng_seed(&untouched, 42);
    double sse = randomSwap(&points, &centroids, 100, 2, &tracker, &actual);
    require(sse == 0 && tracker.rowCount == 1 && tracker.rows[0].count == 0 &&
                rng_u32(&actual) == rng_u32(&untouched) && centroidRow(&centroids, 0)[0] == 0 &&
                centroidRow(&centroids, 1)[0] == 9,
            "Random Swap with K=U did not preserve the initial solution and random stream");
    for (size_t i = 0; i < points.size; ++i)
        require(points.partitions[i] == (i < 2 ? 0u : 1u), "no-target Random Swap changed a partition");
    trackerPause(&tracker);
    trackerFree(&tracker);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testLocalRepartitionMeans(void)
{
    const double values[] = {0, 4, 4, 4, 4, 8, 50, 10};
    const size_t labels[] = {0, 0, 1, 1, 1, 1, 2, 3};
    const size_t expected[] = {0, 1, 1, 1, 1, 3, 2, 3};
    const double initial[] = {2, 5, 50, 10};
    const double means[] = {0, 4, 50, 9};
    Dataset points = allocateDataset(8, 1);
    Centroids centroids = allocateCentroids(4, 1);
    ClusterIndex index = allocateClusterIndex(points.size, centroids.size);
    bool affected[] = {false, false, false, false};
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = values[i];
        points.partitions[i] = labels[i];
    }
    for (size_t c = 0; c < centroids.size; ++c) centroidRow(&centroids, c)[0] = initial[c];

    require(localRepartition(&points, &index, &centroids, 0, REPARTITION_BOTH, affected) == 1,
            "local repartition returned incorrect splittable count");
    for (size_t i = 0; i < points.size; ++i)
        require(points.partitions[i] == expected[i], "local repartition did not use fixed centroids in both passes");
    for (size_t c = 0; c < centroids.size; ++c)
        require(centroidRow(&centroids, c)[0] == means[c], "local repartition did not update the correct means");
    require(affected[0] && affected[1] && !affected[2] && affected[3],
            "local repartition marked the wrong clusters");

    Rng rng;
    rng_seed(&rng, 19);
    require(calculateClusterSSE(&points, &centroids, 1) == 0.0 &&
                calculateClusterSSE(&points, &centroids, 3) == 2.0 &&
                tentativeSseDrop(&points, &index, 1, 10, 0.0, &rng, unitSplitScratch(&points, &index)) == 0.0 &&
                tentativeSseDrop(&points, &index, 3, 10, 2.0, &rng, unitSplitScratch(&points, &index)) == 2.0,
            "local repartition left stale SSE or tentative drops");
    freeClusterIndex(&index);
    freeCentroids(&centroids);
    freeDataset(&points);

    points = allocateDataset(4, 1);
    centroids = allocateCentroids(3, 1);
    index = allocateClusterIndex(points.size, centroids.size);
    const double emptyValues[] = {4, 6, 4, 6};
    const size_t emptyLabels[] = {0, 2, 1, 1};
    bool emptyAffected[] = {false, false, false};
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = emptyValues[i];
        points.partitions[i] = emptyLabels[i];
    }
    centroidRow(&centroids, 0)[0] = 4;
    centroidRow(&centroids, 1)[0] = 5;
    centroidRow(&centroids, 2)[0] = 6;
    localRepartition(&points, &index, &centroids, 0, REPARTITION_BOTH, emptyAffected);
    require(index.sizes[1] == 0 && emptyAffected[1] && centroidRow(&centroids, 1)[0] == 5 &&
                calculateClusterSSE(&points, &centroids, 1) == 0.0 &&
                tentativeSseDrop(&points, &index, 1, 10, 0.0, &rng, unitSplitScratch(&points, &index)) == 0.0,
            "emptied cluster did not retain its centroid with zero SSE and drop");
    freeClusterIndex(&index);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testLocalRepartitionModes(void)
{
    const double values[] = {0, 3, 4, 5, 6, 100, 3.2};
    const size_t initialLabels[] = {0, 2, 2, 2, 2, 2, 1};
    const RepartitionMode modes[] = {REPARTITION_INWARD, REPARTITION_INWARD_NONCHILD, REPARTITION_BOTH};
    const size_t expected[][7] = {{0, 0, 0, 0, 0, 2, 1}, {0, 2, 2, 2, 2, 2, 1}, {0, 1, 1, 1, 1, 2, 1}};
    const double means[][3] = {{3.6, 3.2, 100}, {0, 3.2, 23.6}, {0, 4.24, 100}};
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m)
    {
        Dataset points = allocateDataset(7, 1);
        Centroids centroids = allocateCentroids(3, 1);
        ClusterIndex index = allocateClusterIndex(points.size, centroids.size);
        bool affected[] = {false, false, false};
        for (size_t i = 0; i < points.size; ++i)
        {
            datasetRow(&points, i)[0] = values[i];
            points.partitions[i] = initialLabels[i];
        }
        centroidRow(&centroids, 0)[0] = 0;
        centroidRow(&centroids, 1)[0] = 3.2;
        centroidRow(&centroids, 2)[0] = 23.6;
        localRepartition(&points, &index, &centroids, 0, modes[m], affected);
        for (size_t i = 0; i < points.size; ++i)
            require(points.partitions[i] == expected[m][i], "repartition mode moved the wrong points");
        for (size_t c = 0; c < centroids.size; ++c)
            require(fabs(centroidRow(&centroids, c)[0] - means[m][c]) < 1e-12,
                    "repartition mode produced the wrong means");
        freeClusterIndex(&index);
        freeCentroids(&centroids);
        freeDataset(&points);

        points = allocateDataset(4, 1);
        centroids = allocateCentroids(3, 1);
        index = allocateClusterIndex(points.size, centroids.size);
        const double tiedValues[] = {0, 5, 15, 20};
        const size_t tiedLabels[] = {0, 2, 2, 1};
        bool tiedAffected[] = {false, false, false};
        for (size_t i = 0; i < points.size; ++i)
        {
            datasetRow(&points, i)[0] = tiedValues[i];
            points.partitions[i] = tiedLabels[i];
        }
        centroidRow(&centroids, 0)[0] = 0;
        centroidRow(&centroids, 1)[0] = 20;
        centroidRow(&centroids, 2)[0] = 10;
        localRepartition(&points, &index, &centroids, 0, modes[m], tiedAffected);
        require(points.partitions[1] == (modes[m] == REPARTITION_BOTH ? 0u : 2u),
                "repartition mode did not preserve its tie behavior");
        freeClusterIndex(&index);
        freeCentroids(&centroids);
        freeDataset(&points);
    }
}

static uint64_t seedForPair(size_t count, size_t first, size_t second)
{
    Rng rng;
    for (uint64_t seed = 0; seed < 1000; ++seed)
    {
        rng_seed(&rng, seed);
        if (rng_below(&rng, count) == first && rng_below(&rng, count) == second) return seed;
    }
    require(false, "could not select the local-split test seeds");
    return 0;
}

static void testCachedTentativeSplit(void)
{
    const double values[] = {0, 2, 3, 10};
    const size_t expected[] = {0, 1, 1, 1};
    Dataset points = allocateDataset(4, 1);
    Centroids centroids = allocateCentroids(2, 1);
    centroids.size = 1;
    centroidRow(&centroids, 0)[0] = 3.75;
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = values[i];
        points.partitions[i] = 0;
    }
    ClusterIndex index = allocateClusterIndex(points.size, centroids.capacity);
    rebuildClusterIndex(&index, &points, centroids.size);
    Centroids best = allocateCentroids(2, 1);
    size_t labels[4] = {0};
    TrackOptions options = {0};
    options.inner = true;
    Tracker tracker = trackerCreate(ALGO_SKM_INTRA, ".", NULL, &options);
    trackerBeginRun(&tracker, 0);
    InnerTrace trace = {0};
    Rng rng;
    rng_seed(&rng, seedForPair(4, 0, 2));
    double drop = cacheTentativeSplit(&points, &index, 0, 1, 56.75, &best, labels, &tracker, &trace, &rng,
                                      unitSplitScratch(&points, &index));
    require(drop == 18.75 && centroidRow(&best, 0)[0] == 0 && centroidRow(&best, 1)[0] == 5,
            "cached trial did not retain the scored centroids");
    require(trace.count == 1 && tracker.innerCount == 0 && trace.rows[0].sse == 38,
            "tentative trace was not captured separately");
    trackerAppendTrace(&tracker, &trace, 7);
    require(tracker.innerCount == 1 && tracker.innerRows[0].count == 7 &&
                tracker.innerRows[0].ms == trace.rows[0].ms && tracker.innerRows[0].sse == trace.rows[0].sse,
            "selected trial trace did not retain its evaluation time and SSE");
    applyTentativeSplit(&points, &index, &centroids, 0, &best, labels);
    for (size_t i = 0; i < points.size; ++i)
        require(points.partitions[i] == expected[i], "cached trial did not retain its scored labels");
    require(calculateSSE(&points, &centroids) == 38, "applied trial SSE differs from its evaluated SSE");
    partitionStep(&points, &centroids);
    require(calculateSSE(&points, &centroids) == 33, "fixture did not distinguish reassigned trial labels");
    trackerPause(&tracker);
    trackerFree(&tracker);
    free(trace.rows);
    freeCentroids(&best);
    freeClusterIndex(&index);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testCachedTrialMembership(void)
{
    const double values[] = {0, 100, 200, 2, 102, 202, 3, 104, 204, 10, 106, 210};
    const size_t member2[] = {2, 5, 8, 11};
    Dataset points = allocateDataset(12, 1);
    Centroids centroids = allocateCentroids(5, 1);
    centroids.size = 3;
    centroidRow(&centroids, 0)[0] = 3.75;
    centroidRow(&centroids, 1)[0] = 103;
    centroidRow(&centroids, 2)[0] = 204;
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = values[i];
        points.partitions[i] = i % 3;
    }
    ClusterIndex index = allocateClusterIndex(points.size, centroids.capacity);
    rebuildClusterIndex(&index, &points, centroids.size);
    Centroids first = allocateCentroids(2, 1);
    Centroids untouched = allocateCentroids(2, 1);
    Centroids scratch = allocateCentroids(2, 1);
    size_t labels[12] = {0};
    size_t expected[4];
    Rng rng;
    rng_seed(&rng, 91);
    cacheTentativeSplit(&points, &index, 2, 2, calculateClusterSSE(&points, &centroids, 2),
                        &untouched, labels, NULL, NULL, &rng, unitSplitScratch(&points, &index));
    for (size_t i = 0; i < 4; ++i) expected[i] = labels[member2[i]];
    cacheTentativeSplit(&points, &index, 0, 2, calculateClusterSSE(&points, &centroids, 0),
                        &first, labels, NULL, NULL, &rng, unitSplitScratch(&points, &index));
    applyTentativeSplit(&points, &index, &centroids, 0, &first, labels);
    rebuildClusterIndex(&index, &points, centroids.size);
    for (size_t c = 0; c < centroids.size; c += 3)
        cacheTentativeSplit(&points, &index, c, 2, calculateClusterSSE(&points, &centroids, c),
                            &scratch, labels, NULL, NULL, &rng, unitSplitScratch(&points, &index));
    applyTentativeSplit(&points, &index, &centroids, 2, &untouched, labels);
    for (size_t i = 0; i < 4; ++i)
        require(points.partitions[member2[i]] == (expected[i] == 0 ? 2u : 4u),
                "rebuilding membership corrupted an untouched cached trial");
    for (size_t i = 1; i < points.size; i += 3)
        require(points.partitions[i] == 1, "cached split changed another cluster");
    freeCentroids(&scratch);
    freeCentroids(&untouched);
    freeCentroids(&first);
    freeClusterIndex(&index);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static TrackOptions splitSnapshots(size_t first, size_t last)
{
    TrackOptions options = {0};
    options.snapshots.enabled = true;
    options.snapshots.split = true;
    options.snapshots.runs.all = true;
    options.snapshots.counts.rangeCount = 1;
    options.snapshots.counts.low[0] = first;
    options.snapshots.counts.high[0] = last;
    return options;
}

static Centroids readSnapshotState(const char* directory, const char* algorithm, size_t count, const char* phase,
                                   Dataset* points)
{
    char name[128], path[PATH_MAX];
    int length = snprintf(name, sizeof(name), "%s_run001_iter%04zu_%s_centroids.txt", algorithm, count, phase);
    require(length >= 0 && (size_t)length < sizeof(name), "snapshot centroid name too long");
    joinPath(path, sizeof(path), directory, name);
    Centroids centroids = readCentroids(path);

    length = snprintf(name, sizeof(name), "%s_run001_iter%04zu_%s_partitions.txt", algorithm, count, phase);
    require(length >= 0 && (size_t)length < sizeof(name), "snapshot partition name too long");
    joinPath(path, sizeof(path), directory, name);
    FILE* file = fopen(path, "r");
    require(file != NULL, "could not read snapshot partitions");
    for (size_t i = 0; i < points->size; ++i)
        require(fscanf(file, "%zu", &points->partitions[i]) == 1, "invalid snapshot partition");
    size_t extra = 0;
    require(fscanf(file, "%zu", &extra) == EOF && !ferror(file), "unexpected extra snapshot partitions");
    closeFixture(file);
    return centroids;
}

static void testSelectedTrialIsApplied(const char* directory)
{
    const double values[] = {0, 2, 3, 10, 20, 22, 23, 30};
    const uint64_t seed = 1;
    Dataset expected = allocateDataset(8, 1);
    Dataset actual = allocateDataset(8, 1);
    Centroids expectedCentroids = allocateCentroids(3, 1);
    Centroids actualCentroids = allocateCentroids(3, 1);
    expectedCentroids.size = 1;
    actualCentroids.size = 1;
    for (size_t i = 0; i < expected.size; ++i)
    {
        datasetRow(&expected, i)[0] = values[i];
        datasetRow(&actual, i)[0] = values[i];
        expected.partitions[i] = 0;
        actual.partitions[i] = 0;
    }

    ClusterIndex index = allocateClusterIndex(expected.size, expectedCentroids.capacity);
    rebuildClusterIndex(&index, &expected, expectedCentroids.size);
    Rng rng;
    rng_seed(&rng, seed);
    splitClusterIntraCluster(&expected, &index, &expectedCentroids, 0, 1, NULL, &rng,
                             unitSplitScratch(&expected, &index));
    rebuildClusterIndex(&index, &expected, expectedCentroids.size);
    Centroids trials[2] = {allocateCentroids(2, 1), allocateCentroids(2, 1)};
    size_t labels[8] = {0};
    double drops[2];
    for (size_t c = 0; c < 2; ++c)
        drops[c] = cacheTentativeSplit(&expected, &index, c, 1,
                                       calculateClusterSSE(&expected, &expectedCentroids, c),
                                       &trials[c], labels, NULL, NULL, &rng, unitSplitScratch(&expected, &index));
    const Rng afterTrials = rng;
    size_t selected = index.sizes[0] < 2 ? 1 : (index.sizes[1] >= 2 && drops[1] > drops[0] ? 1 : 0);

    Dataset legacy = allocateDataset(8, 1);
    copyCoordinates(legacy.coordinates, expected.coordinates, expected.size);
    memcpy(legacy.partitions, expected.partitions, expected.size * sizeof(*expected.partitions));
    Centroids legacyCentroids = allocateCentroids(3, 1);
    legacyCentroids.size = 2;
    copyCoordinates(legacyCentroids.coordinates, expectedCentroids.coordinates, 2);
    ClusterIndex legacyIndex = allocateClusterIndex(legacy.size, legacyCentroids.capacity);
    rebuildClusterIndex(&legacyIndex, &legacy, legacyCentroids.size);
    Rng legacyRng = rng;
    splitClusterIntraCluster(&legacy, &legacyIndex, &legacyCentroids, selected, 1, NULL, &legacyRng,
                             unitSplitScratch(&legacy, &legacyIndex));
    double rerunSse = calculateSSE(&legacy, &legacyCentroids);

    applyTentativeSplit(&expected, &index, &expectedCentroids, selected, &trials[selected], labels);
    double evaluatedSse = calculateSSE(&expected, &expectedCentroids);
    require(evaluatedSse != rerunSse, "fixture did not distinguish a re-split from the scored trial");
    TrackOptions options = {0};
    options.rows = true;
    Tracker tracker = trackerCreate(ALGO_SKM_INTRA, ".", NULL, &options);
    trackerBeginRun(&tracker, 0);
    rng_seed(&rng, seed);
    (void)runSseSplit(&actual, &actualCentroids, 3, 1, ALGO_SKM_INTRA, REPARTITION_INWARD, &tracker, &rng,
                      unitKMeansScratch(&actualCentroids));
    require(tracker.rowCount == 4 && tracker.rows[2].sse == evaluatedSse,
            "SSE-split did not apply the evaluated trial");
    require(rng.state == afterTrials.state && rng.inc == afterTrials.inc,
            "K=3 did not preserve the trial evaluation random stream");
    trackerPause(&tracker);
    trackerFree(&tracker);

    char output[PATH_MAX];
    joinPath(output, sizeof(output), directory, "selected-global");
    require(platform_make_directories(output), "could not create global snapshot directory");
    TrackOptions globalOptions = splitSnapshots(2, 2);
    Tracker globalTracker = trackerCreate(ALGO_SKM_GLOBAL, output, NULL, &globalOptions);
    trackerBeginRun(&globalTracker, 0);
    for (size_t i = 0; i < actual.size; ++i) actual.partitions[i] = 0;
    actualCentroids.size = 1;
    centroidRow(&actualCentroids, 0)[0] = 0;
    rng_seed(&rng, seed);
    (void)runSseSplit(&actual, &actualCentroids, 3, 1, ALGO_SKM_GLOBAL, REPARTITION_INWARD, &globalTracker, &rng,
                      unitKMeansScratch(&actualCentroids));
    trackerPause(&globalTracker);
    trackerFree(&globalTracker);

    Dataset snapshot = allocateDataset(expected.size, expected.dimensions);
    Centroids snapshotCentroids = readSnapshotState(output, "SKM_Global", 2, "split", &snapshot);
    require(snapshotCentroids.size == expectedCentroids.size &&
                snapshotCentroids.dimensions == expectedCentroids.dimensions,
            "global split snapshot has the wrong centroid shape");
    for (size_t c = 0; c < snapshotCentroids.size; ++c)
        require(centroidRowConst(&snapshotCentroids, c)[0] == centroidRowConst(&expectedCentroids, c)[0],
                "global split did not apply the evaluated centroids");
    for (size_t i = 0; i < snapshot.size; ++i)
        require(snapshot.partitions[i] == expected.partitions[i],
                "global split did not apply the evaluated labels");
    freeCentroids(&snapshotCentroids);
    freeDataset(&snapshot);

    freeClusterIndex(&legacyIndex);
    freeCentroids(&legacyCentroids);
    freeDataset(&legacy);
    freeCentroids(&trials[1]);
    freeCentroids(&trials[0]);
    freeClusterIndex(&index);
    freeCentroids(&actualCentroids);
    freeCentroids(&expectedCentroids);
    freeDataset(&actual);
    freeDataset(&expected);
}

static void testLocalCachedTrialAcrossSplits(const char* directory)
{
    const double values[] = {0, 2, 3, 10, 20, 22, 23, 30, 100, 102, 103, 110};
    Dataset initial = allocateDataset(12, 1);
    Dataset actual = allocateDataset(12, 1);
    Centroids initialCentroids = allocateCentroids(5, 1);
    Centroids actualCentroids = allocateCentroids(5, 1);
    initialCentroids.size = 1;
    actualCentroids.size = 1;
    for (size_t i = 0; i < initial.size; ++i)
    {
        datasetRow(&initial, i)[0] = values[i];
        datasetRow(&actual, i)[0] = values[i];
        initial.partitions[i] = 0;
        actual.partitions[i] = 0;
    }
    centroidStep(&initialCentroids, &initial, unitKMeansScratch(&initialCentroids));
    centroidRow(&actualCentroids, 0)[0] = centroidRowConst(&initialCentroids, 0)[0];

    ClusterIndex index = allocateClusterIndex(initial.size, initialCentroids.capacity);
    rebuildClusterIndex(&index, &initial, initialCentroids.size);
    Rng rng;
    rng_seed(&rng, 3);
    splitClusterIntraCluster(&initial, &index, &initialCentroids, 0, 1, NULL, &rng, unitSplitScratch(&initial, &index));
    bool affected[5] = {false};
    localRepartition(&initial, &index, &initialCentroids, 0, REPARTITION_INWARD, affected);
    size_t retained = initial.partitions[8];
    size_t other = initial.partitions[0];
    require(retained != other && index.eligible[retained], "local cache fixture did not separate its clusters");
    for (size_t i = 0; i < initial.size; ++i)
        require(initial.partitions[i] == (i < 8 ? other : retained), "local cache fixture has unexpected members");

    Centroids trials[2] = {allocateCentroids(2, 1), allocateCentroids(2, 1)};
    size_t labels[12] = {0};
    for (size_t c = 0; c < 2; ++c)
        (void)cacheTentativeSplit(&initial, &index, c, 1,
                                  calculateClusterSSE(&initial, &initialCentroids, c),
                                  &trials[c], labels, NULL, NULL, &rng, unitSplitScratch(&initial, &index));

    char output[PATH_MAX];
    joinPath(output, sizeof(output), directory, "cached-local");
    require(platform_make_directories(output), "could not create local snapshot directory");
    TrackOptions options = splitSnapshots(3, 4);
    Tracker tracker = trackerCreate(ALGO_SKM_LOCAL, output, NULL, &options);
    trackerBeginRun(&tracker, 0);
    rng_seed(&rng, 3);
    (void)runSseSplit(&actual, &actualCentroids, 5, 1, ALGO_SKM_LOCAL, REPARTITION_INWARD, &tracker, &rng,
                      unitKMeansScratch(&actualCentroids));
    trackerPause(&tracker);
    trackerFree(&tracker);

    Dataset previous = allocateDataset(12, 1);
    Dataset split = allocateDataset(12, 1);
    Centroids previousCentroids = readSnapshotState(output, "SKM_Local", 3, "step", &previous);
    Centroids splitCentroids = readSnapshotState(output, "SKM_Local", 4, "split", &split);
    require(previousCentroids.size == 4 && splitCentroids.size == 5 &&
                previousCentroids.dimensions == 1 && splitCentroids.dimensions == 1,
            "local cache snapshots have the wrong centroid shape");
    require(centroidRowConst(&previousCentroids, retained)[0] ==
                centroidRowConst(&initialCentroids, retained)[0],
            "untouched local cluster's centroid changed before selection");

    bool seen[5] = {false};
    for (size_t i = 0; i < 8; ++i)
    {
        require(previous.partitions[i] < 4 && previous.partitions[i] != retained &&
                    split.partitions[i] == previous.partitions[i],
                "local cached split changed an unrelated point");
        seen[previous.partitions[i]] = true;
    }
    size_t otherClusters = 0;
    for (size_t c = 0; c < 4; ++c)
    {
        if (seen[c]) ++otherClusters;
        if (c != retained)
            require(centroidRowConst(&splitCentroids, c)[0] == centroidRowConst(&previousCentroids, c)[0],
                    "local cached split changed an unrelated centroid");
    }
    require(otherClusters == 3, "local fixture did not split the other cluster twice");

    bool oldChild = false, newChild = false;
    for (size_t i = 8; i < 12; ++i)
    {
        require(previous.partitions[i] == retained && labels[i] < 2 &&
                    split.partitions[i] == (labels[i] == 0 ? retained : 4),
                "local split did not retain the untouched trial labels");
        oldChild |= labels[i] == 0;
        newChild |= labels[i] == 1;
    }
    require(oldChild && newChild &&
                centroidRowConst(&splitCentroids, retained)[0] == centroidRowConst(&trials[retained], 0)[0] &&
                centroidRowConst(&splitCentroids, 4)[0] == centroidRowConst(&trials[retained], 1)[0],
            "local split did not retain the untouched trial centroids");

    freeCentroids(&splitCentroids);
    freeCentroids(&previousCentroids);
    freeDataset(&split);
    freeDataset(&previous);
    freeCentroids(&trials[1]);
    freeCentroids(&trials[0]);
    freeClusterIndex(&index);
    freeCentroids(&actualCentroids);
    freeCentroids(&initialCentroids);
    freeDataset(&actual);
    freeDataset(&initial);
}

static void testSplitSeedFallback(void)
{
    const double values[] = {0, 0, 0, 10, 20};
    const uint64_t seeds[] = {34, 37};
    const double firstMeans[] = {0, 2.5};
    const double secondMeans[] = {15, 20};
    Dataset points = allocateDataset(5, 1);
    Centroids centroids = allocateCentroids(2, 1);
    for (size_t i = 0; i < points.size; ++i)
        datasetRow(&points, i)[0] = values[i];
    ClusterIndex index = allocateClusterIndex(points.size, centroids.capacity);
    for (size_t rank = 0; rank < 2; ++rank)
    {
        centroids.size = 1;
        centroidRow(&centroids, 0)[0] = 0;
        for (size_t i = 0; i < points.size; ++i) points.partitions[i] = 0;
        rebuildClusterIndex(&index, &points, centroids.size);
        Rng actual, expected;
        rng_seed(&actual, seeds[rank]);
        rng_seed(&expected, seeds[rank]);
        for (size_t i = 0; i <= points.size; ++i)
            require(rng_below(&expected, points.size) < 3, "split fallback fixture did not exhaust its redraws");
        Rng withoutFallback = expected;
        require(rng_below(&withoutFallback, points.size) < 3 && rng_below(&expected, 2) == rank,
                "split fallback fixture did not exercise the eligible-row selection");

        splitClusterIntraCluster(&points, &index, &centroids, 0, 1, NULL, &actual, unitSplitScratch(&points, &index));
        require(centroids.size == 2 && centroidRowConst(&centroids, 0)[0] == firstMeans[rank] &&
                    centroidRowConst(&centroids, 1)[0] == secondMeans[rank],
                "split seed fallback did not select the expected distinct row");
        for (size_t i = 0; i < 3; ++i)
            require(points.partitions[i] == 0, "split seed fallback produced incorrect labels");
        require(points.partitions[3] == (rank == 0 ? 1u : 0u) && points.partitions[4] == 1,
                "split seed fallback produced incorrect labels");
        require(rng_u32(&actual) == rng_u32(&expected), "split seed fallback consumed the wrong random draws");
    }

    freeClusterIndex(&index);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testRandomSplitRankFallback(void)
{
    const double values[] = {0, 200, 10, 100, 220, 12, 100, 250, 30, 31};
    const size_t labels[] = {0, 3, 1, 2, 3, 1, 2, 3, 1, 1};
    const size_t rejected[] = {2, 0, 0, 0};
    Dataset expected = allocateDataset(10, 1);
    Dataset actual = allocateDataset(10, 1);
    Centroids expectedCentroids = allocateCentroids(5, 1);
    Centroids actualCentroids = allocateCentroids(5, 1);
    expectedCentroids.size = 4;
    actualCentroids.size = 4;
    for (size_t i = 0; i < expected.size; ++i)
    {
        datasetRow(&expected, i)[0] = values[i];
        datasetRow(&actual, i)[0] = values[i];
        expected.partitions[i] = labels[i];
        actual.partitions[i] = labels[i];
    }
    centroidStep(&expectedCentroids, &expected, unitKMeansScratch(&expectedCentroids));
    copyCoordinates(actualCentroids.coordinates, expectedCentroids.coordinates, 4);
    ClusterIndex index = allocateClusterIndex(expected.size, expectedCentroids.capacity);
    require(rebuildClusterIndex(&index, &expected, expectedCentroids.size) == 2 &&
                !index.eligible[0] && index.eligible[1] && !index.eligible[2] && index.eligible[3],
            "random split fallback fixture has unexpected eligibility");

    Rng actualRng, expectedRng;
    rng_seed(&actualRng, 106);
    rng_seed(&expectedRng, 106);
    for (size_t i = 0; i < 4; ++i)
        require(rng_below(&expectedRng, 4) == rejected[i], "random split fallback fixture did not exhaust its draws");
    Rng withoutFallback = expectedRng;
    require(rng_below(&withoutFallback, 4) == 2 && rng_below(&expectedRng, 2) == 1,
            "random split fallback fixture did not select the second eligible cluster");

    splitClusterIntraCluster(&expected, &index, &expectedCentroids, 3, 1, NULL, &expectedRng,
                             unitSplitScratch(&expected, &index));
    double expectedStepSse = calculateSSE(&expected, &expectedCentroids);
    double expectedFinalSse =
        runKMeans(&expected, 1, &expectedCentroids, NULL, KM_STAGE_FINAL, unitKMeansScratch(&expectedCentroids));
    TrackOptions options = {0};
    options.rows = true;
    Tracker tracker = trackerCreate(ALGO_SKM_RANDOM, ".", NULL, &options);
    trackerBeginRun(&tracker, 0);
    double actualSse =
        runRandomSplit(&actual, &actualCentroids, 5, 1, &tracker, &actualRng, unitKMeansScratch(&actualCentroids));
    require(tracker.rowCount == 3 && tracker.rows[1].sse == expectedStepSse && actualSse == expectedFinalSse,
            "random split fallback did not split the selected eligible cluster");
    for (size_t c = 0; c < actualCentroids.size; ++c)
        require(centroidRowConst(&actualCentroids, c)[0] == centroidRowConst(&expectedCentroids, c)[0],
                "random split fallback produced incorrect centroids");
    for (size_t i = 0; i < actual.size; ++i)
        require(actual.partitions[i] == expected.partitions[i], "random split fallback produced incorrect labels");
    require(rng_u32(&actualRng) == rng_u32(&expectedRng),
            "random split fallback consumed the wrong random draws");

    trackerPause(&tracker);
    trackerFree(&tracker);
    freeClusterIndex(&index);
    freeCentroids(&actualCentroids);
    freeCentroids(&expectedCentroids);
    freeDataset(&actual);
    freeDataset(&expected);
}

static void testBisectingFirstTries(void)
{
    const double values[] = {0, 2, 3, 10};
    const size_t wanted[][2] = {{0, 2}, {0, 3}, {0, 2}};
    uint64_t seed;
    Rng rng;
    for (seed = 0; seed < 1000000; ++seed)
    {
        rng_seed(&rng, seed);
        bool matches = true;
        for (size_t t = 0; t < 3; ++t)
        {
            size_t first = rng_below(&rng, 4);
            size_t second = first;
            while (second == first) second = rng_below(&rng, 4);
            if (first != wanted[t][0] || second != wanted[t][1])
            {
                matches = false;
                break;
            }
        }
        if (matches) break;
    }
    require(seed < 1000000, "could not select bisecting trial seeds");
    Dataset points = allocateDataset(4, 1);
    Centroids centroids = allocateCentroids(2, 1);
    centroids.size = 1;
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = values[i];
        points.partitions[i] = 0;
    }
    TrackOptions options = {0};
    options.rows = true;
    options.inner = true;
    Tracker tracker = trackerCreate(ALGO_BISECTING, ".", NULL, &options);
    trackerBeginRun(&tracker, 0);
    rng_seed(&rng, seed);
    double sse = runBisectingKMeans(&points, &centroids, 2, 1, 3, BISECT_SELECT_SSE, &tracker, &rng,
                                    unitKMeansScratch(&centroids));
    require(tracker.rowCount == 3 && fabs(tracker.rows[1].sse - 14.0 / 3.0) < 1e-12 &&
                fabs(sse - 14.0 / 3.0) < 1e-12 && calculateSSE(&points, &centroids) == sse,
            "bisecting did not apply the best first-split try");
    require(tracker.innerCount == 4 && tracker.innerRows[0].stage == KM_STAGE_TRY &&
                tracker.innerRows[1].stage == KM_STAGE_TRY && tracker.innerRows[2].stage == KM_STAGE_TRY &&
                tracker.innerRows[0].sse == 38 && fabs(tracker.innerRows[1].sse - 14.0 / 3.0) < 1e-12 &&
                tracker.innerRows[2].sse == 38 && tracker.innerRows[3].stage == KM_STAGE_FINAL,
            "bisecting did not execute and track every first-split try");
    trackerPause(&tracker);
    trackerFree(&tracker);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static double bisectSecondSplitSse(const double* values, size_t count, size_t secondSeed, BisectSelect selection)
{
    Dataset points = allocateDataset(count, 1);
    Centroids centroids = allocateCentroids(3, 1);
    centroids.size = 1;
    for (size_t i = 0; i < count; ++i)
    {
        datasetRow(&points, i)[0] = values[i];
        points.partitions[i] = 0;
    }
    centroidStep(&centroids, &points, unitKMeansScratch(&centroids));
    TrackOptions options = {0};
    options.rows = true;
    Tracker tracker = trackerCreate(ALGO_BISECTING, ".", NULL, &options);
    trackerBeginRun(&tracker, 0);
    Rng rng;
    rng_seed(&rng, seedForPair(count, 0, secondSeed));
    (void)runBisectingKMeans(&points, &centroids, 3, 1, 1, selection, &tracker, &rng, unitKMeansScratch(&centroids));
    require(tracker.rowCount == 4, "bisecting selection fixture did not record both splits");
    double sse = tracker.rows[2].sse;
    trackerPause(&tracker);
    trackerFree(&tracker);
    freeCentroids(&centroids);
    freeDataset(&points);
    return sse;
}

static void testBisectSelection(void)
{
    const double different[] = {0, 0, 0, 0, 1, 1, 100, 110, 120};
    require(fabs(bisectSecondSplitSse(different, 9, 7, BISECT_SELECT_SSE) - (50.0 + 4.0 / 3.0)) < 1e-12 &&
                bisectSecondSplitSse(different, 9, 7, BISECT_SELECT_SIZE) == 200,
            "bisecting SSE and size modes did not choose different clusters");
    const double tied[] = {0, 0, 1, 1, 100, 100, 102, 102};
    require(bisectSecondSplitSse(tied, 8, 6, BISECT_SELECT_SIZE) == 4,
            "bisecting size tie did not choose the lowest eligible cluster");
    const double duplicateOnly[] = {0, 0, 0, 0, 0, 100, 110, 120};
    require(bisectSecondSplitSse(duplicateOnly, 8, 6, BISECT_SELECT_SIZE) == 50,
            "bisecting size mode selected a duplicate-only cluster");
}

static void testLocalFirstAndFinalSplit(void)
{
    const double values[] = {0, 2, 3, 10};
    Dataset points = allocateDataset(4, 1);
    Centroids centroids = allocateCentroids(2, 1);
    centroids.size = 1;
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = values[i];
        points.partitions[i] = 0;
    }
    TrackOptions options = {0};
    options.rows = true;
    Tracker tracker = trackerCreate(ALGO_SKM_LOCAL, ".", NULL, &options);
    trackerBeginRun(&tracker, 0);

    Rng rng;
    rng_seed(&rng, seedForPair(4, 0, 2));
    double sse = runSseSplit(&points, &centroids, 2, 1, ALGO_SKM_LOCAL, REPARTITION_INWARD, &tracker, &rng,
                             unitKMeansScratch(&centroids));
    require(tracker.rowCount == 3 && tracker.rows[1].sse == 26.5 && fabs(sse - 14.0 / 3.0) < 1e-12 &&
                fabs(calculateSSE(&points, &centroids) - sse) < 1e-12,
            "first/final local split was not repartitioned before the capped final k-means");

    trackerPause(&tracker);
    trackerFree(&tracker);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testTwoClusterSplitSkipsTrials(void)
{
    const double values[] = {0, 2, 3, 10, 20, 22, 23, 30};
    const struct { Algorithm algorithm; RepartitionMode mode; } cases[] = {
        {ALGO_SKM_INTRA, REPARTITION_BOTH},
        {ALGO_SKM_GLOBAL, REPARTITION_BOTH},
        {ALGO_SKM_GLOBAL_BASIC, REPARTITION_BOTH},
        {ALGO_SKM_LOCAL, REPARTITION_BOTH},
        {ALGO_SKM_LOCAL, REPARTITION_INWARD},
        {ALGO_SKM_LOCAL, REPARTITION_INWARD_NONCHILD},
    };
    const size_t limits[] = {1, SIZE_MAX};
    Centroids truth = allocateCentroids(2, 1);
    centroidRow(&truth, 0)[0] = 0;
    centroidRow(&truth, 1)[0] = 30;

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c)
    {
        for (size_t l = 0; l < sizeof(limits) / sizeof(limits[0]); ++l)
        {
            Dataset expected = allocateDataset(8, 1);
            Dataset actual = allocateDataset(8, 1);
            Centroids expectedCentroids = allocateCentroids(2, 1);
            Centroids actualCentroids = allocateCentroids(2, 1);
            expectedCentroids.size = 1;
            actualCentroids.size = 1;
            for (size_t i = 0; i < expected.size; ++i)
            {
                datasetRow(&expected, i)[0] = values[i];
                datasetRow(&actual, i)[0] = values[i];
                expected.partitions[i] = 0;
                actual.partitions[i] = 0;
            }
            centroidStep(&expectedCentroids, &expected, unitKMeansScratch(&expectedCentroids));
            centroidStep(&actualCentroids, &actual, unitKMeansScratch(&actualCentroids));
            ClusterIndex index = allocateClusterIndex(expected.size, expectedCentroids.capacity);
            rebuildClusterIndex(&index, &expected, expectedCentroids.size);
            Rng expectedRng, actualRng;
            rng_seed(&expectedRng, 1);
            rng_seed(&actualRng, 1);
            splitClusterIntraCluster(&expected, &index, &expectedCentroids, 0, limits[l], NULL, &expectedRng,
                                     unitSplitScratch(&expected, &index));
            if (cases[c].algorithm == ALGO_SKM_LOCAL)
            {
                bool affected[2] = {false, false};
                localRepartition(&expected, &index, &expectedCentroids, 0, cases[c].mode, affected);
            }
            double expectedSse = runKMeans(&expected, limits[l], &expectedCentroids, NULL, KM_STAGE_NONE,
                                           unitKMeansScratch(&expectedCentroids));
            size_t expectedCi = calculateCentroidIndex(&expectedCentroids, &truth);

            TrackOptions options = {0};
            options.rows = true;
            options.inner = true;
            Tracker tracker = trackerCreate(cases[c].algorithm, ".", &truth, &options);
            trackerBeginRun(&tracker, 0);
            double actualSse = runSseSplit(&actual, &actualCentroids, 2, limits[l], cases[c].algorithm,
                                           cases[c].mode, &tracker, &actualRng, unitKMeansScratch(&actualCentroids));
            require(actualSse == expectedSse &&
                        memcmp(actualCentroids.coordinates, expectedCentroids.coordinates, 2 * sizeof(double)) == 0 &&
                        memcmp(actual.partitions, expected.partitions, expected.size * sizeof(*expected.partitions)) == 0 &&
                        tracker.rowCount == 3 && tracker.rows[2].sse == expectedSse && tracker.rows[2].ci == expectedCi,
                    "K=2 did not preserve the split and final state");
            require(actualRng.state == expectedRng.state && actualRng.inc == expectedRng.inc,
                    "K=2 evaluated unused trials after the first split");

            trackerPause(&tracker);
            trackerFree(&tracker);
            freeClusterIndex(&index);
            freeCentroids(&actualCentroids);
            freeCentroids(&expectedCentroids);
            freeDataset(&actual);
            freeDataset(&expected);
        }
    }
    freeCentroids(&truth);
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
    splitClusterIntraCluster(&points, &index, &centroids, 0, 10, NULL, &rng, unitSplitScratch(&points, &index));
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
    require(tentativeSseDrop(&points, &index, 1, 10, 1.0, &rng, unitSplitScratch(&points, &index)) == 0.0 &&
                tentativeSseDrop(&points, &index, 3, 10, 1.0, &rng, unitSplitScratch(&points, &index)) == 0.0 &&
                rng_u32(&rng) == rng_u32(&untouched),
            "empty or singleton trial changed SSE or the random stream");
    rng_seed(&rng, 91);
    rng_seed(&untouched, 91);
    double first = tentativeSseDrop(&points, &index, 2, 10, 20.0, &rng, unitSplitScratch(&points, &index));
    double second = tentativeSseDrop(&points, &index, 2, 10, 20.0, &untouched, unitSplitScratch(&points, &index));
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

static void testIndexedClusterSSE(void)
{
    const double values[][2] = {{1e8, 0.0}, {1.25, -0.5}, {1.0, 0.0}, {-2.5, 0.25},
                                {1.0, 0.0}, {0.5, 2.0}, {1.0, 0.0}, {1.0, 0.0}};
    const size_t labels[] = {0, 1, 0, 1, 0, 1, 0, 0};
    Dataset points = allocateDataset(8, 2);
    Centroids centroids = allocateCentroids(4, 2);
    centroidRow(&centroids, 1)[0] = 0.2;
    centroidRow(&centroids, 1)[1] = 0.3;
    centroidRow(&centroids, 3)[0] = 0.3;
    centroids.size = 3;
    for (size_t i = 0; i < points.size; ++i)
    {
        copyCoordinates(datasetRow(&points, i), values[i], points.dimensions);
        points.partitions[i] = labels[i];
    }
    ClusterIndex index = allocateClusterIndex(points.size, centroids.capacity);
    rebuildClusterIndex(&index, &points, centroids.size);

    for (size_t c = 0; c < centroids.size; ++c)
    {
        double expected = calculateClusterSSE(&points, &centroids, c);
        double actual = indexedClusterSSE(&points, &index, &centroids, c);
        require(memcmp(&actual, &expected, sizeof(actual)) == 0,
                "indexed cluster SSE differs from the dataset-order scan");
    }
    double reverse = 0.0;
    for (size_t i = points.size; i-- > 0;)
    {
        if (points.partitions[i] == 0)
            reverse += calculateSquaredEuclideanDistance(datasetRowConst(&points, i), centroidRowConst(&centroids, 0),
                                                          points.dimensions);
    }
    require(reverse != indexedClusterSSE(&points, &index, &centroids, 0),
            "cluster SSE fixture does not distinguish summation order");

    centroids.size = 4;
    double first = -1.0, second = -1.0;
    splitClusterSSEs(&points, &index, &centroids, 0, &first, &second);
    require(first == calculateClusterSSE(&points, &centroids, 0) && second == 0.0,
            "split SSE did not retain an empty child");
    points.partitions[2] = 3;
    points.partitions[6] = 3;
    splitClusterSSEs(&points, &index, &centroids, 0, &first, &second);
    double expectedFirst = calculateClusterSSE(&points, &centroids, 0);
    double expectedSecond = calculateClusterSSE(&points, &centroids, 3);
    require(memcmp(&first, &expectedFirst, sizeof(first)) == 0 &&
                memcmp(&second, &expectedSecond, sizeof(second)) == 0,
            "split child SSEs differ from separate dataset-order scans");
    rebuildClusterIndex(&index, &points, centroids.size);
    require(indexedClusterSSE(&points, &index, &centroids, 0) == expectedFirst &&
                indexedClusterSSE(&points, &index, &centroids, 3) == expectedSecond,
            "indexed cluster SSE did not use rebuilt membership");

    freeClusterIndex(&index);
    freeCentroids(&centroids);
    freeDataset(&points);
}

static void testDistinctSplitEligibility(void)
{
    const double values[] = {0, -0.0, 7, 7, 2, 3};
    const size_t labels[] = {0, 0, 1, 1, 2, 2};
    Dataset points = allocateDataset(6, 1);
    for (size_t i = 0; i < points.size; ++i)
    {
        datasetRow(&points, i)[0] = values[i];
        points.partitions[i] = labels[i];
    }
    ClusterIndex index = allocateClusterIndex(points.size, 3);
    require(rebuildClusterIndex(&index, &points, 3) == 1 && !index.eligible[0] && !index.eligible[1] &&
                index.eligible[2],
            "duplicate-only clusters were considered splittable");
    Rng actual, untouched;
    rng_seed(&actual, 42);
    rng_seed(&untouched, 42);
    Centroids best = allocateCentroids(2, 1);
    size_t savedLabels[6] = {0};
    InnerTrace trace = {0};
    trace.count = 1;
    require(tentativeSseDrop(&points, &index, 0, 10, 0, &actual, unitSplitScratch(&points, &index)) == 0 &&
                cacheTentativeSplit(&points, &index, 1, 10, 0, &best, savedLabels, NULL, &trace, &actual,
                                    unitSplitScratch(&points, &index)) == 0 &&
                trace.count == 0 && rng_u32(&actual) == rng_u32(&untouched),
            "duplicate-only trial changed the random stream or kept a stale trace");
    points.partitions[5] = 0;
    require(rebuildClusterIndex(&index, &points, 3) == 1 && index.eligible[0] && !index.eligible[1] &&
                !index.eligible[2],
            "rebuilding membership did not refresh distinct-coordinate eligibility");
    freeCentroids(&best);
    freeClusterIndex(&index);
    freeDataset(&points);

    for (size_t basic = 0; basic < 2; ++basic)
    {
        points = allocateDataset(4, 1);
        datasetRow(&points, 3)[0] = 10;
        resetPartitions(&points);
        Centroids centroids = allocateCentroids(2, 1);
        centroids.size = 1;
        index = allocateClusterIndex(points.size, 2);
        rebuildClusterIndex(&index, &points, 1);
        rng_seed(&actual, seedForPair(4, 0, 1));
        if (basic)
            (void)splitClusterGlobal(&points, &index, &centroids, 0, 1, NULL, &actual, unitKMeansScratch(&centroids));
        else
            splitClusterIntraCluster(&points, &index, &centroids, 0, 1, NULL, &actual,
                                     unitSplitScratch(&points, &index));
        require(centroids.size == 2 && centroidRow(&centroids, 0)[0] == 0 &&
                    centroidRow(&centroids, 1)[0] == 10,
                "split selected equal-coordinate seeds");
        freeClusterIndex(&index);
        freeCentroids(&centroids);
        freeDataset(&points);
    }
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

static void testInvalidInternalInputs(const char* executable, const char* directory)
{
    char errorPath[PATH_MAX];
    char command[2 * PATH_MAX + 64];
    int length = snprintf(errorPath, sizeof(errorPath), "%s%cinvalid-allocation.txt", directory, PATHSEP);
    require(length >= 0 && (size_t)length < sizeof(errorPath), "invalid allocation fixture path too long");
    const char* modes[] = {"--overflow", "--index-overflow", "--zero-dataset", "--zero-centroid-dimensions",
                           "--zero-kmeans-core", "--zero-kmeans-view", "--zero-kmeans-tracked",
                           "--zero-sse-split", "--zero-bisect-iterations", "--zero-bisect-tries",
                           "--nonfinite-rs-initial", "--nonfinite-bisect-final", "--nonfinite-kmeans-tracked",
                           "--nonfinite-random-final", "--nonfinite-sse-final", "--nonfinite-basic-original",
                           "--nonfinite-basic-trial", "--unsplittable-split",
                           "--too-few-distinct-init"};
    const char* messages[] = {"exceeds addressable memory", "Invalid cluster index size",
                              "Invalid dataset dimensions", "Invalid dataset dimensions",
                              "K-means requires at least one iteration",
                              "K-means requires at least one iteration",
                              "K-means requires at least one iteration",
                              "SSE split requires at least one k-means iteration",
                              "Bisecting k-means requires positive iterations and tries",
                              "Bisecting k-means requires positive iterations and tries",
                              "Random Swap initial SSE is not finite",
                              "Bisecting k-means produced no finite final SSE",
                              "K-means produced a non-finite SSE",
                              "SKM_Random produced a non-finite k-means SSE",
                              "SKM_Intra produced a non-finite k-means SSE",
                              "Cannot evaluate split of cluster 0: non-finite SSE",
                              "Cannot evaluate split of cluster 0: no finite trial SSE",
                              "found no cluster with two distinct data points",
                              "exceeds the number of distinct data points"};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
    {
        length = snprintf(command, sizeof(command), "cd . && \"%s\" %s 2> \"%s\"", executable, modes[i], errorPath);
        require(length >= 0 && (size_t)length < sizeof(command), "invalid allocation command too long");
        int status = system(command);
        require(status != -1 && status != 0, "invalid internal input did not fail");
        FILE* errors = fopen(errorPath, "r");
        require(errors != NULL, "could not read allocation error");
        char line[256];
        require(fgets(line, sizeof(line), errors) != NULL && strstr(line, messages[i]) != NULL,
                "invalid internal input did not report its error");
        closeFixture(errors);
    }
    require(remove(errorPath) == 0, "could not remove allocation fixture");
}

static void testZeroIterationRunners(const char* executable, const char* directory)
{
    const struct
    {
        const char* mode;
        const char* message;
    } cases[] = {
        {"--zero-kmeans", "Error: K-means requires at least one iteration"},
        {"--zero-rkm", "Error: Repeated k-means requires at least one iteration"},
        {"--zero-random-k1", "Error: Random Split requires at least one k-means iteration"},
        {"--zero-random-k2", "Error: Random Split requires at least one k-means iteration"},
    };
    char errorPath[PATH_MAX];
    char command[3 * PATH_MAX + 96];
    int length = snprintf(errorPath, sizeof(errorPath), "%s%czero-iterations.txt", directory, PATHSEP);
    require(length >= 0 && (size_t)length < sizeof(errorPath), "zero-iteration error path too long");
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        length = snprintf(command, sizeof(command), "cd . && \"%s\" %s \"%s\" 2> \"%s\"",
                          executable, cases[i].mode, directory, errorPath);
        require(length >= 0 && (size_t)length < sizeof(command), "zero-iteration command too long");
        int status = system(command);
        require(status != -1 && status != 0, "zero iteration limit did not fail");
        FILE* errors = fopen(errorPath, "r");
        require(errors != NULL, "could not read zero-iteration error");
        char line[256];
        require(fgets(line, sizeof(line), errors) != NULL && strstr(line, cases[i].message) != NULL,
                "zero iteration limit did not report its error");
        closeFixture(errors);
    }
    require(remove(errorPath) == 0, "could not remove zero-iteration fixture");
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

static void testReportSummary(const char* directory, const char* executable, const char* capture)
{
    char input[PATH_MAX], output[PATH_MAX], folder[PATH_MAX], path[PATH_MAX], arguments[3 * PATH_MAX + 64];
    joinPath(input, sizeof(input), directory, "report-stat-input");
    joinPath(output, sizeof(output), directory, "report-stat-output");
    require(platform_make_directories(input), "could not create report-stat fixture");
    joinPath(path, sizeof(path), input, "run_config.txt");
    FILE* file = writeFixture(path);
    require(fputs("fixture: report statistics\ncalls:\n", file) >= 0, "could not write report-stat configuration");
    closeFixture(file);

    const char* names[] = {"identical", "nearby", "varying", "single"};
    for (size_t d = 0; d < sizeof(names) / sizeof(names[0]); ++d)
    {
        joinPath(folder, sizeof(folder), input, names[d]);
        require(platform_make_directories(folder), "could not create report-stat dataset");
        joinPath(path, sizeof(path), folder, "dataset.txt");
        file = writeFixture(path);
        require(fputs("k: 3\n", file) >= 0, "could not write report-stat dataset metadata");
        closeFixture(file);
        joinPath(path, sizeof(path), folder, "KM_runs.csv");
        file = writeFixture(path);
        require(fputs("run;seed;sse;ci;ms\n", file) >= 0, "could not write report-stat header");
        if (d == 0)
        {
            for (size_t i = 1; i <= 100; ++i)
                require(fprintf(file, "%zu;0;456724496349.7511;0;12345678901234.123\n", i) > 0,
                        "could not write identical report values");
        }
        else if (d == 1)
            require(fputs("1;0;456724496349.7511;0;3\n2;0;456724496349.75116;1;5\n", file) >= 0,
                    "could not write nearby report values");
        else if (d == 2)
            require(fputs("1;0;100;0;200\n2;0;101;1;202\n3;0;102;1;204\n", file) >= 0,
                    "could not write varying report values");
        else
            require(fputs("1;0;100;0;200\n", file) >= 0, "could not write single report value");
        closeFixture(file);
    }

    joinPath(folder, sizeof(folder), input, "identical");
    joinPath(path, sizeof(path), folder, "RS_runs.csv");
    file = writeFixture(path);
    require(fputs("run;seed;sse;ci;ms\n", file) >= 0, "could not write RS report header");
    for (size_t i = 1; i <= 100; ++i)
        require(fprintf(file, "%zu;0;456724496349.7511;0;12345678901234.123\n", i) > 0,
                "could not write identical RS values");
    closeFixture(file);
    joinPath(path, sizeof(path), folder, "RS_log.csv");
    file = writeFixture(path);
    require(fputs("run;count;sse;ci;ms\n", file) >= 0, "could not write RS log header");
    for (size_t i = 1; i <= 100; ++i)
        require(fprintf(file, "%zu;0;456724496349.7511;0;12345678901234.123\n", i) > 0,
                "could not write identical RS log values");
    closeFixture(file);

    int length = snprintf(arguments, sizeof(arguments), "report -o \"%s\" \"%s\"", output, input);
    require(length >= 0 && (size_t)length < sizeof(arguments), "report-stat command too long");
    expectCli(executable, arguments, capture, true);
    joinPath(path, sizeof(path), output, "summary.csv");
    expectText(path, "identical;KM;100;456724496349.751099;0.000000;0.0000;0.0000;100.0;"
                     "12345678901234.123;0.000;");
    expectText(path, "identical;RS;100;456724496349.751099;0.000000;0.0000;0.0000;100.0;"
                     "12345678901234.123;0.000;12345678901234.123");
    expectText(path, "nearby;KM;2;456724496349.751099;0.000043;0.5000;0.1667;50.0;4.000;1.414;");
    expectText(path, "varying;KM;3;101.000000;1.000000;0.6667;0.2222;33.3;202.000;2.000;");
    expectText(path, "single;KM;1;100.000000;NA;0.0000;0.0000;100.0;200.000;NA;");
}

static void testNonfiniteRandomSwap(const char* executable, const char* directory)
{
    char capture[PATH_MAX];
    joinPath(capture, sizeof(capture), directory, "nonfinite-swap.txt");
    expectCli(executable, "--nonfinite-rs-trial", capture, true);
    expectText(capture, "Warning: Random Swap rejected swap 1 with non-finite SSE");
    require(remove(capture) == 0, "could not remove Random Swap test capture");
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

static void requireSameCaseRun(const char* firstOutput, const char* secondOutput, const char* algorithm,
                               const char* message)
{
    FILE* firstRuns = openCaseFile(firstOutput, algorithm, "_runs.csv");
    FILE* secondRuns = openCaseFile(secondOutput, algorithm, "_runs.csv");
    char first[512], second[512];
    require(fgets(first, sizeof(first), firstRuns) != NULL && fgets(second, sizeof(second), secondRuns) != NULL &&
                fgets(first, sizeof(first), firstRuns) != NULL && fgets(second, sizeof(second), secondRuns) != NULL,
            "missing comparison results");
    char* firstTime = strrchr(first, ';');
    char* secondTime = strrchr(second, ';');
    require(firstTime != NULL && secondTime != NULL, "invalid comparison results");
    *firstTime = '\0';
    *secondTime = '\0';
    require(strcmp(first, second) == 0, message);
    closeFixture(firstRuns);
    closeFixture(secondRuns);
}

static void testRepartitionOptions(const char* directory, const char* executable, const char* data, const char* capture)
{
    char defaultOutput[PATH_MAX];
    char explicitOutput[PATH_MAX];
    char bothOutput[PATH_MAX];
    char invalidOutput[PATH_MAX];
    char reportOutput[PATH_MAX];
    char path[PATH_MAX];
    char arguments[3 * PATH_MAX + 512];
    joinPath(defaultOutput, sizeof(defaultOutput), directory, "local-default");
    joinPath(explicitOutput, sizeof(explicitOutput), directory, "local-inward");
    joinPath(bothOutput, sizeof(bothOutput), directory, "local-both");
    joinPath(invalidOutput, sizeof(invalidOutput), directory, "local-invalid");
    joinPath(reportOutput, sizeof(reportOutput), directory, "local-report");

    expectCli(executable, "--help", capture, true);
    expectText(capture, "SKM-Local: both (default)");

    const char* invalid[] = {"--repartition", "--repartition unknown",
                             "--repartition both --repartition inward"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        int length = snprintf(arguments, sizeof(arguments), "-o \"%s\" -k 4 \"%s\" %s",
                              invalidOutput, data, invalid[i]);
        require(length >= 0 && (size_t)length < sizeof(arguments), "invalid repartition case too long");
        expectCli(executable, arguments, capture, false);
        expectText(capture, "Error: --repartition");
        require(!fileExists(invalidOutput), "invalid repartition mode created an output folder");
    }

    runCapCase(executable, data, defaultOutput, capture, "skm-local", "--max-iterations 1", 4);
    runCapCase(executable, data, explicitOutput, capture, "skm-local",
               "--max-iterations 1 --repartition inward", 4);
    runCapCase(executable, data, bothOutput, capture, "skm-local",
               "--max-iterations 1 --repartition both", 4);
    requireSameCaseRun(defaultOutput, bothOutput, "SKM_Local",
                       "default repartition differs from explicit both");
    joinPath(path, sizeof(path), defaultOutput, "run_config.txt");
    expectText(path, "repartition: both\n");
    joinPath(path, sizeof(path), explicitOutput, "run_config.txt");
    expectText(path, "repartition: inward\n");
    joinPath(path, sizeof(path), bothOutput, "run_config.txt");
    expectText(path, "repartition: both\n");

    int length = snprintf(arguments, sizeof(arguments),
                          "--seed 42 -r 1 --max-swaps 1 --repeats 1 --bisect-tries 2 --max-iterations 1 "
                          "--algo skm-local --repartition inward -o \"%s\" -k 4 \"%s\"",
                          defaultOutput, data);
    require(length >= 0 && (size_t)length < sizeof(arguments), "mismatched repartition case too long");
    expectCli(executable, arguments, capture, false);
    expectText(capture, "another configuration");
    expectText(capture, "repartition: inward");
    expectText(capture, "repartition: both");

    length = snprintf(arguments, sizeof(arguments), "report -o \"%s\" \"%s\" \"%s\"",
                      reportOutput, defaultOutput, explicitOutput);
    require(length >= 0 && (size_t)length < sizeof(arguments), "repartition report case too long");
    expectCli(executable, arguments, capture, false);
    expectText(capture, "different configurations");
    expectText(capture, "repartition: inward");
    expectText(capture, "repartition: both");
    require(!fileExists(reportOutput), "mismatched repartition report created an output folder");
}

static void testBisectSelectOptions(const char* directory, const char* executable, const char* data,
                                    const char* capture)
{
    char defaultOutput[PATH_MAX], explicitOutput[PATH_MAX], sizeOutput[PATH_MAX], partOutput[PATH_MAX];
    char invalidOutput[PATH_MAX], reportOutput[PATH_MAX], mergedOutput[PATH_MAX], path[PATH_MAX];
    char arguments[4 * PATH_MAX + 512];
    joinPath(defaultOutput, sizeof(defaultOutput), directory, "bisect-default");
    joinPath(explicitOutput, sizeof(explicitOutput), directory, "bisect-sse");
    joinPath(sizeOutput, sizeof(sizeOutput), directory, "bisect-size");
    joinPath(partOutput, sizeof(partOutput), directory, "bisect-part");
    joinPath(invalidOutput, sizeof(invalidOutput), directory, "bisect-invalid");
    joinPath(reportOutput, sizeof(reportOutput), directory, "bisect-report");
    joinPath(mergedOutput, sizeof(mergedOutput), directory, "bisect-merged");
    const char* invalid[] = {"--bisect-select", "--bisect-select unknown", "--bisect-select sse --bisect-select size"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        int length = snprintf(arguments, sizeof(arguments), "-o \"%s\" -k 4 \"%s\" %s", invalidOutput, data, invalid[i]);
        require(length >= 0 && (size_t)length < sizeof(arguments), "invalid bisect selection case too long");
        expectCli(executable, arguments, capture, false);
        expectText(capture, "Error: --bisect-select");
        require(!fileExists(invalidOutput), "invalid bisect selection created an output folder");
    }

    runCapCase(executable, data, defaultOutput, capture, "bisecting", "--max-iterations 1", 4);
    runCapCase(executable, data, explicitOutput, capture, "bisecting",
               "--max-iterations 1 --bisect-select sse", 4);
    runCapCase(executable, data, sizeOutput, capture, "bisecting",
               "--max-iterations 1 --bisect-select size", 4);
    requireSameCaseRun(defaultOutput, explicitOutput, "Bisecting_KM",
                       "default bisect selection differs from explicit SSE");
    joinPath(path, sizeof(path), defaultOutput, "run_config.txt");
    expectText(path, "bisect-select: sse\n");
    joinPath(path, sizeof(path), sizeOutput, "run_config.txt");
    expectText(path, "bisect-select: size\n");

    int length = snprintf(arguments, sizeof(arguments),
                          "--seed 42 -r 1 --max-swaps 1 --repeats 1 --bisect-tries 2 --max-iterations 1 "
                          "--algo bisecting --bisect-select size -o \"%s\" -k 4 \"%s\"",
                          defaultOutput, data);
    require(length >= 0 && (size_t)length < sizeof(arguments), "mismatched bisect selection case too long");
    expectCli(executable, arguments, capture, false);
    expectText(capture, "another configuration");
    expectText(capture, "bisect-select: sse");
    expectText(capture, "bisect-select: size");

    length = snprintf(arguments, sizeof(arguments), "report -o \"%s\" \"%s\" \"%s\"",
                      reportOutput, defaultOutput, sizeOutput);
    require(length >= 0 && (size_t)length < sizeof(arguments), "bisect selection report case too long");
    expectCli(executable, arguments, capture, false);
    expectText(capture, "different configurations");
    expectText(capture, "bisect-select: sse");
    expectText(capture, "bisect-select: size");
    require(!fileExists(reportOutput), "mismatched bisect report created an output folder");

    length = snprintf(arguments, sizeof(arguments),
                      "--seed 42 -r 2 --first-run 2 --max-swaps 1 --repeats 1 --bisect-tries 2 "
                      "--max-iterations 1 --track-progress --algo bisecting --bisect-select sse "
                      "-o \"%s\" -k 4 \"%s\"",
                      partOutput, data);
    require(length >= 0 && (size_t)length < sizeof(arguments), "bisect selection split-job case too long");
    expectCli(executable, arguments, capture, true);
    length = snprintf(arguments, sizeof(arguments), "report -o \"%s\" \"%s\" \"%s\"",
                      mergedOutput, defaultOutput, partOutput);
    require(length >= 0 && (size_t)length < sizeof(arguments), "bisect selection merged report too long");
    expectCli(executable, arguments, capture, true);
    joinPath(path, sizeof(path), mergedOutput, "summary.csv");
    expectText(path, "s01_blobs2d;Bisecting KM;2;");
}

static void testLocalSplitSnapshots(const char* directory, const char* executable, const char* data, const char* capture)
{
    char output[PATH_MAX];
    char arguments[3 * PATH_MAX + 512];
    char splitPath[PATH_MAX];
    char stepPath[PATH_MAX];
    char centroidPath[PATH_MAX];
    joinPath(output, sizeof(output), directory, "local-snapshots");
    int length = snprintf(arguments, sizeof(arguments),
                          "--algo skm-local --seed 42 -r 1 --max-iterations 1 --log-level 3 --track-progress "
                          "--snapshots 1:all:split -o \"%s\" -k 4 \"%s\"",
                          output, data);
    require(length >= 0 && (size_t)length < sizeof(arguments), "local snapshot case too long");
    expectCli(executable, arguments, capture, true);

    FILE* trace = fopen(capture, "r");
    require(trace != NULL, "could not open local repartition trace");
    char line[1024];
    size_t splits = 0;
    size_t finalMoves = 0;
    while (fgets(line, sizeof(line), trace) != NULL)
    {
        const char* event = strstr(line, "(SKM-Local) repartition:");
        if (event != NULL)
        {
            size_t touched = 0, outward = 0, inward = 0;
            require(sscanf(event, "(SKM-Local) repartition: touched=%zu pass1=%zu pass2=%zu",
                           &touched, &outward, &inward) == 3 && touched >= 2,
                    "invalid local repartition counters");
            ++splits;
            finalMoves = outward + inward;
        }
    }
    require(!ferror(trace) && splits == 3 && finalMoves > 0,
            "local repartition did not run on the first and final splits");
    closeFixture(trace);

    caseFile(splitPath, sizeof(splitPath), output, "SKM_Local_run001_iter0003_split_partitions.txt");
    caseFile(stepPath, sizeof(stepPath), output, "SKM_Local_run001_iter0003_step_partitions.txt");
    caseFile(centroidPath, sizeof(centroidPath), output, "SKM_Local_run001_iter0003_step_centroids.txt");
    FILE* before = fopen(splitPath, "r");
    FILE* after = fopen(stepPath, "r");
    require(before != NULL && after != NULL, "missing final split or repartition snapshot");
    Dataset points = readDataset(data);
    Centroids recorded = readCentroids(centroidPath);
    Centroids means = {0};
    deepCopyCentroids(&recorded, &means);
    bool moved = false;
    for (size_t i = 0; i < points.size; ++i)
    {
        size_t oldLabel = 0, newLabel = 0;
        require(fscanf(before, "%zu", &oldLabel) == 1 && fscanf(after, "%zu", &newLabel) == 1 &&
                    oldLabel < recorded.size && newLabel < recorded.size,
                "invalid final split snapshot partition");
        moved |= oldLabel != newLabel;
        points.partitions[i] = newLabel;
    }
    size_t extra = 0;
    require(fscanf(before, "%zu", &extra) == EOF && fscanf(after, "%zu", &extra) == EOF &&
                !ferror(before) && !ferror(after) && moved,
            "final repartition snapshot did not record moved points");
    closeFixture(before);
    closeFixture(after);
    centroidStep(&means, &points, unitKMeansScratch(&means));
    for (size_t i = 0; i < means.size * means.dimensions; ++i)
        require(fabs(recorded.coordinates[i] - means.coordinates[i]) <= 1e-6,
                "final repartition snapshot has stale centroids");
    freeCentroids(&means);
    freeCentroids(&recorded);
    freeDataset(&points);
}

static void testBisectingSnapshots(const char* directory, const char* executable, const char* data, const char* capture)
{
    char dataDirectory[PATH_MAX];
    char overlap[PATH_MAX];
    char output[PATH_MAX];
    char datasetOutput[PATH_MAX];
    char arguments[3 * PATH_MAX + 512];
    char name[96];
    char path[PATH_MAX];
    const char* separator = strrchr(data, '/');
    const char* backslash = strrchr(data, '\\');
    if (backslash != NULL && (separator == NULL || backslash > separator)) separator = backslash;
    require(separator != NULL && (size_t)(separator - data) < sizeof(dataDirectory), "invalid dataset fixture path");
    size_t length = (size_t)(separator - data);
    memcpy(dataDirectory, data, length);
    dataDirectory[length] = '\0';
    joinPath(overlap, sizeof(overlap), dataDirectory, "s03_overlap.txt");
    joinPath(output, sizeof(output), directory, "bisect-snapshots");
    joinPath(datasetOutput, sizeof(datasetOutput), output, "s03_overlap");
    int written = snprintf(arguments, sizeof(arguments),
                           "--algo bisecting --seed 42 -r 1 --bisect-tries 3 --max-iterations 1 "
                           "--track-progress --track-inner --snapshots 1:all:split -o \"%s\" -k 4 \"%s\"",
                           output, overlap);
    require(written >= 0 && (size_t)written < sizeof(arguments), "bisecting snapshot case too long");
    expectCli(executable, arguments, capture, true);

    size_t selected[4] = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
    joinPath(path, sizeof(path), datasetOutput, "Bisecting_KM_iteration_stats.txt");
    FILE* stats = fopen(path, "r");
    require(stats != NULL, "missing bisecting snapshot statistics");
    char line[512];
    require(fgets(line, sizeof(line), stats) != NULL, "missing bisecting snapshot header");
    while (fgets(line, sizeof(line), stats) != NULL)
    {
        size_t run = 0, count = 0, k = 0, cluster = 0;
        char phase[16];
        require(sscanf(line, "%zu;%zu;%15[^;];%zu;%*[^;];%*[^;];%zu;", &run, &count, phase, &k, &cluster) == 5,
                "invalid bisecting snapshot statistics");
        if (strcmp(phase, "split") == 0)
        {
            require(run == 1 && count >= 1 && count <= 3 && k == count + 1 && cluster < count,
                    "invalid bisecting split snapshot");
            selected[count] = cluster;
        }
    }
    require(!ferror(stats) && selected[1] != SIZE_MAX && selected[2] != SIZE_MAX && selected[3] != SIZE_MAX,
            "bisecting is missing a split snapshot");
    closeFixture(stats);

    Dataset points = readDataset(overlap);
    size_t potentialOutsiderMoves = 0;
    for (size_t count = 1; count <= 3; ++count)
    {
        if (count == 1)
            written = snprintf(name, sizeof(name), "Bisecting_KM_run001_iter0000_start_partitions.txt");
        else
            written = snprintf(name, sizeof(name), "Bisecting_KM_run001_iter%04zu_step_partitions.txt", count - 1);
        require(written >= 0 && (size_t)written < sizeof(name), "bisecting snapshot name too long");
        joinPath(path, sizeof(path), datasetOutput, name);
        FILE* before = fopen(path, "r");
        written = snprintf(name, sizeof(name), "Bisecting_KM_run001_iter%04zu_split_partitions.txt", count);
        require(written >= 0 && (size_t)written < sizeof(name), "bisecting snapshot name too long");
        joinPath(path, sizeof(path), datasetOutput, name);
        FILE* split = fopen(path, "r");
        written = snprintf(name, sizeof(name), "Bisecting_KM_run001_iter%04zu_step_partitions.txt", count);
        require(written >= 0 && (size_t)written < sizeof(name), "bisecting snapshot name too long");
        joinPath(path, sizeof(path), datasetOutput, name);
        FILE* step = fopen(path, "r");
        written = snprintf(name, sizeof(name), "Bisecting_KM_run001_iter%04zu_split_centroids.txt", count);
        require(written >= 0 && (size_t)written < sizeof(name), "bisecting snapshot name too long");
        joinPath(path, sizeof(path), datasetOutput, name);
        Centroids centroids = readCentroids(path);
        require(before != NULL && split != NULL && step != NULL, "missing bisecting partition snapshot");
        for (size_t i = 0; i < points.size; ++i)
        {
            size_t oldLabel = 0, splitLabel = 0, stepLabel = 0;
            require(fscanf(before, "%zu", &oldLabel) == 1 && fscanf(split, "%zu", &splitLabel) == 1 &&
                        fscanf(step, "%zu", &stepLabel) == 1 && splitLabel == stepLabel,
                    "bisecting split and step snapshots have different partitions");
            if (oldLabel != selected[count])
            {
                require(splitLabel == oldLabel, "bisecting changed a point outside the split cluster");
                if (findNearestCentroid(datasetRowConst(&points, i), &centroids) != oldLabel)
                    ++potentialOutsiderMoves;
            }
            else
                require(splitLabel == oldLabel || splitLabel == count, "bisecting wrote an invalid child label");
        }
        size_t extra = 0;
        require(fscanf(before, "%zu", &extra) == EOF && fscanf(split, "%zu", &extra) == EOF &&
                    fscanf(step, "%zu", &extra) == EOF && !ferror(before) && !ferror(split) && !ferror(step),
                "invalid bisecting snapshot length");
        closeFixture(before);
        closeFixture(split);
        closeFixture(step);
        freeCentroids(&centroids);
    }
    require(potentialOutsiderMoves > 0, "bisecting fixture did not distinguish a global partition step");
    freeDataset(&points);
}

static void testRandomSwapInitialReport(const char* directory, const char* executable, const char* capture)
{
    char input[PATH_MAX];
    char output[PATH_MAX];
    char report[PATH_MAX];
    char path[PATH_MAX];
    char arguments[3 * PATH_MAX + 512];
    joinPath(input, sizeof(input), directory, "rs_constant.txt");
    joinPath(output, sizeof(output), directory, "rs-initial");
    joinPath(report, sizeof(report), directory, "rs-initial-report");
    FILE* fixture = writeFixture(input);
    require(fputs("0\n0\n0\n", fixture) >= 0, "could not write Random Swap fixture");
    closeFixture(fixture);
    int length = snprintf(arguments, sizeof(arguments),
                          "--algo rs --seed 42 -r 1 --max-swaps 17 --track-progress --snapshots none "
                          "-o \"%s\" -k 1 \"%s\"",
                          output, input);
    require(length >= 0 && (size_t)length < sizeof(arguments), "Random Swap report case too long");
    expectCli(executable, arguments, capture, true);
    joinPath(path, sizeof(path), output, "rs_constant");
    char logPath[PATH_MAX];
    joinPath(logPath, sizeof(logPath), path, "RS_log.csv");
    FILE* log = fopen(logPath, "r");
    require(log != NULL, "missing Random Swap initial log");
    char line[512];
    size_t run = 0, count = 0;
    double sse = -1;
    require(fgets(line, sizeof(line), log) != NULL && fgets(line, sizeof(line), log) != NULL &&
                sscanf(line, "%zu;%zu;%lf;", &run, &count, &sse) == 3 && run == 1 && count == 0 && sse == 0 &&
                fgets(line, sizeof(line), log) == NULL && !ferror(log),
            "all-rejected Random Swap did not retain only the initial row");
    closeFixture(log);
    length = snprintf(arguments, sizeof(arguments), "report -o \"%s\" \"%s\"", report, output);
    require(length >= 0 && (size_t)length < sizeof(arguments), "Random Swap report command too long");
    expectCli(executable, arguments, capture, true);
    joinPath(path, sizeof(path), report, "summary.csv");
    expectText(path, "rs_constant;RS;1;0.000000;NA;NA;NA;NA;");
    require(remove(input) == 0, "could not remove Random Swap fixture");
}

static void testSplitMeanStart(const char* directory, const char* executable, const char* capture)
{
    const char* algorithms[] = {"SKM_Random", "SKM_Intra", "SKM_Global", "SKM_Global_Basic", "SKM_Local",
                                "Bisecting_KM"};
    char input[PATH_MAX], output[PATH_MAX], datasetOutput[PATH_MAX], path[PATH_MAX], name[128];
    char arguments[3 * PATH_MAX + 512];
    joinPath(input, sizeof(input), directory, "mean-start.txt");
    joinPath(output, sizeof(output), directory, "mean-start-output");
    joinPath(datasetOutput, sizeof(datasetOutput), output, "mean-start");
    FILE* fixture = writeFixture(input);
    require(fputs("0\n0\n9\n", fixture) >= 0, "could not write split-mean fixture");
    closeFixture(fixture);
    int length = snprintf(arguments, sizeof(arguments),
                          "--algo skm-random,skm-intra,skm-global,skm-global-basic,skm-local,bisecting "
                          "--seed 42 -r 2 --max-iterations 1 --track-progress --snapshots all:0 "
                          "-o \"%s\" -k 1 \"%s\"",
                          output, input);
    require(length >= 0 && (size_t)length < sizeof(arguments), "split-mean case too long");
    expectCli(executable, arguments, capture, true);

    for (size_t a = 0; a < sizeof(algorithms) / sizeof(algorithms[0]); ++a)
    {
        for (size_t run = 1; run <= 2; ++run)
        {
            length = snprintf(name, sizeof(name), "%s_run%03zu_iter0000_start_centroids.txt", algorithms[a], run);
            require(length >= 0 && (size_t)length < sizeof(name), "split-mean snapshot name too long");
            joinPath(path, sizeof(path), datasetOutput, name);
            Centroids start = readCentroids(path);
            require(start.size == 1 && start.dimensions == 1 && centroidRow(&start, 0)[0] == 3,
                    "split runner did not start at the dataset mean");
            freeCentroids(&start);
        }
        length = snprintf(name, sizeof(name), "%s_log.csv", algorithms[a]);
        require(length >= 0 && (size_t)length < sizeof(name), "split-mean log name too long");
        joinPath(path, sizeof(path), datasetOutput, name);
        FILE* log = fopen(path, "r");
        require(log != NULL, "missing split-mean log");
        char line[512];
        size_t starts = 0;
        require(fgets(line, sizeof(line), log) != NULL, "missing split-mean log header");
        while (fgets(line, sizeof(line), log) != NULL)
        {
            size_t run = 0, count = 0;
            double sse = 0;
            require(sscanf(line, "%zu;%zu;%lf;", &run, &count, &sse) == 3, "invalid split-mean log");
            if (count == 0)
            {
                require(run >= 1 && run <= 2 && sse == 54, "incorrect split-mean initial SSE");
                ++starts;
            }
        }
        require(!ferror(log) && starts == 2, "missing split-mean start rows");
        closeFixture(log);
    }

    fixture = writeFixture(input);
    require(fputs("0\n2\n3\n10\n", fixture) >= 0, "could not write split-seed fixture");
    closeFixture(fixture);
    joinPath(output, sizeof(output), directory, "mean-seed-output");
    joinPath(datasetOutput, sizeof(datasetOutput), output, "mean-start");
    length = snprintf(arguments, sizeof(arguments),
                      "--algo skm-intra --seed 42 -r 1 --max-iterations 1 --snapshots 1:all "
                      "-o \"%s\" -k 2 \"%s\"",
                      output, input);
    require(length >= 0 && (size_t)length < sizeof(arguments), "split-seed case too long");
    expectCli(executable, arguments, capture, true);

    Dataset expected = readDataset(input);
    Dataset legacy = readDataset(input);
    resetPartitions(&expected);
    resetPartitions(&legacy);
    Centroids expectedCentroids = allocateCentroids(2, 1);
    Centroids legacyCentroids = allocateCentroids(2, 1);
    expectedCentroids.size = 1;
    legacyCentroids.size = 1;
    centroidStep(&expectedCentroids, &expected, unitKMeansScratch(&expectedCentroids));
    ClusterIndex expectedIndex = allocateClusterIndex(expected.size, 2);
    ClusterIndex legacyIndex = allocateClusterIndex(legacy.size, 2);
    rebuildClusterIndex(&expectedIndex, &expected, 1);
    rebuildClusterIndex(&legacyIndex, &legacy, 1);
    Rng expectedRng, legacyRng;
    rng_seed(&expectedRng, rng_derive_seed(42, 0));
    rng_seed(&legacyRng, rng_derive_seed(42, 0));
    generateRandomCentroids(1, &legacy, &legacyCentroids, &legacyRng, unitInitScratch(&legacy, INIT_RANDOM));
    splitClusterIntraCluster(&expected, &expectedIndex, &expectedCentroids, 0, 1, NULL, &expectedRng,
                             unitSplitScratch(&expected, &expectedIndex));
    splitClusterIntraCluster(&legacy, &legacyIndex, &legacyCentroids, 0, 1, NULL, &legacyRng,
                             unitSplitScratch(&legacy, &legacyIndex));
    require(memcmp(expected.partitions, legacy.partitions, expected.size * sizeof(*expected.partitions)) != 0 ||
                memcmp(expectedCentroids.coordinates, legacyCentroids.coordinates, 2 * sizeof(double)) != 0,
            "split-seed fixture did not distinguish the old initial RNG draw");

    joinPath(path, sizeof(path), datasetOutput, "SKM_Intra_run001_iter0001_step_centroids.txt");
    Centroids recorded = readCentroids(path);
    require(recorded.size == 2, "missing split-seed centroids");
    for (size_t c = 0; c < recorded.size; ++c)
        require(fabs(centroidRow(&recorded, c)[0] - centroidRow(&expectedCentroids, c)[0]) <= 1e-6,
                "split runner consumed randomness while initializing the mean");
    freeCentroids(&recorded);
    joinPath(path, sizeof(path), datasetOutput, "SKM_Intra_run001_iter0001_step_partitions.txt");
    FILE* partitions = fopen(path, "r");
    require(partitions != NULL, "missing split-seed partitions");
    for (size_t i = 0; i < expected.size; ++i)
    {
        size_t label = 0;
        require(fscanf(partitions, "%zu", &label) == 1 && label == expected.partitions[i],
                "split runner changed the first split's random stream");
    }
    closeFixture(partitions);
    freeClusterIndex(&legacyIndex);
    freeClusterIndex(&expectedIndex);
    freeCentroids(&legacyCentroids);
    freeCentroids(&expectedCentroids);
    freeDataset(&legacy);
    freeDataset(&expected);

    fixture = writeFixture(input);
    require(fputs("1e308\n1e308\n", fixture) >= 0, "could not write split-mean overflow fixture");
    closeFixture(fixture);
    joinPath(output, sizeof(output), directory, "mean-overflow-output");
    length = snprintf(arguments, sizeof(arguments), "--algo skm-intra -r 1 -o \"%s\" -k 1 \"%s\"", output, input);
    require(length >= 0 && (size_t)length < sizeof(arguments), "split-mean overflow case too long");
    expectCli(executable, arguments, capture, false);
    expectText(capture, "Error: Initial split centroid is not finite");
    require(remove(input) == 0, "could not remove split-mean fixture");
}

static void testDistinctCli(const char* directory, const char* executable, const char* capture)
{
    char input[PATH_MAX], output[PATH_MAX], datasetOutput[PATH_MAX], arguments[3 * PATH_MAX + 512];
    joinPath(input, sizeof(input), directory, "distinct-fixture.txt");
    FILE* fixture = writeFixture(input);
    require(fputs("0\n-0\n1\n", fixture) >= 0, "could not write distinct-coordinate fixture");
    closeFixture(fixture);
    const char* methods[] = {"random", "kmeans++"};
    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i)
    {
        joinPath(output, sizeof(output), directory, i == 0 ? "distinct-invalid-random" : "distinct-invalid-plus");
        joinPath(datasetOutput, sizeof(datasetOutput), output, "distinct-fixture");
        int length = snprintf(arguments, sizeof(arguments), "--algo km --init %s -r 1 -o \"%s\" -k 3 \"%s\"",
                              methods[i], output, input);
        require(length >= 0 && (size_t)length < sizeof(arguments), "distinct-coordinate case too long");
        expectCli(executable, arguments, capture, false);
        expectText(capture, "K (3) exceeds the number of distinct data points (2)");
        require(!fileExists(datasetOutput), "invalid distinct-coordinate count wrote dataset results");
    }

    fixture = writeFixture(input);
    require(fputs("0\n1e-200\n", fixture) >= 0, "could not write k-means++ underflow fixture");
    closeFixture(fixture);
    joinPath(output, sizeof(output), directory, "distinct-underflow");
    int length = snprintf(arguments, sizeof(arguments),
                          "--algo km --init kmeans++ -r 1 -o \"%s\" -k 2 \"%s\"", output, input);
    require(length >= 0 && (size_t)length < sizeof(arguments), "k-means++ underflow case too long");
    expectCli(executable, arguments, capture, false);
    expectText(capture, "k-means++ squared distances underflow for distinct data points");

    fixture = writeFixture(input);
    require(fputs("0\n1e-200\n2e-200\n", fixture) >= 0, "could not write zero-drop split fixture");
    closeFixture(fixture);
    joinPath(output, sizeof(output), directory, "distinct-zero-drop");
    length = snprintf(arguments, sizeof(arguments),
                      "--algo skm-random,skm-intra,skm-global,skm-global-basic,skm-local,bisecting "
                      "--seed 42 -r 1 --max-iterations 1 --snapshots none -o \"%s\" -k 3 \"%s\"",
                      output, input);
    require(length >= 0 && (size_t)length < sizeof(arguments), "zero-drop split case too long");
    expectCli(executable, arguments, capture, true);
    require(remove(input) == 0, "could not remove distinct-coordinate fixture");
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

static void checkSelectedSplitRows(const char* output, const char* algorithm)
{
    FILE* file = openCaseFile(output, algorithm, "_inner.csv");
    char line[512];
    require(fgets(line, sizeof(line), file) != NULL, "missing inner log header");
    size_t splits[4] = {0};
    while (fgets(line, sizeof(line), file) != NULL)
    {
        size_t run = 0, count = 0, iteration = 0;
        char stage[16];
        require(sscanf(line, "%zu;%zu;%15[^;];%zu;", &run, &count, stage, &iteration) == 4,
                "invalid selected-split trace");
        if (strcmp(stage, "split") == 0)
        {
            require(run == 1 && count >= 1 && count <= 3 && iteration == 1,
                    "selected-split trace has an invalid count");
            ++splits[count];
        }
    }
    require(!ferror(file) && splits[1] == 1 && splits[2] == 1 && splits[3] == 1,
            "selected trial traces are missing or duplicated");
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
    for (size_t k = 2; k <= 3; ++k)
    {
        joinPath(output, sizeof(output), directory, k == 2 ? "trials-k2" : "trials-k3");
        int length = snprintf(arguments, sizeof(arguments),
                              "--algo skm-intra --seed 42 -r 1 --max-iterations 1 --log-level 3 --no-track "
                              "-o \"%s\" -k %zu \"%s\"",
                              output, k, data);
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
        require(!ferror(file) && calls == (k == 2 ? 2u : 4u),
                "unexpected number of capped split and trial k-means calls");
        closeFixture(file);
    }
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

    testReportSummary(root, executable, capture);

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
    testRepartitionOptions(root, executable, data, capture);
    testBisectSelectOptions(root, executable, data, capture);
    testLocalSplitSnapshots(root, executable, data, capture);
    testBisectingSnapshots(root, executable, data, capture);
    testRandomSwapInitialReport(root, executable, capture);
    testSplitMeanStart(root, executable, capture);
    testDistinctCli(root, executable, capture);
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
                      {"Bisecting_KM", STAGE_TRY | STAGE_FINAL}};
    for (size_t i = 0; i < sizeof(splitCases) / sizeof(splitCases[0]); ++i)
    {
        checkInnerIterations(one, splitCases[i].name, 1, splitCases[i].stages);
        checkInnerIterations(two, splitCases[i].name, 2, splitCases[i].stages);
    }
    checkSelectedSplitRows(one, "SKM_Intra");
    checkSelectedSplitRows(one, "SKM_Global");
    checkSelectedSplitRows(one, "SKM_Local");
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
    if (argc == 2 && (strcmp(argv[1], "--zero-kmeans-core") == 0 ||
                      strcmp(argv[1], "--zero-kmeans-view") == 0 ||
                      strcmp(argv[1], "--zero-kmeans-tracked") == 0))
    {
        double coordinate = 0;
        size_t partition = 0;
        Dataset points = {&coordinate, &partition, 1, 1};
        Centroids centroids = {&coordinate, 1, 1, 1};
        if (strcmp(argv[1], "--zero-kmeans-core") == 0)
            (void)runKMeans(&points, 0, &centroids, NULL, KM_STAGE_NONE, unitKMeansScratch(&centroids));
        else if (strcmp(argv[1], "--zero-kmeans-view") == 0)
        {
            size_t index = 0;
            DatasetView view = {&points, &index, &partition, 1};
            (void)runKMeansView(&view, 0, &centroids, NULL, KM_STAGE_NONE, unitKMeansScratch(&centroids));
        }
        else
        {
            TrackOptions options = {0};
            Tracker tracker = trackerCreate(ALGO_KM, ".", NULL, &options);
            (void)runKMeansWithTracking(&points, 0, &centroids, &tracker, unitKMeansScratch(&centroids));
        }
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--zero-sse-split") == 0)
    {
        double coordinate = 0;
        size_t partition = 0;
        Dataset points = {&coordinate, &partition, 1, 1};
        Centroids centroids = {&coordinate, 1, 1, 1};
        TrackOptions options = {0};
        Tracker tracker = trackerCreate(ALGO_SKM_INTRA, ".", NULL, &options);
        Rng rng;
        rng_seed(&rng, 1);
        (void)runSseSplit(&points, &centroids, 1, 0, ALGO_SKM_INTRA, REPARTITION_INWARD, &tracker, &rng,
                          unitKMeansScratch(&centroids));
        return EXIT_SUCCESS;
    }
    if (argc == 2 && (strcmp(argv[1], "--zero-bisect-iterations") == 0 ||
                      strcmp(argv[1], "--zero-bisect-tries") == 0))
    {
        double coordinate = 0;
        size_t partition = 0;
        Dataset points = {&coordinate, &partition, 1, 1};
        Centroids centroids = {&coordinate, 1, 1, 1};
        TrackOptions options = {0};
        Tracker tracker = trackerCreate(ALGO_BISECTING, ".", NULL, &options);
        Rng rng;
        rng_seed(&rng, 1);
        size_t iterations = strcmp(argv[1], "--zero-bisect-iterations") == 0 ? 0 : 1;
        size_t tries = strcmp(argv[1], "--zero-bisect-tries") == 0 ? 0 : 1;
        (void)runBisectingKMeans(&points, &centroids, 1, iterations, tries, BISECT_SELECT_SSE, &tracker, &rng,
                                 unitKMeansScratch(&centroids));
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--nonfinite-rs-initial") == 0)
    {
        double coordinates[] = {1e308, -1e308};
        size_t labels[] = {0, 0};
        double initial[] = {1e308};
        Dataset points = {coordinates, labels, 2, 1};
        Centroids centroids = {initial, 1, 1, 1};
        TrackOptions options = {0};
        Tracker tracker = trackerCreate(ALGO_RS, ".", NULL, &options);
        Rng rng;
        rng_seed(&rng, 1);
        (void)randomSwap(&points, &centroids, 1, 2, &tracker, &rng);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--nonfinite-bisect-final") == 0)
    {
        double coordinates[] = {1e308, -1e308};
        size_t labels[] = {0, 0};
        double initial = 0;
        Dataset points = {coordinates, labels, 2, 1};
        Centroids centroids = {&initial, 1, 1, 1};
        TrackOptions options = {0};
        Tracker tracker = trackerCreate(ALGO_BISECTING, ".", NULL, &options);
        Rng rng;
        rng_seed(&rng, 1);
        (void)runBisectingKMeans(&points, &centroids, 1, 1, 1, BISECT_SELECT_SSE, &tracker, &rng,
                                 unitKMeansScratch(&centroids));
        return EXIT_SUCCESS;
    }
    if (argc == 2 && (strcmp(argv[1], "--nonfinite-kmeans-tracked") == 0 ||
                      strcmp(argv[1], "--nonfinite-random-final") == 0 ||
                      strcmp(argv[1], "--nonfinite-sse-final") == 0))
    {
        double coordinates[] = {1e308, -1e308};
        size_t labels[] = {0, 0};
        double initial = 0;
        Dataset points = {coordinates, labels, 2, 1};
        Centroids centroids = {&initial, 1, 1, 1};
        TrackOptions options = {0};
        Tracker tracker = trackerCreate(ALGO_SKM_INTRA, ".", NULL, &options);
        Rng rng;
        rng_seed(&rng, 1);
        if (strcmp(argv[1], "--nonfinite-kmeans-tracked") == 0)
            (void)runKMeansWithTracking(&points, 1, &centroids, &tracker, unitKMeansScratch(&centroids));
        else if (strcmp(argv[1], "--nonfinite-random-final") == 0)
            (void)runRandomSplit(&points, &centroids, 1, 1, &tracker, &rng, unitKMeansScratch(&centroids));
        else
            (void)runSseSplit(&points, &centroids, 1, 1, ALGO_SKM_INTRA, REPARTITION_INWARD, &tracker, &rng,
                              unitKMeansScratch(&centroids));
        return EXIT_SUCCESS;
    }
    if (argc == 2 && (strcmp(argv[1], "--nonfinite-basic-original") == 0 ||
                      strcmp(argv[1], "--nonfinite-basic-trial") == 0))
    {
        double coordinates[] = {0, 1e308, -1e308};
        size_t labels[] = {0, 0, 0};
        Dataset points = {coordinates, labels, 3, 1};
        ClusterIndex index = allocateClusterIndex(points.size, 1);
        rebuildClusterIndex(&index, &points, 1);
        Rng rng;
        rng_seed(&rng, 1);
        double originalSse = strcmp(argv[1], "--nonfinite-basic-original") == 0 ? INFINITY : 0.0;
        (void)tentativeSseDrop(&points, &index, 0, 1, originalSse, &rng, unitSplitScratch(&points, &index));
        freeClusterIndex(&index);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--nonfinite-rs-trial") == 0)
    {
        double coordinates[] = {0, 0, 0, 0, 1e154, 1e154, 1e154, 1e154, -9e153};
        size_t labels[] = {0, 0, 0, 0, 1, 1, 1, 1, 0};
        double initial[] = {0, 1e154};
        const size_t proposals[][2] = {{1, 8}};
        Dataset points = {coordinates, labels, 9, 1};
        Centroids centroids = {initial, 2, 2, 1};
        TrackOptions options = {0};
        options.rows = true;
        Tracker tracker = trackerCreate(ALGO_RS, ".", NULL, &options);
        trackerBeginRun(&tracker, 0);
        Rng rng;
        rng_seed(&rng, seedForSwapSequence(2, 9, proposals, 1));
        double sse = randomSwap(&points, &centroids, 1, 3, &tracker, &rng);
        require(isfinite(sse) && sse == calculateSSE(&points, &centroids) && labels[8] == 0 &&
                    initial[0] == 0 && initial[1] == 1e154 && tracker.rowCount == 1,
                "non-finite Random Swap trial was not rejected and restored");
        trackerPause(&tracker);
        trackerFree(&tracker);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--unsplittable-split") == 0)
    {
        double coordinates[] = {0, 0};
        size_t labels[] = {0, 0};
        double initial[] = {0, 0};
        Dataset points = {coordinates, labels, 2, 1};
        Centroids centroids = {initial, 1, 2, 1};
        TrackOptions options = {0};
        Tracker tracker = trackerCreate(ALGO_SKM_INTRA, ".", NULL, &options);
        Rng rng;
        rng_seed(&rng, 1);
        (void)runSseSplit(&points, &centroids, 2, 1, ALGO_SKM_INTRA, REPARTITION_INWARD, &tracker, &rng,
                          unitKMeansScratch(&centroids));
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--too-few-distinct-init") == 0)
    {
        double coordinates[] = {0, 0, 9};
        size_t labels[] = {0, 0, 0};
        double initial[] = {0, 0, 0};
        Dataset points = {coordinates, labels, 3, 1};
        Centroids centroids = {initial, 3, 3, 1};
        Rng rng;
        rng_seed(&rng, 1);
        generateRandomCentroids(3, &points, &centroids, &rng, unitInitScratch(&points, INIT_RANDOM));
        return EXIT_SUCCESS;
    }
    if (argc == 3 && (strcmp(argv[1], "--zero-kmeans") == 0 || strcmp(argv[1], "--zero-rkm") == 0 ||
                      strcmp(argv[1], "--zero-random-k1") == 0 || strcmp(argv[1], "--zero-random-k2") == 0))
    {
        double coordinates[] = {0, 10};
        size_t labels[] = {0, 0};
        Dataset points = {coordinates, labels, 2, 1};
        TrackOptions options = {0};
        if (strcmp(argv[1], "--zero-kmeans") == 0)
            runKMeansAlgorithm(&points, NULL, 1, 0, 0, 1, 1, "zero-limit", argv[2], &options, INIT_RANDOM, 1);
        else if (strcmp(argv[1], "--zero-rkm") == 0)
            runRepeatedKMeansAlgorithm(&points, NULL, 1, 0, 1, 0, 1, 1, "zero-limit", argv[2], &options,
                                       INIT_RANDOM, 1);
        else
        {
            double initial[] = {5, 0};
            Centroids centroids = {initial, 1, 2, 1};
            Tracker tracker = trackerCreate(ALGO_SKM_RANDOM, argv[2], NULL, &options);
            Rng rng;
            rng_seed(&rng, 1);
            size_t k = strcmp(argv[1], "--zero-random-k1") == 0 ? 1 : 2;
            (void)runRandomSplit(&points, &centroids, k, 0, &tracker, &rng, unitKMeansScratch(&centroids));
        }
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
    testInvalidInternalInputs(argv[0], argv[1]);
    testZeroIterationRunners(argv[0], argv[1]);
    testNonfiniteRandomSwap(argv[0], argv[1]);
    testFlatStorage();
    testMetrics();
    testRng();
    testDistinctInitialization();
    testKMeans();
    testKMeansView();
    testKMeansScratchReuse();
    testInitScratchReuse();
    testLastKMeansState();
    testLastTrialState();
    testLastStateSplitRunners();
    testBisectingLastTrialWins();
    testKmAndRkmLastState(argv[1]);
    testRkmNoFiniteFallback(argv[1]);
    testRepartition();
    testRandomSwapRepartition();
    testAffectedCentroidStep();
    testRandomSwapAcceptance();
    testRandomSwapDistinctTargets();
    testRandomSwapNoTarget();
    testLocalRepartitionMeans();
    testLocalRepartitionModes();
    testCachedTentativeSplit();
    testCachedTrialMembership();
    testSelectedTrialIsApplied(argv[1]);
    testLocalCachedTrialAcrossSplits(argv[1]);
    testSplitSeedFallback();
    testRandomSplitRankFallback();
    testBisectingFirstTries();
    testBisectSelection();
    testLocalFirstAndFinalSplit();
    testTwoClusterSplitSkipsTrials();
    testSplitCapacity();
    testClusterIndex();
    testIndexedClusterSSE();
    testDistinctSplitEligibility();
    if (argc == 6) testCliIterationLimit(argv[1], argv[2], argv[3], argv[4], argv[5]);
    puts("test_unit: parser, flat data, metrics, RNG, k-means, membership and repartition cases passed");
    return EXIT_SUCCESS;
}
