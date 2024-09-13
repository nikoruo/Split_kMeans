#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <float.h>

//Constants for file locations
const char* DATA_FILENAME = "data/s3.txt";
const char* GT_FILENAME = "GroundTruth/s3-cb.txt";
const char* CENTROID_FILENAME = "outputs/centroid.txt";
const char* PARTITION_FILENAME = "outputs/partition.txt";
const char SEPARATOR = ' ';

//for centroids
const int NUM_CENTROIDS = 15;
// s            = 15
// unbalanced   = 8 
// a            = 20,35,50 
// TODO: tarkasta loput aineistot

// for clustering
const int MAX_ITERATIONS = 1000; // k-means rajoitus
const int MAX_REPEATS = 100; // repeated k-means toistojen lkm, TODO: lopulliseen 100kpl
const int MAX_SWAPS = 5000; // random swap toistojen lkm, TODO: lopulliseen 1000kpl

// for logging
const int LOGGING = 1; // 1 = basic, 2 = detailed, 3 = debug

//Structs
//
//
typedef struct
{
    double* attributes;
    size_t dimensions;
    int partition;
} DataPoint;

typedef struct
{
    DataPoint* points;
    size_t size;
} DataPoints;

typedef struct
{
    DataPoint* points;
    int size;
} Centroids;

typedef struct
{
    double sse;
    int* partition;
    DataPoint* centroids;
    size_t centroidIndex;
} ClusteringResult;


//Memories
//
//

// Function to handle memory error
void handleMemoryError(void* ptr)
{
    if (ptr == NULL)
    {
        fprintf(stderr, "Error: Unable to allocate memory\n");
        exit(EXIT_FAILURE);
    }
}

//function to free data point array
void freeDataPointArray(DataPoint* points, size_t size)
{
    if (points == NULL) return;

    for (size_t i = 0; i < size; ++i)
    {
        if (points[i].attributes != NULL)
        {
            free(points[i].attributes);
            points[i].attributes = NULL;
        }
    }
    free(points);
    points = NULL;
}

// Function to free data points
void freeDataPoints(DataPoints* dataPoints)
{
    if (dataPoints == NULL) return;
    freeDataPointArray(dataPoints->points, dataPoints->size);
    dataPoints->points = NULL;
}

// Function to free centroids
void freeCentroids(Centroids* centroids)
{
    if (centroids == NULL) return;
    freeDataPointArray(centroids->points, centroids->size);
    centroids->points = NULL;
}

// Function to free ClusteringResult
// TODO: ei viel� k�yt�ss� miss��n
void freeClusteringResult(ClusteringResult* result)
{
    if (result == NULL) return;

    if (result->partition != NULL)
    {
        free(result->partition);
        result->partition = NULL;
    }

    if (result->centroids != NULL)
    {
        freeDataPointArray(result->centroids, NUM_CENTROIDS);
        result->centroids = NULL;
    }
}


//Helpers
//
// 
// Function to calculate the squared Euclidean distance between two data points
//TODO: static inline double... sellaisten funktioiden eteen jotka eiv�t muuta mit��n ja joita kutsutaan? Saattaa tehostaa suoritusta
double calculateSquaredEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
{
    double sum = 0.0;
    for (size_t i = 0; i < point1->dimensions; ++i)
    {
        double diff = point1->attributes[i] - point2->attributes[i];
        sum += diff * diff;
    }
    return sum;
}

//function to calculate the Euclidean distance between two data points
double calculateEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
{
	double sqrtDistance = sqrt(calculateSquaredEuclideanDistance(point1, point2));
    return sqrtDistance;
}

// Function to handle file error
void handleFileError(const char* filename)
{
    fprintf(stderr, "Error: Unable to open file '%s'\n", filename);
    exit(EXIT_FAILURE);
}

// Function to get the number of dimensions in the data
int getNumDimensions(const char* filename)
{
    FILE* file = fopen(filename, "r");
    if (file == NULL)
    {
        handleFileError(filename);
    }

    //for disabling the CS6387 warning
    //safe to use as we actually check for NULL earlier
    //_Analysis_assume_(file != NULL);

    char firstLine[256];
    if (fgets(firstLine, sizeof(firstLine), file) == NULL)
    {
        fprintf(stderr, "Error: File '%s' is empty\n", filename);
        exit(EXIT_FAILURE);
    }

    int dimensions = 0;
    char* context = NULL;
    char* token = strtok_s(firstLine, " ", &context);
    while (token != NULL)
    {
        dimensions++;
        token = strtok_s(NULL, " ", &context);
    }

    fclose(file);

    return dimensions;
}

// Function to read data points from a file
DataPoints readDataPoints(const char* filename)
{
    FILE* file = fopen(filename, "r");
    if (file == NULL)
    {
        handleFileError(filename);
    }

    DataPoints dataPoints;
    dataPoints.points = NULL;
    dataPoints.size = 0;
    size_t allocatedSize = 0;

    char line[256];
    while (fgets(line, sizeof(line), file))
    {
        size_t attributeAllocatedSize = 4;

        DataPoint point;
        point.attributes = malloc(sizeof(double) * attributeAllocatedSize);
        handleMemoryError(point.attributes);
        point.dimensions = 0;
        point.partition = -1;

        char* context = NULL;
        char* token = strtok_s(line, " ", &context);
        while (token != NULL)
        {
            if (point.dimensions == attributeAllocatedSize)
            {
                attributeAllocatedSize = attributeAllocatedSize > 0 ? attributeAllocatedSize * 2 : 1;
                double* temp = realloc(point.attributes, sizeof(double) * attributeAllocatedSize);
                handleMemoryError(temp);
                point.attributes = temp;
            }

            point.attributes[point.dimensions++] = strtod(token, NULL); //TODO strtod = double, atoi = int
			token = strtok_s(NULL, " \t\n", &context); //TODO spaced " ", tabs "\t", newlines "\n"
        }

        if (dataPoints.size == allocatedSize)
        {
            allocatedSize = allocatedSize > 0 ? allocatedSize * 2 : 1;
            DataPoint* temp = realloc(dataPoints.points, sizeof(DataPoint) * allocatedSize);
            handleMemoryError(temp);
            dataPoints.points = temp;
        }

        dataPoints.points[dataPoints.size++] = point;
    }

    fclose(file);

    if (LOGGING == 3)
    {
        for (size_t i = 0; i < dataPoints.size; ++i)
        {
            printf("Data point %zu attributes: ", i);
            for (size_t j = 0; j < dataPoints.points[i].dimensions; ++j)
            {
                printf("%f ", dataPoints.points[i].attributes[j]);
            }
            printf("\n");
        }
    }
    
    return dataPoints;
}

// Function to read centroids from a file
Centroids readCentroids(const char* filename)
{
    DataPoints points = readDataPoints(filename);

    Centroids centroids;
    centroids.size = points.size;
    centroids.points = points.points;

    return centroids;
}

// Function to write the centroids to a file
//TODO: ei k�yt�ss� t�ll� hetkell�
void writeCentroidsToFile(const char* filename, DataPoint* centroids, int numCentroids)
{
    FILE* centroidFile = fopen(filename, "w");
    if (centroidFile == NULL)
    {
        handleFileError(filename);
    }

    for (int i = 0; i < numCentroids; ++i)
    {
        for (size_t j = 0; j < centroids[i].dimensions; ++j)
        {
            fprintf(centroidFile, "%f ", centroids[i].attributes[j]);
        }
        fprintf(centroidFile, "\n"); //false positive
    }

    fclose(centroidFile); //false positive
    printf("Centroid file created successfully: %s\n", filename);
}

// Function to write partition to a text file
//TODO: ei k�yt�ss� t�ll� hetkell�
void writePartitionToFile(int* partition, int partitionSize, const char* fileName)
{
    FILE* outFile = fopen(fileName, "w");
    if (outFile == NULL)
    {
        handleFileError(fileName);
    }

    // Write each data point and its corresponding cluster to the file
    for (int i = 0; i < partitionSize; ++i)
    {
        fprintf(outFile, "Data Point %d: Cluster %d\n", i, partition[i]);
    }

    fclose(outFile);

    printf("Optimal partition written to OptimalPartition.txt\n");
}

//Funtion to create a deep copy of a data point
void deepCopyDataPoint(DataPoint* destination, DataPoint* source)
{
    destination->dimensions = source->dimensions;
    destination->attributes = malloc(source->dimensions * sizeof(double));
    handleMemoryError(destination->attributes);
    memcpy(destination->attributes, source->attributes, source->dimensions * sizeof(double));
}

//Funtion to create deep copies of data points
void deepCopyDataPoints(DataPoint* destination, DataPoint* source, int size)
{
	for (int i = 0; i < size; ++i)
	{
		destination[i].dimensions = source[i].dimensions;
		destination[i].attributes = malloc(source[i].dimensions * sizeof(double));
		handleMemoryError(destination[i].attributes);
		memcpy(destination[i].attributes, source[i].attributes, source[i].dimensions * sizeof(double));
	}
}

//Funtion to copy centroids
void copyCentroids(Centroids* source, Centroids* destination, int numCentroids)
{
    destination->size = source->size;
    destination->points = malloc(numCentroids * sizeof(DataPoint));
    handleMemoryError(destination->points);
    for (int i = 0; i < numCentroids; ++i)
    {
        destination->points[i].dimensions = source->points[i].dimensions;
        deepCopyDataPoint(&destination->points[i], &source->points[i]);
    }
}


// Clustering
//
// 
// Function to choose random data points to be centroids
//TODO poista numCentroids t��lt�
void generateRandomCentroids(int numCentroids, DataPoints* dataPoints, DataPoint* centroids)
{    
    /*DEBUGGING
    if (dataPoints->size < numCentroids)
    {
        fprintf(stderr, "Error: There are less data points than the required number of clusters\n");
        exit(EXIT_FAILURE);
    }*/

    size_t* indices = malloc(sizeof(size_t) * dataPoints->size);
    handleMemoryError(indices);

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        indices[i] = i;
    }

    for (size_t i = 0; i < numCentroids; ++i)
    {
        int j = i + rand() % (dataPoints->size - i);
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }

    for (size_t i = 0; i < numCentroids; ++i)
    {
        size_t selectedIndex = indices[i];
        deepCopyDataPoint(&centroids[i], &dataPoints->points[selectedIndex]);
    }

    free(indices);
}

//Function to calculate the sum of squared errors (SSE)
double calculateSSE(DataPoints* dataPoints, Centroids* centroids)
{
    double sse = 0.0;

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        int cIndex = dataPoints->points[i].partition;

        //TODO squared vai ei?
        sse += calculateEuclideanDistance(&dataPoints->points[i], &centroids->points[cIndex]);
    }

    return sse;
}

//calculate MSE
double calculateMSE(DataPoints* dataPoints, Centroids* centroids)
{
    double sse = calculateSSE(dataPoints, centroids);

    double mse = sse / (dataPoints->size * dataPoints->points[0].dimensions);

    return mse;
}

// Function to find the nearest centroid of a data point
size_t findNearestCentroid(DataPoint* queryPoint, Centroids* targetCentroids)
{
    /* DEBUGGING    
    if (targetPoints->size == 0)
    {
        fprintf(stderr, "Error: Cannot find nearest centroid in an empty set of data\n");
        exit(EXIT_FAILURE);
    }*/

    size_t nearestCentroidId = -1;
    double minDistance = DBL_MAX;
	double newDistance = DBL_MAX;

    for (size_t i = 0; i < targetCentroids->size; ++i)
    {
        newDistance = calculateEuclideanDistance(queryPoint, &targetCentroids->points[i]);
		
        if (newDistance < minDistance)
        {
            minDistance = newDistance;
            nearestCentroidId = i;
        }
    }

    return nearestCentroidId;
}

// Function for optimal partitioning
void optimalPartition(DataPoints* dataPoints, Centroids* centroids)
{
    /* DEBUGGING    
    if (dataPoints->size == 0 || centroids->size == 0)
    {
        fprintf(stderr, "Error: Cannot perform optimal partition with empty data or centroids\n");
        exit(EXIT_FAILURE);
    }*/

	size_t nearestCentroidId = -1;

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        nearestCentroidId = findNearestCentroid(&dataPoints->points[i], centroids);
        dataPoints->points[i].partition = nearestCentroidId;
    }
}

// Function to perform the centroid step in k-means
void centroidStep(Centroids* centroids, DataPoints* dataPoints)
{
    int numClusters = centroids->size;
    size_t dimensions = dataPoints->points[0].dimensions;

    // Temporary storage for sums and counts
    double** sums = malloc(sizeof(double*) * numClusters);
    size_t* counts = calloc(numClusters, sizeof(size_t));
    handleMemoryError(sums);
    handleMemoryError(counts);

    // Initialize sums and counts for each centroid
    for (size_t clusterLabel = 0; clusterLabel < numClusters; ++clusterLabel)
    {
        sums[clusterLabel] = calloc(dimensions, sizeof(double));
        handleMemoryError(sums[clusterLabel]);
    }

    // Accumulate sums and counts for each cluster based on data points
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        DataPoint* point = &dataPoints->points[i];
        int clusterLabel = point->partition; 

        for (size_t dim = 0; dim < dimensions; ++dim)
        {
            sums[clusterLabel][dim] += point->attributes[dim];
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
                centroids->points[clusterLabel].attributes[dim] = sums[clusterLabel][dim] / counts[clusterLabel];
            }
        }
        /*else DEBUGGING
        {
            if(LOGGING == 2) fprintf(stderr, "Warning: Cluster %d has no points assigned.\n", clusterLabel);
        }*/

        free(sums[clusterLabel]);
    }

    free(sums);
    free(counts);
}

//function to count orphans (helper for calculateCentroidIndex)
size_t countOrphans(Centroids* centroids1, Centroids* centroids2)
{
    size_t* closest = calloc(centroids2->size, sizeof(size_t));
    handleMemoryError(closest);

    size_t countFrom1to2 = 0;

    for (size_t i = 0; i < centroids1->size; ++i)
    {
        /*double minDistance = DBL_MAX;
        int closestIndex = -1;

        for (int j = 0; j < centroids2->size; ++j)
        {
            double distance = calculateEuclideanDistance(&centroids1->points[i], &centroids2->points[j]);
            if (distance < minDistance)
            {
                minDistance = distance;
                closestIndex = j;
            }
        }*/

        //TODO vanhaa koodia yl�puolella, poista jos funkkari toimii
        size_t closestIndex = findNearestCentroid(&centroids1->points[i], centroids2);
        closest[closestIndex]++;
    }

    for (size_t i = 0; i < centroids2->size; ++i)
    {
        if (closest[i] == 0)
        {
            countFrom1to2++;
        }
    }

    free(closest);

    return countFrom1to2;
}

//function to calculate Centroid Index (CI)
size_t calculateCentroidIndex(Centroids* centroids1, Centroids* centroids2)
{
    size_t countFrom1to2 = countOrphans(centroids1, centroids2);
    size_t countFrom2to1 = countOrphans(centroids2, centroids1);

    return (countFrom1to2 > countFrom2to1) ? countFrom1to2 : countFrom2to1;
}

// Function to run the k-means algorithm
ClusteringResult runKMeans(DataPoints* dataPoints, int iterations, Centroids* centroids, Centroids* groundTruth)
{
    double bestSse = DBL_MAX;
    int centroidIndex = -1;
    double sse = DBL_MAX;

    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        // Partition step
        optimalPartition(dataPoints, centroids);

        // Centroid step
        centroidStep(centroids, dataPoints);
                
        //Centroid Index
        centroidIndex = calculateCentroidIndex(centroids, groundTruth);
        //if (LOGGING == 1) printf("(runKMeans)CI after iteration %d: %d\n", iteration + 1, centroidIndex);

        // SSE Calculation
        // TODO: t�m� on nyt MSE, eik� SSE, pit�� siis refactoroida kaikki paikat, mutta nyt saa olla n�in
        sse = calculateMSE(dataPoints, centroids);
        //DEBUGGING if (LOGGING == 2) printf("(runKMeans)Total SSE after iteration %d: %.0f\n", iteration + 1, sse / 10000000);

        if (LOGGING == 2) printf("(runKMeans)After iteration %d: CI = %d and MSE = %.5f\n", iteration + 1, centroidIndex, sse/10000);


        if (sse < bestSse)
        {
			//printf("Best MSE so far: %.0f\n", sse);
            bestSse = sse;
        }
        else
        {
            break; //TODO: break toimii, mutta on aika ruma ratkaisu
        }
    }

    ClusteringResult result;
    result.sse = bestSse;
    result.centroidIndex = calculateCentroidIndex(centroids, groundTruth);
    return result;
}

// Function to perform random swap
void randomSwap(DataPoints* dataPoints, Centroids* centroids, Centroids* groundTruth, ClusteringResult* bestResult)
{
    double bestSse = DBL_MAX;
    int kMeansIterations = 2;

    Centroids originalCentroids;
    originalCentroids.size = centroids->size;
    originalCentroids.points = malloc(centroids->size * sizeof(DataPoint));
    handleMemoryError(originalCentroids.points);
    for (int i = 0; i < centroids->size; ++i)
    {
        originalCentroids.points[i].attributes = malloc(centroids->points[i].dimensions * sizeof(double));
        handleMemoryError(originalCentroids.points[i].attributes);
        deepCopyDataPoint(&originalCentroids.points[i], &centroids->points[i]);
    }

    for (int i = 0; i < MAX_SWAPS; ++i)
    {
        int randomCentroidId = rand() % centroids->size;
        int randomDataPointId = rand() % dataPoints->size;

        //Saving the old centroid
        deepCopyDataPoint(&centroids->points[randomCentroidId], &centroids->points[randomCentroidId]);

        //Swapping
        deepCopyDataPoint(&centroids->points[randomCentroidId], &dataPoints->points[randomDataPointId]);

        ClusteringResult result = runKMeans(dataPoints, kMeansIterations, centroids, groundTruth);

        //If 1) CI or 2) SSE improves, we keep the change
        //if not, we reverse the swap
        if (result.centroidIndex < bestResult->centroidIndex 
            || result.centroidIndex == bestResult->centroidIndex && result.sse < bestResult->sse)
        {
			if (LOGGING == 1) printf("(RS) Round %d: Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (MSE): %.4f\n", i+1, result.centroidIndex, result.sse / 10000);
            
            bestResult->sse = result.sse;
            bestResult->centroidIndex = result.centroidIndex;
            deepCopyDataPoints(bestResult->centroids, centroids->points, centroids->size);
        }
        else
        {
            deepCopyDataPoint(&centroids->points[randomCentroidId], &originalCentroids.points[randomCentroidId]);
        }
    }
}

/*
So this need to be refactored into split k-means
This requires a lot of work, and is not currently in use
Initial version from KLU course used high level of randomization

// Function to run the split k-means algorithm
double runSplit(DataPoint* dataPoints, int dataPointsSize, int size)
{
    DataPoint* centroids = generateRandomCentroids(1, dataPoints, dataPointsSize);
    int* partition = malloc(sizeof(int) * dataPointsSize);
    handleMemoryError(partition);

    for (int i = 0; i < dataPointsSize; ++i)
    {
        partition[i] = 0;
    }

    double sse = 0;

    while (numCentroids(centroids) != size)
    {
        int c = selectRandomly(centroids);

        int* indexes = malloc(sizeof(int) * dataPointsSize);
        handleMemoryError(indexes);
        DataPoint* dpoints = malloc(sizeof(DataPoint) * dataPointsSize);
        handleMemoryError(dpoints);

        int indexCount = 0;
        for (int i = 0; i < dataPointsSize; ++i)
        {
            if (partition[i] == c)
            {
                indexes[indexCount] = i;
                dpoints[indexCount] = dataPoints[i];
                indexCount++;
            }
        }

        int c1 = indexes[selectRandomly(indexes)];
        int c2 = c1;
        centroids[c] = dataPoints[c1];

        while (c2 == c1)
        {
            c2 = indexes[selectRandomly(indexes)];
        }
        addCentroid(&centroids, dataPoints[c2]);

        DataPoint* newCentroids = malloc(sizeof(DataPoint) * 2);
        handleMemoryError(newCentroids);
        newCentroids[0] = dataPoints[c1];
        newCentroids[1] = dataPoints[c2];

        ClusteringResult result = runKMeans(dpoints, indexCount, 5, newCentroids, 2);

        int c1index = findCentroidIndex(centroids, dataPoints[c1]);
        int c2index = findCentroidIndex(centroids, dataPoints[c2]);

        for (int i = 0; i < result.partitionSize; ++i)
        {
            partition[indexes[i]] = result.partition[i] == 0 ? c1index : c2index;
        }

        sse = result.sse;

        free(indexes);
        free(dpoints);
        free(newCentroids);
    }

    ClusteringResult result = runKMeans(dataPoints, dataPointsSize, MAX_ITERATIONS, centroids, numCentroids(centroids));
    printf("Split without global kmeans : %f\n", calculateSSE(dataPoints, dataPointsSize, centroids, numCentroids(centroids), partition));

    free(partition);
    free(centroids);

    return result.sse;
}*/


//TODO k�y l�pi size_t muuttujat, int on parempi jos voi olla negatiivinen

///////////
// Main //
/////////
int main()
{
	// Seeding the random number generator
    srand((unsigned int)time(NULL));

    printf("Starting the process\n");
    printf("File name: %s\n", DATA_FILENAME);

    int numDimensions = getNumDimensions(DATA_FILENAME);

    if (numDimensions > 0)
    {        
        printf("Number of dimensions in the data: %d\n", numDimensions);

		int maxIterations = MAX_ITERATIONS;//TODO tarkasta kaikki, ett� k�ytet��n oikeita muuttujia

        DataPoints dataPoints = readDataPoints(DATA_FILENAME);
        printf("Dataset size: %zu\n\n", dataPoints.size);

        Centroids groundTruth = readCentroids(GT_FILENAME);

        //////////////
        // K-means //
        ////////////
        printf("K-means\n");

        Centroids centroids;
        centroids.size = NUM_CENTROIDS;
        centroids.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(centroids.points); //TODO: pit�isik� lopullisesta versiosta kommentoida n�m� pois? Helpottavat l�hinn� debuggausta
		for (int i = 0; i < NUM_CENTROIDS; ++i)
		{
			centroids.points[i].attributes = malloc(numDimensions * sizeof(double));
			handleMemoryError(centroids.points[i].attributes);
            centroids.points[i].dimensions = numDimensions; //TODO tarkista tehd��nk� muualla
		}

        clock_t start = clock();

        generateRandomCentroids(NUM_CENTROIDS, &dataPoints, centroids.points);
		size_t centroidIndex = calculateCentroidIndex(&centroids, &groundTruth);
		if (LOGGING == 1) printf("(K-means)Initial Centroid Index (CI): %zu\n", centroidIndex);

        ClusteringResult result0 = runKMeans(&dataPoints, maxIterations, &centroids, &groundTruth);
        //DEBUGGING if (LOGGING == 2) printf("(K-means)Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (SSE): %.4f\n", result0.centroidIndex, result0.sse / 10000000);

        clock_t end = clock();
        double duration = ((double)(end - start)) / CLOCKS_PER_SEC;

        printf("(K-means)Time taken: %f seconds\n\n", duration);

        result0.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(result0.centroids);
        deepCopyDataPoints(result0.centroids, centroids.points, NUM_CENTROIDS);

        result0.partition = malloc(dataPoints.size * sizeof(int));
        handleMemoryError(result0.partition);
        for (size_t i = 0; i < dataPoints.size; ++i)
        {
            result0.partition[i] = dataPoints.points[i].partition;
        }

        //printf("(K-means)Best Centroid Index (CI): %d and Best Mean Sum-of-Squared Errors (MSE): %.4f\n\n", result0.centroidIndex, result0.sse / 10000);
                
        ///////////////////////
        // Repeated k-means //
        /////////////////////
        printf("Repeated K-means\n");

        ClusteringResult result1;        
        ClusteringResult bestResult1;
        bestResult1.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(bestResult1.centroids);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
        {
            bestResult1.centroids[i].attributes = malloc(numDimensions * sizeof(double));
            handleMemoryError(bestResult1.centroids[i].attributes);
        }
        bestResult1.partition = malloc(dataPoints.size * sizeof(int));
        handleMemoryError(bestResult1.partition);
        bestResult1.sse = DBL_MAX;
        bestResult1.centroidIndex = INT_MAX;

        Centroids centroids1;
        centroids1.size = NUM_CENTROIDS; //TODO: tee constista paikallinen muuttuja
        centroids1.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(centroids1.points);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
        {
            centroids.points[i].attributes = malloc(numDimensions * sizeof(double));
            handleMemoryError(centroids.points[i].attributes);
        }

        start = clock();        

        for (int repeat = 0; repeat < MAX_REPEATS; ++repeat)
        {
            //DEBUGGING if(LOGGING == 3) printf("round: %d\n", repeat);

            generateRandomCentroids(NUM_CENTROIDS, &dataPoints, centroids1.points);
            int centroidIndex = calculateCentroidIndex(&centroids1, &groundTruth);
            if (LOGGING == 2) printf("(K-means)Initial Centroid Index (CI): %d\n", centroidIndex);

            // K-means
            result1 = runKMeans(&dataPoints, maxIterations, &centroids1, &groundTruth);

			if(LOGGING == 2) printf("(RKM) Round %d: Latest Centroid Index (CI): %d and Latest Mean Sum-of-Squared Errors (MSE): %.4f\n", repeat, result1.centroidIndex, result1.sse / 10000);
            
            if (result1.centroidIndex < bestResult1.centroidIndex 
                || (result1.centroidIndex == bestResult1.centroidIndex && result1.sse < bestResult1.sse))
            {
				deepCopyDataPoints(bestResult1.centroids, centroids1.points, NUM_CENTROIDS);
                bestResult1.sse = result1.sse;
                bestResult1.centroidIndex = result1.centroidIndex;
                for (size_t i = 0; i < dataPoints.size; ++i)
                {
                    bestResult1.partition[i] = dataPoints.points[i].partition;
                }
            }
        }
        
        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Repeated k-means)Time taken: %.2f seconds\n\n", duration);
        //printf("(Repeated K-means)Best Centroid Index (CI): %d and Best Mean Sum-of-Squared Errors (MSE): %.4f\n", bestResult1.centroidIndex, bestResult1.sse / 10000);
        
        
        //////////////////
        // Random Swap //
        ////////////////
        ClusteringResult result2;

        result2.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(result2.centroids);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
        {
            result2.centroids[i].attributes = malloc(numDimensions * sizeof(double));
            handleMemoryError(result2.centroids[i].attributes);
        }
        result2.partition = malloc(dataPoints.size * sizeof(int));
        handleMemoryError(result2.partition);
		result2.centroidIndex = INT_MAX;
		result2.sse = DBL_MAX;

        Centroids centroids2;
        centroids2.size = NUM_CENTROIDS;
        centroids2.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(centroids2.points);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
        {
            centroids2.points[i].attributes = malloc(numDimensions * sizeof(double));
            handleMemoryError(centroids2.points[i].attributes);
        }

        printf("Random swap\n");
        start = clock();

        generateRandomCentroids(NUM_CENTROIDS, &dataPoints, centroids2.points);

        // Random Swap
        randomSwap(&dataPoints, &centroids2, &groundTruth, &result2);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;

        //Update partition array
        memcpy(result2.partition, dataPoints.points, dataPoints.size * sizeof(int));

        printf("(Random Swap)Time taken: %.2f seconds\n\n", duration);

        ////////////////////
        // Split k-means //
        //////////////////
        printf("Split k-means\n");
        start = clock();

        //not refactored yet
        //double bestSse4 = runSplit(dataPoints, dataPoints.size, NUM_CENTROIDS);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Split)Time taken: %.2f seconds\n\n", duration);

        /////////////
        // Prints //
        ///////////
        printf("(K-means)Best Centroid Index (CI): %d and Best Mean Sum-of-Squared Errors (MSE): %.4f\n", result0.centroidIndex, result0.sse / 10000);
        printf("(Repeated K-means)Best Centroid Index (CI): %d and Mean Best Sum-of-Squared Errors (MSE): %.4f\n", bestResult1.centroidIndex, bestResult1.sse / 10000);
        printf("(Random Swap)Best Centroid Index (CI): %d and Best Mean Sum-of-Squared Errors (MSE): %.4f\n", result2.centroidIndex, result2.sse / 10000);
        //printf("(Split)Best Centroid Index (CI): %d and best Sum-of-Squared Errors (MSE): %f\n", result3.centroidIndex, result3.sse / 10000000);


        ///////////////
        // Clean up //
        /////////////
        
        //DEBUGGING if (LOGGING == 2) printf("Muistin vapautus\n");
        //K-means
		freeClusteringResult(&result0);
        freeCentroids(&centroids);

		//Repeated k-means
        freeClusteringResult(&bestResult1);
		freeCentroids(&centroids1);

        //Random Swap
		freeClusteringResult(&result2);
		freeCentroids(&centroids2);

        //Split k-means


		//Datapoints
        freeDataPoints(&dataPoints);

		//Ground truth
		freeDataPoints(&groundTruth);
        
        return 0;
    }
}
