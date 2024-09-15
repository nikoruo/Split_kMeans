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

            point.attributes[point.dimensions++] = strtod(token, NULL); //TODO: strtod = double, atoi = int
			token = strtok_s(NULL, " \t\n", &context); //TODO: spaced " ", tabs "\t", newlines "\n"
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

    /* DEBUGGING
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
    }*/
    
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
//TODO: ei käytössä tällä hetkellä
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
        fprintf(centroidFile, "\n");
    }

    fclose(centroidFile);
    printf("Centroid file created successfully: %s\n", filename);
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

        //TODO: squared vai ei?
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

    size_t nearestCentroidId = SIZE_MAX;
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
    size_t countFrom1to2 = countOrphans(centroids1, centroids2);
    size_t countFrom2to1 = countOrphans(centroids2, centroids1);

    return (countFrom1to2 > countFrom2to1) ? countFrom1to2 : countFrom2to1;
}

// Function to run the k-means algorithm
ClusteringResult runKMeans(DataPoints* dataPoints, int iterations, Centroids* centroids, Centroids* groundTruth)
{
    double bestMse = DBL_MAX;
    size_t centroidIndex = SIZE_MAX;
    double mse = DBL_MAX;

    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        // Partition step
        partitionStep(dataPoints, centroids);

        // Centroid step
        centroidStep(centroids, dataPoints);
                
        /*DEBUGGING Centroid Index
        if (LOGGING == 1)
        {
            centroidIndex = calculateCentroidIndex(centroids, groundTruth);
            printf("(runKMeans)CI after iteration %d: %d\n", iteration + 1, centroidIndex);
        }*/

        // MSE Calculation
        mse = calculateMSE(dataPoints, centroids);
        //DEBUGGING if (LOGGING == 2) printf("(runKMeans)Total MSE after iteration %d: %.0f\n", iteration + 1, mse / 10000000);

        if (LOGGING == 2) printf("(runKMeans)After iteration %zu: CI = %zu and MSE = %.5f\n", iteration + 1, centroidIndex, mse /10000);


        if (mse < bestMse)
        {
			//printf("Best MSE so far: %.0f\n", mse);
            bestMse = mse;
        }
        else
        {
            break; //TODO: break toimii, mutta on aika ruma ratkaisu (iteration = iterations?)
        }
    }

    ClusteringResult result;
    result.mse = bestMse;
    result.centroidIndex = calculateCentroidIndex(centroids, groundTruth);
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

        //If 1) CI or 2) MSE improves, we keep the change
        //if not, we reverse the swap
        if (result.centroidIndex < bestResult->centroidIndex 
            || result.centroidIndex == bestResult->centroidIndex && result.mse < bestResult->mse)
        {
			if (LOGGING == 1) printf("(RS) Round %d: Best Centroid Index (CI): %zu and Best Sum-of-Squared Errors (MSE): %.5f\n", i+1, result.centroidIndex, result.mse / 10000);
            
            bestResult->mse = result.mse;
            bestResult->centroidIndex = result.centroidIndex;
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

// Function to run the split k-means algorithm with random splitting
//TODO: kesken
ClusteringResult runRandomSplit(DataPoints* dataPoints, Centroids* centroids, int maxCentroids, Centroids* groundTruth)
{
    size_t currentNumCentroids = centroids->size;
    size_t localMaxIterations = 2;

    partitionStep(dataPoints, centroids);
    centroidStep(centroids, dataPoints);

    while(centroids->size < maxCentroids)
    {
        //TODO: 1.versio valitaan randomilla
        size_t clusterToSplit = rand() % centroids->size;

		//TODO: Koska ei enää klustereita, niin tässä pitää koostaa
        size_t clusterSize = 0;

        for (size_t i = 0; i < dataPoints->size; ++i)
        {
            if (dataPoints->points[i].partition == clusterToSplit)
            {
                clusterSize++;
            }
        }

        //TODO: testaa myös ilman tätä <- tehokkuus
        if (clusterSize < 2)
        {
            continue;
        }

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

        //TODO: Klusterin jakaminen
            //TODO: Valitaan randomilla kaksi pistettä
        size_t idx1 = rand() % clusterSize;
        size_t idx2 = idx1;
        while (idx2 == idx1)
        {
            idx2 = rand() % clusterSize;
        }

        size_t dataIndex1 = clusterIndices[idx1];
        size_t dataIndex2 = clusterIndices[idx2];

        //TODO: Lokaali k-means
        Centroids localCentroids;
        localCentroids.size = 2;
        localCentroids.points = malloc(2 * sizeof(DataPoint));
        handleMemoryError(localCentroids.points);
        deepCopyDataPoint(&localCentroids.points[0], &dataPoints->points[dataIndex1]);
        deepCopyDataPoint(&localCentroids.points[1], &dataPoints->points[dataIndex2]);

        DataPoints pointsInCluster;
        pointsInCluster.size = clusterSize;
        pointsInCluster.points = malloc(clusterSize * sizeof(DataPoint));
        handleMemoryError(pointsInCluster.points);
        for (size_t i = 0; i < clusterSize; ++i)
        {
            pointsInCluster.points[i] = dataPoints->points[clusterIndices[i]];
        }

		    //TODO: rajoitetaanko lokaalin k-meansin iteraatiot?
        ClusteringResult localResult = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, groundTruth);
        
        //TODO: rakenteiden päivitys
        for (size_t i = 0; i < clusterSize; ++i)
        {
            size_t originalIndex = clusterIndices[i];
            dataPoints->points[originalIndex].partition = (pointsInCluster.points[i].partition == 0) ? clusterToSplit : centroids->size;
        }

        centroids->size++;
        centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint));
        handleMemoryError(centroids->points);
        deepCopyDataPoint(&centroids->points[centroids->size - 1], &localCentroids.points[1]);

        if (LOGGING == 1)
        {
            size_t ci = calculateCentroidIndex(centroids, groundTruth);
            double mse = calculateMSE(dataPoints, centroids);
            printf("Number of centroids: %zu, CI: %zu, and MSE: %.5f \n", centroids->size, ci, mse / 10000);
        }

        free(clusterIndices);
        free(pointsInCluster.points);
        free(localCentroids.points);
        //free(localResult.partition);

        currentNumCentroids++;
    }	

    //TODO: globaali k-means  
    ClusteringResult finalResult = runKMeans(dataPoints, MAX_ITERATIONS, centroids, groundTruth);

    return finalResult;
}

/*
// Function to run the split k-means algorithm with tentative splitting (choosing to split the one that reduces the MSE the most)
//TODO: jakaa suurimman osan toiminnallisuuksista runRandomSplitin kanssa, joten pilko yhteiset osat funktioiksi
ClusteringResult runTentativeSplit(DataPoints* dataPoints, Centroids* centroids, int maxCentroids, int maxIterations, Centroids* groundTruth)
{
    double mse = 0.0;
    size_t currentNumCentroids = centroids->size;

    partitionStep(dataPoints, centroids);
    centroidStep(centroids, dataPoints);

    while (centroids->size < maxCentroids)
    {
        //TODO: 2.versio valitaan siten, että MSE pienenee eniten
        //Tähän pitää kehittää MSE laskentaa, jotta voidaan valita paras jako
        int clusterToSplit = rand() % centroids->size;

        //TODO: Pitääkö kerätä kaikki pisteet, jotka kuuluvat klusteriin?
        DataPoints pointsInCluster;
        pointsInCluster.size = 0;
        pointsInCluster.points = malloc(dataPoints->size * sizeof(DataPoint));
        handleMemoryError(pointsInCluster.points);

        for (int i = 0; i < dataPoints->size; ++i)
        {
            if (dataPoints->points[i].partition == clusterToSplit)
            {
                pointsInCluster.points[pointsInCluster.size++] = dataPoints->points[i];
            }
        }

        //TODO: testaa myös ilman tätä <- tehokkuus
        if (pointsInCluster.size < 2)
        {
            free(pointsInCluster.points);
            continue;
        }

        //TODO: Klusterin jakaminen
            //TODO: Valitaan randomilla kaksi pistettä
        int index1 = rand() % pointsInCluster.size;
        int index2 = index1;
        while (index2 == index1)
        {
            index2 = rand() % pointsInCluster.size;
        }

        //TODO: Lokaali k-means
        Centroids localCentroids;
        localCentroids.size = 2;
        localCentroids.points = malloc(2 * sizeof(DataPoint));
        handleMemoryError(localCentroids.points);
        deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[index1]);
        deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[index2]);

        //TODO: rajoitetaanko lokaalin k-meansin iteraatiot?
        ClusteringResult localResult = runKMeans(&pointsInCluster, MAX_ITERATIONS, &localCentroids, groundTruth);

        //TODO: rakenteiden päivitys
        for (int i = 0; i < pointsInCluster.size; ++i)
        {
            int originalIndex = findOriginalIndex(dataPoints, pointsInCluster.points[i]); //<-- Refaktoroi käyttämään indeksejä, piste == piste on huono
            dataPoints->points[originalIndex].partition = (pointsInCluster.points[i].partition == 0) ? clusterToSplit : centroids->size;
        }

        centroids->size++;
        centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint));
        handleMemoryError(centroids->points);
        deepCopyDataPoint(&centroids->points[centroids->size - 1], &localCentroids.points[1]);

        size_t ci = calculateCentroidIndex(centroids, groundTruth);
        double mse = calculateMSE(dataPoints, centroids);

        if (LOGGING == 1) printf("Number of centroids: %zu, CI: %zu, and MSE: %.5f \n", centroids->size, ci, mse / 10000);

        free(pointsInCluster.points);
        free(localCentroids.points);
        //free(localResult.partition);

        currentNumCentroids++;
    }

    //TODO: globaali k-means  
    ClusteringResult finalResult = runKMeans(dataPoints, MAX_ITERATIONS, centroids, groundTruth);

    return finalResult;
}*/

///////////
// Main //
/////////

//TODO käy läpi size_t muuttujat, int on parempi jos voi olla negatiivinen
//TODO disabloidaanko false positive varoitukset?
//TODO: tee consteista paikallisia muuttujia
//TODO: kommentoi kaikki muistintarkastukset pois lopullisesta versiosta <-tehokkuus?
//TODO: "static inline..." sellaisten funktioiden eteen jotka eivät muuta mitään ja joita kutsutaan? Saattaa tehostaa suoritusta
//TODO: partition on int, mutta se voisi olla size_t. Jotta onnistuu niin -1 alustukset pitää käydä läpi

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
		size_t centroidIndex = calculateCentroidIndex(&centroids, &groundTruth);
		if (LOGGING == 1) printf("(K-means)Initial Centroid Index (CI): %zu\n", centroidIndex);

        ClusteringResult result0 = runKMeans(&dataPoints, maxIterations, &centroids, &groundTruth);
        //DEBUGGING if(LOGGING == 2) printf("(K-means)Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (MSE): %.4f\n", result0.centroidIndex, result0.mse / 10000000);

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

			if(LOGGING == 2) printf("(RKM) Round %d: Latest Centroid Index (CI): %zu and Latest Mean Sum-of-Squared Errors (MSE): %.4f\n", repeat, result1.centroidIndex, result1.mse / 10000);
            
            if (result1.centroidIndex < bestResult1.centroidIndex 
                || (result1.centroidIndex == bestResult1.centroidIndex && result1.mse < bestResult1.mse))
            {
				deepCopyDataPoints(bestResult1.centroids, centroids1.points, NUM_CENTROIDS);
                bestResult1.mse = result1.mse;
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

        //Update partition array
        memcpy(result2.partition, dataPoints.points, dataPoints.size * sizeof(int));

        printf("(Random Swap)Time taken: %.2f seconds\n\n", duration);
        
        ////////////////////
        // Split k-means //
        //////////////////
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

        printf("Split k-means\n");

        start = clock();

        generateRandomCentroids(centroids.size, &dataPoints, centroids3.points);

        result3 = runRandomSplit(&dataPoints, &centroids3, NUM_CENTROIDS, &groundTruth);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Split1)Time taken: %.2f seconds\n\n", duration);

        /////////////
        // Prints //
        ///////////
        printf("(K-means)Best Centroid Index (CI): %zu and Best Mean Sum-of-Squared Errors (MSE): %.5f\n", result0.centroidIndex, result0.mse / 10000);
        printf("(Repeated K-means)Best Centroid Index (CI): %zu and Mean Best Sum-of-Squared Errors (MSE): %.5f\n", bestResult1.centroidIndex, bestResult1.mse / 10000);
        printf("(Random Swap)Best Centroid Index (CI): %zu and Best Mean Sum-of-Squared Errors (MSE): %.5f\n", result2.centroidIndex, result2.mse / 10000);
        printf("(Split1)Best Centroid Index (CI): %zu and best Sum-of-Squared Errors (MSE): %.5f\n", result3.centroidIndex, result3.mse / 10000);
        //printf("(Split2)Best Centroid Index (CI): %zu and best Sum-of-Squared Errors (MSE): %f\n", result4.centroidIndex, result4.mse / 10000);


        ///////////////
        // Clean up //
        /////////////
        
        //DEBUGGING if (LOGGING == 2) printf("Freeing memory\n");
        //K-means
		freeClusteringResult(&result0);
        freeCentroids(&centroids);

		//Repeated k-means
        /*freeClusteringResult(&bestResult1);
		freeCentroids(&centroids1);

        //Random Swap
		freeClusteringResult(&result2);
		freeCentroids(&centroids2);*/

        //random Split
        //freeClusteringResult(&result3); TODO: centroideja ei tallenneta vielä tähän
		freeCentroids(&centroids3);

		//Datapoints
        freeDataPoints(&dataPoints);

		//Ground truth
		freeCentroids(&groundTruth);
        
        return 0;
    }
}
