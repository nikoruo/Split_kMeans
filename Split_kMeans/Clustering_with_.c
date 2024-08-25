#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <float.h>

//Constants for file locations
const char* DATA_FILENAME = "data/s1.txt";
const char* GT_FILENAME = "GroundTruth/s1-cb.txt";
const char* CENTROID_FILENAME = "outputs/centroid.txt";
const char* PARTITION_FILENAME = "outputs/partition.txt";
const char SEPARATOR = ' ';

//and for clustering
const int NUM_CENTROIDS = 15;  // klustereiden lkm: s4 = 15, unbalanced = 8
const int MAX_ITERATIONS = 100; // k-means rajoitus
const int MAX_REPEATS = 40; // repeated k-means toistojen lkm
const int MAX_SWAPS = 100; // random swap toistojen lkm

//and for logging
const int LOGGING = 1; // 1 = basic, 2 = detailed, 3 = debug

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
    int centroidIndex;
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

// Function to free clusters
void freeClusters(Cluster* clusters)
{
    for (int i = 0; i < clusters->size; ++i)
    {
        free(clusters->points[i].attributes);
    }
    free(clusters->points);
    free(clusters);
}

// Function to free KMeansResult
void freeKMeansResult(KMeansResult* result)
{
    // Free the partition array
    if (result->partition != NULL)
    {
        free(result->partition);
        result->partition = NULL; // Avoid dangling pointer
    }

    // Free each DataPoint in the centroids array
    if (result->centroids != NULL)
    {
        for (int i = 0; i < result->centroidIndex; ++i)
        {
            if (result->centroids[i].attributes != NULL)
            {
                free(result->centroids[i].attributes);
                result->centroids[i].attributes = NULL; // Avoid dangling pointer
            }
        }
        // Free the centroids array itself
        free(result->centroids);
        result->centroids = NULL; // Avoid dangling pointer
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
        sum += pow(point1->attributes[i] - point2->attributes[i], 2);
    }
    return sum;
}

//function to calculate the Euclidean distance between two data points
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

//Funtion to create a deep copy of a data point
void deepCopyDataPoint(DataPoint* destination, DataPoint* source)
{
    destination->dimensions = source->dimensions;
    destination->attributes = malloc(source->dimensions * sizeof(double));
    handleMemoryError(destination->attributes);
    memcpy(destination->attributes, source->attributes, source->dimensions * sizeof(double));
}

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
    //memcpy(dataPointsShuffled, dataPoints->points, dataPoints->size * sizeof(DataPoint));

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        dataPointsShuffled[i].dimensions = dataPoints->points[i].dimensions;
        deepCopyDataPoint(&dataPointsShuffled[i], &dataPoints->points[i]);
    }

    /*
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        dataPointsShuffled[i] = dataPoints->points[i];
    }
    */

    // Shuffle the dataPointsShuffled array
    for (size_t i = 0; i < numCentroids; ++i)
    {
        size_t j = rand() % (i + 1);
        DataPoint temp = dataPointsShuffled[i];
        dataPointsShuffled[i] = dataPointsShuffled[j];
        dataPointsShuffled[j] = temp;
    }

    // Copy the first numCentroids elements of dataPointsShuffled to centroids
    for (int i = 0; i < numCentroids; ++i)
    {
        centroids[i].dimensions = dataPointsShuffled[i].dimensions;
        deepCopyDataPoint(&centroids[i], &dataPointsShuffled[i]);
    }

    // Free the shuffled data points
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        free(dataPointsShuffled[i].attributes);
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
        
        emptyClusters = false;

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
            deepCopyDataPoint(&newClusters[clusterLabel].points[newClusters[clusterLabel].size], &dataPoints[i]);
            newClusters[clusterLabel].size++;
        }

        // Check for clusters with size 0
        for (int i = 0; i < centroidsSize; ++i)
        {
            if (LOGGING == 2) printf("Checking: Cluster size\n");
            if (LOGGING == 2) printf("Cluster size: %d\n", newClusters[i].size);
            if (newClusters[i].size == 0)
            {
                if (LOGGING == 1) printf("Warning: Cluster has size 0\n");

                int randomIndex = rand() % dataPointsSize;
                deepCopyDataPoint(&centroids[i], &dataPoints[randomIndex]);
                emptyClusters = true;
            }
        }
    }

    return partition;
}

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

// Function to perform the centroid step in k-means
DataPoint* kMeansCentroidStep(Cluster* newClusters , int numClusters)
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

// Function to run the k-means algorithm
KMeansResult runKMeans(DataPoint* dataPoints, int dataPointsSize, int iterations, DataPoint* centroids, int numClusters, DataPoints* groundTruth)
{
    int stopCounter = 0;
    double bestSse = DBL_MAX;
    double previousSSE = DBL_MAX;
    int centroidIndex = -1;

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

        DataPoint* newCentroids = kMeansCentroidStep(newClusters, numClusters);

        double sse = calculateSSE(dataPoints, dataPointsSize, newCentroids, numClusters, newPartition);
        if(LOGGING == 2) printf("(runKMeans)Total SSE after iteration %d: %.0f\n", iteration + 1, sse / 10000000);
        
        //TODO: tarvitaanko tätä, jos ei niin poista
        //koska sse pitäisi olla aina < tai = kuin bestSse
        if (sse < bestSse)
        {
            bestSse = sse;
        }
        else if (sse == previousSSE)
        {
            stopCounter++;
        }

        centroidIndex = calculateCentroidIndex(newCentroids, numClusters, groundTruth->points, groundTruth->size);
        if (LOGGING == 2) printf("(runKMeans)CI after iteration %d: %d\n", iteration + 1, centroidIndex);

        previousSSE = sse;
        previousPartition = newPartition;
        //centroids = newCentroids;
        // Assuming newCentroids is calculated as the average of data points in each cluster
        for (int i = 0; i < numClusters; ++i)
        {
            deepCopyDataPoint(&centroids[i], &newCentroids[i]);
        }
        free(newCentroids); //freeDataPoints(newCentroids) <- TODO: miksei toimi?
        newCentroids = NULL;

        if (stopCounter == 3)
        {
            break;
        }
    }

    KMeansResult result;
    result.sse = bestSse;
    result.partition = previousPartition;
    result.centroids = centroids;
    result.centroidIndex = centroidIndex;


    freeClusters(newClusters);
    newClusters = NULL;

    return result;
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

// Function to perform random swap
KMeansResult randomSwap(DataPoint* dataPoints, int dataPointsSize, DataPoint* centroids, int centroidsSize, DataPoints* groundTruth)
{
    double bestSse = DBL_MAX;
    int bestCI = -1;
    //DataPoint oldCentroid;
    KMeansResult bestResult;

    bestResult.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint)); //since results.centroids = centroids.points, do we need to allocate memory?
    handleMemoryError(bestResult.centroids);
    bestResult.partition = malloc(dataPointsSize * sizeof(int));
    handleMemoryError(bestResult.partition);

    for (int i = 0; i < MAX_SWAPS; ++i)
    {
        int randomCentroid = rand() % centroidsSize;
        //oldCentroid = centroids[randomCentroid];
        DataPoint oldCentroid;
        deepCopyDataPoint(&oldCentroid, &centroids[randomCentroid]);

        int randomDataPoint = rand() % dataPointsSize;

        //random swap
        deepCopyDataPoint(&centroids[randomCentroid], &dataPoints[randomDataPoint]);

        KMeansResult result = runKMeans(dataPoints, dataPointsSize, 2, centroids, centroidsSize, groundTruth);

        //If SSE improves, we keep the change
        //if not, we reverse the swap
        if (bestCI == -1 || result.centroidIndex < bestResult.centroidIndex || result.sse < bestResult.sse)
        {
            bestCI = 1;
            bestResult.sse = result.sse;
            bestResult.centroidIndex = result.centroidIndex;
            memcpy(bestResult.partition, result.partition, dataPointsSize * sizeof(int));
            deepCopyDataPoints(bestResult.centroids, result.centroids, centroidsSize);
        }
        else
        {
            //centroids[randomCentroid] = oldCentroid;
            deepCopyDataPoint(&centroids[randomCentroid], &oldCentroid);
        }

        free(oldCentroid.attributes);
    }

    return bestResult;
}

//function to calculate Centroid Index (CI)
int calculateCentroidIndex(DataPoint* centroids1, int size1, DataPoint* centroids2, int size2)
{
    int countFrom1to2 = 0;
    int countFrom2to1 = 0;

    //TODO: tämä pitäisi pohjustaa isomman (size1 vs. size2) avulla
    //keksi uusi nimi, esim Diskreetit rakenteet kurssilta se miten c1 piirretään nuolia c2 jne (transpoosio? yms)
    int* closest = malloc(sizeof(int) * size1);
    handleMemoryError(closest);
    for (int i = 0; i < size1; ++i)
    {
        closest[i] = 0;
    }

    //TODO: Voisiko tästä tehdä funktion?'
    // koska nyt toistetaan C1-> C2 ja C2 -> C1
    // niin duplikaattikoodia
    // C1 -> C2
    for (int i = 0; i < size1; ++i)
    {
        double minDistance = DBL_MAX;
        int closestIndex = -1;

        for (int j = 0; j < size2; ++j)
        {
            double distance = calculateSquaredEuclideanDistance(&centroids1[i], &centroids2[j]);
            if (distance < minDistance)
            {
                minDistance = distance;
                closestIndex = j;
            }
        }

        if (closestIndex != -1)
		{
			closest[closestIndex] += 1;
		}
    }

    for (int i = 0; i < size1; ++i)
    {
        if (closest[i] == 0)
        {
            countFrom1to2++;
        }
    }

    // C2 -> C1
    for (int i = 0; i < size2; ++i)
    {
        double minDistance = DBL_MAX;
        int closestIndex = -1;

        for (int j = 0; j < size1; ++j)
        {
            double distance = calculateSquaredEuclideanDistance(&centroids2[i], &centroids1[j]);
            if (distance < minDistance)
            {
                minDistance = distance;
                closestIndex = j;
            }
        }

        if (closestIndex != -1)
        {
            closest[closestIndex] += 1;
        }
    }

    for (int i = 0; i < size2; ++i)
    {
        if (closest[i] == 0)
        {
            countFrom2to1++;
        }
    }

    free(closest);
    return (countFrom1to2 > countFrom2to1) ? countFrom1to2 : countFrom2to1;
}

// Main
//
//
int main()
{
    int numDimensions = getNumDimensions(DATA_FILENAME);

    if (numDimensions > 0)
    {
        printf("Number of dimensions in the data: %d\n", numDimensions);

        DataPoints dataPoints = readDataPoints(DATA_FILENAME);
        printf("Dataset size: %d\n", (int)dataPoints.size);

        DataPoints groundTruth = readDataPoints(GT_FILENAME);

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
        
        /*
        * optimalPartition vaatii nyt Clusters structin, mutta se taas alustetaan vasta runKMeansissa
        int* initialPartition = optimalPartition(dataPoints.points, (int)dataPoints.size, centroids.points, NUM_CENTROIDS);
        double initialSSE = calculateSSE(dataPoints.points, (int)dataPoints.size, centroids.points, NUM_CENTROIDS, initialPartition);
        printf("Initial Total Sum-of-Squared Errors (SSE): %.0f\n", initialSSE / 10000000);
        */

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

        double RKSse = DBL_MAX;
        KMeansResult result;
        int CI1 = -1;

        result.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint)); //since results.centroids = centroids.points, do we need to allocate memory?
        handleMemoryError(result.centroids);
        result.partition = malloc(dataPoints.size * sizeof(int));
        handleMemoryError(result.partition);

        clock_t start = clock();
        printf("Repeated K-means\n");

        for (int repeat = 0; repeat < MAX_REPEATS; ++repeat)
        {
            if(LOGGING == 2) printf("round: %d\n", repeat);

            if (repeat != 0)
            {
                generateRandomCentroids(NUM_CENTROIDS, &dataPoints, centroids.points);
            }

            result = runKMeans(dataPoints.points, (int)dataPoints.size, MAX_ITERATIONS, centroids.points, NUM_CENTROIDS, &groundTruth);

            //TODO: korjaa tämä koko homma, halutaan ottaa talteen paras tulos, ei pelkästään SSE (RKSse <- pitäisi olla KMeansResult?)
            if (result.sse < RKSse)
            {
                RKSse = result.sse;
                //TODO: korjaa oikeaan paikkaan ja koko rakenne
                CI1 = result.centroidIndex;
            }

        }
        
        clock_t end = clock();
        double duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Repeated k-means)Time taken: %.2f seconds\n", duration);


        KMeansResult result2;
        int CI2 = -1;

        result2.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint)); //since results.centroids = centroids.points, do we need to allocate memory?
        handleMemoryError(result2.centroids);
        result2.partition = malloc(dataPoints.size * sizeof(int));
        handleMemoryError(result2.partition);

        printf("Random swap\n");
        start = clock();

        //TODO: palautus oikeaan muotoon, nyt vain SSE
        result2 = randomSwap(dataPoints.points, (int)dataPoints.size, ogCentroids.points, NUM_CENTROIDS, &groundTruth);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Random Swap)Time taken: %.2f seconds\n", duration);

        printf("Split k-means\n");
        start = clock();

        //not refactored yet
        //double bestSse4 = runSplit(dataPoints, dataPoints.size, NUM_CENTROIDS);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Split)Time taken: %.2f seconds\n", duration);

        //printf("(K-means)Best Sum-of-Squared Errors (SSE): %.0f\n", bestSse1);
        printf("(Repeated K-means)Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (SSE): %.4f\n", CI1, RKSse / 10000000);
        printf("(Random Swap)Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (SSE): %.4f\n", result2.centroidIndex, result2.sse / 10000000);
        //printf("(Split)Best Centroid Index (CI): %d and best Sum-of-Squared Errors (SSE): %f\n", result3.centroidIndex, result3.sse / 10000000);

       
        if(LOGGING == 2) printf("Muistin vapautus\n");        
        freeDataPoints(&dataPoints);
        if(LOGGING == 3) printf("Eka\n");
        freeCentroids(&centroids);
        if(LOGGING == 3)printf("toka\n");
        freeCentroids(&ogCentroids);
        if(LOGGING == 3)printf("kolmas\n");
        //free(initialPartition);
        //freeDataPoints(&result.centroids); result.centroids = centroids.points, joten ei tarvitse vapauttaa
        //if(LOGGING == 1)printf("neljäs\n");
        free(result.partition);
        if(LOGGING == 3)printf("neljas\n");
        free(result2.partition);
        if (LOGGING == 3)printf("viides\n");

        return 0;
    }
}
