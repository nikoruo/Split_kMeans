#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <float.h>

//Constants for file locations
const char* DATA_FILENAME = "data/s4.txt";
const char* CENTROID_FILENAME = "outputs/centroid.txt";
const char* PARTITION_FILENAME = "outputs/partition.txt";
const char SEPARATOR = ' ';

//and for clustering
const int NUM_CENTROIDS = 15;  // klustereiden lkm: s4 = 15, unbalanced = 8
const int MAX_ITERATIONS = 100; // k-means rajoitus
const int MAX_REPEATS = 25; // repeated kmeans toistojen lkm
const int MAX_SWAPS = 100; // 

//Structs
//
//
typedef struct
{
    double* attributes;
    size_t size;
} DataPoint;

typedef struct
{
    DataPoint* points;
    size_t size;
} DataPoints;

typedef struct
{
    double sse;
    int* partition;
} KMeansResult;

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

// Function to free data points
// note: may not need this
void freeDataPoints(DataPoints* dataPoints)
{
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        free(dataPoints->points[i].attributes);
    }
    free(dataPoints->points);
}

//Helpers
//
// 
// Function to read data points from a file
double calculateSquaredEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
{
    double sum = 0.0;
    for (size_t i = 0; i < point1->size; ++i)
    {
        sum += pow(point1->attributes[i] - point2->attributes[i], 2);
    }
    return sum;
}

double calculateEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
{
    return sqrt(calculateSquaredEuclideanDistance(point1, point2));
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
    char* token = strtok(firstLine, " ");
    while (token != NULL)
    {
        dimensions++;
        token = strtok(NULL, " ");
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

    char line[256];

    while (fgets(line, sizeof(line), file))
    {
        DataPoint point;
        
        point.attributes = malloc(sizeof(int) * 256);
        handleMemoryError(point.attributes);
        point.size = 0;

        char* token = strtok(line, " ");
        while (token != NULL)
        {
            point.attributes[point.size++] = atoi(token);
            token = strtok(NULL, " ");
        }

        DataPoint* newPoints = realloc(dataPoints.points, sizeof(DataPoint) * (dataPoints.size + 1));
        handleMemoryError(newPoints);
        dataPoints.points = newPoints;
        dataPoints.points[dataPoints.size++] = point;
    }

    fclose(file);

    return dataPoints;
}

// Function to write the centroids to a file
void writeCentroidsToFile(const char* filename, DataPoint* centroids, int numCentroids)
{
    FILE* centroidFile = fopen(filename, "w");
    if (centroidFile == NULL)
    {
        handleFileError(filename);
    }

    for (int i = 0; i < numCentroids; ++i)
    {
        for (size_t j = 0; j < centroids[i].size; ++j)
        {
            fprintf(centroidFile, "%f ", centroids[i].attributes[j]);
        }
        fprintf(centroidFile, "\n");
    }

    fclose(centroidFile);
    printf("Centroid file created successfully: %s\n", filename);
}

// Function to write partition to a text file
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

// Clustering
//
// 
// Function to chooses random data points to be centroids
void generateRandomCentroids(int numCentroids, DataPoints* dataPoints, DataPoint* centroids)
{
    srand((unsigned int)time(NULL));

    // Create a copy of the dataPoints array
    DataPoint* dataPointsShuffled = malloc(sizeof(DataPoint) * dataPoints->size);
    handleMemoryError(dataPointsShuffled);

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        dataPointsShuffled[i] = dataPoints->points[i];
    }

    // Shuffle the dataPointsShuffled array
    for (size_t i = dataPoints->size - 1; i > 0; --i)
    {
        size_t j = rand() % (i + 1);
        DataPoint temp = dataPointsShuffled[i];
        dataPointsShuffled[i] = dataPointsShuffled[j];
        dataPointsShuffled[j] = temp;
    }

    // Copy the first numCentroids elements of dataPointsShuffled to centroids
    for (int i = 0; i < numCentroids; ++i)
    {
        centroids[i] = dataPointsShuffled[i];
    }

    free(dataPointsShuffled);
}

//Function to calculate the sum of squared errors (SSE)
double calculateSSE(DataPoint* dataPoints, int dataPointsSize, DataPoint* centroids, int centroidsSize, int* partition, int partitionSize)
{
    if (dataPointsSize != partitionSize)
    {
        fprintf(stderr, "Error: Data points and partition size mismatch\n");
        exit(EXIT_FAILURE);
    }

    double sse = 0.0;

    for (int i = 0; i < partitionSize; ++i)
    {
        int cIndex = partition[i];

        if (cIndex >= 0 && cIndex < centroidsSize)
        {
            // SSE between the data point and its assigned centroid
            sse += calculateEuclideanDistance(&dataPoints[i], &centroids[cIndex]);
        }
        else
        {
            fprintf(stderr, "Error: Invalid centroid index in partition\n");
            exit(EXIT_FAILURE);
        }
    }

    return sse;
}

// Function to assign each data point to the nearest centroid
// note: not used yet
void assignDataPointsToCentroids(DataPoint* dataPoints, int dataPointsSize, DataPoint* centroids, int centroidsSize, int* partition)
{
	for (int i = 0; i < dataPointsSize; ++i)
	{
		double minDistance = calculateEuclideanDistance(&dataPoints[i], &centroids[0]);
		int minIndex = 0;

		for (int j = 1; j < centroidsSize; ++j)
		{
			double distance = calculateEuclideanDistance(&dataPoints[i], &centroids[j]);
			if (distance < minDistance)
			{
				minDistance = distance;
				minIndex = j;
			}
		}

		partition[i] = minIndex;
	}
}

// Function to find the nearest centroid of a data point
DataPoint findNearestCentroid(DataPoint* queryPoint, DataPoint* clusterPoints, int clusterPointsSize)
{
    if (clusterPointsSize == 0)
    {
        fprintf(stderr, "Error: Cannot find nearest centroid in an empty set of data\n");
        exit(EXIT_FAILURE);
    }

    int nearestCentroid = -1;
    double* distances = malloc(sizeof(double) * clusterPointsSize);
    handleMemoryError(distances);

    for (int i = 0; i < clusterPointsSize; ++i)
    {
        distances[i] = calculateEuclideanDistance(queryPoint, &clusterPoints[i]);
    }

    double minDistance = distances[0];

    for (int i = 0; i < clusterPointsSize; ++i)
    {
        if (distances[i] <= minDistance)
        {
            minDistance = distances[i];
            nearestCentroid = i;
        }
    }

    // This is not currently in use, could be utilized for Fast K-means
    //queryPoint->minDistance = minDistance;

    free(distances);

    return clusterPoints[nearestCentroid];
}

// Function to check if two data points are equal
//note: not used yet
//note: should we even use this, or just work with the indexes?
bool areDataPointsEqual(DataPoint* point1, DataPoint* point2)
{
    if (point1->size != point2->size)
    {
        return false;
    }

    for (size_t i = 0; i < point1->size; ++i)
    {
        if (point1->attributes[i] != point2->attributes[i])
        {
            return false;
        }
    }

    return true;
}

// Function to update the centroids
//note: not used yet
void updateCentroids(DataPoint* dataPoints, int dataPointsSize, DataPoint* centroids, int centroidsSize, int* partition)
{
	int* clusterSizes = malloc(sizeof(int) * centroidsSize);
	handleMemoryError(clusterSizes);

	// Initialize the cluster sizes to 0
	for (int i = 0; i < centroidsSize; ++i)
	{
		clusterSizes[i] = 0;
	}

	// Initialize the centroids to 0
	for (int i = 0; i < centroidsSize; ++i)
	{
		for (size_t j = 0; j < dataPoints->size; ++j)
		{
			centroids[i].attributes[j] = 0.0;
		}
	}

	// Calculate the sum of the data points in each cluster
	for (int i = 0; i < dataPointsSize; ++i)
	{
		int cIndex = partition[i];

		if (cIndex >= 0 && cIndex < centroidsSize)
		{
			clusterSizes[cIndex]++;

			for (size_t j = 0; j < dataPoints->size; ++j)
			{
				centroids[cIndex].attributes[j] += dataPoints[i].attributes[j];
			}
		}
		else
		{
			fprintf(stderr, "Error: Invalid centroid index in partition\n");
			exit(EXIT_FAILURE);
		}
	}

	// Calculate the average of the data points in each cluster
	for (int i = 0; i < centroidsSize; ++i)
	{
		if (clusterSizes[i] > 0)
		{
			for (size_t j = 0; j < dataPoints->size; ++j)
			{
				centroids[i].attributes[j] /= clusterSizes[i];
			}
		}
	}

	free(clusterSizes);
}

// Function for optimal partitioning
int* optimalPartition(DataPoint* dataPoints, int dataPointsSize, DataPoint* centroids, int centroidsSize)
{
    if (dataPointsSize == 0 || centroidsSize == 0)
    {
        fprintf(stderr, "Error: Cannot perform optimal partition with empty data or centroids\n");
        exit(EXIT_FAILURE);
    }

    int* partition = malloc(sizeof(int) * dataPointsSize);
    handleMemoryError(partition);

    // Initialize the partition with -1
    for (int i = 0; i < dataPointsSize; ++i)
    {
        partition[i] = -1;
    }

    // Iterate through each data point to find its nearest centroid
    for (int i = 0; i < dataPointsSize; ++i)
    {
        DataPoint nearestCentroid = findNearestCentroid(&dataPoints[i], centroids, centroidsSize);

        // Find the index of the nearest centroid
        for (int j = 0; j < centroidsSize; ++j)
        {
            if (areDataPointsEqual(&nearestCentroid, &centroids[j]))
            {
                // Update the partition with the index of the nearest centroid
                partition[i] = j;
                break;
            }
        }
    }

    return partition;
}

// KLU: HUOM!BUG!
// KLU: random swap + esimerkiksi unbalanced data set sylkee tänne välillä tyhjiä vektoreita
// KLU: ja koodi kaatuu siten heti ensimmäiseen iffiin. Tämä vaatii gradun tapauksessa tarkastelua
// Calculate the centroid of a set of data points
DataPoint calculateCentroid(DataPoint* dataPoints, int dataPointsSize)
{
    if (dataPointsSize == 0)
    {
        fprintf(stderr, "Error: Cannot calculate centroid for an empty set of data points\n");
        exit(EXIT_FAILURE);
    }

    DataPoint centroid;
    centroid.attributes = malloc(sizeof(double) * dataPoints[0].size);
    handleMemoryError(centroid.attributes);
    centroid.size = dataPoints[0].size;

    // Initialize the centroid attributes to 0
    for (size_t i = 0; i < centroid.size; ++i)
    {
        centroid.attributes[i] = 0.0;
    }

    // Loop through each dimension
    for (size_t dim = 0; dim < centroid.size; ++dim)
    {
        double sum = 0.0;

        // Calculate the sum of the current dimension
        for (int i = 0; i < dataPointsSize; ++i)
        {
            sum += dataPoints[i].attributes[dim];
        }

        // Calculate the average for the current dimension and add it to centroid
        centroid.attributes[dim] = sum / dataPointsSize;
    }

    return centroid;
}

// Function to perform the k-means algorithm
//note: not used yet
void kMeans(DataPoint* dataPoints, int dataPointsSize, DataPoint* centroids, int centroidsSize, int* partition)
{
	if (dataPointsSize == 0 || centroidsSize == 0)
	{
		fprintf(stderr, "Error: Cannot perform k-means with empty data or centroids\n");
		exit(EXIT_FAILURE);
	}

	// Initialize the partition with -1
	for (int i = 0; i < dataPointsSize; ++i)
	{
		partition[i] = -1;
	}

	// Assign each data point to the nearest centroid
	assignDataPointsToCentroids(dataPoints, dataPointsSize, centroids, centroidsSize, partition);

	// Update the centroids
	updateCentroids(dataPoints, dataPointsSize, centroids, centroidsSize, partition);
}

// Function to perform the centroid step in k-means
DataPoint* kMeansCentroidStep(DataPoint* dataPoints, int dataPointsSize, int* partition, int numClusters)
{
    DataPoint* newCentroids = malloc(sizeof(DataPoint) * numClusters);
    handleMemoryError(newCentroids);

    // Create an array of DataPoint arrays to hold the clusters
    DataPoint** clusters = malloc(sizeof(DataPoint*) * numClusters);
    handleMemoryError(clusters);

    // Initialize the clusters
    for (int i = 0; i < numClusters; ++i)
    {
        clusters[i] = malloc(sizeof(DataPoint) * dataPointsSize);
        handleMemoryError(clusters[i]);
    }

    // Assign each data point to its cluster
    for (int i = 0; i < dataPointsSize; ++i)
    {
        int clusterLabel = partition[i];
        clusters[clusterLabel][i] = dataPoints[i];
    }

    // Calculate the new centroids
    for (int clusterLabel = 0; clusterLabel < numClusters; ++clusterLabel)
    {
        newCentroids[clusterLabel] = calculateCentroid(clusters[clusterLabel], dataPointsSize);
    }

    // Free the memory allocated for the clusters
    for (int i = 0; i < numClusters; ++i)
    {
        free(clusters[i]);
    }
    free(clusters);

    return newCentroids;
}

KMeansResult runKMeans(DataPoint* dataPoints, int dataPointsSize, int iterations, DataPoint* centroids, int numClusters)
{
    double bestSse = DBL_MAX;
    int stopCounter = 0;
    double previousSSE = DBL_MAX;
    int* previousPartition = malloc(sizeof(int) * dataPointsSize);
    handleMemoryError(previousPartition);

    for (int i = 0; i < dataPointsSize; ++i)
    {
        previousPartition[i] = -1;
    }

    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        int* newPartition = optimalPartition(dataPoints, dataPointsSize, centroids, numClusters);

        centroids = kMeansCentroidStep(dataPoints, dataPointsSize, newPartition, numClusters);

        double sse = calculateSSE(dataPoints, dataPointsSize, centroids, numClusters, newPartition);
        printf("(runKMeans)Total SSE after iteration %d: %f\n", iteration + 1, sse);

        if (sse < bestSse)
        {
            bestSse = sse;
        }
        else if (sse == previousSSE)
        {
            stopCounter++;
        }

        if (stopCounter == 3)
        {
            break;
        }

        previousSSE = sse;
        free(previousPartition);
        previousPartition = newPartition;
    }

    KMeansResult result;
    result.sse = bestSse;
    result.partition = previousPartition;

    return result;
}

// Function to perform repeated k-means
//note: not used yet
KMeansResult repeatedKMeans(DataPoint* dataPoints, int dataPointsSize, int numClusters, int numRepeats)
{
	KMeansResult bestResult;
	bestResult.sse = DBL_MAX;
	bestResult.partition = NULL;

	for (int i = 0; i < numRepeats; ++i)
	{
		DataPoint* centroids = malloc(sizeof(DataPoint) * numClusters);
		handleMemoryError(centroids);

		generateRandomCentroids(numClusters, dataPoints, centroids);

		KMeansResult result = runKMeans(dataPoints, dataPointsSize, MAX_ITERATIONS, centroids, numClusters);

		if (result.sse < bestResult.sse)
		{
			bestResult = result;
		}

		free(centroids);
	}

	return bestResult;
}

/*

This requires a lot of work, and is not currently in use
Initial version from KLU course used high level of randomization
So this need to be refactored into split k-means

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

        KMeansResult result = runKMeans(dpoints, indexCount, 5, newCentroids, 2);

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

    KMeansResult result = runKMeans(dataPoints, dataPointsSize, MAX_ITERATIONS, centroids, numCentroids(centroids));
    printf("Split without global kmeans : %f\n", calculateSSE(dataPoints, dataPointsSize, centroids, numCentroids(centroids), partition));

    free(partition);
    free(centroids);

    return result.sse;
}*/

// Main
//
//
int mainTest()
{
	// Read data points from a file
	DataPoints dataPoints = readDataPoints(DATA_FILENAME);

	// Get the number of dimensions in the data
	int numDimensions = getNumDimensions(DATA_FILENAME);

	// Initialize the centroids
	DataPoint* centroids = malloc(sizeof(DataPoint) * NUM_CENTROIDS);
	handleMemoryError(centroids);

	// Generate random centroids
	generateRandomCentroids(NUM_CENTROIDS, &dataPoints, centroids);

	// Perform repeated k-means
	KMeansResult result = repeatedKMeans(dataPoints.points, dataPoints.size, NUM_CENTROIDS, MAX_REPEATS);

	// Write the centroids to a file
	writeCentroidsToFile(CENTROID_FILENAME, centroids, NUM_CENTROIDS);

	// Write the partition to a file
	writePartitionToFile(result.partition, dataPoints.size, PARTITION_FILENAME);

	// Free the memory allocated for the data points and centroids
	freeDataPoints(&dataPoints);
	free(centroids);
	free(result.partition);

	return 0;
}

int main()
{
    int numDimensions = getNumDimensions(DATA_FILENAME);

    if (numDimensions > 0)
    {
        printf("Number of dimensions in the data: %d\n", numDimensions);

        int dataPointsSize;
        DataPoints dataPoints = readDataPoints(DATA_FILENAME, &dataPointsSize);
        printf("Dataset size: %d\n", dataPointsSize);

        DataPoints centroids = generateRandomCentroids(NUM_CENTROIDS, dataPoints, dataPointsSize);
        DataPoints ogCentroids = copyCentroids(centroids, NUM_CENTROIDS);

        writeCentroidsToFile(centroids, NUM_CENTROIDS, CENTROID_FILENAME);
        writePartitionToFile(generateRandomPartitions(dataPointsSize, NUM_CENTROIDS), dataPointsSize, PARTITION_FILENAME);

        int* initialPartition = optimalPartition(dataPoints, dataPointsSize, centroids, NUM_CENTROIDS);
        double initialSSE = calculateSSE(dataPoints, dataPointsSize, centroids, NUM_CENTROIDS, initialPartition);
        printf("Initial Total Sum-of-Squared Errors (SSE): %f\n", initialSSE);

        clock_t start = clock();

        double bestSse1 = runKMeans(dataPoints, dataPointsSize, MAX_ITERATIONS, centroids, NUM_CENTROIDS);

        clock_t end = clock();
        double duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(K-means)Time taken: %f seconds\n", duration);

        double bestSse5 = bestSse1;

        for (int repeat = 0; repeat < MAX_REPEATS; ++repeat)
        {
            printf("round: %d\n", repeat);

            if (repeat != 0)
            {
                free(centroids);
                centroids = generateRandomCentroids(NUM_CENTROIDS, dataPoints, dataPointsSize);
            }

            double newSse = runKMeans(dataPoints, dataPointsSize, MAX_ITERATIONS, centroids, NUM_CENTROIDS);

            if (newSse < bestSse5)
            {
                bestSse5 = newSse;
            }
        }

        start = clock();

        double bestSse2 = randomSwap(dataPoints, dataPointsSize, ogCentroids, NUM_CENTROIDS, 0);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Random Swap)Time taken: %f seconds\n", duration);

        start = clock();

        double bestSse3 = randomSwap(dataPoints, dataPointsSize, ogCentroids, NUM_CENTROIDS, 1);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Deterministic)Time taken: %f seconds\n", duration);

        start = clock();

        double bestSse4 = runSplit(dataPoints, dataPointsSize, NUM_CENTROIDS);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Split)Time taken: %f seconds\n", duration);

        printf("(K-means)Best Sum-of-Squared Errors (SSE): %f\n", bestSse1);
        printf("(Repeated K-means)Best Sum-of-Squared Errors (SSE): %f\n", bestSse5);
        printf("(Random Swap)Best Sum-of-Squared Errors (SSE): %f\n", bestSse2);
        printf("(Deterministic Swap)Best Sum-of-Squared Errors (SSE): %f\n", bestSse3);
        printf("(Split)Best Sum-of-Squared Errors (SSE): %f\n", bestSse4);

        free(dataPoints);
        free(centroids);
        free(ogCentroids);
        free(initialPartition);

        return 0;
    }
}
