#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <float.h>

//Constants for file locations
const char* DATA_FILENAME = "data/s2.txt";
const char* GT_FILENAME = "GroundTruth/s2-cb.txt";
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
const int MAX_REPEATS = 10; // repeated k-means toistojen lkm, TODO: lopulliseen 100kpl
const int MAX_SWAPS = 1000; // random swap toistojen lkm, TODO: lopulliseen 1000kpl

// for logging
const int LOGGING = 1; // 1 = basic, 2 = detailed, 3 = debug, TODO: käy kaikki läpi ja tarkista, että käytetään oikeita muuttujia

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
        char* token = strtok_s(line, " \t", &context);
        while (token != NULL)
        {
            if (point.dimensions == attributeAllocatedSize)
            {
                attributeAllocatedSize = attributeAllocatedSize > 0 ? attributeAllocatedSize * 2 : 1;
                double* temp = realloc(point.attributes, sizeof(double) * attributeAllocatedSize);
                handleMemoryError(temp);
                point.attributes = temp;
            }

            point.attributes[point.dimensions++] = strtod(token, NULL); //atoi(token); //strtod(token, NULL); //TODO: strtod = double, atoi = int
			token = strtok_s(NULL, " \t", &context); //TODO: spaced " ", tabs "\t", newlines "\n"
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
    
    if (LOGGING == 2)
    {
        for (size_t i = 0; i < centroids.size; ++i)
        {
            printf("Centroid %zu (dimensions: %zu) attributes: ", i, points.points[i].dimensions);
            for (size_t j = 0; j < points.points[i].dimensions; ++j)
            {
                printf("%f ", points.points[i].attributes[j]);
            }
            printf("\n");
        }
    }    

    return centroids;
}

// Function to write the centroids to a file
//TODO: ei käytössä tällä hetkellä
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
    printf("\nCentroid file created successfully: %s\n", filename);
}

// Function to write partition to a text file
//TODO: ei käytössä tällä hetkellä
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


// Clustering
//
// 
// Function to choose random data points to be centroids
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

//calculate MSE
double calculateMSE(DataPoints* dataPoints, Centroids* centroids)
{
    double sse = calculateSSE(dataPoints, centroids);

    double mse = sse / (dataPoints->size * dataPoints->points[0].dimensions);

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

    double mse = sse / (count * dimensions);
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
        printf("centroids1: %zu AND 2: %zu\n\n", centroids1->points->attributes[0], centroids2->points->attributes[0]);
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
        mse = calculateMSE(dataPoints, centroids);

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
                printf("(RS) Round %d: Best Centroid Index (CI): %zu and Best Sum-of-Squared Errors (MSE): %.5f\n", i + 1, result.centroidIndex, result.mse / 10000);
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
void localRepartition(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, bool* clustersAffected)
{
    // Reassign data points from the split clusters to their nearest centroids
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterToSplit || dataPoints->points[i].partition == centroids->size - 1)
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

    // Attract data points from other clusters to the new cluster
    size_t newClusterIndex = centroids->size - 1; // Index of the new centroid
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        size_t currentCluster = dataPoints->points[i].partition;

        // Skip if the point is in the split clusters
        if (currentCluster == clusterToSplit || currentCluster == newClusterIndex)
            continue;

        size_t nearestCentroid = findNearestCentroid(&dataPoints->points[i], centroids);

        if (nearestCentroid == newClusterIndex)
        {
            clustersAffected[currentCluster] = true;    // Mark the old cluster as affected
            clustersAffected[newClusterIndex] = true;   // Mark the new cluster as affected
            dataPoints->points[i].partition = newClusterIndex;
        }
    }
}

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
    double newClusterMSE = calculateMSE(&pointsInCluster, &localCentroids);

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

// Function to run the split k-means algorithm with random splitting
//TODO: kesken
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
    size_t localMaxIterations = splitType == 0 ? MAX_ITERATIONS : splitType == 1 ? 5 : 2; //TODO: pohdi tarkemmat arvot, globaaliin 5 näyttää toimivan hyvin
	
    double* clusterMSEs = malloc(centroids->size * sizeof(double));
    handleMemoryError(clusterMSEs);

    size_t* MseDrops = calloc(centroids->size, sizeof(size_t));
    handleMemoryError(MseDrops);

    bool* clustersAffected = calloc(maxCentroids, sizeof(bool));
    handleMemoryError(clustersAffected);

    //Only 1 cluster, so no need for decision making
    size_t initialClusterToSplit = 0;
    splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, localMaxIterations, groundTruth);

    for (size_t i = 0; i < centroids->size; ++i)
    {
        clusterMSEs[i] = calculateClusterMSE(dataPoints, centroids, i); //TODO: tarvitaanko omaa rakennetta?
        MseDrops[i] = tentativeMseDrop(dataPoints, centroids, i, localMaxIterations, clusterMSEs[i]);
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

        if(splitType == 0) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, localMaxIterations, groundTruth);
		else if (splitType == 1) splitClusterGlobal(dataPoints, centroids, clusterToSplit, localMaxIterations, groundTruth);
		else if (splitType == 2) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, localMaxIterations, groundTruth);

        if (splitType == 0)
        {
            // Recalculate MSE for the affected clusters
            clusterMSEs[clusterToSplit] = calculateClusterMSE(dataPoints, centroids, clusterToSplit);
            clusterMSEs = realloc(clusterMSEs, centroids->size * sizeof(double));
            handleMemoryError(clusterMSEs);
            clusterMSEs[centroids->size - 1] = calculateClusterMSE(dataPoints, centroids, centroids->size - 1);

            // Update MseDrops for the affected clusters
            MseDrops[clusterToSplit] = tentativeMseDrop(dataPoints, centroids, clusterToSplit, localMaxIterations, clusterMSEs[clusterToSplit]);
            MseDrops = realloc(MseDrops, centroids->size * sizeof(double));
            handleMemoryError(MseDrops);
            MseDrops[centroids->size - 1] = tentativeMseDrop(dataPoints, centroids, centroids->size - 1, localMaxIterations, clusterMSEs[centroids->size - 1]);
        }
        else if (splitType == 1)
        {
            clusterMSEs = realloc(clusterMSEs, centroids->size * sizeof(double));
            handleMemoryError(clusterMSEs);
            MseDrops = realloc(MseDrops, centroids->size * sizeof(double));
            handleMemoryError(MseDrops);

            for (size_t i = 0; i < centroids->size; ++i)
            {
                clusterMSEs[i] = calculateClusterMSE(dataPoints, centroids, i);
                MseDrops[i] = tentativeMseDrop(dataPoints, centroids, i, localMaxIterations, clusterMSEs[i]);
            }
        }
        else if(splitType == 2)
        {
            localRepartition(dataPoints, centroids, clusterToSplit, clustersAffected);
            
			clustersAffected[clusterToSplit] = true;
			clustersAffected[centroids->size - 1] = true;

            // +1 for sizes
            clusterMSEs = realloc(clusterMSEs, centroids->size * sizeof(double));
            handleMemoryError(clusterMSEs);
            MseDrops = realloc(MseDrops, centroids->size * sizeof(double));
            handleMemoryError(MseDrops);

			// Recalculate MseDrop for affected clusters (old and new)
            for (size_t i = 0; i < centroids->size - 1; ++i)
            {
                if (clustersAffected[i])
                {
                    clusterMSEs[i] = calculateClusterMSE(dataPoints, centroids, i);
                    MseDrops[i] = tentativeMseDrop(dataPoints, centroids, i, localMaxIterations, clusterMSEs[i]);
                }
            }
        }

        if (LOGGING == 2)
        {
            size_t ci = calculateCentroidIndex(centroids, groundTruth);
            double mse = calculateMSE(dataPoints, centroids);
            printf("(MseSplit) Number of centroids: %zu, CI: %zu, and MSE: %.5f \n", centroids->size, ci, mse / 10000);
        }
    }

    //free(clustersAffected);

    //TODO: globaali k-means  
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

//Q: lasketaanko MSE clusterin datapisteillä vai koko datasetillä splitissä?

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

		int maxIterations = MAX_ITERATIONS;//TODO tarkasta kaikki, että käytetään oikeita muuttujia

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
        handleMemoryError(centroids.points); //TODO: pitäisikö lopullisesta versiosta kommentoida nämä pois? Helpottavat lähinnä debuggausta
		for (int i = 0; i < NUM_CENTROIDS; ++i)
		{
			centroids.points[i].attributes = malloc(numDimensions * sizeof(double));
			handleMemoryError(centroids.points[i].attributes);
            centroids.points[i].dimensions = numDimensions; //TODO tarkista tehdäänkö muualla
		}

        clock_t start = clock();

        generateRandomCentroids(NUM_CENTROIDS, &dataPoints, centroids.points);
		
        if (LOGGING == 2) { 
            size_t centroidIndex = calculateCentroidIndex(&centroids, &groundTruth);
            printf("(K-means)Initial Centroid Index (CI): %zu\n", centroidIndex); 
        }

        ClusteringResult result0 = runKMeans(&dataPoints, maxIterations, &centroids, &groundTruth);
        //DEBUGGING if(LOGGING == 2) printf("(K-means)Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (MSE): %.4f\n", result0.centroidIndex, result0.mse / 10000000);

        clock_t end = clock();
        double duration = ((double)(end - start)) / CLOCKS_PER_SEC;

        printf("(K-means)Time taken: %f seconds\n\n", duration);

		result0.centroidIndex = calculateCentroidIndex(&centroids, &groundTruth);

        result0.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(result0.centroids);
        deepCopyDataPoints(result0.centroids, centroids.points, NUM_CENTROIDS);

        result0.partition = malloc(dataPoints.size * sizeof(int));
        handleMemoryError(result0.partition);
        for (size_t i = 0; i < dataPoints.size; ++i)
        {
            result0.partition[i] = dataPoints.points[i].partition;
        }
        
        //DEBUGGING
        //printCentroidsInfo(&centroids);

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
        bestResult1.mse = DBL_MAX;
        bestResult1.centroidIndex = INT_MAX;

        Centroids centroids1;
        centroids1.size = NUM_CENTROIDS;
        centroids1.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(centroids1.points);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
        {
            centroids1.points[i].attributes = malloc(numDimensions * sizeof(double));
            handleMemoryError(centroids1.points[i].attributes);
        }

        Centroids bestCentroids1;
        bestCentroids1.size = NUM_CENTROIDS;
        bestCentroids1.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(bestCentroids1.points);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
        {
            bestCentroids1.points[i].attributes = malloc(numDimensions * sizeof(double));
            handleMemoryError(bestCentroids1.points[i].attributes);
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

			if(LOGGING == 2) printf("(RKM) Round %d: Latest Centroid Index (CI): %zu and Latest Mean Sum-of-Squared Errors (MSE): %.4f\n", repeat, result1.centroidIndex, result1.mse / 10000);
            
            if (result1.mse < bestResult1.mse)
            {
				//deepCopyDataPoints(bestResult1.centroids, centroids1.points, NUM_CENTROIDS);
				deepCopyCentroids(&centroids1, &bestCentroids1, NUM_CENTROIDS);
                bestResult1.mse = result1.mse;
                for (size_t i = 0; i < dataPoints.size; ++i) //TODO: halutaanko tätä loopata timerin sisällä?
                {
                    bestResult1.partition[i] = dataPoints.points[i].partition;
                }
            }
        }
        
        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Repeated k-means)Time taken: %.2f seconds\n\n", duration);

		bestResult1.centroidIndex = calculateCentroidIndex(&bestCentroids1, &groundTruth);
        //printf("(Repeated K-means)Best Centroid Index (CI): %zu and Best Mean Sum-of-Squared Errors (MSE): %.4f\n", bestResult1.centroidIndex, bestResult1.mse / 10000);
        

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
		result2.mse = DBL_MAX;

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

		result2.centroidIndex = calculateCentroidIndex(&centroids2, &groundTruth);

        //Update partition array
        memcpy(result2.partition, dataPoints.points, dataPoints.size * sizeof(int));

        printf("(Random Swap)Time taken: %.2f seconds\n\n", duration);
        
        ///////////////////
        // Random Split //
        /////////////////
        ClusteringResult result3;

        result3.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(result3.centroids);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
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
        centroids3.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(centroids3.points);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
        {
            centroids3.points[i].attributes = malloc(numDimensions * sizeof(double));
            handleMemoryError(centroids3.points[i].attributes);
        }

        printf("Random Split k-means\n");

        start = clock();

        generateRandomCentroids(centroids.size, &dataPoints, centroids3.points);

        result3 = runRandomSplit(&dataPoints, &centroids3, NUM_CENTROIDS, &groundTruth);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Split1)Time taken: %.5f seconds\n\n", duration);

		result3.centroidIndex = calculateCentroidIndex(&centroids3, &groundTruth);


        ///////////////////////////////
        // MSE Split, Intra-cluster //
        /////////////////////////////
        ClusteringResult result4;

        result4.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(result4.centroids);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
        {
            result4.centroids[i].attributes = malloc(numDimensions * sizeof(double));
            handleMemoryError(result4.centroids[i].attributes);
        }
        result4.partition = malloc(dataPoints.size * sizeof(int));
        handleMemoryError(result4.partition);
        result4.centroidIndex = INT_MAX;
        result4.mse = DBL_MAX;

        Centroids centroids4;
        centroids4.size = 1;
        centroids4.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(centroids4.points);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
        {
            centroids4.points[i].attributes = malloc(numDimensions * sizeof(double));
            handleMemoryError(centroids4.points[i].attributes);
        }

        printf("MSE Split k-means (Intra)\n");

        start = clock();

        generateRandomCentroids(centroids.size, &dataPoints, centroids4.points);

        result4 = runMseSplit(&dataPoints, &centroids4, NUM_CENTROIDS, &groundTruth, 0);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Split2)Time taken: %.2f seconds\n\n", duration);

        result4.centroidIndex = calculateCentroidIndex(&centroids4, &groundTruth);
        

        ////////////////////////
        // MSE Split, Global //
        //////////////////////
        ClusteringResult result5;

        result5.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(result5.centroids);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
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
        centroids5.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(centroids5.points);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
        {
            centroids5.points[i].attributes = malloc(numDimensions * sizeof(double));
            handleMemoryError(centroids5.points[i].attributes);
        }

        printf("MSE Split k-means (Global)\n");

        start = clock();

        generateRandomCentroids(centroids.size, &dataPoints, centroids5.points);

        result5 = runMseSplit(&dataPoints, &centroids5, NUM_CENTROIDS, &groundTruth, 1);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Split3)Time taken: %.2f seconds\n\n", duration);

        result5.centroidIndex = calculateCentroidIndex(&centroids5, &groundTruth);


        ///////////////////////////////////
        // MSE Split, Local repartition //
        /////////////////////////////////
        ClusteringResult result6;

        result6.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(result6.centroids);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
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
        centroids6.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(centroids6.points);
        for (int i = 0; i < NUM_CENTROIDS; ++i)
        {
            centroids6.points[i].attributes = malloc(numDimensions * sizeof(double));
            handleMemoryError(centroids6.points[i].attributes);
        }

        printf("MSE Split k-means (Repartition)\n");

        start = clock();

        generateRandomCentroids(centroids.size, &dataPoints, centroids6.points);

        result6 = runMseSplit(&dataPoints, &centroids6, NUM_CENTROIDS, &groundTruth, 2);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Split4)Time taken: %.2f seconds\n\n", duration);

        result6.centroidIndex = calculateCentroidIndex(&centroids6, &groundTruth);


        /////////////
        // Prints //
        ///////////
        printf("(K-means)Best Centroid Index (CI): %zu and Best Mean Sum-of-Squared Errors (MSE): %.5f\n", result0.centroidIndex, result0.mse / 10000);
        printf("(Repeated K-means)Best Centroid Index (CI): %zu and Mean Best Sum-of-Squared Errors (MSE): %.5f\n", bestResult1.centroidIndex, bestResult1.mse / 10000);
        printf("(Random Swap)Best Centroid Index (CI): %zu and Best Mean Sum-of-Squared Errors (MSE): %.5f\n", result2.centroidIndex, result2.mse / 10000);
        printf("(RandomSplit)Best Centroid Index (CI): %zu and best Sum-of-Squared Errors (MSE): %.5f\n", result3.centroidIndex, result3.mse / 10000);
        printf("(MseSplit_intra)Best Centroid Index (CI): %zu and best Sum-of-Squared Errors (MSE): %.5f\n", result4.centroidIndex, result4.mse / 10000);
        printf("(MseSplit_global)Best Centroid Index (CI): %zu and best Sum-of-Squared Errors (MSE): %.5f\n", result5.centroidIndex, result5.mse / 10000);
        printf("(MseSplit_repartition)Best Centroid Index (CI): %zu and best Sum-of-Squared Errors (MSE): %.5f\n", result6.centroidIndex, result6.mse / 10000);

		writeCentroidsToFile("outputs/testifile.txt", &centroids);

        ///////////////
        // Clean up //
        /////////////
        
        //DEBUGGING if (LOGGING == 2) printf("Freeing memory\n");
        //K-means
		freeClusteringResult(&result0);
        freeCentroids(&centroids);

		//Repeated k-means
        freeClusteringResult(&bestResult1);
		freeCentroids(&centroids1);
		freeCentroids(&bestCentroids1);

        //Random Swap
		freeClusteringResult(&result2);
		freeCentroids(&centroids2);

        //random Split
        //freeClusteringResult(&result3); TODO: centroideja ei tallenneta vielä tähän
		freeCentroids(&centroids3);

		//MSE Split, Intra
        //freeClusteringResult(&result3); TODO: centroideja ei tallenneta vielä tähän
        freeCentroids(&centroids4);

		//MSE Split, Global
        //freeClusteringResult(&result3); TODO: centroideja ei tallenneta vielä tähän
        freeCentroids(&centroids5);

		//MS Split, Local repartition
        //freeClusteringResult(&result3); TODO: centroideja ei tallenneta vielä tähän
        freeCentroids(&centroids6);

		//Datapoints
        freeDataPoints(&dataPoints);

		//Ground truth
		freeCentroids(&groundTruth);
        
        return 0;
    }
}