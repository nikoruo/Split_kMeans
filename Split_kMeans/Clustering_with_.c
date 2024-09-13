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

//and for clustering
const int NUM_CENTROIDS = 15;  // klustereiden lkm: s = 15, unbalanced = 8, a = 20,35,50
const int MAX_ITERATIONS = 1000; // k-means rajoitus
const int MAX_REPEATS = 100; // repeated k-means toistojen lkm, TODO: lopulliseen 100kpl
const int MAX_SWAPS = 5000; // random swap toistojen lkm, TODO: lopulliseen 1000kpl

//and for logging
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
    int centroidIndex;
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

// Function to free ClusteringResult
// TODO: ei vielä käytössä missään
void freeClusteringResult(ClusteringResult* result)
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
        point.partition = -1;

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
void generateRandomCentroids(int numCentroids, DataPoints* dataPoints, DataPoint* centroids)
{
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
        dataPointsShuffled[i].dimensions = dataPoints->points[i].dimensions;
        deepCopyDataPoint(&dataPointsShuffled[i], &dataPoints->points[i]);
    }

    // Shuffle the dataPointsShuffled array
    for (size_t i = 0; i < numCentroids; ++i)
    {
        size_t j = rand() % dataPoints->size;
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

//calculate MSE
//TODO: rework, currently just copy of SSE
double calculateMSE(DataPoint* dataPoints, int dataPointsSize, DataPoint* centroids, int centroidsSize, int* partition)
{
	double mse = 0.0;

	for (int i = 0; i < dataPointsSize; ++i)
	{
		int cIndex = partition[i];

		if (cIndex >= 0 && cIndex < centroidsSize)
		{
			// SSE between the data point and its assigned centroid
			mse += calculateEuclideanDistance(&dataPoints[i], &centroids[cIndex]);
		}
		else
		{
			fprintf(stderr, "Error: Invalid centroid index in partition\n");
			exit(EXIT_FAILURE);
		}
	}

	mse = mse / dataPointsSize;

	return mse;
}

//Function to calculate the sum of squared errors (SSE)
double calculateSSE(DataPoints* dataPoints, Centroids* centroids)
{
    double sse = 0.0;

    for (int i = 0; i < dataPoints->size; ++i)
    {
        int cIndex = dataPoints->points[i].partition;

        if (cIndex >= 0 && cIndex < centroids->size)
        {
            // SSE between the data point and its assigned centroid
            sse += calculateEuclideanDistance(&dataPoints->points[i], &centroids->points[cIndex]);
        }
        else
        {
            //Debugging
            //CI was initialized to -1
            //TODO: check if its still possible
            fprintf(stderr, "Error: Invalid centroid index in partition\n");
            exit(EXIT_FAILURE);
        }
    }

    return sse;
}

// Function to find the nearest centroid of a data point
int findNearestCentroid(DataPoint* queryPoint, Centroids* targetPoints)
{
    if (targetPoints->size == 0)
    {
        fprintf(stderr, "Error: Cannot find nearest centroid in an empty set of data\n");
        exit(EXIT_FAILURE);
    }

    int nearestCentroidId = -1;
    double minDistance = DBL_MAX;
	double newDistance;

    for (int i = 0; i < targetPoints->size; ++i)
    {
        newDistance = calculateEuclideanDistance(queryPoint, &targetPoints->points[i]);
		
        if (newDistance < minDistance)
        {
            minDistance = newDistance;
            nearestCentroidId = i;
        }
    }

    // This is not currently in use, could be utilized for Fast K-means
    //queryPoint->minDistance = minDistance;

    return nearestCentroidId;
}

// Function for optimal partitioning
void optimalPartition(DataPoints* dataPoints, Centroids* centroids)
{
    if (dataPoints->size == 0 || centroids->size == 0)
    {
        fprintf(stderr, "Error: Cannot perform optimal partition with empty data or centroids\n");
        exit(EXIT_FAILURE);
    }

    // Iterate through each data point to find its nearest centroid
    for (int i = 0; i < dataPoints->size; ++i)
    {
        int nearestCentroidId = findNearestCentroid(&dataPoints->points[i], centroids);
        dataPoints->points[i].partition = nearestCentroidId;
    }
}

// Calculate the centroid of a cluster
DataPoint calculateCentroid(DataPoint* dataPoints, int dataPointsSize)
{
    if (dataPointsSize <= 0)
    {
        fprintf(stderr, "Error: Cannot calculate centroid for an empty set of data points\n");
        exit(EXIT_FAILURE);
    }

    DataPoint centroid;
    centroid.dimensions = dataPoints[0].dimensions;
    centroid.attributes = malloc(sizeof(double) * dataPoints[0].dimensions);
    handleMemoryError(centroid.attributes);

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
/*DataPoint* centroidStep(int numClusters, DataPoints* dataPoints)
{
    DataPoint* newCentroids = malloc(sizeof(DataPoint) * numClusters);
    handleMemoryError(newCentroids);
    
	

    // Calculate the new centroids
    for (int clusterLabel = 0; clusterLabel < numClusters; ++clusterLabel)
    {
        newCentroids[clusterLabel] = calculateCentroid(dataPoints->points, dataPoints->size);

        // Initialize the clusters
        // This is for the empty cluster check
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

        
        for (int j = 0; j < dataPoints->size; ++j)
		{
			
		}
        
        if (clusterPoints[0] == -1)
        {

        }
        else
        {

        }

    }

    return newCentroids;
}*/

// Function to perform the centroid step in k-means
//TODO: haluaa DataPoints mutta saa DataPoint*, siksi tämä hajoaa
Centroids* centroidStep(int numClusters, DataPoints* dataPoints)
{
    // Allocate memory for the new centroids
    Centroids* newCentroids = malloc(sizeof(DataPoint) * numClusters);
    newCentroids->size = NUM_CENTROIDS;
    newCentroids->points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
    handleMemoryError(newCentroids->points);
    handleMemoryError(newCentroids);

    // Temporary storage for sums and counts
    double** sums = malloc(sizeof(double*) * numClusters);
    int* counts = malloc(sizeof(int) * numClusters);
    handleMemoryError(sums);
    handleMemoryError(counts);

    // Initialize sums and counts
    for (int clusterLabel = 0; clusterLabel < numClusters; ++clusterLabel)
    {
        sums[clusterLabel] = calloc(dataPoints->points[0].dimensions, sizeof(double));
        handleMemoryError(sums[clusterLabel]);
        counts[clusterLabel] = 0;
    }

    // Accumulate sums and counts for each cluster
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        DataPoint* point = &dataPoints->points[i];
        int clusterLabel = point->partition;

        for (size_t dim = 0; dim < point->dimensions; ++dim)
        {
            sums[clusterLabel][dim] += point->attributes[dim];
        }
        counts[clusterLabel]++;
    }

    // Calculate the new centroids
    for (int clusterLabel = 0; clusterLabel < numClusters; ++clusterLabel)
    {
        newCentroids->points[clusterLabel].dimensions = dataPoints->points[0].dimensions;
        newCentroids->points[clusterLabel].attributes = malloc(sizeof(double) * newCentroids->points[clusterLabel].dimensions);
        handleMemoryError(newCentroids->points[clusterLabel].attributes);

        if (counts[clusterLabel] > 0)
        {
            for (size_t dim = 0; dim < newCentroids->points[clusterLabel].dimensions; ++dim)
            {
                newCentroids->points[clusterLabel].attributes[dim] = sums[clusterLabel][dim] / counts[clusterLabel];
            }
        }
        else
        {
            //TODO: tyhjän klusterin käsittely
            fprintf(stderr, "Warning: Cluster %d has no points assigned.\n", clusterLabel);
        }

        free(sums[clusterLabel]);
    }

    free(sums);
    free(counts);

    return newCentroids;
}

// Function to run the k-means algorithm
ClusteringResult runKMeans(DataPoints* dataPoints, int iterations, Centroids* centroids, Centroids* groundTruth)
{
    int stopCounter = 0;
    double bestSse = DBL_MAX;
    int centroidIndex = -1;
    double sse = DBL_MAX;

    int* partition = malloc(sizeof(int) * dataPoints->size);
    handleMemoryError(partition);

    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        //Partition step
        optimalPartition(dataPoints, centroids);

		//Centroid step
        Centroids* newCentroids = centroidStep(centroids->size, dataPoints);

        //CI
        centroidIndex = calculateCentroidIndex(newCentroids, groundTruth);
        if (LOGGING == 2) printf("(runKMeans)CI after iteration %d: %d\n", iteration + 1, centroidIndex);

        //SSE
        sse = calculateSSE(dataPoints, newCentroids);        
        if(LOGGING == 2) printf("(runKMeans)Total SSE after iteration %d: %.0f\n", iteration + 1, sse / 10000000);
        
        if (sse < bestSse)
        {
            bestSse = sse;
			stopCounter = 0;
        }
        else
        {
            stopCounter++;
        }

        // Assuming newCentroids is calculated as the average of data points in each cluster
        //TODO: tartteeko täällä pitää deepcopya, vai riittääkö shallow copy?
        for (int i = 0; i < centroids->size; ++i)
        {
            deepCopyDataPoint(&centroids->points[i], &newCentroids->points[i]);
        }
        free(newCentroids); //freeDataPoints(newCentroids) <- TODO: miksei toimi?
        newCentroids = NULL;

        if (stopCounter == 3)
        {
            break;
        }
    }

    ClusteringResult result;
    result.sse = sse;
    result.partition = partition;
    result.centroids = centroids;
    result.centroidIndex = centroidIndex;

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

// Function to perform random swap
ClusteringResult randomSwap(DataPoints* dataPoints, Centroids* centroids, Centroids* groundTruth)
{
    double bestSse = DBL_MAX;
    ClusteringResult bestResult;
    int kMeansIterations = 2;

    //TODO: tarvitaanko tätä alustusta, varsinkaan jos alustus tehdään jo mainissa?
    bestResult.centroids = malloc(centroids->size * sizeof(DataPoint)); //since results.centroids = centroids.points, do we need to allocate memory?
    handleMemoryError(bestResult.centroids);
    bestResult.partition = malloc(dataPoints->size * sizeof(int));
    handleMemoryError(bestResult.partition);
	bestResult.sse = DBL_MAX;
	bestResult.centroidIndex = INT_MAX;

    for (int i = 0; i < MAX_SWAPS; ++i)
    {
        DataPoint oldCentroid;
        int randomCentroidId = rand() % centroids->size;
        int randomDataPointId = rand() % dataPoints->size;

        //Saving the old centroid
        deepCopyDataPoint(&oldCentroid, &centroids[randomCentroidId]);    

        //Swapping
        deepCopyDataPoint(&centroids[randomCentroidId], &dataPoints[randomDataPointId]);

		//TODO: tätä ei nyt alusteta, pitäisikö?
        ClusteringResult result = runKMeans(dataPoints, kMeansIterations, centroids, groundTruth);

        //If 1) CI or 2) SSE improves, we keep the change
        //if not, we reverse the swap
        if (result.centroidIndex < bestResult.centroidIndex || result.centroidIndex == bestResult.centroidIndex && result.sse < bestResult.sse)
        {
			if (LOGGING == 1) printf("(RS) Round %d: Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (SSE): %.4f\n", i+1, result.centroidIndex, result.sse / 10000);
            bestResult.sse = result.sse;
            bestResult.centroidIndex = result.centroidIndex;
            memcpy(bestResult.partition, result.partition, dataPoints->size * sizeof(int));
            deepCopyDataPoints(bestResult.centroids, result.centroids, centroids->size);
        }
        else
        {
            deepCopyDataPoint(&centroids[randomCentroidId], &oldCentroid);
        }

        free(oldCentroid.attributes);
    }

    return bestResult;
}

//function to calculate Centroid Index (CI)
int calculateCentroidIndex(Centroids* centroids1, Centroids* centroids2)
{
    int countFrom1to2 = 0;
    int countFrom2to1 = 0;
	int biggerSize = (centroids1->size > centroids2->size) ? centroids1->size : centroids2->size;

    //TODO: keksi uusi nimi, esim Diskreetit rakenteet kurssilta se miten c1 piirretään nuolia c2 jne (transpoosio? tmv)
    int* closest = malloc(sizeof(int) * biggerSize);//TODO: voiko käyttää calloc?
    handleMemoryError(closest);

    //TODO: tätä ei ehkä tarvita?
    for (int i = 0; i < biggerSize; ++i)
    {
        closest[i] = 0;
    }

    //TODO: Voisiko tästä tehdä funktion?
    // koska nyt toistetaan C1-> C2 ja C2 -> C1, niin duplikaattikoodia
    
    // C1 -> C2
    for (int i = 0; i < centroids1->size; ++i)
    {
        double minDistance = DBL_MAX;
        int closestIndex = -1;

        for (int j = 0; j < centroids2->size; ++j)
        {
            double distance = calculateEuclideanDistance(&centroids1->points[i], &centroids2->points[j]);
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

    for (int i = 0; i < centroids1->size; ++i)
    {
        if (closest[i] == 0)
        {
            countFrom1to2++;
        }
    }

    // C2 -> C1
    for (int i = 0; i < centroids2->size; ++i)
    {
        double minDistance = DBL_MAX;
        int closestIndex = -1;

        for (int j = 0; j < centroids1->size; ++j)
        {
            double distance = calculateEuclideanDistance(&centroids2->points[i], &centroids1->points[j]);
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

    for (int i = 0; i < centroids2->size; ++i)
    {
        if (closest[i] == 0)
        {
            countFrom2to1++;
        }
    }

    free(closest);
    return (countFrom1to2 > countFrom2to1) ? countFrom1to2 : countFrom2to1;
}

///////////
// Main //
/////////
int main()
{
	// Seeding the random number generator
    srand((unsigned int)time(NULL));

    printf("Starting the process");
    printf("File name: %s\n", DATA_FILENAME);

    int numDimensions = getNumDimensions(DATA_FILENAME);

    if (numDimensions > 0)
    {        
        printf("Number of dimensions in the data: %d\n", numDimensions);

        DataPoints dataPoints = readDataPoints(DATA_FILENAME);
        printf("Dataset size: %d\n", (int)dataPoints.size);

        DataPoints groundTruth = readDataPoints(GT_FILENAME);

        Centroids centroids;
        centroids.size = NUM_CENTROIDS;
        centroids.points = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(centroids.points);

        //////////////
        // K-means //
        ////////////
        printf("K-means\n");
        clock_t start = clock();

        generateRandomCentroids(NUM_CENTROIDS, &dataPoints, centroids.points);

        //ClusteringResult result0 = runKMeans(dataPoints.points, (int)dataPoints.size, MAX_ITERATIONS, centroids.points, NUM_CENTROIDS, &groundTruth);
        ClusteringResult result0 = runKMeans(&dataPoints, MAX_ITERATIONS, &centroids, &groundTruth);
        if (LOGGING == 2) printf("(K-means)Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (SSE): %.4f\n", result0.centroidIndex, result0.sse / 10000000);

        clock_t end = clock();
        double duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(K-means)Time taken: %f seconds\n", duration);
        /*
        ///////////////////////
        // Repeated k-means //
        /////////////////////
        ClusteringResult result1;
        ClusteringResult bestResult1;

        //TODO: tarkista tarvitaanko näitä alustuksia, sillä alustukset tehdään myös syvemmällä koodissa
        //result.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint)); //since results.centroids = centroids.points, do we need to allocate memory?
        //handleMemoryError(result.centroids);
        //result.partition = malloc(dataPoints.size * sizeof(int));
        //handleMemoryError(result.partition);
        result1.sse = DBL_MAX;
        result1.centroidIndex = INT_MAX;

        bestResult1.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint));
        handleMemoryError(bestResult1.centroids);
        bestResult1.partition = malloc(dataPoints.size * sizeof(int));
        handleMemoryError(bestResult1.partition);
        bestResult1.sse = DBL_MAX;
        bestResult1.centroidIndex = INT_MAX;

        start = clock();
        printf("Repeated K-means\n");

        for (int repeat = 0; repeat < MAX_REPEATS; ++repeat)
        {
            if(LOGGING == 2) printf("round: %d\n", repeat);

            generateRandomCentroids(NUM_CENTROIDS, &dataPoints, centroids.points);

            // K-means
            result1 = runKMeans(dataPoints.points, (int)dataPoints.size, MAX_ITERATIONS, centroids.points, NUM_CENTROIDS, &groundTruth);

			if(LOGGING == 2) printf("(RKM) Round %d: Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (SSE): %.4f\n", repeat, result1.centroidIndex, result1.sse / 10000000);
            
            if (result1.centroidIndex < bestResult1.centroidIndex || result1.sse < bestResult1.sse)
            {
                if (LOGGING == 2) printf("in we goooo");
				deepCopyDataPoints(bestResult1.centroids, result1.centroids, NUM_CENTROIDS);
                bestResult1.sse = result1.sse;
                bestResult1.centroidIndex = result1.centroidIndex;
				memcpy(bestResult1.partition, result1.partition, dataPoints.size * sizeof(int));
            }
        }
        
        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Repeated k-means)Time taken: %.2f seconds\n", duration);

        //////////////////
        // Random Swap //
        ////////////////
        ClusteringResult result2;

        result2.centroids = malloc(NUM_CENTROIDS * sizeof(DataPoint)); //since results.centroids = centroids.points, do we need to allocate memory?
        handleMemoryError(result2.centroids);
        result2.partition = malloc(dataPoints.size * sizeof(int));
        handleMemoryError(result2.partition);

        generateRandomCentroids(NUM_CENTROIDS, &dataPoints, centroids.points);

        printf("Random swap\n");
        start = clock();

        // Random Swap
        result2 = randomSwap(dataPoints.points, (int)dataPoints.size, centroids.points, NUM_CENTROIDS, &groundTruth);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Random Swap)Time taken: %.2f seconds\n", duration);

        ////////////////////
        // Split k-means //
        //////////////////
        printf("Split k-means\n");
        start = clock();

        //not refactored yet
        //double bestSse4 = runSplit(dataPoints, dataPoints.size, NUM_CENTROIDS);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        printf("(Split)Time taken: %.2f seconds\n", duration);

        /////////////
        // Prints //
        ///////////
        printf("(K-means)Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (SSE): %.4f\n", result0.centroidIndex, result0.sse / 10000000);
        printf("(Repeated K-means)Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (SSE): %.4f\n", bestResult1.centroidIndex, bestResult1.sse / 10000000);
        printf("(Random Swap)Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (SSE): %.4f\n", result2.centroidIndex, result2.sse / 10000000);
        //printf("(Split)Best Centroid Index (CI): %d and best Sum-of-Squared Errors (SSE): %f\n", result3.centroidIndex, result3.sse / 10000000);


        ///////////////
        // Clean up //
        /////////////
        if(LOGGING == 2) printf("Muistin vapautus\n");        
        freeDataPoints(&dataPoints);
        if(LOGGING == 3) printf("Eka\n");
        freeCentroids(&centroids);
        if(LOGGING == 3)printf("toka\n");
        if(LOGGING == 3)printf("kolmas\n");
        //free(initialPartition);
        //freeDataPoints(&result.centroids); result.centroids = centroids.points, joten ei tarvitse vapauttaa
        //if(LOGGING == 1)printf("neljäs\n");
        free(result1.partition);
        if(LOGGING == 3)printf("neljas\n");
        free(result2.partition);
        if (LOGGING == 3)printf("viides\n");
        */
        return 0;
    }
}
