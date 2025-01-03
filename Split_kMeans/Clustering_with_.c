#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <float.h>
#include <sys/stat.h>
#include <sys/types.h>

// Credits: Niko Ruohonen, 2024

//Constants for file locations
const char* DATA_FILENAME = "a1.txt";
const char* GT_FILENAME = "a1-ga-cb.txt";
const char* CENTROID_FILENAME = "outputs/centroid.txt"; //TODO: ei käytössä
const char* PARTITION_FILENAME = "outputs/partition.txt"; //TODO: ei käytössä
const char SEPARATOR = ' ';

//for centroids
const int NUM_CENTROIDS = 20;
// S            = 15
// Unbalance    = 8
// A            = 20,35,50
// Birch        = 100
// G2           = 2
// Dim (high)   = 16
// Dim (low)	= 9

// for clustering
const int MAX_ITERATIONS = 1000; // k-means rajoitus
const int MAX_REPEATS = 10; // repeated k-means toistojen lkm, TODO: lopulliseen 100kpl
const int MAX_SWAPS = 1000; // random swap toistojen lkm, TODO: lopulliseen 1000kpl
const size_t MAX_LOOPS = 1; //TODO ei käytössä

// for logging
const int LOGGING = 1; // 1 = basic, 2 = detailed, 3 = debug, TODO: käy kaikki läpi ja tarkista, että käytetään oikeita muuttujia

//////////////
// Structs //
////////////

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
    size_t size;
	//size_t mse; TODO <- tarvitaanko?
} Centroids;

typedef struct
{
    double mse;
    int* partition;
    DataPoint* centroids;
    size_t centroidIndex;
} ClusteringResult;

///////////////
// Memories //
/////////////

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
// TODO: ei käytössä
void freeClusteringResult(ClusteringResult* result, int numCentroids)
{
    if (result == NULL) return;

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

// Function to create a string list
char** createStringList(size_t size)
{
    char** list = malloc(size * sizeof(char*));
    if (list == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < size; ++i)
    {
        list[i] = malloc(256 * sizeof(char));
        if (list[i] == NULL)
        {
            fprintf(stderr, "Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }
    }

    return list;
}

// Function to free a string list
void freeStringList(char** list, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        free(list[i]);
    }
    free(list);
}

//////////////
// Helpers //
////////////

// Function to calculate the squared Euclidean distance between two data points
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

    //TODO: pitäisikö heittää tiedoston alkuun ja disabloida kaikki, vaikeuttaa warningien tutkimista
    //for disabling the CS6387 warning
    //safe to use as we actually check for NULL earlier
    //_Analysis_assume_(file != NULL);

    char firstLine[512];
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

    char line[512];
    while (fgets(line, sizeof(line), file))
    {
        size_t attributeAllocatedSize = 6;

        DataPoint point;
        point.attributes = malloc(sizeof(double) * attributeAllocatedSize);
        handleMemoryError(point.attributes);
        point.dimensions = 0;
        point.partition = -1;

        char* context = NULL;
        char* token = strtok_s(line, " \t\r\n", &context);
        while (token != NULL)
        {
            if (point.dimensions == attributeAllocatedSize)
            {
                attributeAllocatedSize = attributeAllocatedSize > 0 ? attributeAllocatedSize * 2 : 1;
                double* temp = realloc(point.attributes, sizeof(double) * attributeAllocatedSize);
                handleMemoryError(temp);
                point.attributes = temp;
            }
            //printf("Token: %s\n", token);
            point.attributes[point.dimensions++] = strtod(token, NULL); //atoi(token); //strtod(token, NULL); //TODO: strtod = double, atoi = int
			token = strtok_s(NULL, " \t\r\n", &context); //TODO: spaced " ", tabs "\t", newlines "\n"
        }

        //printf("\n", token);

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
        for (size_t i = 0; i < 2; ++i)
        {
            printf("Data point %zu attributes: ", i);
            for (size_t j = 0; j < dataPoints.points[i].dimensions; ++j)
            {
                printf("%.0f ", dataPoints.points[i].attributes[j]);
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
    
    if (LOGGING == 2)
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

// Function to write the centroids to a file
void writeCentroidsToFile(const char* filename, Centroids* centroids)
{
    FILE* centroidFile = fopen(filename, "w");
    if (centroidFile == NULL)
    {
        handleFileError(filename);
        return;
    }

    for (size_t i = 0; i < centroids->size; ++i) // Loop through each centroid
    {
        for (size_t j = 0; j < centroids->points[i].dimensions; ++j) // Loop through each dimension of a centroid
        {
            fprintf(centroidFile, "%f ", centroids->points[i].attributes[j]);
        }
        fprintf(centroidFile, "\n"); // New line after each centroid's attributes
    }

    fclose(centroidFile);
}

// Function to write data point partitions to a file
// TODO: refaktoroi käyttämäänn resulttia DataPointsin sijaan
void writeDataPointPartitionsToFile(const char* filename, DataPoints* dataPoints)
{
    FILE* file = fopen(filename, "w");
    if (file == NULL)
    {
        handleFileError(filename);
        return;
    }

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        fprintf(file, "%d\n", dataPoints->points[i].partition);
    }

    fclose(file);
}

//Funtion to create a deep copy of a data point
void deepCopyDataPoint(DataPoint* destination, DataPoint* source)
{
    destination->dimensions = source->dimensions;
    //destination->partition = source->partition; TODO tarvitaanko?
    destination->attributes = malloc(source->dimensions * sizeof(double));
    handleMemoryError(destination->attributes);
    memcpy(destination->attributes, source->attributes, source->dimensions * sizeof(double));
}

//Funtion to create deep copies of data points
void deepCopyDataPoints(DataPoint* destination, DataPoint* source, size_t size)
{
	for (size_t i = 0; i < size; ++i)
	{
		destination[i].dimensions = source[i].dimensions;
		destination[i].attributes = malloc(source[i].dimensions * sizeof(double));
		handleMemoryError(destination[i].attributes);
		memcpy(destination[i].attributes, source[i].attributes, source[i].dimensions * sizeof(double));
	}
}

//Funtion to copy centroids
//TODO: vanhaa koodia, ei käytössä
void deepCopyCentroids(Centroids* source, Centroids* destination, size_t numCentroids)
{
    destination->size = source->size;
    destination->points = malloc(numCentroids * sizeof(DataPoint));
    handleMemoryError(destination->points);
    for (size_t i = 0; i < numCentroids; ++i)
    {
        destination->points[i].dimensions = source->points[i].dimensions;
        deepCopyDataPoint(&destination->points[i], &source->points[i]);
    }
}

// Function to print all information about the centroids
void printCentroidsInfo(const Centroids* centroids)
{
    // Print the size of the centroids set
    printf("Number of centroids: %zu\n", centroids->size);

    // Loop through all centroids and print their details
    for (size_t i = 0; i < centroids->size; ++i)
    {
        DataPoint* centroid = &centroids->points[i];

        // Print the centroid index and its dimensions
        printf("Centroid %zu (dimensions: %zu) attributes: ", i, centroid->dimensions);

        // Print each attribute of the centroid
        for (size_t j = 0; j < centroid->dimensions; ++j)
        {
            printf("%f ", centroid->attributes[j]);
        }
        printf("\n");
    }
    printf("\n");
}

// Function to print partition assignments for data points with partition >= cMax
void printDataPointsPartitions(const DataPoints* dataPoints, size_t cMax)
{
    printf("Data Points Partition Assignments (Partition <= %zu):\n", cMax);
    printf("Data Points size: %zu\n", dataPoints->size);
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition >= cMax)
        {
            printf("Data Point %zu: Partition %d\n", i, dataPoints->points[i].partition);
        }
    }
    printf("\n");
}

// Function to reset all partitions to 0
void resetPartitions(DataPoints* dataPoints)
{
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        dataPoints->points[i].partition = 0;
    }
}

// Function to write results to a file
void writeResultsToFile(const char* filename, double ciSum, double mseSum, double timeSum, double successRate, int numCentroids, char* header, size_t loopCount, size_t scaling, char* outputDirectory)
{
    char outputFilePath[256];
    snprintf(outputFilePath, sizeof(outputFilePath), "%s/%s", outputDirectory, filename);

    FILE* file = fopen(outputFilePath, "a"); // "a" = appends
    if (file == NULL)
    {
        fprintf(stderr, "Error: Cannot open file %s for writing\n", filename);
        return;
    }

    fprintf(file, "%s\n", header);
    fprintf(file, "Average CI: %.2f and MSE: %.2f\n", ciSum / loopCount, mseSum / loopCount / scaling);
    fprintf(file, "Relative CI: %.2f\n", ciSum / loopCount / numCentroids);
    fprintf(file, "Average time taken: %.2f seconds\n", timeSum / loopCount);
    fprintf(file, "Success rate: %.2f%%\n\n", successRate / loopCount * 100);

    fclose(file);
    if(LOGGING == 3) printf("Metrics written to file: %s\n", filename);
}

// Function to create a unique directory based on the current date and time
void createUniqueDirectory(char* outputDirectory, size_t size)
{
    time_t now = time(NULL);
    struct tm* t = localtime(&now);

    strftime(outputDirectory, size, "outputs/%Y-%m-%d_%H-%M-%S", t);

    mkdir(outputDirectory, 0777);
}

/////////////////
// Clustering //
///////////////

// Function to choose random data points to be centroids
//TODO: refaktoroi käyttämään Centroideja?
void generateRandomCentroids(size_t numCentroids, DataPoints* dataPoints, DataPoint* centroids)
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
        size_t j = i + rand() % (dataPoints->size - i);
        size_t temp = indices[i];
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

        sse += calculateEuclideanDistance(&dataPoints->points[i], &centroids->points[cIndex]);
    }

    return sse;
}

//calculate MSE (divider is the size of the dataPoints
double calculateMSE(DataPoints* dataPoints, Centroids* centroids)
{
    double sse = calculateSSE(dataPoints, centroids);

    double mse = sse / (dataPoints->size * dataPoints->points[0].dimensions);

    return mse;
}

//calculate MSE (divider is the size of the whole data set)
double calculateMSEWithSize(DataPoints* dataPoints, Centroids* centroids, size_t size)
{
    double sse = calculateSSE(dataPoints, centroids);

    double mse = sse / (size * dataPoints->points[0].dimensions);

    return mse;
}

// calculate MSE of a specific cluster //TODO: mieti voiko toteuttaa järkevämmin kuin omana funkkarina joka duplikoi calculateMSE
double calculateClusterMSE(DataPoints* dataPoints, Centroids* centroids, size_t clusterLabel)
{
    double sse = 0.0;
    size_t count = 0;
    size_t dimensions = dataPoints->points[0].dimensions;

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterLabel)
        {
            sse += calculateEuclideanDistance(&dataPoints->points[i], &centroids->points[clusterLabel]);
            count++;
        }
    }

    if (count == 0) //TODO: Debugger helpper
    {
        return 0.0;
    }

	//TODO: count vai dataPoints->size? Eli datapisteiden määrä clusterissa vai koko datasetissä?
    double mse = sse;// (dataPoints->size * dimensions);
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

    size_t nearestCentroidId = SIZE_MAX;
    double minDistance = DBL_MAX;
	double newDistance = DBL_MAX;

    for (size_t i = 0; i < targetCentroids->size; ++i)
    {
        newDistance = calculateSquaredEuclideanDistance(queryPoint, &targetCentroids->points[i]);
		
        if (newDistance < minDistance)
        {
            minDistance = newDistance;
            nearestCentroidId = i;
        }
    }

    return nearestCentroidId;
}

// Function for optimal partitioning
void partitionStep(DataPoints* dataPoints, Centroids* centroids)
{
    /* DEBUGGING    
    if (dataPoints->size == 0 || centroids->size == 0)
    {
        fprintf(stderr, "Error: Cannot perform optimal partition with empty data or centroids\n");
        exit(EXIT_FAILURE);
    }*/

	size_t nearestCentroidId = SIZE_MAX;

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        nearestCentroidId = findNearestCentroid(&dataPoints->points[i], centroids);
        dataPoints->points[i].partition = nearestCentroidId;
    }
}

// Function to perform the centroid step in k-means
void centroidStep(Centroids* centroids, const DataPoints* dataPoints)
{
    size_t numClusters = centroids->size;
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

    // Accumulate sums and counts for each cluster
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
            if(LOGGING == 2) fprintf(stderr, "Warning: Cluster %zu has no points assigned.\n", clusterLabel);
        }*/

        free(sums[clusterLabel]);
    }

    free(sums);
    free(counts);
}

// Function to count orphans (helper for calculateCentroidIndex)
size_t countOrphans(Centroids* centroids1, Centroids* centroids2)
{
    size_t countFrom1to2 = 0;
    size_t* closest = calloc(centroids2->size, sizeof(size_t));
    handleMemoryError(closest);

    for (size_t i = 0; i < centroids1->size; ++i)
    {
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
    if (LOGGING == 3)
    {
        printf("centroids1: %zu AND 2: %zu\n\n", centroids1->size, centroids2->size);
        printf("centroids1: %f AND 2: %f\n\n", centroids1->points->attributes[0], centroids2->points->attributes[0]);
    }
    
    size_t countFrom1to2 = countOrphans(centroids1, centroids2);
    size_t countFrom2to1 = countOrphans(centroids2, centroids1);

    if (LOGGING == 2)
    {
        printf("Count from 1 to 2: %zu AND 2 to 1: %zu\n\n", countFrom1to2, countFrom2to1);
    }

    return (countFrom1to2 > countFrom2to1) ? countFrom1to2 : countFrom2to1;
}

// Function to run the k-means algorithm
ClusteringResult runKMeans(DataPoints* dataPoints, int iterations, Centroids* centroids, Centroids* groundTruth) //TODO: groundTruth ei tarvita täällä, niin pois?
{
    double bestMse = DBL_MAX;
    double mse = DBL_MAX;

    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        // Partition step
        partitionStep(dataPoints, centroids);

        // Centroid step
        centroidStep(centroids, dataPoints);
        
        // MSE Calculation
        //TODO: mse vai SSE? Tällä hetkellä SSE vaikka muuttujat ovat mse
        mse = calculateSSE(dataPoints, centroids);

        if (LOGGING == 2)
        {
			size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
            printf("(runKMeans)After iteration %zu: CI = %zu and MSE = %.5f\n", iteration + 1, centroidIndex, mse / 10000);
        }


        if (mse < bestMse)
        {
			//if (LOGGING == 2) printf("Best MSE so far: %.5f\n", mse / 10000);
            bestMse = mse;
        }
        else
        {
            break; //TODO: break toimii, mutta on aika ruma ratkaisu (iteration = iterations?)
        }
    }

    ClusteringResult result;
    result.mse = bestMse;
    return result;
}

// Function to perform random swap
void randomSwap(DataPoints* dataPoints, Centroids* centroids, Centroids* groundTruth, ClusteringResult* bestResult)
{
    double bestMse = DBL_MAX;
    size_t kMeansIterations = 2;
    size_t numCentroids = centroids->size;
    size_t dimensions = centroids->points[0].dimensions;

    size_t totalAttributes = numCentroids * dimensions;

    double* backupAttributes = malloc(totalAttributes * sizeof(double));
    handleMemoryError(backupAttributes);

    for (size_t i = 0; i < MAX_SWAPS; ++i)
    {
        //Backup
        size_t offset = 0;
        for (size_t j = 0; j < numCentroids; ++j)
        {
            memcpy(&backupAttributes[offset], centroids->points[j].attributes, dimensions * sizeof(double));
            offset += dimensions;
        }

        //Swap
        size_t randomCentroidId = rand() % centroids->size;
        size_t randomDataPointId = rand() % dataPoints->size;
        memcpy(centroids->points[randomCentroidId].attributes, dataPoints->points[randomDataPointId].attributes, dimensions * sizeof(double));
        
        //K-means
        ClusteringResult result = runKMeans(dataPoints, kMeansIterations, centroids, groundTruth);

        //If 1) MSE improves, we keep the change
        //if not, we reverse the swap
        if (result.mse < bestResult->mse)
        {
            if (LOGGING == 2)
            {
				result.centroidIndex = calculateCentroidIndex(centroids, groundTruth);
                printf("(RS) Round %zu: Best Centroid Index (CI): %zu and Best Sum-of-Squared Errors (MSE): %.5f\n", i + 1, result.centroidIndex, result.mse / 10000);
            }
            
            bestResult->mse = result.mse;
            deepCopyDataPoints(bestResult->centroids, centroids->points, centroids->size);
        }
        else
        {
            offset = 0;
            for (size_t j = 0; j < numCentroids; ++j)
            {
                memcpy(centroids->points[j].attributes, &backupAttributes[offset], dimensions * sizeof(double));
                offset += dimensions;
            }
        }
    }

    free(backupAttributes);
}

// Function to run split k-means, k-means intra-cluster
void splitClusterIntraCluster(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t localMaxIterations, Centroids* groundTruth)
{
    size_t clusterSize = 0;
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterToSplit)
        {
            clusterSize++;
        }
    }

    if (clusterSize < 2) //TODO: tarvitaanko? <- "turhat" iffit hidastaa
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
    size_t c1 = rand() % clusterSize;
    size_t c2 = c1;
    while (c2 == c1)
    {
        c2 = rand() % clusterSize;
    }
    size_t datapoint1 = clusterIndices[c1];
    size_t datapoint2 = clusterIndices[c2];

    // Initialize local centroids
    Centroids localCentroids;
    localCentroids.size = 2;
    localCentroids.points = malloc(2 * sizeof(DataPoint));
    handleMemoryError(localCentroids.points);
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
    ClusteringResult localResult = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, groundTruth);

    // Update partitions
    for (size_t i = 0; i < clusterSize; ++i)
    {
        size_t originalIndex = clusterIndices[i];
        dataPoints->points[originalIndex].partition = (pointsInCluster.points[i].partition == 0) ? clusterToSplit : centroids->size;
    }

    // Update centroids
    // TODO: deepcopyjen sijaan suoraan memcpy?
    //#1
    deepCopyDataPoint(&centroids->points[clusterToSplit], &localCentroids.points[0]);
    //memcpy(&centroids->points[clusterToSplit].attributes, &localCentroids.points[0].attributes, localCentroids.points[0].dimensions * sizeof(double));


    //#2
    centroids->size++;
    centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint));
    handleMemoryError(centroids->points);
    deepCopyDataPoint(&centroids->points[centroids->size - 1], &localCentroids.points[1]);
    //memcpy(&centroids->points[centroids->size - 1].attributes, &localCentroids.points[1].attributes, localCentroids.points[1].dimensions * sizeof(double));


    // Cleanup
    free(clusterIndices);
    free(pointsInCluster.points);
    free(localCentroids.points);
}

// Function to run split k-means, k-means globally
void splitClusterGlobal(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t globalMaxIterations, Centroids* groundTruth)
{
    size_t clusterSize = 0;

    // Count the number of points in the cluster to be split
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterToSplit)
        {
            clusterSize++;
        }
    }

    // If there are fewer than 2 points, splitting is not possible
    if (clusterSize < 2)
    {
        return;
    }

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
    size_t c1 = rand() % clusterSize;
    size_t c2 = c1;
    while (c2 == c1)
    {
        c2 = rand() % clusterSize;
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
    deepCopyDataPoint(&centroids->points[centroids->size - 1], &dataPoints->points[datapoint2]);

    // Run global k-means, this time including the new centroids
    ClusteringResult globalResult = runKMeans(dataPoints, globalMaxIterations, centroids, groundTruth);

    // Cleanup
    free(clusterIndices);
    free(globalResult.partition); // assuming runKMeans dynamically allocates the partition array
}

// Function to run local repartitioning
// TODO: vain split k-means, haluanko myös random swappiin?
// TODO: kesken
void localRepartition(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, bool* clustersAffected)
{
    size_t newClusterIndex = centroids->size - 1;

    /* TODO: Q: Kysy/selvitä, että tarvitaanko tätä? Oma oletus on, että ei tarvita
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
    }*/

    // old clusters -> new clusters
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        size_t currentCluster = dataPoints->points[i].partition;

        // skip new cluster -> old cluster
        if (currentCluster == clusterToSplit || currentCluster == newClusterIndex)
            continue;

        size_t nearestCentroid = findNearestCentroid(&dataPoints->points[i], centroids);

        if (nearestCentroid == newClusterIndex || nearestCentroid == clusterToSplit)
        {
            if (LOGGING == 3) printf("Local repartition: Success, point %zu is reassigned\n", i);

            clustersAffected[currentCluster] = true;    // Mark old as true
            dataPoints->points[i].partition = nearestCentroid;
        }
    }

    if(LOGGING == 3) printf("Local repartition is over\n\n");
}

// Function to check Mse drop
//todo: centroids turha tänne?
double tentativeMseDrop(DataPoints* dataPoints, Centroids* centroids, size_t clusterLabel, size_t localMaxIterations, double originalClusterMSE)
{
    size_t clusterSize = 0;
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterLabel)
        {
            clusterSize++;
        }
    }

    if (clusterSize < 2) //TODO sama kuin splitClusteri, jos turha niin poista
    {
        return 0.0;
    }

    DataPoints pointsInCluster;
    pointsInCluster.size = clusterSize;
    pointsInCluster.points = malloc(clusterSize * sizeof(DataPoint));
    handleMemoryError(pointsInCluster.points);

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
    size_t idx1 = rand() % clusterSize;
    size_t idx2 = idx1;
    while (idx2 == idx1)
    {
        idx2 = rand() % clusterSize;
    }

    Centroids localCentroids;
    localCentroids.size = 2;
    localCentroids.points = malloc(2 * sizeof(DataPoint));
    handleMemoryError(localCentroids.points);
    deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[idx1]);
    deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[idx2]);

    // k-means
    ClusteringResult localResult = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, NULL);

    // Calculate combined MSE of the two clusters
	//WithSize vai ilman? Eli koko datasetin mukaan vai pelkästään clusterin mukaan?
    double newClusterMSE = calculateMSEWithSize(&pointsInCluster, &localCentroids, dataPoints->size);

    // Calculate the MSE drop
    double mseDrop = originalClusterMSE - newClusterMSE;

    // Cleanup TODO: tämä laajempi vai riittääkö se mikä on splitClusterissa?
    for (size_t i = 0; i < pointsInCluster.size; ++i)
    {
        free(pointsInCluster.points[i].attributes);
    }
    free(pointsInCluster.points);
    for (size_t i = 0; i < localCentroids.size; ++i)
    {
        free(localCentroids.points[i].attributes);
    }
    free(localCentroids.points);

    return mseDrop;
}

// Function to check and return mse drops
//todo: centroids turha tänne?
ClusteringResult tentativeSplitterForBisecting(DataPoints* dataPoints, Centroids* centroids, size_t clusterLabel, size_t localMaxIterations)
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
        printf("Not enough points to split the cluster\n");
        exit(EXIT_FAILURE);
    }


    DataPoints pointsInCluster;
    pointsInCluster.size = clusterSize;
    pointsInCluster.points = malloc(clusterSize * sizeof(DataPoint));
    handleMemoryError(pointsInCluster.points);

    size_t index = 0;
    for (size_t i = 0; i < dataPoints->size; ++i) //todo: tämän loopin voi ehkä yhdistää ylemmän kanssa? ps. tai ehkä ei koska clusterSize?
    {
        if (dataPoints->points[i].partition == clusterLabel)
        {
            deepCopyDataPoint(&pointsInCluster.points[index], &dataPoints->points[i]);
            index++;
        }
    }

    // Random centroids
    size_t idx1 = rand() % clusterSize;
    size_t idx2 = idx1;
    while (idx2 == idx1)
    {
        idx2 = rand() % clusterSize;
    }

    Centroids localCentroids;
    localCentroids.size = 2;
    localCentroids.points = malloc(2 * sizeof(DataPoint));
    handleMemoryError(localCentroids.points);
    deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[idx1]);
    deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[idx2]);

    
    ClusteringResult localResult;    
    localResult.mse = DBL_MAX;

    // k-means
    localResult = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, NULL);

    // Calculate combined MSE of the two clusters
    //WithSize vai ilman? Eli koko datasetin mukaan vai pelkästään clusterin mukaan?
    double newClusterMSE = calculateMSEWithSize(&pointsInCluster, &localCentroids, dataPoints->size);

    // Calculate the MSE drop
    localResult.mse = newClusterMSE;

    localResult.centroids = malloc(2 * sizeof(DataPoint));
    handleMemoryError(localResult.centroids);
    for (int i = 0; i < 2; ++i)
    {
        localResult.centroids[i].attributes = malloc(dataPoints->points[1].dimensions * sizeof(double));
        handleMemoryError(localResult.centroids[i].attributes);
    }
    localResult.partition = malloc(dataPoints->size * sizeof(int));
    handleMemoryError(localResult.partition);
    localResult.centroidIndex = INT_MAX;
	
    localCentroids.size = 2;
	deepCopyDataPoint(&localResult.centroids[0], &localCentroids.points[0]);
    deepCopyDataPoint(&localResult.centroids[1], &localCentroids.points[1]);

    //localResult.centroids = localCentroids.points;

    // Cleanup TODO: tämä laajempi vai riittääkö se mikä on splitClusterissa?
    for (size_t i = 0; i < pointsInCluster.size; ++i)
    {
        free(pointsInCluster.points[i].attributes);
    }
    free(pointsInCluster.points);
    for (size_t i = 0; i < localCentroids.size; ++i)
    {
        free(localCentroids.points[i].attributes);
    }
    free(localCentroids.points);    

    return localResult;
}


// Function to run the split k-means algorithm with random splitting
ClusteringResult runRandomSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, Centroids* groundTruth)
{
    size_t localMaxIterations = 2;

    partitionStep(dataPoints, centroids);
    centroidStep(centroids, dataPoints);

    while(centroids->size < maxCentroids)
    {
		//Versio 1, we choose randomly
        size_t clusterToSplit = rand() % centroids->size;

        splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, localMaxIterations, groundTruth);

        if (LOGGING == 2)
        {
            size_t ci = calculateCentroidIndex(centroids, groundTruth);
            double mse = calculateMSE(dataPoints, centroids);
            printf("(RandomSplit) Number of centroids: %zu, CI: %zu, and MSE: %.5f \n", centroids->size, ci, mse / 10000);
        }
    }	

    //TODO: globaali k-means  
    ClusteringResult finalResult = runKMeans(dataPoints, MAX_ITERATIONS, centroids, groundTruth);

    return finalResult;
}

// Function to run the split k-means algorithm with tentative splitting (choosing to split the one that reduces the MSE the most)
// splitType 0 = intra-cluster, 1 = global, 2 = local
ClusteringResult runMseSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, Centroids* groundTruth, int splitType)
{
    //TODO: entä jos globaalia ei rajoittaisi, olisiko tullut paremmat tulokset???????
    size_t iterations = splitType == 0 ? MAX_ITERATIONS : splitType == 1 ? 4 : 1000; //TODO: pohdi tarkemmat arvot, globaaliin 5 näyttää toimivan hyvin

    double* clusterMSEs = malloc(maxCentroids * sizeof(double));
    handleMemoryError(clusterMSEs);

    size_t* MseDrops = calloc(maxCentroids, sizeof(size_t));
    handleMemoryError(MseDrops);

    bool* clustersAffected = calloc(maxCentroids*2, sizeof(bool));
    handleMemoryError(clustersAffected);

    //Only 1 cluster, so no need for decision making
    size_t initialClusterToSplit = 0;
    splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, iterations, groundTruth);

    for (size_t i = 0; i < centroids->size; ++i)
    {
        clusterMSEs[i] = calculateClusterMSE(dataPoints, centroids, i); //TODO: tarvitaanko omaa rakennetta?
        MseDrops[i] = tentativeMseDrop(dataPoints, centroids, i, iterations, clusterMSEs[i]);
    }

    while (centroids->size < maxCentroids)
    {
		//Versio 2, we choose the one that reduces the MSE the most
        size_t clusterToSplit = 0;
        double maxMseDrop = MseDrops[0];

        for (size_t i = 1; i < centroids->size; ++i)
        {
            if (MseDrops[i] > maxMseDrop)
            {
                maxMseDrop = MseDrops[i];
                clusterToSplit = i;
            }
        }

        if (maxMseDrop <= 0.0) //TODO: tarvitaanko? nopeuttaa jos ei aina tarkisteta
        {
            break;
        }

        if (LOGGING == 2 && splitType == 3)
        {
            printDataPointsPartitions(dataPoints, centroids->size);
        }

        if(splitType == 0) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, iterations, groundTruth);
		else if (splitType == 1) splitClusterGlobal(dataPoints, centroids, clusterToSplit, iterations, groundTruth);
		else if (splitType == 2) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, iterations, groundTruth);

        if (splitType == 0)
        {
            // Recalculate MSE for the affected clusters
            clusterMSEs[clusterToSplit] = calculateClusterMSE(dataPoints, centroids, clusterToSplit);
            clusterMSEs[centroids->size - 1] = calculateClusterMSE(dataPoints, centroids, centroids->size - 1);

            // Update MseDrops for the affected clusters
            MseDrops[clusterToSplit] = tentativeMseDrop(dataPoints, centroids, clusterToSplit, iterations, clusterMSEs[clusterToSplit]);
            MseDrops[centroids->size - 1] = tentativeMseDrop(dataPoints, centroids, centroids->size - 1, iterations, clusterMSEs[centroids->size - 1]);
        }
        else if (splitType == 1)
        {            
            for (size_t i = 0; i < centroids->size; ++i)
            {
                clusterMSEs[i] = calculateClusterMSE(dataPoints, centroids, i);
                MseDrops[i] = tentativeMseDrop(dataPoints, centroids, i, iterations, clusterMSEs[i]);
            }
        }
        else if(splitType == 2)
        {            
            localRepartition(dataPoints, centroids, clusterToSplit, clustersAffected);
            
			clustersAffected[clusterToSplit] = true;
			clustersAffected[centroids->size - 1] = true;

			// Recalculate MseDrop for affected clusters (old and new)
            for (size_t i = 0; i < centroids->size; ++i)
            {
                if (clustersAffected[i])
                {
                    if (LOGGING == 2) printf("Affected cluster: %zu\n", i);

                    clusterMSEs[i] = calculateClusterMSE(dataPoints, centroids, i);
                    MseDrops[i] = tentativeMseDrop(dataPoints, centroids, i, iterations, clusterMSEs[i]);
                }
            }

            memset(clustersAffected, 0, (maxCentroids * 2) * sizeof(bool));
        }

        if (LOGGING == 2 && splitType == 2)
        {
            size_t ci = calculateCentroidIndex(centroids, groundTruth);
            double mse = calculateMSE(dataPoints, centroids);
            printf("(MseSplit) Number of centroids: %zu, CI: %zu, and MSE: %.5f \n", centroids->size, ci, mse / 10000);
        }


        if (LOGGING == 2 && splitType == 2) printf("Round over\n\n");
    }

    free(clusterMSEs);
	free(MseDrops);
	free(clustersAffected);

    //TODO: globaali k-means  
    ClusteringResult finalResult = runKMeans(dataPoints, MAX_ITERATIONS, centroids, groundTruth);

    return finalResult;
}

// Function to run the Bisecting k-means algorithm
ClusteringResult runBisectingKMeans(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, Centroids* groundTruth)
{
    //Variables
	size_t bisectingIterations = 5;
    
    double* SseList = malloc(maxCentroids * sizeof(size_t));
    handleMemoryError(SseList);

    ClusteringResult bestResult;
    /*bestResult.centroids = malloc(2 * sizeof(DataPoint));
    handleMemoryError(bestResult.centroids);
    for (int i = 0; i < 2; ++i)
    {
        bestResult.centroids[i].attributes = malloc(dataPoints->points[0].dimensions * sizeof(double));
        handleMemoryError(bestResult.centroids[i].attributes);
    }
    bestResult.partition = malloc(dataPoints->size * sizeof(int));
    handleMemoryError(bestResult.partition);
    bestResult.centroidIndex = INT_MAX;
    */
    bestResult.mse = DBL_MAX;

    DataPoint newCentroid1;
    newCentroid1.attributes = malloc(sizeof(double) * dataPoints->points[0].dimensions);
    handleMemoryError(newCentroid1.attributes);
    newCentroid1.dimensions = dataPoints->points[0].dimensions;
    newCentroid1.partition = -1;

    DataPoint newCentroid2;
    newCentroid2.attributes = malloc(sizeof(double) * dataPoints->points[0].dimensions);
    handleMemoryError(newCentroid2.attributes);
    newCentroid2.dimensions = dataPoints->points[0].dimensions;
    newCentroid2.partition = -1;

    DataPoint ogCentroid;
    ogCentroid.attributes = malloc(sizeof(double) * dataPoints->points[0].dimensions);
    handleMemoryError(ogCentroid.attributes);
    ogCentroid.dimensions = dataPoints->points[0].dimensions;
    ogCentroid.partition = -1;

    size_t* ogPartitions = malloc(dataPoints->size * sizeof(size_t));
	handleMemoryError(ogPartitions);

	//Step 0: Only 1 cluster, so no need for decision making
    size_t initialClusterToSplit = 0;
    splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, MAX_ITERATIONS, groundTruth);

	SseList[0] = calculateClusterMSE(dataPoints, centroids, 0);
    SseList[1] = calculateClusterMSE(dataPoints, centroids, 1);

    //Repeat until we have K clusters
    for (int i = 2; centroids->size < maxCentroids; ++i)
    {
        if (LOGGING == 1) printf("(BKM) Round %d: Centroids: %zu\n", i-1, centroids->size);

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
        
        //TODO: ota talteen alkuperäinen centroid        
        deepCopyDataPoint(&ogCentroid, &centroids->points[clusterToSplit]);

        //TODO: myös alkuperäinen partition?
        for (size_t j = 0; j < dataPoints->size; ++j)
        {
            ogPartitions[j] = dataPoints->points[j].partition;
        }

		//Repeat for a set number of iterations
        for (int j = 0; j < bisectingIterations; ++j)
        {
			//options 2: eli vain lasketaan, mutta ei suoriteta
            //tämä kuulostaa paremmalta
			ClusteringResult curr = tentativeSplitterForBisecting(dataPoints, centroids, clusterToSplit, 1000);

            //if (LOGGING == 2) printf("(RKM) Round %d: Latest Centroid Index (CI): %zu and Latest Mean Sum-of-Squared Errors (MSE): %.4f\n", repeat, result1.centroidIndex, result1.mse / 10000);

			// If the result is better than the best result so far, update the best result
            if (curr.mse < bestResult.mse)
            {
                if (LOGGING == 1) printf("(from inner) Round %d\n", j);

                // Save the SSE
                bestResult.mse = curr.mse;
                Centroids tempCentroids; //Debug helper
                tempCentroids.size = 2; // Number of centroids in curr <- this is why we needed debug helper
                tempCentroids.points = curr.centroids;
                // Print centroids using the existing function
                printCentroidsInfo(&tempCentroids); 
                
                // Save the two new centroids
				deepCopyDataPoint(&newCentroid1, &curr.centroids[0]);
                deepCopyDataPoint(&newCentroid2, &curr.centroids[1]);
				//free(curr.centroids);
				freeClusteringResult(&curr, 2);
                // Save the partition (of just the new clusters?)
                //ei ehkä tarvi, jos vaan partition step loppuun?
                /*for (size_t i = 0; i < dataPoints->size; ++i) //TODO: halutaanko tätä loopata timerin sisällä?
                {
                    bestResult.partition[i] = dataPoints->points[i].partition;
                }*/
            }

            //TODO: palauta alkuperäinen centroid
            /*deepCopyDataPoint(&centroids->points[clusterToSplit], &ogCentroid);

            //TODO: palauta partition? <- voisiko tämän skipata jotenkin?
			for (size_t i = 0; i < dataPoints->size; ++i)
			{
				dataPoints->points[i].partition = ogPartitions[i];
			}*/
                        
            //TODO: poista uusi centroid
            //TODO: Tarvitaanko muuta kuin size pienennys?
            //free(centroids->points[centroids->size - 1].attributes);
            //centroids->points[centroids->size - 1].attributes = NULL;
			//centroids->size--;
        }

		//Choose the best cluster split
        deepCopyDataPoint(&centroids->points[clusterToSplit], &newCentroid1);
        deepCopyDataPoint(&centroids->points[centroids->size], &newCentroid2);
		centroids->size++;
        
        if (LOGGING == 1) printf("(from outer) Round %d\n", i);
        printCentroidsInfo(centroids);

        partitionStep(dataPoints, centroids);

		//Step 3: Update the SSE list
        // Recalculate MSE for the affected clusters
        SseList[clusterToSplit] = calculateClusterMSE(dataPoints, centroids, clusterToSplit);
        SseList[centroids->size - 1] = calculateClusterMSE(dataPoints, centroids, centroids->size - 1);

		writeCentroidsToFile("outputs/centroids.txt", centroids);
		writeDataPointPartitionsToFile("outputs/partitions.txt", dataPoints);
        //DEBUGGING if(LOGGING == 3) printf("round: %d\n", repeat);
        //centroids->size++;

        bestResult.mse = DBL_MAX;
    }

	//Step 4: Run the final k-means
    ClusteringResult finalResult = runKMeans(dataPoints, MAX_ITERATIONS, centroids, groundTruth);
    return finalResult;
}

///////////
// Main //
/////////

//TODO käy läpi size_t muuttujat, int on parempi jos voi olla negatiivinen
//TODO disabloidaanko false positive varoitukset?
//TODO: tee consteista paikallisia muuttujia
//TODO: kommentoi kaikki muistintarkastukset pois lopullisesta versiosta <-tehokkuus?
//TODO: "static inline..." sellaisten funktioiden eteen jotka eivät muuta mitään ja joita kutsutaan? Saattaa tehostaa suoritusta
//TODO: partition on int, mutta se voisi olla size_t. Jotta onnistuu niin -1 alustukset pitää käydä läpi
//TODO: credits koodin alkuun
//TODO: koodin palastelu eri tiedostoihin?
//TODO: konsolikysymykset, kuten "do you want to run split k-means?" ja "do you want to run random swap?" jne?
//TODO: komentoriviargumentit, kuten "split k-means" ja "random swap" jne? (eli että voi ajaa suoraan powershellistä)

int main()
{
    size_t size = 15;

    char** datasetList = createStringList(size);
    strcpy_s(datasetList[0], 20, "a1.txt");
    strcpy_s(datasetList[1], 20, "a2.txt");
    strcpy_s(datasetList[2], 20, "a3.txt");
    strcpy_s(datasetList[3], 20, "s1.txt");
    strcpy_s(datasetList[4], 20, "s2.txt");
    strcpy_s(datasetList[5], 20, "s3.txt");
    strcpy_s(datasetList[6], 20, "s4.txt");
    strcpy_s(datasetList[7], 20, "unbalance.txt");
    strcpy_s(datasetList[8], 20, "birch1.txt");
    strcpy_s(datasetList[9], 20, "birch2.txt");
    strcpy_s(datasetList[10], 20, "birch3.txt");
    strcpy_s(datasetList[11], 20, "dim032.txt");
    strcpy_s(datasetList[12], 20, "dim064.txt");
    strcpy_s(datasetList[13], 20, "g2-1-10.txt");
    strcpy_s(datasetList[14], 20, "g2-1-20.txt");


    char** gtList = createStringList(size);
    strcpy_s(gtList[0], 20, "a1-ga-cb.txt");
    strcpy_s(gtList[1], 20, "a2-ga-cb.txt");
    strcpy_s(gtList[2], 20, "a3-ga-cb.txt");
    strcpy_s(gtList[3], 20, "s1-cb.txt");
    strcpy_s(gtList[4], 20, "s2-cb.txt");
    strcpy_s(gtList[5], 20, "s3-cb.txt");
    strcpy_s(gtList[6], 20, "s4-cb.txt");
    strcpy_s(gtList[7], 20, "unbalance-gt.txt");
    strcpy_s(gtList[8], 20, "b1-gt.txt");
    strcpy_s(gtList[9], 20, "b2-gt.txt");
    strcpy_s(gtList[10], 20, "b3-gt.txt");
    strcpy_s(gtList[11], 20, "dim032.txt");
    strcpy_s(gtList[12], 20, "dim064.txt");
    strcpy_s(gtList[13], 20, "g2-1-10-gt.txt");
    strcpy_s(gtList[14], 20, "g2-1-20-gt.txt");


    int kNumList[] = {
    20,   // A1
    35,   // A2
    50,   // A3
    15,   // S1
    15,   // S2
    15,   // S3
    15,   // S4
    8,    // Unbalance    
    100,  // Birch1
    100,  // Birch2
    100,  // Birch3
    16,   // Dim (high)
    16,   // Dim (high)
    2,    // G2
    2,    // G2
    9     // Dim (low)
    };

    char outputDirectory[256];
    createUniqueDirectory(outputDirectory, sizeof(outputDirectory));

    for (size_t i = 5; i < 6; ++i)
    {
        int maxIterations = MAX_ITERATIONS;//TODO tarkasta kaikki, että käytetään oikeita muuttujia
        int numCentroids = kNumList[i];
        char* fileName = datasetList[i];
        char* dataFile[256];
        snprintf(dataFile, sizeof(dataFile), "data/%s", fileName);
		char* gtName = gtList[i];
        char* gtFile[256];
        snprintf(gtFile, sizeof(gtFile), "gt/%s", gtName);

        size_t loopCount = 100;
        size_t scaling = 10000;

        // Seeding the random number generator
        srand((unsigned int)time(NULL));

        printf("Starting the process\n");
        printf("File name: %s\n", dataFile);

        int numDimensions = getNumDimensions(dataFile);

        if (numDimensions > 0)
        {
            printf("Number of dimensions in the data: %d\n", numDimensions);

            DataPoints dataPoints = readDataPoints(dataFile);
            printf("Dataset size: %zu\n", dataPoints.size);

            printf("Number of clusters in the data: %d\n", numCentroids);

            Centroids groundTruth = readCentroids(gtFile);

            printf("Number of loops: %zu\n\n", loopCount);

            //////////////
            // K-means //
            ////////////

            double mseSum = 0;
            double ciSum = 0;
            double timeSum = 0;
            double successRate = 0;

            clock_t start = clock();
            clock_t end = clock();
            double duration = 0;
            
            printf("K-means\n");

            for (int i = 0; i < loopCount; i++)
            {
                Centroids centroids;
                centroids.size = numCentroids;
                centroids.points = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(centroids.points); //TODO: pitäisikö lopullisesta versiosta kommentoida nämä pois? Helpottavat lähinnä debuggausta
                for (int i = 0; i < numCentroids; ++i)
                {
                    centroids.points[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(centroids.points[i].attributes);
                    centroids.points[i].dimensions = numDimensions; //TODO tarkista tehdäänkö muualla
                }

                start = clock();

                generateRandomCentroids(numCentroids, &dataPoints, centroids.points);

                if (LOGGING == 2)
                {
                    size_t centroidIndex = calculateCentroidIndex(&centroids, &groundTruth);
                    printf("(K-means)Initial Centroid Index (CI): %zu\n", centroidIndex);
                }

                ClusteringResult result0 = runKMeans(&dataPoints, maxIterations, &centroids, &groundTruth);
                //DEBUGGING if(LOGGING == 2) printf("(K-means)Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (MSE): %.4f\n", result0.centroidIndex, result0.mse / 10000000);

                end = clock();
                duration = ((double)(end - start)) / CLOCKS_PER_SEC;

                if (LOGGING == 3) printf("(K-means)Time taken: %.2f seconds\n\n", duration);

                result0.centroidIndex = calculateCentroidIndex(&centroids, &groundTruth);

                result0.centroids = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(result0.centroids);
                deepCopyDataPoints(result0.centroids, centroids.points, numCentroids);

                result0.partition = malloc(dataPoints.size * sizeof(int));
                handleMemoryError(result0.partition);
                for (size_t i = 0; i < dataPoints.size; ++i)
                {
                    result0.partition[i] = dataPoints.points[i].partition;
                }
                mseSum += result0.mse;
                ciSum += result0.centroidIndex;
                timeSum += duration;
                if (result0.centroidIndex == 0) successRate++;

                //TODO: free() muistit?

                if (LOGGING == 3) printf("Round %d\n", i + 1);

                if (i == 0)
                {
                    writeCentroidsToFile("outputs/kMeans_centroids.txt", &centroids);
                    writeDataPointPartitionsToFile("outputs/kMeans_partitions.txt", &dataPoints);
                }
            }

            printf("(K-means)Average CI: %.2f and MSE: %.2f\n", ciSum / loopCount, mseSum / loopCount / scaling);
            printf("(K-means)Relative CI: %.2f\n", ciSum / loopCount / numCentroids);
            printf("(K-means)Average time taken: %.2f seconds\n", timeSum / loopCount);
            printf("(K-means)Success rate: %.2f\%\n\n", successRate / loopCount * 100);

            writeResultsToFile(fileName, ciSum, mseSum, timeSum, successRate, numCentroids, "K-means", loopCount, scaling, outputDirectory);

            ///////////////////////
            // Repeated k-means //
            /////////////////////
            /*
            * 
            * Tässä menee liaan kauan ajaa, joten jätetään pois
            * 
            mseSum = 0;
            ciSum = 0;
            timeSum = 0;
            successRate = 0;

            printf("Repeated K-means\n");

            for (int i = 0; i < loopCount; i++)
            {
                ClusteringResult result1;
                ClusteringResult bestResult1;
                bestResult1.centroids = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(bestResult1.centroids);
                for (int i = 0; i < numCentroids; ++i)
                {
                    bestResult1.centroids[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(bestResult1.centroids[i].attributes);
                }
                bestResult1.partition = malloc(dataPoints.size * sizeof(int));
                handleMemoryError(bestResult1.partition);
                bestResult1.mse = DBL_MAX;
                bestResult1.centroidIndex = INT_MAX;

                Centroids centroids1;
                centroids1.size = numCentroids;
                centroids1.points = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(centroids1.points);
                for (int i = 0; i < numCentroids; ++i)
                {
                    centroids1.points[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(centroids1.points[i].attributes);
                }

                Centroids bestCentroids1;
                bestCentroids1.size = numCentroids;
                bestCentroids1.points = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(bestCentroids1.points);
                for (int i = 0; i < numCentroids; ++i)
                {
                    bestCentroids1.points[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(bestCentroids1.points[i].attributes);
                }

                start = clock();

                for (int repeat = 0; repeat < MAX_REPEATS; ++repeat)
                {
                    //DEBUGGING if(LOGGING == 3) printf("round: %d\n", repeat);

                    generateRandomCentroids(numCentroids, &dataPoints, centroids1.points);
                    int centroidIndex = calculateCentroidIndex(&centroids1, &groundTruth);
                    if (LOGGING == 2) printf("(K-means)Initial Centroid Index (CI): %d\n", centroidIndex);

                    // K-means
                    result1 = runKMeans(&dataPoints, maxIterations, &centroids1, &groundTruth);

                    if (LOGGING == 2) printf("(RKM) Round %d: Latest Centroid Index (CI): %zu and Latest Mean Sum-of-Squared Errors (MSE): %.4f\n", repeat, result1.centroidIndex, result1.mse / 10000);

                    if (result1.mse < bestResult1.mse)
                    {
                        //deepCopyDataPoints(bestResult1.centroids, centroids1.points, numCentroids);
                        deepCopyCentroids(&centroids1, &bestCentroids1, numCentroids);
                        bestResult1.mse = result1.mse;
                        for (size_t i = 0; i < dataPoints.size; ++i) //TODO: halutaanko tätä loopata timerin sisällä?
                        {
                            bestResult1.partition[i] = dataPoints.points[i].partition;
                        }
                    }
                }

                end = clock();
                duration = ((double)(end - start)) / CLOCKS_PER_SEC;
                if (LOGGING == 3) printf("(Repeated k-means)Time taken: %.2f seconds\n\n", duration);

                bestResult1.centroidIndex = calculateCentroidIndex(&bestCentroids1, &groundTruth);
                //printf("(Repeated K-means)Best Centroid Index (CI): %zu and Best Mean Sum-of-Squared Errors (MSE): %.4f\n", bestResult1.centroidIndex, bestResult1.mse / 10000);

                mseSum += bestResult1.mse;
                ciSum += bestResult1.centroidIndex;
                timeSum += duration;
                if (bestResult1.centroidIndex == 0) successRate++;

                //TODO: free() muistit?

                if (LOGGING == 3) printf("Round %zu\n", i + 1);
            }

            printf("(Repeated k-means)Average CI: %.2f and MSE: %.2f\n", ciSum / loopCount, mseSum / loopCount / scaling);
            printf("(Repeated k-means)Relative CI: %.2f\n", ciSum / loopCount / numCentroids);
            printf("(Repeated k-means)Average time taken: %.2f seconds\n", timeSum / loopCount);
            printf("(Repeated k-means)Success rate: %.2f\%\n\n", successRate / loopCount * 100);

            writeResultsToFile(fileName, ciSum, mseSum, timeSum, successRate, numCentroids, "Repeated K-means", loopCount, scaling, outputDirectory);
            */
            //////////////////
            // Random Swap //
            ////////////////
            /*
            printf("Random swap\n");

            mseSum = 0;
            ciSum = 0;
            timeSum = 0;
            successRate = 0;

            for (int i = 0; i < loopCount; i++)
            {
                ClusteringResult result2;

                result2.centroids = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(result2.centroids);
                for (int i = 0; i < numCentroids; ++i)
                {
                    result2.centroids[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(result2.centroids[i].attributes);
                }
                result2.partition = malloc(dataPoints.size * sizeof(int));
                handleMemoryError(result2.partition);
                result2.centroidIndex = INT_MAX;
                result2.mse = DBL_MAX;

                Centroids centroids2;
                centroids2.size = numCentroids;
                centroids2.points = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(centroids2.points);
                for (int i = 0; i < numCentroids; ++i)
                {
                    centroids2.points[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(centroids2.points[i].attributes);
                }

                start = clock();

                generateRandomCentroids(numCentroids, &dataPoints, centroids2.points);

                // Random Swap
                randomSwap(&dataPoints, &centroids2, &groundTruth, &result2);

                end = clock();
                duration = ((double)(end - start)) / CLOCKS_PER_SEC;

                result2.centroidIndex = calculateCentroidIndex(&centroids2, &groundTruth);

                //Update partition array
                //memcpy(result2.partition, dataPoints.points, dataPoints.size * sizeof(int));
                for (size_t i = 0; i < dataPoints.size; ++i)
                {
                    result2.partition[i] = dataPoints.points[i].partition;
                }

                if (LOGGING == 3) printf("(Random Swap)Time taken: %.2f seconds\n\n", duration);

                mseSum += result2.mse;
                ciSum += result2.centroidIndex;
                timeSum += duration;
                if (result2.centroidIndex == 0) successRate++;

                //TODO: free() muistit?

                if (LOGGING == 3) printf("Round %d\n", i + 1);

                if (i==0)
                {
                    writeCentroidsToFile("outputs/randomSwap_centroids.txt", &centroids2);
                    writeDataPointPartitionsToFile("outputs/randomSwap_partitions.txt", &dataPoints);
                }
            }

            printf("(Random Swap)Average CI: %.2f and MSE: %.2f\n", ciSum / loopCount, mseSum / loopCount / scaling);
            printf("(Random Swap)Relative CI: %.2f\n", ciSum / loopCount / numCentroids);
            printf("(Random Swap)Average time taken: %.2f seconds\n", timeSum / loopCount);
            printf("(Random Swap)Success rate: %.2f\%\n\n", successRate / loopCount * 100);

            writeResultsToFile(fileName, ciSum, mseSum, timeSum, successRate, numCentroids, "Random swap", loopCount, scaling, outputDirectory);
            */

            ///////////////////
            // Random Split //
            /////////////////
            /*
            mseSum = 0;
            ciSum = 0;
            timeSum = 0;
            successRate = 0;

            printf("Random Split k-means\n");

            for (size_t i = 0; i < loopCount; i++)
            {
                ClusteringResult result3;

                result3.centroids = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(result3.centroids);
                for (int i = 0; i < numCentroids; ++i) //TODO: Muuta loopit size_t
                {
                    result3.centroids[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(result3.centroids[i].attributes);
                }
                result3.partition = malloc(dataPoints.size * sizeof(int));
                handleMemoryError(result3.partition);
                result3.centroidIndex = INT_MAX;
                result3.mse = DBL_MAX;

                Centroids centroids3;
                centroids3.size = 1;
                centroids3.points = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(centroids3.points);
                for (int i = 0; i < numCentroids; ++i)
                {
                    centroids3.points[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(centroids3.points[i].attributes);
                }

                start = clock();

                generateRandomCentroids(centroids3.size, &dataPoints, centroids3.points);

                result3 = runRandomSplit(&dataPoints, &centroids3, numCentroids, &groundTruth);

                end = clock();
                duration = ((double)(end - start)) / CLOCKS_PER_SEC;
                if (LOGGING == 3) printf("(Split1)Time taken: %.2f seconds\n\n", duration);

                result3.centroidIndex = calculateCentroidIndex(&centroids3, &groundTruth);

                mseSum += result3.mse;
                ciSum += result3.centroidIndex;
                timeSum += duration;
                if (result3.centroidIndex == 0) successRate++;

                //TODO: free() muistit?

                if (LOGGING == 3) printf("Round %zu\n", i + 1);
                                
                if (i == 0)
                {
                    writeCentroidsToFile("outputs/randomSplit_centroids.txt", &centroids3);
                    writeDataPointPartitionsToFile("outputs/randomSplit_partitions.txt", &dataPoints);
                }
            }

            printf("(Random Split)Average CI: %.2f and MSE: %.2f\n", ciSum / loopCount, mseSum / loopCount / scaling);
            printf("(Random Split)Relative CI: %.2f\n", ciSum / loopCount / numCentroids);
            printf("(Random Split)Average time taken: %.2f seconds\n", timeSum / loopCount);
            printf("(Random Split)Success rate: %.2f\%\n\n", successRate / loopCount * 100);

            writeResultsToFile(fileName, ciSum, mseSum, timeSum, successRate, numCentroids, "Random Split", loopCount, scaling, outputDirectory);
            */
            ///////////////////////////////
            // MSE Split, Intra-cluster //
            /////////////////////////////
            /*
            mseSum = 0;
            ciSum = 0;
            timeSum = 0;
            successRate = 0;

            printf("MSE Split k-means (Intra)\n");

            for (int i = 0; i < loopCount; i++)
            {
                resetPartitions(&dataPoints);

                ClusteringResult result4;

                result4.centroids = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(result4.centroids);
                for (int i = 0; i < numCentroids; ++i)
                {
                    result4.centroids[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(result4.centroids[i].attributes);
                }
                result4.partition = malloc(dataPoints.size * sizeof(int));
                handleMemoryError(result4.partition);
                result4.centroidIndex = INT_MAX; //TODO: voiko muuttaa = (size_t) -1?
                result4.mse = DBL_MAX;

                Centroids centroids4;
                centroids4.size = 1;
                centroids4.points = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(centroids4.points);
                for (int i = 0; i < numCentroids; ++i)
                {
                    centroids4.points[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(centroids4.points[i].attributes);
                }

                start = clock();

                generateRandomCentroids(centroids4.size, &dataPoints, centroids4.points);

                result4 = runMseSplit(&dataPoints, &centroids4, numCentroids, &groundTruth, 0);

                end = clock();
                duration = ((double)(end - start)) / CLOCKS_PER_SEC;
                if (LOGGING == 3) printf("(Split2)Time taken: %.2f seconds\n\n", duration);

                result4.centroidIndex = calculateCentroidIndex(&centroids4, &groundTruth);

                mseSum += result4.mse;
                ciSum += result4.centroidIndex;
                timeSum += duration;
                if (result4.centroidIndex == 0) successRate++;

                //TODO: free() muistit?

                if (LOGGING == 3) printf("Round %d\n", i + 1);

                if (i == 0)
                {
                    writeCentroidsToFile("outputs/intraCluster_centroids.txt", &centroids4);
                    writeDataPointPartitionsToFile("outputs/intraCluster_partitions.txt", &dataPoints);
                }
            }

            printf("(Split2)Average CI: %.2f and MSE: %.2f\n", ciSum / loopCount, mseSum / loopCount / scaling);
            printf("(Split2)Relative CI: %.2f\n", ciSum / loopCount / numCentroids);
            printf("(Split2)Average time taken: %.2f seconds\n", timeSum / loopCount);
            printf("(Split2)Success rate: %.2f\%\n\n", successRate / loopCount * 100);

            writeResultsToFile(fileName, ciSum, mseSum, timeSum, successRate, numCentroids, "MSE Split, Intra-cluster", loopCount, scaling, outputDirectory);
            */
            ////////////////////////
            // MSE Split, Global //
            //////////////////////
            /*
            mseSum = 0;
            ciSum = 0;
            timeSum = 0;
            successRate = 0;

            printf("MSE Split k-means (Global)\n");

            for (int i = 0; i < loopCount; i++)
            {
                resetPartitions(&dataPoints);

                ClusteringResult result5;

                result5.centroids = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(result5.centroids);
                for (int i = 0; i < numCentroids; ++i)
                {
                    result5.centroids[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(result5.centroids[i].attributes);
                }
                result5.partition = malloc(dataPoints.size * sizeof(int));
                handleMemoryError(result5.partition);
                result5.centroidIndex = INT_MAX;
                result5.mse = DBL_MAX;

                Centroids centroids5;
                centroids5.size = 1;
                centroids5.points = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(centroids5.points);
                for (int i = 0; i < numCentroids; ++i)
                {
                    centroids5.points[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(centroids5.points[i].attributes);
                }

                start = clock();

                generateRandomCentroids(centroids5.size, &dataPoints, centroids5.points);

                result5 = runMseSplit(&dataPoints, &centroids5, numCentroids, &groundTruth, 1);

                end = clock();
                duration = ((double)(end - start)) / CLOCKS_PER_SEC;
                if (LOGGING == 3) printf("(Split3)Time taken: %.2f seconds\n\n", duration);

                result5.centroidIndex = calculateCentroidIndex(&centroids5, &groundTruth);

                mseSum += result5.mse;
                ciSum += result5.centroidIndex;
                timeSum += duration;
                if (result5.centroidIndex == 0) successRate++;

                //TODO: free() muistit?

                if (LOGGING == 3) printf("Round %d\n", i + 1);

                if (i == 0)
                {
                    writeCentroidsToFile("outputs/globalSplit_centroids.txt", &centroids5);
                    writeDataPointPartitionsToFile("outputs/globalSplit_partitions.txt", &dataPoints);
                }
            }

            printf("(Split3)Average CI: %.2f and MSE: %.2f\n", ciSum / loopCount, mseSum / loopCount / scaling);
            printf("(Split3)Relative CI: %.2f\n", ciSum / loopCount / numCentroids);
            printf("(Split3)Average time taken: %.2f seconds\n", timeSum / loopCount);
            printf("(Split3)Success rate: %.2f\%\n\n", successRate / loopCount * 100);

            writeResultsToFile(fileName, ciSum, mseSum, timeSum, successRate, numCentroids, "MSE Split, Global", loopCount, scaling, outputDirectory);
            */
            ///////////////////////////////////
            // MSE Split, Local repartition //
            /////////////////////////////////
            /*
            mseSum = 0;
            ciSum = 0;
            timeSum = 0;
            successRate = 0;

            printf("MSE Split k-means (Repartition)\n");

            for (int i = 0; i < loopCount; i++)
            {
                resetPartitions(&dataPoints);

                ClusteringResult result6;

                result6.centroids = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(result6.centroids);
                for (int i = 0; i < numCentroids; ++i)
                {
                    result6.centroids[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(result6.centroids[i].attributes);
                }
                result6.partition = malloc(dataPoints.size * sizeof(int));
                handleMemoryError(result6.partition);
                result6.centroidIndex = INT_MAX;
                result6.mse = DBL_MAX;

                Centroids centroids6;
                centroids6.size = 1;
                centroids6.points = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(centroids6.points);
                for (int i = 0; i < numCentroids; ++i)
                {
                    centroids6.points[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(centroids6.points[i].attributes);
                }

                start = clock();

                generateRandomCentroids(centroids6.size, &dataPoints, centroids6.points);

                if (LOGGING == 3) printDataPointsPartitions(&dataPoints, centroids6.size);

                result6 = runMseSplit(&dataPoints, &centroids6, numCentroids, &groundTruth, 2);

                end = clock();
                duration = ((double)(end - start)) / CLOCKS_PER_SEC;
                if (LOGGING == 3) printf("(Split4)Time taken: %.2f seconds\n\n", duration);

                result6.centroidIndex = calculateCentroidIndex(&centroids6, &groundTruth);

                mseSum += result6.mse;
                ciSum += result6.centroidIndex;
                timeSum += duration;
                if (result6.centroidIndex == 0) successRate++;

                //TODO: free() muistit?

                if (LOGGING == 3) printf("Round %d\n", i + 1);

                if (i == 0)
                {
                    writeCentroidsToFile("outputs/localRepartition_centroids.txt", &centroids6);
                    writeDataPointPartitionsToFile("outputs/localRepartition_partitions.txt", &dataPoints);
                }
            }

            printf("(Split4)Average CI: %.2f and MSE: %.2f\n", ciSum / loopCount, mseSum / loopCount / scaling);
            printf("(Split4)Relative CI: %.2f\n", ciSum / loopCount / numCentroids);
            printf("(Split4)Average time taken: %.2f seconds\n", timeSum / loopCount);
            printf("(Split4)Success rate: %.2f\%\n\n", successRate / loopCount * 100);

            writeResultsToFile(fileName, ciSum, mseSum, timeSum, successRate, numCentroids, "MSE Split, Local repartition", loopCount, scaling, outputDirectory);
            */
            ////////////////////////
            // Bisecting k-means //
            //////////////////////
            
            mseSum = 0;
            ciSum = 0;
            timeSum = 0;
            successRate = 0;

            printf("Bisecting k-means\n");

            for (int i = 0; i < loopCount; i++)
            {
                resetPartitions(&dataPoints);


                /*result7.centroids = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(result7.centroids);
                for (int i = 0; i < numCentroids; ++i)
                {
                    result7.centroids[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(result7.centroids[i].attributes);
                }
                result7.partition = malloc(dataPoints.size * sizeof(int));
                handleMemoryError(result7.partition);
                result7.centroidIndex = INT_MAX;
                result7.mse = DBL_MAX;
                */

                Centroids centroids7;
                centroids7.size = 1;
                centroids7.points = malloc(numCentroids * sizeof(DataPoint));
                handleMemoryError(centroids7.points);
                for (int i = 0; i < numCentroids; ++i)
                {
                    centroids7.points[i].attributes = malloc(numDimensions * sizeof(double));
                    handleMemoryError(centroids7.points[i].attributes);
                }

                start = clock();

                generateRandomCentroids(centroids7.size, &dataPoints, centroids7.points);

                if (LOGGING == 3) printDataPointsPartitions(&dataPoints, centroids7.size);

                ClusteringResult result7 = runBisectingKMeans(&dataPoints, &centroids7, numCentroids, &groundTruth);

                end = clock();
                duration = ((double)(end - start)) / CLOCKS_PER_SEC;
                if (LOGGING == 3) printf("(Bisecting)Time taken: %.2f seconds\n\n", duration);

                result7.centroidIndex = calculateCentroidIndex(&centroids7, &groundTruth);

                mseSum += result7.mse;
                ciSum += result7.centroidIndex;
                timeSum += duration;
                if (result7.centroidIndex == 0) successRate++;

                //TODO: free() muistit?

                if (LOGGING == 3) printf("Round %d\n", i + 1);
            }

            printf("(Bisecting)Average CI: %.2f and MSE: %.2f\n", ciSum / loopCount, mseSum / loopCount / scaling);
            printf("(Bisecting)Relative CI: %.2f\n", ciSum / loopCount / numCentroids);
            printf("(Bisecting)Average time taken: %.2f seconds\n", timeSum / loopCount);
            printf("(Bisecting)Success rate: %.2f\%\n\n", successRate / loopCount * 100);

            writeResultsToFile(fileName, ciSum, mseSum, timeSum, successRate, numCentroids, "Bisecting k-means", loopCount, scaling, outputDirectory);


            /////////////
            // Prints //
            ///////////
            /*printf("(K-means) CI: %zu and MSE: %.5f\n\n", result0.centroidIndex, result0.mse / 10000);
            printf("(Repeated K-means) CI: %zu and MSE: %.5f\n", bestResult1.centroidIndex, bestResult1.mse / 10000);
            printf("(Random Swap) CI: %zu and MSE: %.5f\n\n", result2.centroidIndex, result2.mse / 10000);
            printf("(RandomSplit) CI: %zu and MSE: %.5f\n", result3.centroidIndex, result3.mse / 10000);
            printf("(MseSplit_intra) CI: %zu and MSE: %.5f\n", result4.centroidIndex, result4.mse / 10000);
            printf("(MseSplit_global) CI: %zu and MSE: %.5f\n", result5.centroidIndex, result5.mse / 10000);
            printf("(MseSplit_repartition) CI: %zu and MSE: %.5f\n", result6.centroidIndex, result6.mse / 10000);*/

            //writeCentroidsToFile("outputs/centroids.txt", &centroids6);
            //writeDataPointPartitionsToFile("outputs/partitions.txt", &dataPoints);

            ///////////////
            // Clean up //
            /////////////

            //DEBUGGING if (LOGGING == 2) printf("Freeing memory\n");
            //K-means
            //freeClusteringResult(&result0, numCentroids);
            //freeCentroids(&centroids);

            //Repeated k-means
            //freeClusteringResult(&bestResult1, numCentroids);
            //freeCentroids(&centroids1);
            //freeCentroids(&bestCentroids1);

            //Random Swap
            //freeClusteringResult(&result2, numCentroids);
            //freeCentroids(&centroids2);

            //random Split
            //freeClusteringResult(&result3, numCentroids); TODO: centroideja ei tallenneta vielä tähän
            //freeCentroids(&centroids3);

            //MSE Split, Intra
            //freeClusteringResult(&result4, numCentroids); TODO: centroideja ei tallenneta vielä tähän
            //freeCentroids(&centroids4);

            //MSE Split, Global
            //freeClusteringResult(&result5, numCentroids); TODO: centroideja ei tallenneta vielä tähän
            //freeCentroids(&centroids5);

            //MS Split, Local repartition
            //freeClusteringResult(&result6, numCentroids); TODO: centroideja ei tallenneta vielä tähän
            //freeCentroids(&centroids6);

            //Datapoints
            //freeDataPoints(&dataPoints);

            //Ground truth
            //freeCentroids(&groundTruth);
        }
    }

    return 0;
}