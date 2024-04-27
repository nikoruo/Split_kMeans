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
const int MAX_REPEATS = 1; // repeated k-means toistojen lkm
const int MAX_SWAPS = 500; // random swap toistojen lkm

//and for logging
const int LOGGING = 2; // 1 = basic, 2 = detailed, 3 = debug

//Structs
//
//
typedef struct
{
    double* attributes;
    size_t dimensions;
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
} Cluster;

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
void freeDataPoints(DataPoints* dataPoints)
{
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        free(dataPoints->points[i].attributes);
    }
    free(dataPoints->points);
}

// Function to free centroids
void freeCentroids(Centroids* centroids)
{
	for (int i = 0; i < centroids->size; ++i)
	{
		free(centroids->points[i].attributes);
	}
	free(centroids->points);
}

//Helpers
//
// 
// Function to read data points from a file
double calculateSquaredEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
{
    double sum = 0.0;
    for (size_t i = 0; i < point1->dimensions; ++i)
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
        DataPoint point;

        point.attributes = malloc(sizeof(int) * 256);
        handleMemoryError(point.attributes);
        point.dimensions = 0;

        char* context = NULL;
        char* token = strtok_s(line, " ", &context);
        while (token != NULL)
        {
            point.attributes[point.dimensions++] = atoi(token);
            token = strtok_s(NULL, " ", &context);
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

    if(LOGGING == 3)
    { 
        // Print the attributes of each data point    
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

//Funtion to copy centroids
void copyCentroids(Centroids* source, Centroids* destination, int numCentroids)
{
    destination->size = source->size;
    for (int i = 0; i < numCentroids; ++i)
    {
        destination->points[i].dimensions = source->points[i].dimensions;
        destination->points[i].attributes = malloc(source->points[i].dimensions * sizeof(double));
        handleMemoryError(destination->points[i].attributes);
        memcpy(destination->points[i].attributes, source->points[i].attributes, source->points[i].dimensions * sizeof(double)); //false positive
    }
}


// Clustering
//
// 
// Function to chooses random data points to be centroids
void generateRandomCentroids(int numCentroids, DataPoints* dataPoints, DataPoint* centroids)
{
    srand((unsigned int)time(NULL));

    if (dataPoints->size < numCentroids)
    {
        fprintf(stderr, "Error: There are less data points than the required number of clusters\n");
        exit(EXIT_FAILURE);
    }

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
double calculateSSE(DataPoint* dataPoints, int dataPointsSize, DataPoint* centroids, int centroidsSize, int* partition)
{
    double sse = 0.0;

    for (int i = 0; i < dataPointsSize; ++i)
    {
        int cIndex = partition[i];

        if (cIndex >= 0 && cIndex < centroidsSize)
        {
            // SSE between the data point and its assigned centroid
            sse += calculateEuclideanDistance(&dataPoints[i], &centroids[cIndex]);
        }
        else
        {
            //Debugging
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
int findNearestCentroid(DataPoint* queryPoint, DataPoint* targetPoints, int targetPointsSize)
{
    if (targetPointsSize == 0)
    {
        fprintf(stderr, "Error: Cannot find nearest centroid in an empty set of data\n");
        exit(EXIT_FAILURE);
    }

    int nearestCentroid = -1;
    double* distances = malloc(sizeof(double) * targetPointsSize);
    handleMemoryError(distances);

    for (int i = 0; i < targetPointsSize; ++i)
    {
        distances[i] = calculateEuclideanDistance(queryPoint, &targetPoints[i]);
    }

    double minDistance = distances[0];

    for (int i = 0; i < targetPointsSize; ++i)
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

    return nearestCentroid;
}

// Function to check if two data points are equal
//note: should we even use this, or just work with the indexes?
//note2: tämän käyttö on poistettu joka paikasta, joten voisi poistaa koko funktion
bool areDataPointsEqual(DataPoint* point1, DataPoint* point2)
{
    if (point1->dimensions != point2->dimensions)
    {
        return false;
    }

    for (size_t i = 0; i < point1->dimensions; ++i)
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
		for (size_t j = 0; j < dataPoints->dimensions; ++j)
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

			for (size_t j = 0; j < dataPoints->dimensions; ++j)
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
			for (size_t j = 0; j < dataPoints->dimensions; ++j)
			{
				centroids[i].attributes[j] /= clusterSizes[i];
			}
		}
	}

	free(clusterSizes);
}

// Function for optimal partitioning
int* optimalPartition(DataPoint* dataPoints, int dataPointsSize, DataPoint* centroids, int centroidsSize, Cluster* newClusters)
{
    if (dataPointsSize == 0 || centroidsSize == 0)
    {
        fprintf(stderr, "Error: Cannot perform optimal partition with empty data or centroids\n");
        exit(EXIT_FAILURE);
    }

    int* partition = malloc(sizeof(int) * dataPointsSize);
    handleMemoryError(partition);
    bool emptyClusters = true;

    while(emptyClusters){
        
        // Initialize the partition with -1
        // TODO: Do we need to initialize the partition with -1?
        for (int i = 0; i < dataPointsSize; ++i)
        {
            partition[i] = -1;
        }

        // Iterate through each data point to find its nearest centroid
        for (int i = 0; i < dataPointsSize; ++i)
        {
            int nearestCentroidId = findNearestCentroid(&dataPoints[i], centroids, centroidsSize);
            partition[i] = nearestCentroidId;
        }               

        // Initialize the clusters
        for (int i = 0; i < centroidsSize; ++i)
        {
            newClusters[i].size = 0;
        }

        // Assign each data point to its cluster
        for (int i = 0; i < dataPointsSize; ++i)
        {
            int clusterLabel = partition[i];
            newClusters[clusterLabel].points[newClusters[clusterLabel].size++] = dataPoints[i];
        }

        // Check for clusters with size 0
        for (int i = 0; i < centroidsSize; ++i)
        {
            if (LOGGING == 2) printf("Checking: Cluster size\n");
            if (LOGGING == 2) printf("Cluster size: %d\n", newClusters[i].size);
            if (newClusters[i].size == 0)
            {
                if (LOGGING == 2) printf("Warning: Cluster has size 0\n");

                int randomIndex = rand() % dataPointsSize;
                centroids[i] = dataPoints[randomIndex];
                emptyClusters = true;
            }
            else
            {
                emptyClusters = false;
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
    if (dataPointsSize <= 0)
    {
        fprintf(stderr, "Error: Cannot calculate centroid for an empty set of data points\n");
        exit(EXIT_FAILURE);
    }

    DataPoint centroid;
    centroid.attributes = malloc(sizeof(double) * dataPoints[0].dimensions);
    handleMemoryError(centroid.attributes);
    centroid.dimensions = dataPoints[0].dimensions;

    // Initialize the centroid attributes to 0
    for (size_t i = 0; i < centroid.dimensions; ++i)
    {
        centroid.attributes[i] = 0.0;
    }

    // Loop through each dimension
    for (size_t dim = 0; dim < centroid.dimensions; ++dim)
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
DataPoint* kMeansCentroidStep(Cluster* clusters , int numClusters)
{
    // Calculate the new centroids
    DataPoint* newCentroids = malloc(sizeof(DataPoint) * numClusters);
    handleMemoryError(newCentroids);
    for (int clusterLabel = 0; clusterLabel < numClusters; ++clusterLabel)
    {
        newCentroids[clusterLabel] = calculateCentroid(newClusters[clusterLabel].points, newClusters[clusterLabel].size);
    }

    return newCentroids;
}

KMeansResult runKMeans(DataPoint* dataPoints, int dataPointsSize, int iterations, DataPoint* centroids, int numClusters)
{
    double bestSse = DBL_MAX;
    int stopCounter = 0;
    double previousSSE = DBL_MAX;

    int* previousPartition = malloc(sizeof(int) * dataPointsSize);
    handleMemoryError(previousPartition);

    Cluster* newClusters = malloc(sizeof(Cluster) * numClusters);
    handleMemoryError(newClusters);

    // Initialize the clusters
    for (int i = 0; i < numClusters; ++i)
    {
        newClusters[i].points = malloc(sizeof(DataPoint) * dataPointsSize);
        newClusters[i].size = 0;
        handleMemoryError(newClusters[i].points);
    }

    for (int i = 0; i < dataPointsSize; ++i)
    {
        previousPartition[i] = -1;
    }

    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        int* newPartition = optimalPartition(dataPoints, dataPointsSize, centroids, numClusters, newClusters);

        centroids = kMeansCentroidStep(dataPoints, dataPointsSize, newPartition, numClusters);

        double sse = calculateSSE(dataPoints, dataPointsSize, centroids, numClusters, newPartition);
        if(LOGGING == 2) printf("(runKMeans)Total SSE after iteration %d: %.0f\n", iteration + 1, sse);
        
        if (sse < bestSse)
        {
            bestSse = sse;
        }
        else if (sse == previousSSE)
        {
            stopCounter++;
        }       

        previousSSE = sse;
        previousPartition = newPartition;

        if (stopCounter == 3)
        {
            break;
        }
    }

    KMeansResult result;
    result.sse = bestSse;
    result.partition = previousPartition;
    result.centroids = centroids;

    free(newClusters->points);
    free(newClusters);

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

		generateRandomCentroids(NUM_CENTROIDS, dataPoints, centroids);

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

//random swap beta
//note: not used yet
double randomSwapBeta(DataPoints dataPoints, int dataPointsSize, DataPoints ogCentroids, int numCentroids, int deterministic)
{
    DataPoint* centroids = malloc(sizeof(DataPoint) * numCentroids);
    handleMemoryError(centroids);

    for (int i = 0; i < numCentroids; ++i)
    {
        centroids[i].attributes = malloc(sizeof(double) * dataPoints.points[0].dimensions);
        handleMemoryError(centroids[i].attributes);
        centroids[i].dimensions = dataPoints.points[0].dimensions;
    }

    for (int i = 0; i < numCentroids; ++i)
    {
        for (size_t j = 0; j < dataPoints.points[0].dimensions; ++j)
        {
            centroids[i].attributes[j] = ogCentroids.points[i].attributes[j];
        }
    }

    int* partition = malloc(sizeof(int) * dataPointsSize);
    handleMemoryError(partition);

    for (int i = 0; i < dataPointsSize; ++i)
    {
        partition[i] = -1;
    }

    double bestSse = calculateSSE(dataPoints.points, dataPointsSize, centroids, numCentroids, partition);

    for (int i = 0; i < MAX_SWAPS; ++i)
    {
        int c1 = rand() % numCentroids;
        int c2 = c1;

        while (c2 == c1)
        {
            c2 = rand() % numCentroids;
        }

        DataPoint temp = centroids[c1];
        centroids[c1] = centroids[c2];
        centroids[c2] = temp;

        int* newPartition = optimalPartition(dataPoints.points, dataPointsSize, centroids, numCentroids);
        double newSse = calculateSSE(dataPoints.points, dataPointsSize, centroids, numCentroids, newPartition);

        if (newSse < bestSse)
        {
            bestSse = newSse;
            for (int i = 0; i < dataPointsSize; ++i)
            {
                partition[i] = newPartition[i];
            }
        }

        free(newPartition);
    }

    printf("(Random Swap)Best Sum-of-Squared Errors (SSE): %f\n", bestSse);

    free(partition);
    free(centroids);

    return bestSse;
}

double randomSwap(DataPoints* dataPoints, int dataPointsSize, DataPoint* centroids, int centroidsSize)
{
    double bestSse = DBL_MAX;
    DataPoint oldCentroid;

    for (int i = 0; i < MAX_SWAPS; ++i)
    {
        int randomDataPoint = -1;

        int randomCentroid = rand() % centroidsSize;
        oldCentroid = centroids[randomCentroid];

        //random swap
        randomDataPoint = rand() % dataPoints->size;

        centroids[randomCentroid] = dataPoints->points[randomDataPoint];

        KMeansResult result = runKMeans(dataPoints->points, dataPointsSize, MAX_ITERATIONS, centroids, centroidsSize);
        double sse = result.sse;

        //If SSE improves, we keep the change
        //if not, we reverse the swap
        if (sse < bestSse)
        {
            bestSse = sse;
        }
        else
        {
            centroids[randomCentroid] = oldCentroid;
        }
    }

    return bestSse;
}


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
	KMeansResult result = repeatedKMeans(dataPoints.points, (int)dataPoints.size, NUM_CENTROIDS, MAX_REPEATS);

	// Write the centroids to a file
	writeCentroidsToFile(CENTROID_FILENAME, centroids, NUM_CENTROIDS);

	// Write the partition to a file
	writePartitionToFile(result.partition, (int)dataPoints.size, PARTITION_FILENAME);

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

        DataPoints dataPoints = readDataPoints(DATA_FILENAME);
        printf("Dataset size: %d\n", (int)dataPoints.size);

        Centroids centroids;
        centroids.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        centroids.size = NUM_CENTROIDS;
        generateRandomCentroids(NUM_CENTROIDS, &dataPoints, centroids.points);
        
        Centroids ogCentroids;
        ogCentroids.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        ogCentroids.size = NUM_CENTROIDS;
        copyCentroids(&centroids, &ogCentroids, NUM_CENTROIDS);

        writeCentroidsToFile(CENTROID_FILENAME, centroids.points, NUM_CENTROIDS);
        //not used yet, need to be added later in code (would be useless here)
        //writePartitionToFile();

        int* initialPartition = optimalPartition(dataPoints.points, (int)dataPoints.size, centroids.points, NUM_CENTROIDS);
        double initialSSE = calculateSSE(dataPoints.points, (int)dataPoints.size, centroids.points, NUM_CENTROIDS, initialPartition);
        printf("Initial Total Sum-of-Squared Errors (SSE): %.0f\n", initialSSE / 10000000);

        //This runs the k-means algorithm just once
        //but we wanna use teh repeated k-means
        /*
        printf("K-means\n");
        clock_t start = clock();

        KMeansResult result = runKMeans(dataPoints.points, (int)dataPoints.size, MAX_ITERATIONS, centroids.points, NUM_CENTROIDS);
        double bestSse1 = result.sse;

        clock_t end = clock();
        double duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(K-means)Time taken: %f seconds\n", duration);
        

        double bestSse5 = bestSse1;
        */

        clock_t start = clock();
        printf("Repeated K-means\n");

        double RKSse = DBL_MAX;
        KMeansResult result;

		result.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint));
		handleMemoryError(result.centroids);
		result.partition = malloc(dataPoints.size * sizeof(int));
		handleMemoryError(result.partition);

        for (int repeat = 0; repeat < MAX_REPEATS; ++repeat)
        {
            if(LOGGING == 2) printf("round: %d\n", repeat);

            if (repeat != 0)
            {
                generateRandomCentroids(NUM_CENTROIDS, &dataPoints, centroids.points);
            }

            result = runKMeans(dataPoints.points, (int)dataPoints.size, MAX_ITERATIONS, centroids.points, NUM_CENTROIDS);
            double newSse = result.sse;

            if (newSse < RKSse)
            {
                RKSse = newSse;
            }
        }
        
        clock_t end = clock();
        double duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Repeated k-means)Time taken: %f seconds\n", duration);

        printf("Random swap\n");
        start = clock();

        double SseRS = randomSwap(&dataPoints, (int)dataPoints.size, ogCentroids.points, NUM_CENTROIDS);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Random Swap)Time taken: %f seconds\n", duration);

        printf("Split k-means\n");
        start = clock();

        //not refactored yet
        //double bestSse4 = runSplit(dataPoints, dataPoints.size, NUM_CENTROIDS);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Split)Time taken: %f seconds\n", duration);

        //printf("(K-means)Best Sum-of-Squared Errors (SSE): %.0f\n", bestSse1);
        printf("(Repeated K-means)Best Sum-of-Squared Errors (SSE): %f\n", RKSse / 10000000);
        printf("(Random Swap)Best Sum-of-Squared Errors (SSE): %f\n", SseRS / 10000000);
        //printf("(Split)Best Sum-of-Squared Errors (SSE): %f\n", bestSse4);

        freeDataPoints(&dataPoints);
        freeCentroids(&centroids);
        freeCentroids(&ogCentroids);
        free(initialPartition);
        freeDataPoints(&result.centroids);
        free(result.partition);

        return 0;
    }
}
