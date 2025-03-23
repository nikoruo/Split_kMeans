#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <float.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <direct.h>
#include <stddef.h>
#include <locale.h>

// Change logs
// 20-01-2025: Initial release by Niko Ruohonen

// Introduction
/**
 * Project Name: Clustering_with_c
 * 
 * Description: 
 * This project focuses on the development and implementation of various clustering algorithms.
 * The primary goal was to create a novel clustering algorithm, the MSE Split Algorithm, 
 * and also to implement existing algorithms. All algorithms were designed and optimized 
 * to ensure maximum efficiency and effectiveness when applied to multi-dimensional data points.
  * 
 * Author: Niko Ruohonen
 * Date: January 20, 2025
 * Version: 1.0.0
 *
 * Details:
 * - Implements multiple clustering algorithms: K-means, Repeated K-means, Random Swap, Random Split, MSE Split (Intra-cluster, Global, Local Repartition), and Bisecting K-means.
 * - Provides detailed logging options for debugging and performance analysis.
 * - Includes memory management functions to handle dynamic allocation and deallocation of data structures.
 * - Supports reading and writing data points and centroids from/to files.
 * - Calculates various metrics such as Mean Squared Error (MSE) and Centroid Index (CI) to evaluate clustering performance.
 *
 * Usage:
 * The project can be run by executing the main function, which initializes datasets, ground truth files, and clustering parameters.
 * It then runs different clustering algorithms on each dataset and writes the results to output files.
 *
 * Notes:
 * - Ensure that the data files and ground truth files are placed in the appropriate directories before running the project.
 * - Remember to update gtList, dataFiles and kNumList arrays with the correct information.
 * - Remember to set the "Settings" to desired levels before running the project.
 * - Future plans include adding more clustering algorithms and improving the performance of existing ones.
 */

//////////////
// DEFINES //
////////////

#define CLOCKS_PER_MS (CLOCKS_PER_SEC / 1000)

 /////////////
// GLOBALS //
////////////

// For logging level
// 1 = none, 2 = debug, 3 = everything
// Currently most of the LOGGING lines are commented out
const size_t LOGGING = 1;

//////////////
// Structs //
////////////

/**
 * @brief Represents a single data point in a multi-dimensional space.
 *
 * This struct contains an array of attributes representing the coordinates of the data point
 * in a multi-dimensional space, the number of dimensions, and the partition index indicating
 * which cluster the data point belongs to.
 */
typedef struct
{
    double* attributes;  /**< Array of attributes representing the coordinates of the data point. */
    size_t dimensions;   /**< Number of dimensions (length of the attributes array). */
	size_t partition;    /**< Partition index indicating the cluster to which the data point belongs. */
} DataPoint;

/**
 * @brief Represents a collection of data points.
 *
 * This struct contains an array of DataPoint structures and the number of data points in the array.
 */
typedef struct
{
    DataPoint* points;   /**< Array of DataPoint structures. */
    size_t size;         /**< Number of data points in the array. */
} DataPoints;

/**
 * @brief Represents a collection of centroids used in clustering algorithms.
 *
 * This struct contains an array of DataPoint structures representing the centroids
 * and the number of centroids in the array.
 */
typedef struct
{
    DataPoint* points;   /**< Array of DataPoint structures representing the centroids. */ //TODO: tarvitaanko Centroid -struct?
    size_t size;         /**< Number of centroids in the array. */
	//size_t mse; TODO <- tarvitaanko?
} Centroids;

/**
 * @brief Represents the result of a clustering algorithm.
 *
 * This struct contains the mean squared error (MSE) of the clustering result, //TODO: SSE
 * an array of partition indices indicating the cluster assignment for each data point,
 * an array of centroids representing the cluster centers, and the Centroid Index (CI) value.
 */
typedef struct
{
    double mse;           /**< Mean squared error (MSE) of the clustering result. */
	size_t* partition;    /**< Array of partition indices indicating the cluster assignment for each data point. */
    DataPoint* centroids; /**< Array of DataPoint structures representing the centroids. */
    size_t centroidIndex; /**< Centroid Index value. */
} ClusteringResult;

/**
 * @brief Represents statistical data collected during the execution of clustering algorithms.
 *
 * This struct contains the sum of mean squared errors (MSE), the sum of Centroid Index (CI) values,
 * the total time taken for the clustering process, and the success rate of the clustering algorithm.
 */
typedef struct
{
    double mseSum;       /**< Sum of mean squared errors (MSE) values. */
    size_t ciSum;        /**< Sum of Centroid Index (CI) values. */
    double timeSum;      /**< Total time taken for the clustering process. */
    double successRate;  /**< Success rate of the clustering algorithm. */
} Statistics;


///////////////
// Memories //
/////////////

/**
 * @brief Handles memory allocation errors.
 *
 * Function checks if the given pointer is NULL, indicating a memory allocation failure.
 * If the pointer is NULL, it prints an error message to stderr and exits the program with a failure status.
 *
 * @param ptr A pointer to the allocated memory. If this pointer is NULL, the function will handle the error.
 */
void handleMemoryError(void* ptr)
{
    if (ptr == NULL)
    {
        fprintf(stderr, "Error: Unable to allocate memory\n");
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Frees the memory allocated for a single DataPoint structure.
 *
 * This function frees the memory allocated for the attributes of the DataPoint
 * and then sets the pointer to the attributes to NULL.
 *
 * @param point A pointer to the DataPoint structure to be freed.
 */
void freeDataPoint(DataPoint* point)
{
    //if (point == NULL) return;

    if (point->attributes != NULL)
    {
        free(point->attributes);
        point->attributes = NULL;
    }
}

/**
 * @brief Frees the memory allocated for an array of DataPoint structures.
 *
 * This function iterates through an array of DataPoint structures, freeing the memory
 * allocated for the attributes of each DataPoint. It then frees the memory allocated for
 * the array of DataPoint structures itself.
 *
 * @param points A pointer to the array of DataPoint structures to be freed.
 * @param size The number of DataPoint structures in the array.
 */
void freeDataPointArray(DataPoint* points, size_t size)
{
    //if (points == NULL) return;

    for (size_t i = 0; i < size; ++i)
    {
        if (points[i].attributes != NULL) //TODO: if pois lopullisesta versiosta?
        {
            free(points[i].attributes);
            points[i].attributes = NULL;
        }
    }
    free(points);
}

/**
 * @brief Frees the memory allocated for a DataPoints structure.
 *
 * This function frees the memory allocated for the array of DataPoint structures
 * within the DataPoints structure and then sets the pointer to the array to NULL.
 *
 * @param dataPoints A pointer to the DataPoints structure to be freed.
 */void freeDataPoints(DataPoints* dataPoints)
{
    if (dataPoints == NULL) return;
    freeDataPointArray(dataPoints->points, dataPoints->size);
    dataPoints->points = NULL;
}

 /**
  * @brief Frees the memory allocated for a Centroids structure.
  *
  * This function frees the memory allocated for the array of DataPoint structures
  * within the Centroids structure and then sets the pointer to the array to NULL.
  *
  * @param centroids A pointer to the Centroids structure to be freed.
  */
void freeCentroids(Centroids* centroids)
{
    if (centroids == NULL) return;
    freeDataPointArray(centroids->points, centroids->size);
    centroids->points = NULL;
}

/**
 * @brief Frees the memory allocated for a ClusteringResult structure.
 *
 * This function frees the memory allocated for the partition array and the array of DataPoint structures
 * representing the centroids within the ClusteringResult structure. It then sets the pointers to NULL.
 *
 * @param result A pointer to the ClusteringResult structure to be freed.
 * @param numCentroids The number of centroids in the ClusteringResult structure.
 */
void freeClusteringResult(ClusteringResult* result, size_t numCentroids)
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

/**
 * @brief Creates a list of strings.
 *
 * This function allocates memory for a list of strings, each with a fixed size of 256 characters.
 *
 * @param size The number of strings in the list.
 * @return A pointer to the list of strings.
 */char** createStringList(size_t size)
{
    char** list = malloc(size * sizeof(char*));
	handleMemoryError(list);

    const size_t stringSize = 256;

    for (size_t i = 0; i < size; ++i)
    {
        // Suppress warning about potential NULL dereference
		// We already check for NULL pointers in handleMemoryError
        #pragma warning(suppress : 6011)
        list[i] = malloc(stringSize * sizeof(char));
		handleMemoryError(list[i]);
    }

    return list;
}

 /**
  * @brief Frees the memory allocated for a list of strings.
  *
  * This function frees the memory allocated for each string in the list and then frees the list itself.
  *
  * @param list A pointer to the list of strings to be freed.
  * @param size The number of strings in the list.
  */
 void freeStringList(char** list, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        free(list[i]);
    }
    free(list);
}

 /**
 * @brief Allocates and initializes a DataPoint structure.
 *
 * This function allocates memory for the attributes of a DataPoint structure and initializes its dimensions and partition.
 *
 * @param dimensions The number of dimensions for the DataPoint.
 * @return A DataPoint structure with allocated memory for its attributes.
 */
 DataPoint allocateDataPoint(size_t dimensions)
 {
     DataPoint point;
     point.attributes = malloc(dimensions * sizeof(double));
     handleMemoryError(point.attributes);
     point.dimensions = dimensions;
     point.partition = SIZE_MAX; // Initialize partition to default value, here SIZE_MAX. Cant use -1 as its size_t

     return point;
 }

 /**
 * @brief Allocates and initializes a DataPoints structure.
 *
 * This function allocates memory for an array of DataPoint structures and initializes each DataPoint.
 *
 * @param size The number of DataPoint structures in the array.
 * @param dimensions The number of dimensions for each DataPoint.
 * @return A DataPoints structure with allocated memory for its points.
 */
 DataPoints allocateDataPoints(size_t size, size_t dimensions)
 {
     DataPoints dataPoints;
     dataPoints.points = malloc(size * sizeof(DataPoint));
     handleMemoryError(dataPoints.points);
     dataPoints.size = size;
     for (size_t i = 0; i < size; ++i)
     {
         dataPoints.points[i] = allocateDataPoint(dimensions);
     }

     return dataPoints;
 }

 /**
 * @brief Allocates and initializes a Centroids structure.
 *
 * This function allocates memory for an array of DataPoint structures representing the centroids and initializes each DataPoint.
 *
 * @param size The number of centroids.
 * @param dimensions The number of dimensions for each centroid.
 * @return A Centroids structure with allocated memory for its points.
 */
 Centroids allocateCentroids(size_t size, size_t dimensions)
 {
     Centroids centroids;
     centroids.points = malloc(size * sizeof(DataPoint));
     handleMemoryError(centroids.points);
     centroids.size = size;
     for (size_t i = 0; i < size; ++i)
     {
         centroids.points[i] = allocateDataPoint(dimensions);
     }

     return centroids;
 }

 /**
 * @brief Allocates and initializes a ClusteringResult structure.
 *
 * This function allocates memory for the partition array and the array of DataPoint structures representing the centroids.
 *
 * @param numDataPoints The number of data points.
 * @param numCentroids The number of centroids.
 * @param dimensions The number of dimensions for each centroid.
 * @return A ClusteringResult structure with allocated memory for its partition and centroids.
 */
 ClusteringResult allocateClusteringResult(size_t numDataPoints, size_t numCentroids, size_t dimensions)
 {
     ClusteringResult result;
     result.partition = malloc(numDataPoints * sizeof(size_t));
     handleMemoryError(result.partition);
     result.centroids = malloc(numCentroids * sizeof(DataPoint));
     handleMemoryError(result.centroids);
     for (size_t i = 0; i < numCentroids; ++i)
     {
         result.centroids[i] = allocateDataPoint(dimensions);
     }
     result.mse = DBL_MAX;
     result.centroidIndex = SIZE_MAX;

     return result;
 }

 /**
 * @brief Initializes a Statistics structure.
 *
 * This function sets all fields of the Statistics structure to their default values.
 *
 * @param stats A pointer to the Statistics structure to be initialized.
 */
 void initializeStatistics(Statistics* stats)
 {
     if (stats == NULL)
     {
         fprintf(stderr, "Error: Null pointer passed to initializeStatistics\n");
         exit(EXIT_FAILURE);
     }

     stats->mseSum = 0.0;
     stats->ciSum = 0;
     stats->timeSum = 0.0;
     stats->successRate = 0.0;
 }


//////////////
// Helpers //
////////////

/**
 * @brief Calculates the squared Euclidean distance between two data points.
 *
 * This function computes the squared Euclidean distance between two data points
 * by summing the squared differences of their corresponding attributes.
 *
 * @param point1 A pointer to the first DataPoint structure.
 * @param point2 A pointer to the second DataPoint structure.
 * @return The squared Euclidean distance between the two data points.
 */
 double calculateSquaredEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
 {
     if (point1 == NULL || point2 == NULL)
     {
         fprintf(stderr, "Error: Null pointer passed to calculateSquaredEuclideanDistance\n");
         exit(EXIT_FAILURE);
     }

     if (point1->dimensions != point2->dimensions)
     {
         fprintf(stderr, "Error: Data points have different dimensions in calculateSquaredEuclideanDistance\n");
         exit(EXIT_FAILURE);
     }

    double sum = 0.0;
    for (size_t i = 0; i < point1->dimensions; ++i)
    {
        double diff = point1->attributes[i] - point2->attributes[i];
        sum += diff * diff;
    }
    return sum;
 }

 /**
  * @brief Calculates the Euclidean distance between two data points.
  *
  * This function computes the Euclidean distance between two data points
  * by first calculating the squared Euclidean distance and then taking the square root.
  *
  * @param point1 A pointer to the first DataPoint structure.
  * @param point2 A pointer to the second DataPoint structure.
  * @return The Euclidean distance between the two data points.
  */
 double calculateEuclideanDistance(const DataPoint* point1, const DataPoint* point2)
 {
	double sqrtDistance = sqrt(calculateSquaredEuclideanDistance(point1, point2));
    return sqrtDistance;
 }

  /**
   * @brief Handles file opening errors.
   *
   * This function prints an error message to stderr indicating that the specified file
   * could not be opened and then exits the program with a failure status.
   *
   * @param filename The name of the file that could not be opened.
   */
 void handleFileError(const char* filename)
 {
    fprintf(stderr, "Error: Unable to open file '%s'\n", filename);
    exit(EXIT_FAILURE);
 }

 /**
 * @brief Handles file read errors.
 *
 * This function prints an error message to stderr indicating that an error occurred while reading
 * from the specified file and then exits the program with a failure status.
 *
 * @param filename The name of the file that could not be read.
 */
 void handleFileReadError(const char* filename)
 {
     fprintf(stderr, "Error: Unable to read from file '%s'\n", filename);
     exit(EXIT_FAILURE);
 }

/**
* @brief Gets the number of dimensions in the data file.
*
* This function reads the first line of the specified file and counts the number of tokens
* (assumed to be the number of dimensions) separated by spaces (" ", tabs "\t", newlines "\n", carriage return "\r").
*
* @param filename The name of the file to read.
* @return The number of dimensions in the data file.
*/
size_t getNumDimensions(const char* filename)
{
    FILE* file = fopen(filename, "r");
    if (file == NULL)
    {
        handleFileError(filename);
    }

    // For disabling the CS6387 warning,
    // inform the static analyzer that 'file' is not NULL.
    // Safe to use as we actually check for NULL earlier
    _Analysis_assume_(file != NULL);

    //TODO: mieti, että tarvitseeko näitä buffereita yhdenmukaistaa, tässä 512 muualla 256
	char firstLine[512]; // Buffer size = 512, increase if needed
    if (fgets(firstLine, sizeof(firstLine), file) == NULL)
    {
        handleFileReadError(filename);
    }

    size_t dimensions = 0;
    char* context = NULL;
	char* token = strtok_s(firstLine, " \t\r\n", &context); //Delimiter = " ", tabs "\t", newlines "\n", carriage return "\r" //TODO: Haluanko muuta kuin " "? TODO: halutaanko CONST?
    while (token != NULL)
    {
        dimensions++;
        token = strtok_s(NULL, " \t\r\n", &context); //Delimiter = " ", tabs "\t", newlines "\n", carriage return "\r" 
    }

    fclose(file);

    return dimensions;
}

/**
 * @brief Reads data points from a file.
 *
 * This function reads data points from the specified file, where each line represents a data point
 * with attributes separated by spaces, tabs, newlines, or carriage returns. It allocates memory
 * for the data points and their attributes, and returns a DataPoints structure containing the data points.
 *
 * @param filename The name of the file to read.
 * @return A DataPoints structure containing the data points read from the file.
 */
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

    char line[512]; // Buffer size = 512, increase if needed
    while (fgets(line, sizeof(line), file))
    {
        size_t attributeAllocatedSize = 6;

        DataPoint point;
        point.attributes = malloc(sizeof(double) * attributeAllocatedSize);
        handleMemoryError(point.attributes);
        point.dimensions = 0;
        point.partition = SIZE_MAX;

        char* context = NULL;
        char* token = strtok_s(line, " \t\r\n", &context); // Delimiter = " ", tabs "\t", newlines "\n", carriage return "\r"
        while (token != NULL)
        {
            if (point.dimensions == attributeAllocatedSize)
            {
                attributeAllocatedSize = attributeAllocatedSize > 0 ? attributeAllocatedSize * 2 : 1;
                double* temp = realloc(point.attributes, sizeof(double) * attributeAllocatedSize);
                handleMemoryError(temp);
                point.attributes = temp;
            }
            
            // if(LOGGING >= 3) printf("Token: %s\n", token);
            point.attributes[point.dimensions++] = strtod(token, NULL); // atoi(token) for int or strtod(token, NULL) for double
            token = strtok_s(NULL, " \t\r\n", &context); // Delimiter = " ", tabs "\t", newlines "\n", carriage return "\r"
        }

        // if(LOGGING >= 3) printf("\n", token);

        if (dataPoints.size == allocatedSize)
        {
            allocatedSize = allocatedSize > 0 ? allocatedSize * 2 : 1;
            DataPoint* temp = realloc(dataPoints.points, sizeof(DataPoint) * allocatedSize);
            handleMemoryError(temp);
            dataPoints.points = temp;
        }

        // Remove the suppression, if there are unknown issues while running the code.
        // The "if(dataPoints.size == allocatedSize)" -check above should be enough
		// to handle this warning, but the static analyzer is not able to detect it.
        // 
        // Suppress warning C6386 for this line
        #pragma warning(suppress : 6386)
        dataPoints.points[dataPoints.size++] = point;
    }

    fclose(file);

    /*if (LOGGING >= 3)
    {
        // for (size_t i = 0; i < dataPoints.size; ++i) // Debug helper: print all data points
        for (size_t i = 0; i < dataPoints.size; ++i) // Debug helper: print the first two data points
        {
            printf("Data point %zu attributes: ", i);
            for (size_t j = 0; j < dataPoints.points[i].dimensions; ++j)
            {
                printf("%.0f ", dataPoints.points[i].attributes[j]);
            }
            printf("\n");
        }
    }*/
    
    return dataPoints;
}


/**
 * @brief Reads centroids from a file.
 *
 * This function reads centroids from the specified file by first reading the data points
 * and then converting them into centroids. It allocates memory for the centroids and their attributes,
 * and returns a Centroids structure containing the centroids.
 *
 * @param filename The name of the file to read.
 * @return A Centroids structure containing the centroids read from the file.
 */
Centroids readCentroids(const char* filename)
{
    DataPoints points = readDataPoints(filename);

    Centroids centroids;
    centroids.size = points.size;
    centroids.points = points.points;
    
    /*if (LOGGING >= 3)
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
    }*/

    return centroids;
}

// Append a line with "iteration, numCentroids, sse" to a CSV file
void appendLogCsv(const char* filePath, size_t numCentroids, size_t ci, double sse)
{
    setlocale(LC_NUMERIC, "fi_FI");

    FILE* file = fopen(filePath, "a");
    if (file == NULL) {
        handleFileError(filePath);
        return;
    }
    // Write time, number of centroids, centroid index (CI) and SSE
    fprintf(file, "%zu;%zu;%.0f\n", ci, numCentroids, sse);
    fclose(file);
    setlocale(LC_NUMERIC, "C");
}

/**
 * @brief Checks if a file exists.
 *
 * This function checks if a file with the given file path exists.
 *
 * @param filePath The path to the file to check.
 * @return true if the file exists, false otherwise.
 */
bool fileExists(const char* filePath)
{
    struct stat buffer;
    return (stat(filePath, &buffer) == 0);
}

/**
 * @brief Writes centroids to a file.
 *
 * This function writes the centroids to the specified file. Each centroid's attributes
 * are written on a new line, with attributes separated by spaces.
 *
 * @param filename The name of the file to write the centroids to.
 * @param centroids A pointer to the Centroids structure containing the centroids to be written.
 */
void writeCentroidsToFile(const char* filename, const Centroids* centroids, const char* outputDirectory)
{
    char outputFilePath[256];
    snprintf(outputFilePath, sizeof(outputFilePath), "%s/%s", outputDirectory, filename);

    FILE* centroidFile = fopen(outputFilePath, "w");
    if (centroidFile == NULL)
    {
        handleFileError(outputFilePath);
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

    //if (LOGGING >= 3) printf("Centroids written to file: %s\n", filename);
}

/**
 * @brief Writes data point partitions to a file.
 *
 * This function writes the partition indices of data points to the specified file.
 * Each partition index is written on a new line.
 *
 * @param filename The name of the file to write the partitions to.
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 */
void writeDataPointPartitionsToFile(const char* filename, const DataPoints* dataPoints, const char* outputDirectory)
{
    char outputFilePath[256];
    snprintf(outputFilePath, sizeof(outputFilePath), "%s/%s", outputDirectory, filename);

    FILE* file = fopen(outputFilePath, "w"); //TODO: päivitä fopen -> fopen_s, vaatii hieman muutoksia
    if (file == NULL)
    {
        handleFileError(outputFilePath);
        return;
    }

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        fprintf(file, "%zu\n", dataPoints->points[i].partition);
    }

    fclose(file);

    //if (LOGGING >= 3) printf("Data point partitions written to file: %s\n", filename);
}

/**
 * @brief Creates a deep copy of a data point.
 *
 * This function copies the attributes and dimensions of the source data point
 * to the destination data point. It allocates memory for the attributes of the
 * destination data point and copies the attribute values from the source.
 *
 * @param destination A pointer to the destination DataPoint structure.
 * @param source A pointer to the source DataPoint structure.
 */
void deepCopyDataPoint(DataPoint* destination, const DataPoint* source)
{
    //Debugging
    /*if (destination == NULL || source == NULL)
    {
        fprintf(stderr, "Error: Null pointer passed to deepCopyDataPoint\n");
        exit(EXIT_FAILURE);
    }*/

    destination->dimensions = source->dimensions;
    destination->partition = source->partition;
    
    if (destination->attributes != NULL)
    {
        free(destination->attributes);
    }

    destination->attributes = malloc(source->dimensions * sizeof(double));
    handleMemoryError(destination->attributes);
    
    // Suppress warning C6387 for this line
	// The static analyzer is not able to detect that 'destination->attributes' is not NULL
    #pragma warning(suppress : 6387)
    memcpy(destination->attributes, source->attributes, source->dimensions * sizeof(double));
}

/**
 * @brief Creates deep copies of an array of data points.
 *
 * This function copies the attributes and dimensions of each source data point
 * to the corresponding destination data point. It allocates memory for the attributes
 * of each destination data point and copies the attribute values from the source.
 *
 * @param destination A pointer to the array of destination DataPoint structures.
 * @param source A pointer to the array of source DataPoint structures.
 * @param size The number of data points in the source and destination arrays.
 */
void deepCopyDataPoints(DataPoint* destination, const DataPoint* source, size_t size)
{
    if (destination == NULL || source == NULL)
    {
        fprintf(stderr, "Error: Null pointer passed to deepCopyDataPoints\n");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < size; ++i)
    {
        deepCopyDataPoint(&destination[i], &source[i]);
    }
}

/**
 * @brief Creates a deep copy of a Centroids structure.
 *
 * This function copies the centroids from the source Centroids structure
 * to the destination Centroids structure. It allocates memory for the centroids
 * in the destination structure and copies the attribute values from the source.
 *
 * @param source A pointer to the source Centroids structure.
 * @param destination A pointer to the destination Centroids structure.
 * @param numCentroids The number of centroids to copy.
 */
void deepCopyCentroids(const Centroids* source, Centroids* destination, size_t numCentroids)
{
    destination->size = source->size;

    if (destination->points != NULL)
    {
        freeDataPointArray(destination->points, destination->size);
    }

	destination->points = allocateDataPoints(numCentroids, source->points[0].dimensions).points;

    for (size_t i = 0; i < numCentroids; ++i)
    {
        deepCopyDataPoint(&destination->points[i], &source->points[i]);
    }
}

/**
 * @brief Prints all information about the centroids.
 *
 * This function prints the number of centroids and the attributes of each centroid.
 * Each centroid's attributes are printed on a new line, with attributes separated by spaces.
 *
 * @param centroids A pointer to the Centroids structure containing the centroids to be printed.
 */
void printCentroidsInfo(const Centroids* centroids)
{
    printf("Number of centroids: %zu\n", centroids->size);

    for (size_t i = 0; i < centroids->size; ++i)
    {
        DataPoint* centroid = &centroids->points[i];

        printf("Centroid %zu (dimensions: %zu) attributes: ", i, centroid->dimensions);

        for (size_t j = 0; j < centroid->dimensions; ++j)
        {
            printf("%f ", centroid->attributes[j]);
        }
        printf("\n");
    }
    printf("\n");
}

/**
 * @brief Prints partition assignments for data points with partition >= cMax.
 *
 * This function prints the partition assignments for data points whose partition index
 * is greater than or equal to the specified cMax value. It prints the data point index
 * and its partition index.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param cMax The partition index threshold.
 */
void printDataPointsPartitions(const DataPoints* dataPoints, size_t cMax)
{
    printf("Data Points Partition Assignments (Partition <= %zu):\n", cMax);
    printf("Data Points size: %zu\n", dataPoints->size);
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition >= cMax)
        {
            printf("Data Point %zu: Partition %zu\n", i, dataPoints->points[i].partition);
        }
    }
    printf("\n");
}

/**
 * @brief Resets all partitions to 0.
 *
 * This function iterates through all data points in the DataPoints structure
 * and sets their partition index to 0.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 */
void resetPartitions(DataPoints* dataPoints)
{    
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        dataPoints->points[i].partition = 0;
    }
}

/**
 * @brief Writes clustering results to a file.
 *
 * This function writes the clustering results, including average CI, MSE, relative CI,
 * average time taken, and success rate, to the specified file. The results are appended
 * to the file if it already exists.
 *
 * @param filename The name of the file to write the results to.
 * @param ciSum The sum of Centroid Index (CI) values.
 * @param mseSum The sum of Mean Squared Error (MSE) values.
 * @param timeSum The sum of time taken for each loop.
 * @param successRate The success rate of the clustering algorithm.
 * @param numCentroids The number of centroids used in the clustering algorithm.
 * @param header A header string to be written at the beginning of the results.
 * @param loopCount The number of loops performed.
 * @param scaling A scaling factor for the MSE values.
 * @param outputDirectory The directory where the results file is located.
 */
void writeResultsToFile(const char* filename, Statistics stats, size_t numCentroids, const char* header, size_t loopCount, size_t scaling, const char* outputDirectory)
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
    fprintf(file, "Average CI: %.2f and MSE: %.2f\n", (double)stats.ciSum / loopCount, stats.mseSum / loopCount / scaling);
    fprintf(file, "Relative CI: %.2f\n", (double)stats.ciSum / loopCount / numCentroids);
    fprintf(file, "Average time taken: %.2f seconds\n", stats.timeSum / loopCount);
    fprintf(file, "Success rate: %.2f%%\n\n", stats.successRate / loopCount * 100);

    fclose(file);
    
    //if(LOGGING >= 3) printf("Metrics written to file: %s\n", filename);
}

/**
 * @brief Creates a unique directory based on the current date and time.
 *
 * This function generates a unique directory name based on the current date and time
 * and attempts to create the directory. The directory name is stored in the provided
 * outputDirectory buffer.
 *
 * @param outputDirectory A buffer to store the generated directory name.
 * @param size The size of the outputDirectory buffer.
 */
void createUniqueDirectory(char* outputDirectory, size_t size)
{
    time_t now = time(NULL);
    struct tm* t = localtime(&now);

    if (strftime(outputDirectory, size, "outputs/%Y-%m-%d_%H-%M-%S", t) == 0)
    {
        fprintf(stderr, "Error: Buffer size is too small in createUniqueDirectory\n");
        return; //TODO: Exit_failure mieluummin näihin?
    }

    if (_mkdir(outputDirectory) != 0)
    {
        perror("Error: Unable to create directory");
		exit(EXIT_FAILURE);
    }
}

void createDatasetDirectory(const char* baseDirectory, const char* datasetName, char* datasetDirectory, size_t size)
{
    snprintf(datasetDirectory, size, "%s/%s", baseDirectory, datasetName);

    if (_mkdir(datasetDirectory) != 0)
    {
        perror("Error: Unable to create dataset directory");
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Gets the name of the split type based on the provided split type index.
 *
 * This function returns the name of the split type corresponding to the provided split type index.
 * If the split type index is invalid, it prints an error message to stderr and returns NULL.
 *
 * @param splitType The index of the split type.
 * @return The name of the split type, or NULL if the split type index is invalid.
 */
const char* getSplitTypeName(size_t splitType)
{
    switch (splitType)
    {
    case 0:
        return "Intra-cluster";
    case 1:
        return "Global";
    case 2:
        return "Local repartition";
    default:
        fprintf(stderr, "Error: Invalid split type provided\n");
        return NULL;
    }
}

/**
 * @brief Prints the statistics of a clustering algorithm.
 *
 * This function prints the average Centroid Index (CI), Mean Squared Error (MSE),
 * relative CI, average time taken, and success rate of a clustering algorithm.
 *
 * @param algorithmName The name of the clustering algorithm.
 * @param stats A Statistics structure containing the results to be printed.
 * @param loopCount The number of loops performed.
 * @param numCentroids The number of centroids used in the clustering algorithm.
 * @param scaling A scaling factor for the MSE values.
 */
void printStatistics(const char* algorithmName, Statistics stats, size_t loopCount, size_t numCentroids, size_t scaling)
{
    printf("(%s) Average CI: %.2f and MSE: %.2f\n", algorithmName, (double)stats.ciSum / loopCount, stats.mseSum / loopCount / scaling);
    printf("(%s) Relative CI: %.2f\n", algorithmName, (double)stats.ciSum / loopCount / numCentroids);
    printf("(%s) Average time taken: %.2f seconds\n", algorithmName, stats.timeSum / loopCount);
    printf("(%s) Success rate: %.2f%%\n\n", algorithmName, stats.successRate / loopCount * 100);
}


/////////////////
// Clustering //
///////////////

/**
 * @brief Generates random centroids from the data points.
 *
 * This function selects random data points to be used as initial centroids for clustering.
 *
 * @param numCentroids The number of centroids to generate.
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure to store the generated centroids.
 */
void generateRandomCentroids(size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids)
{    
    //DEBUGGING
    /*if (dataPoints->size < numCentroids)
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
        deepCopyDataPoint(&centroids->points[i], &dataPoints->points[selectedIndex]);
    }

    free(indices);
}

/**
 * @brief Calculates the sum of squared errors (SSE) for the given data points and centroids.
 *
 * This function computes the SSE by summing the squared Euclidean distances between each data point
 * and its assigned centroid.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @return The sum of squared errors (SSE).
 */
double calculateSSE(const DataPoints* dataPoints, const Centroids* centroids)
{
    double sse = 0.0;

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        size_t cIndex = dataPoints->points[i].partition;

        //Debugging
        /*if (cIndex >= centroids->size)
        {
            fprintf(stderr, "Error: Invalid partition index %zu for data point %zu\n", cIndex, i);
            exit(EXIT_FAILURE);
        }*/

        sse += calculateEuclideanDistance(&dataPoints->points[i], &centroids->points[cIndex]);
    }

    return sse;
}

/**
 * @brief Calculates the mean squared error (MSE) for the given data points and centroids.
 *
 * This function computes the MSE by dividing the sum of squared errors (SSE) by the total number
 * of data points and dimensions.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @return The mean squared error (MSE).
 */
double calculateMSE(const DataPoints* dataPoints, const Centroids* centroids)
{
    double sse = calculateSSE(dataPoints, centroids);

    double mse = sse / (dataPoints->size * dataPoints->points[0].dimensions);

    return mse;
}

/**
 * @brief Calculates the mean squared error (MSE) for the given data points and centroids.
 *
 * This function computes the MSE by dividing the sum of squared errors (SSE) by the total number
 * of data points and dimensions.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param size The size of the whole data set.
 * @return The mean squared error (MSE).
 */
double calculateMSEWithSize(const DataPoints* dataPoints, const Centroids* centroids, size_t size)
{
    double sse = calculateSSE(dataPoints, centroids);

    double mse = sse / (size * dataPoints->points[0].dimensions);

    return mse;
}

//TODO: käy description läpi, että vastaa sitä mitä lasketaan (count vai size)
/**
 * @brief Calculates the mean squared error (MSE) for a specific cluster.
 *
 * This function computes the MSE for a specific cluster by summing the Euclidean distances
 * between data points and their assigned centroid, and then dividing by the number of data points
 * in the cluster and the number of dimensions.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param clusterLabel The label of the cluster for which to calculate the MSE.
 * @return The mean squared error (MSE) for the specified cluster.
 */
double calculateClusterMSE(const DataPoints* dataPoints, const Centroids* centroids, size_t clusterLabel)
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

    //Debugger
    /*if (count == 0)
    {
        return 0.0;
    }*/

	//TODO: count vai dataPoints->size? Eli datapisteiden määrä clusterissa vai koko datasetissä?
    double mse = sse;// (dataPoints->size * dimensions);
    return mse;
}

/**
 * @brief Finds the nearest centroid to a given data point.
 *
 * This function calculates the squared Euclidean distance between the query point and each centroid,
 * and returns the index of the nearest centroid.
 *
 * @param queryPoint A pointer to the DataPoint structure representing the query point.
 * @param targetCentroids A pointer to the Centroids structure containing the centroids.
 * @return The index of the nearest centroid.
 */
size_t findNearestCentroid(const DataPoint* queryPoint, const Centroids* targetCentroids)
{
    // Debugging    
    /*if (targetPoints->size == 0)
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

/**
 * @brief Assigns each data point to the nearest centroid.
 *
 * This function iterates through all data points and assigns each one to the nearest centroid
 * based on the squared Euclidean distance.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 */
void partitionStep(DataPoints* dataPoints, const Centroids* centroids)
{
    //DEBUGGING    
    /*if (dataPoints->size == 0 || centroids->size == 0)
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

/**
 * @brief Performs the centroid step in the k-means algorithm.
 *
 * This function updates the centroids by calculating the mean of the data points assigned to each centroid.
 *
 * @param centroids A pointer to the Centroids structure containing the centroids to be updated.
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 */
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
        size_t clusterLabel = point->partition; 

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
        /*else
        {
            if(LOGGING >= 3) fprintf(stderr, "Warning: Cluster %zu has no points assigned.\n", clusterLabel);
        }*/

        free(sums[clusterLabel]);
    }

    free(sums);
    free(counts);
}

/**
 * @brief Counts the number of centroids in centroids2 that do not have any centroids in centroids1 assigned to them.
 *
 * This function iterates through all centroids in centroids1 and finds the nearest centroid in centroids2.
 * It then counts the number of centroids in centroids2 that do not have any centroids in centroids1 assigned to them.
 *
 * @param centroids1 A pointer to the first Centroids structure.
 * @param centroids2 A pointer to the second Centroids structure.
 * @return The number of centroids in centroids2 that do not have any centroids in centroids1 assigned to them.
 */
size_t countOrphans(const Centroids* centroids1, const Centroids* centroids2)
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

/**
 * @brief Calculates the Centroid Index (CI) between two sets of centroids.
 *
 * This function calculates the Centroid Index (CI) by counting the number of orphan centroids
 * between two sets of centroids. It returns the maximum count of orphans from either set.
 *
 * @param centroids1 A pointer to the first Centroids structure.
 * @param centroids2 A pointer to the second Centroids structure.
 * @return The Centroid Index (CI) between the two sets of centroids.
 */
size_t calculateCentroidIndex(const Centroids* centroids1, const Centroids* centroids2)
{
    if (LOGGING >= 2)
    {
        printf("centroids1: %zu AND 2: %zu\n\n", centroids1->size, centroids2->size);
        printf("centroids1: %f AND 2: %f\n\n", centroids1->points->attributes[0], centroids2->points->attributes[0]);
    }
    
    size_t countFrom1to2 = countOrphans(centroids1, centroids2);
    size_t countFrom2to1 = countOrphans(centroids2, centroids1);

    if (LOGGING >= 2)
    {
        printf("Count from 1 to 2: %zu AND 2 to 1: %zu\n\n", countFrom1to2, countFrom2to1);
    }

    return (countFrom1to2 > countFrom2to1) ? countFrom1to2 : countFrom2to1;
}

/**
 * @brief Saves the state of centroids and partitions at a specific iteration.
 *
 * This function saves the current state of centroids and partitions during algorithm execution,
 * allowing for visualization of how the algorithm progresses over iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param iteration The current iteration number.
 * @param outputDirectory The directory where the files should be created.
 * @param algorithmName A name identifier for the algorithm being run.
 */
void saveIterationState(const DataPoints* dataPoints, const Centroids* centroids,
    size_t iteration, const char* outputDirectory, const char* algorithmName)
{
    char centroidsFileName[256];
    char partitionsFileName[256];

    snprintf(centroidsFileName, sizeof(centroidsFileName), "%s_centroids_iter_%zu.txt", algorithmName, iteration);
    snprintf(partitionsFileName, sizeof(partitionsFileName), "%s_partitions_iter_%zu.txt", algorithmName, iteration);

    writeCentroidsToFile(centroidsFileName, centroids, outputDirectory);
    writeDataPointPartitionsToFile(partitionsFileName, dataPoints, outputDirectory);
}

/**
 * @brief Writes iteration statistics to a file.
 *
 * This function appends iteration statistics to a file, recording metrics at each step
 * of the algorithm for later visualization and analysis.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param iteration The current iteration number.
 * @param mse The mean squared error at this iteration.
 * @param splitCluster The cluster that was split at this iteration (if applicable).
 * @param outputDirectory The directory where the file should be created.
 * @param algorithmName A name identifier for the algorithm being run.
 */
void writeIterationStats(const DataPoints* dataPoints, const Centroids* centroids,
    const Centroids* groundTruth, size_t iteration, double mse,
    size_t splitCluster, const char* outputDirectory, const char* algorithmName)
{
    char statsFileName[256];
    snprintf(statsFileName, sizeof(statsFileName), "%s_iteration_stats.txt", algorithmName);

    char outputFilePath[256];
    snprintf(outputFilePath, sizeof(outputFilePath), "%s/%s", outputDirectory, statsFileName);

    // Create the file with headers if it doesn't exist
    FILE* statsFile = NULL;
    if (iteration == 0) {
        if (fopen_s(&statsFile, outputFilePath, "w") != 0) {
            handleFileError(outputFilePath);
            return;
        }
        fprintf(statsFile, "Iteration,NumCentroids,MSE,CI,SplitCluster\n");
    }
    else {
        if (fopen_s(&statsFile, outputFilePath, "a") != 0) {
            handleFileError(outputFilePath);
            return;
        }
    }

    size_t ci = calculateCentroidIndex(centroids, groundTruth);
    fprintf(statsFile, "%zu,%zu,%.5f,%zu,%zu\n",
        iteration, centroids->size, mse, ci, splitCluster);

    fclose(statsFile);
}

/**
 * @brief Runs the k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, calculates the MSE,
 * and returns the best MSE obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param iterations The maximum number of iterations to run the k-means algorithm.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @return The best mean squared error (MSE) obtained during the iterations.
 */
double runKMeans(DataPoints* dataPoints, size_t iterations, Centroids* centroids, const Centroids* groundTruth)
{
    double bestMse = DBL_MAX;
    double mse = DBL_MAX;

    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        partitionStep(dataPoints, centroids);

        centroidStep(centroids, dataPoints);
        
        //TODO: mse vai SSE? Tällä hetkellä SSE vaikka muuttujat ovat mse
        mse = calculateSSE(dataPoints, centroids);

        /*if (LOGGING >= 3)
        {
			size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
            printf("(runKMeans)After iteration %zu: CI = %zu and MSE = %.5f\n", iteration + 1, centroidIndex, mse / 10000);
        }*/

        if (mse < bestMse)
        {
			//if (LOGGING >= 3) printf("Best MSE so far: %.5f\n", mse / 10000);
            bestMse = mse;
        }
        else
        {
            break; // Exit the loop if the MSE does not improve
        }
    }

    return bestMse;
}

/**
 * @brief Performs random swaps of centroids and evaluates the resulting clustering using k-means.
 *
 * This function performs random swaps of centroids with data points, runs k-means on the modified centroids,
 * and keeps the changes if the mean squared error (MSE) improves. If the MSE does not improve, it reverses the swap.
 * The function returns the best mean squared error (MSE) obtained during the swaps.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param maxSwaps The maximum number of attempted swaps.
 * @return The best mean squared error (MSE) obtained during the swaps.
 */
double randomSwap(DataPoints* dataPoints, Centroids* centroids, size_t maxSwaps, const Centroids* groundTruth)
{
    double bestMse = DBL_MAX;
    size_t kMeansIterations = 2;
    size_t numCentroids = centroids->size;
    size_t dimensions = centroids->points[0].dimensions;

    size_t totalAttributes = numCentroids * dimensions;

    double* backupAttributes = malloc(totalAttributes * sizeof(double));
    handleMemoryError(backupAttributes);

    for (size_t i = 0; i < maxSwaps; ++i)
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
        double resultMse = runKMeans(dataPoints, kMeansIterations, centroids, groundTruth);

        //If 1) MSE improves, we keep the change
        //if not, we reverse the swap
        if (resultMse < bestMse)
        {
            /*if (LOGGING >= 3)
            {
				size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
                printf("(RS) Round %zu: Best Centroid Index (CI): %zu and Best Sum-of-Squared Errors (MSE): %.5f\n", i + 1, centroidIndex, resultMse / 10000);
            }*/
            
            bestMse = resultMse;
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

    return bestMse;
}

/**
*@brief Splits a cluster into two sub - clusters using local k - means.
*
*This function splits a cluster into two sub - clusters by selecting two random centroids
* from the data points in the cluster and running local k - means.It updates the partitions
* and centroids based on the results of the local k - means.
*
*@param dataPoints A pointer to the DataPoints structure containing the data points.
* @param centroids A pointer to the Centroids structure containing the centroids.
* @param clusterToSplit The index of the cluster to split.
* @param localMaxIterations The maximum number of iterations for the local k - means.
* @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
*/
void splitClusterIntraCluster(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t localMaxIterations, const Centroids* groundTruth)
{
    size_t clusterSize = 0;
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterToSplit)
        {
            clusterSize++;
        }
    }

    // Random split will break without this
    if (clusterSize < 2)
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
	Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);

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
    //TODO: poista "resultMse" jos ei tarvita
    double resultMse = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, groundTruth);

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
    
    //#2
    centroids->size++;
    centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint));
    handleMemoryError(centroids->points);
	centroids->points[centroids->size - 1] = allocateDataPoint(dataPoints->points[0].dimensions);
    deepCopyDataPoint(&centroids->points[centroids->size - 1], &localCentroids.points[1]);
    

    // Cleanup
    free(clusterIndices);
    free(pointsInCluster.points);
    free(localCentroids.points);
}

/**
 * @brief Splits a cluster into two sub-clusters using global k-means.
 *
 * This function splits a cluster into two sub-clusters by selecting two random centroids
 * from the data points in the cluster and running global k-means. It updates the partitions
 * and centroids based on the results of the global k-means.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param clusterToSplit The index of the cluster to split.
 * @param globalMaxIterations The maximum number of iterations for the global k-means.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 */
void splitClusterGlobal(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t globalMaxIterations, const Centroids* groundTruth)
{
    size_t clusterSize = 0;

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterToSplit)
        {
            clusterSize++;
        }
    }

    //Debugging
    /*if (clusterSize < 2)
    {
        return;
    }*/

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
	centroids->points[centroids->size - 1] = allocateDataPoint(dataPoints->points[0].dimensions);
    deepCopyDataPoint(&centroids->points[centroids->size - 1], &dataPoints->points[datapoint2]);

    // Run global k-means, this time including the new centroids
    double resultMse = runKMeans(dataPoints, globalMaxIterations, centroids, groundTruth);

    // Cleanup
    free(clusterIndices);
}

// TODO: vain split k-means, haluanko myös random swappiin?
// TODO: kesken
/**
 * @brief Performs local repartitioning of data points to the nearest centroid.
 *
 * This function reassigns data points to the nearest centroid if the nearest centroid
 * is either the new cluster or the cluster to be split. It updates the clustersAffected
 * array to mark the affected clusters.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param clusterToSplit The index of the cluster to split.
 * @param clustersAffected A pointer to the array indicating which clusters are affected.
 */
void localRepartition(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, bool* clustersAffected)
{
    size_t newClusterIndex = centroids->size - 1;

    /* TODO: Kysy/selvitä, että tarvitaanko tätä? Oma oletus on, että ei tarvita
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
            //if (LOGGING >= 3) printf("Local repartition: Success, point %zu is reassigned\n", i);

            clustersAffected[currentCluster] = true;    // Mark the old cluster as affected
            dataPoints->points[i].partition = nearestCentroid;
        }
    }

    //if(LOGGING >= 3) printf("Local repartition is over\n\n");
}

//todo: centroids turha tänne?
/**
 * @brief Calculates the MSE drop for a tentative split of a cluster.
 *
 * This function calculates the MSE drop by running local k-means on a cluster and comparing
 * the new MSE with the original MSE. It returns the difference between the original MSE and the new MSE.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param clusterLabel The label of the cluster to split.
 * @param localMaxIterations The maximum number of iterations for the local k-means.
 * @param originalClusterMSE The original MSE of the cluster before the split.
 * @return The MSE drop for the tentative split of the cluster.
 */
double tentativeMseDrop(DataPoints* dataPoints, size_t clusterLabel, size_t localMaxIterations, double originalClusterMSE)
{
    size_t clusterSize = 0;
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterLabel)
        {
            clusterSize++;
        }
    }
    
    //DEBUGGING
    /*if (clusterSize < 2)
    {
        return 0.0;
    }*/

    DataPoints pointsInCluster = allocateDataPoints(clusterSize, dataPoints->points[0].dimensions);

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

    Centroids localCentroids = allocateCentroids(2,dataPoints->points[0].dimensions);

    deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[idx1]);
    deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[idx2]);

    // k-means
    double resultMse = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, NULL);

    // Calculate combined MSE of the two clusters
	//TODO: WithSize vai ilman? Eli koko datasetin mukaan vai pelkästään clusterin mukaan?
    //      Jos withSize, niin tätähän ei edes tarvita, resultMse on sama?
    double newClusterMSE = calculateMSEWithSize(&pointsInCluster, &localCentroids, dataPoints->size);

    double mseDrop = originalClusterMSE - resultMse;

    freeDataPoints(&pointsInCluster);
    freeCentroids(&localCentroids);

    return mseDrop;
}

/**
 * @brief Calculates the MSE drop for a tentative split of a cluster.
 *
 * This function calculates the MSE drop by running local k-means on a cluster and comparing
 * the new MSE with the original MSE. It returns the difference between the original MSE and the new MSE.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param clusterLabel The label of the cluster to split.
 * @param localMaxIterations The maximum number of iterations for the local k-means.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @return A ClusteringResult structure containing the new centroids and the MSE drop.
 */
ClusteringResult tentativeSplitterForBisecting(DataPoints* dataPoints, size_t clusterLabel, size_t localMaxIterations, const Centroids* groundTruth)
{
    size_t clusterSize = 0;
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterLabel)
        {
            clusterSize++;
        }
    }

	//DEBUGGING
    /*if (clusterSize < 2)
    {
        printf("Not enough points to split the cluster\n");
        exit(EXIT_FAILURE);
    }*/


    DataPoints pointsInCluster = allocateDataPoints(clusterSize, dataPoints->points[0].dimensions);

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

    Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);
    deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[idx1]);
    deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[idx2]);

    ClusteringResult localResult = allocateClusteringResult(dataPoints->size, 2, dataPoints->points[0].dimensions);

    // k-means
    localResult.mse = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, groundTruth);

    // Calculate combined MSE of the two clusters
    //TODO: WithSize vai ilman? Eli koko datasetin mukaan vai pelkästään clusterin mukaan?
    //      Jos withSize, niin tätähän ei edes tarvita, resultMse on sama?
    double newClusterMSE = calculateMSEWithSize(&pointsInCluster, &localCentroids, dataPoints->size);

    // Calculate the MSE drop
    //localResult.mse = newClusterMSE;

	deepCopyDataPoint(&localResult.centroids[0], &localCentroids.points[0]);
    deepCopyDataPoint(&localResult.centroids[1], &localCentroids.points[1]);

    freeDataPoints(&pointsInCluster);
    freeCentroids(&localCentroids);;

    return localResult;
}


/**
 * @brief Runs the split k-means algorithm with random splitting.
 *
 * This function iterates through partition and centroid steps, randomly selects a cluster to split,
 * and updates the centroids and partitions based on the results of the local k-means.
 * It returns the best mean squared error (MSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param maxCentroids The maximum number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @return The best mean squared error (MSE) obtained during the iterations.
 */
double runRandomSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, const Centroids* groundTruth)
{
    size_t localMaxIterations = 2;

    partitionStep(dataPoints, centroids);
    centroidStep(centroids, dataPoints);

    while(centroids->size < maxCentroids)
    {
        size_t clusterToSplit = rand() % centroids->size;

        splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, localMaxIterations, groundTruth);

        /*if (LOGGING >= 3)
        {
            size_t ci = calculateCentroidIndex(centroids, groundTruth);
            double mse = calculateMSE(dataPoints, centroids);
            printf("(RandomSplit) Number of centroids: %zu, CI: %zu, and MSE: %.5f \n", centroids->size, ci, mse / 10000);
        }*/
    }	

    //TODO: globaali k-means  
    double finalResultMse = runKMeans(dataPoints, maxIterations, centroids, groundTruth);

    return finalResultMse;
}

/**
 * @brief Runs the split k-means algorithm with tentative splitting (choosing to split the one that reduces the MSE the most).
 *
 * This function iterates through partition and centroid steps, selects a cluster to split based on the MSE drop,
 * and updates the centroids and partitions based on the results of the local k-means.
 * It returns the best mean squared error (MSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param maxCentroids The maximum number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param splitType The type of split to perform (0 = intra-cluster, 1 = global, 2 = local repartition).
 * @return The best mean squared error (MSE) obtained during the iterations.
 */
double runMseSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, const Centroids* groundTruth, size_t splitType, const char* outputDirectory, 
    bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv)
{
    //TODO: entä jos globaalia ei rajoittaisi, olisiko tullut paremmat tulokset???????
    //TODO: pohdi tarkemmat arvot, globaaliin 5 näyttää toimivan hyvin
    size_t iterations = splitType == 0 ? maxIterations : splitType == 1 ? maxIterations : maxIterations;

    double* clusterMSEs = malloc(maxCentroids * sizeof(double));
    handleMemoryError(clusterMSEs);

    double* MseDrops = calloc(maxCentroids, sizeof(size_t));
    handleMemoryError(MseDrops);

    bool* clustersAffected = calloc(maxCentroids*2, sizeof(bool));
    handleMemoryError(clustersAffected);

    clock_t iterEnd;
	double iterDuration = 0.0;
    char sseCsvFile[256];    
    if (trackTime) 
    {
        snprintf(sseCsvFile, sizeof(sseCsvFile), "%s/sse_log.csv", outputDirectory);

        if (!fileExists(sseCsvFile))
        {
            FILE* csvFile = fopen(sseCsvFile, "w");
            if (csvFile == NULL) {
                handleFileError(sseCsvFile);
            }
            fprintf(csvFile, "ci;centroids;sse\n");
            fclose(csvFile);
        }

        iterEnd = clock();
        iterDuration = ((double)(iterEnd - start)) / CLOCKS_PER_MS;
        timeList[(*timeIndex)++] = iterDuration;
    }
    
    if (trackProgress) //TODO: tämä tehdään nyt vain iteraatiolle 0. Halutaanko tehdä esim niin monta kertaa että CI=0 tmv?
    {
        const char* splitTypeName = getSplitTypeName(splitType);
        double initialMse = calculateMSE(dataPoints, centroids);
        writeIterationStats(dataPoints, centroids, groundTruth, 0, initialMse, SIZE_MAX, outputDirectory, splitTypeName);
        saveIterationState(dataPoints, centroids, 0, outputDirectory, splitTypeName);   
    }

    size_t currentCi = 0;
    double currentSse = 0.0;
    if (createCsv)
    {
        currentCi = calculateCentroidIndex(centroids, groundTruth);
        currentSse = calculateSSE(dataPoints, centroids);
        appendLogCsv(sseCsvFile, centroids->size, currentCi, currentSse);
    }

    //Only 1 cluster, so no need for decision making
    size_t initialClusterToSplit = 0;
    splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, iterations, groundTruth);

    if (trackTime)
    {
        iterEnd = clock();
        iterDuration = ((double)(iterEnd - start)) / CLOCKS_PER_MS;
        timeList[(*timeIndex)++] = iterDuration;
    }

    if (createCsv)
    {
        currentCi = calculateCentroidIndex(centroids, groundTruth);
        currentSse = calculateSSE(dataPoints, centroids);
        appendLogCsv(sseCsvFile, centroids->size, currentCi, currentSse);
    }    

    if (trackProgress)
    {
        const char* splitTypeName = getSplitTypeName(splitType);
        double initialMse = calculateMSE(dataPoints, centroids);
        writeIterationStats(dataPoints, centroids, groundTruth, 1, initialMse, SIZE_MAX, outputDirectory, splitTypeName);
        saveIterationState(dataPoints, centroids, 1, outputDirectory, splitTypeName);
    }

    for (size_t i = 0; i < centroids->size; ++i)
    {
        clusterMSEs[i] = calculateClusterMSE(dataPoints, centroids, i); //TODO: tarvitaanko omaa rakennetta?
        MseDrops[i] = tentativeMseDrop(dataPoints, i, iterations, clusterMSEs[i]);
    }

    size_t iterationCount = 2; //Helper for tracking progress

    while (centroids->size < maxCentroids)
    {
		// Choose the cluster that reduces the MSE the most
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

        /*if (LOGGING >= 3 && splitType == 3)
        {
            printDataPointsPartitions(dataPoints, centroids->size);
        }*/

        if(splitType == 0) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, iterations, groundTruth);
		else if (splitType == 1) splitClusterGlobal(dataPoints, centroids, clusterToSplit, iterations, groundTruth);
		else if (splitType == 2) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, iterations, groundTruth);

		if (splitType == 0) // Intra-cluster
        {
            // Recalculate MSE for the affected clusters
            clusterMSEs[clusterToSplit] = calculateClusterMSE(dataPoints, centroids, clusterToSplit);
            clusterMSEs[centroids->size - 1] = calculateClusterMSE(dataPoints, centroids, centroids->size - 1);

            // Update MseDrops for the affected clusters
            MseDrops[clusterToSplit] = tentativeMseDrop(dataPoints, clusterToSplit, iterations, clusterMSEs[clusterToSplit]);
            MseDrops[centroids->size - 1] = tentativeMseDrop(dataPoints, centroids->size - 1, iterations, clusterMSEs[centroids->size - 1]);
        }
		else if (splitType == 1) // Global
        {            
            for (size_t i = 0; i < centroids->size; ++i)
            {
                clusterMSEs[i] = calculateClusterMSE(dataPoints, centroids, i);
                MseDrops[i] = tentativeMseDrop(dataPoints, i, iterations, clusterMSEs[i]);
            }
        }
		else if (splitType == 2) // Local repartition
        {            
            localRepartition(dataPoints, centroids, clusterToSplit, clustersAffected);
            
			clustersAffected[clusterToSplit] = true;
			clustersAffected[centroids->size - 1] = true;

			// Recalculate MseDrop for affected clusters (old and new)
            for (size_t i = 0; i < centroids->size; ++i)
            {
                if (clustersAffected[i])
                {
                    //if (LOGGING >= 3) printf("Affected cluster: %zu\n", i);

                    clusterMSEs[i] = calculateClusterMSE(dataPoints, centroids, i);
                    MseDrops[i] = tentativeMseDrop(dataPoints, i, iterations, clusterMSEs[i]);
                }
            }

            memset(clustersAffected, 0, (maxCentroids * 2) * sizeof(bool));
        }

        if (trackTime)
        {
            iterEnd = clock();
            iterDuration = ((double)(iterEnd - start)) / CLOCKS_PER_MS;
            timeList[(*timeIndex)++] = iterDuration;
        }

        if (trackProgress)
        {
            const char* splitTypeName = getSplitTypeName(splitType);
            double initialMse = calculateMSE(dataPoints, centroids);
            writeIterationStats(dataPoints, centroids, groundTruth, iterationCount, initialMse, SIZE_MAX, outputDirectory, splitTypeName);
            saveIterationState(dataPoints, centroids, iterationCount, outputDirectory, splitTypeName);
            iterationCount++;
        }
        
        if (createCsv)
        {
            currentCi = calculateCentroidIndex(centroids, groundTruth);
            currentSse = calculateSSE(dataPoints, centroids);
            appendLogCsv(sseCsvFile, centroids->size, currentCi, currentSse);
        }        
        
        /*if (LOGGING >= 3 && splitType == 2)
        {
            size_t ci = calculateCentroidIndex(centroids, groundTruth);
            double mse = calculateMSE(dataPoints, centroids);
            printf("(MseSplit) Number of centroids: %zu, CI: %zu, and MSE: %.5f \n", centroids->size, ci, mse / 10000);
        }*/


        //if (LOGGING >= 3 && splitType == 2) printf("Round over\n\n");
    }

    free(clusterMSEs);
	free(MseDrops);
	free(clustersAffected);

    //TODO: globaali k-means  
    double finalResultMse = runKMeans(dataPoints, maxIterations, centroids, groundTruth);

    if (trackTime)
    {
        iterEnd = clock();
        iterDuration = ((double)(iterEnd - start)) / CLOCKS_PER_SEC;
        timeList[(*timeIndex)++] = iterDuration;
    }

    if (trackProgress)
    {
        const char* splitTypeName = getSplitTypeName(splitType);
        writeIterationStats(dataPoints, centroids, groundTruth, iterationCount, finalResultMse, SIZE_MAX, outputDirectory, splitTypeName);
        saveIterationState(dataPoints, centroids, iterationCount, outputDirectory, splitTypeName);
        iterationCount++;
    }

    if (createCsv)
    {
        currentCi = calculateCentroidIndex(centroids, groundTruth);
        currentSse = calculateSSE(dataPoints, centroids);
        appendLogCsv(sseCsvFile, centroids->size, currentCi, currentSse);
    }

    return finalResultMse;
}

/**
 * @brief Runs the Bisecting k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, selects a cluster to split based on the SSE,
 * and updates the centroids and partitions based on the results of the local k-means.
 * It returns the best mean squared error (MSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param maxCentroids The maximum number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @return The best mean squared error (MSE) obtained during the iterations.
 */
double runBisectingKMeans(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, const Centroids* groundTruth)
{
	size_t bisectingIterations = 5;
    
    double* SseList = malloc(maxCentroids * sizeof(size_t));
    handleMemoryError(SseList);
    double bestMse = DBL_MAX;

    DataPoint newCentroid1 = allocateDataPoint(dataPoints->points[0].dimensions);
    DataPoint newCentroid2 = allocateDataPoint(dataPoints->points[0].dimensions);

	//Step 0: Only 1 cluster, so no need for decision making
    size_t initialClusterToSplit = 0;
    splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, maxIterations, groundTruth);

	SseList[0] = calculateClusterMSE(dataPoints, centroids, 0);
    SseList[1] = calculateClusterMSE(dataPoints, centroids, 1);

    //Repeat until we have K clusters
    for (size_t i = 2; centroids->size < maxCentroids; ++i)
    {
        //if (LOGGING >= 3) printf("(BKM) Round %zu: Centroids: %zu\n", i-1, centroids->size);

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

		//Repeat for a set number of iterations
        for (size_t j = 0; j < bisectingIterations; ++j)
        {
			ClusteringResult curr = tentativeSplitterForBisecting(dataPoints, clusterToSplit, maxIterations, groundTruth);

            //if (LOGGING >= 3) printf("(RKM) Round %d: Latest Centroid Index (CI): %zu and Latest Mean Sum-of-Squared Errors (MSE): %.4f\n", repeat, result1.centroidIndex, result1.mse / 10000);

			// If the result is better than the best result so far, update the best result
            if (curr.mse < bestMse)
            {
                //if (LOGGING >= 3) printf("(from inner) Round %zu\n", j);
                bestMse = curr.mse;
                
                /*if (LOGGING >= 3)
                {
                    Centroids tempCentroids;
                    tempCentroids.size = 2;
                    tempCentroids.points = curr.centroids;
                    printCentroidsInfo(&tempCentroids);
                }*/                
                
                // Save the two new centroids
				deepCopyDataPoint(&newCentroid1, &curr.centroids[0]);
                deepCopyDataPoint(&newCentroid2, &curr.centroids[1]);

				freeClusteringResult(&curr, 2);
            }
        }

		// Replace the old centroid with the new centroid1
        deepCopyDataPoint(&centroids->points[clusterToSplit], &newCentroid1);

		// Increase the size of the centroids array and add the new centroid2
        centroids->points = realloc(centroids->points, (centroids->size + 1) * sizeof(DataPoint));
        handleMemoryError(centroids->points);
        centroids->points[centroids->size] = allocateDataPoint(newCentroid2.dimensions);
        deepCopyDataPoint(&centroids->points[centroids->size], &newCentroid2);
        centroids->size++;

        //printf("CI %zu\n", calculateCentroidIndex(centroids, groundTruth));

        //if (LOGGING >= 3) printf("(from outer) Round %zu\n", i);
        //printCentroidsInfo(centroids);

		partitionStep(dataPoints, centroids); //partition step, vai pitäisikö tallentaa ne loopissa ja tehdä tässä sitten muutokset? (esim kaikki 0->clusterToSplit, 1->centroids->size-1)

		//Step 3: Update the SSE list
        // Recalculate MSE for the affected clusters
        SseList[clusterToSplit] = calculateClusterMSE(dataPoints, centroids, clusterToSplit);
        SseList[centroids->size - 1] = calculateClusterMSE(dataPoints, centroids, centroids->size - 1);
        
        /*if (centroids->size == maxCentroids - 1) {
            writeCentroidsToFile("Bisecting_lastStep_centroids.txt", centroids);
            writeDataPointPartitionsToFile("Bisecting_lastStep_partitions.txt", dataPoints,);
        }*/

        bestMse = DBL_MAX;
    }

	//Step 4: Run the final k-means
    double finalResultMse = runKMeans(dataPoints, maxIterations, centroids, groundTruth); //TODO: Pitäisikö poistaa? Final K-means ei taida olla algoritmia
    //printf("size  %zu\n", centroids->size);
    // Cleanup
    free(SseList);
	freeDataPoint(&newCentroid1);
	freeDataPoint(&newCentroid2);

    //TODO: Kommentoi/poista/tee jotain, ei tartte joka kierroksella kirjoittaa tiedostoon
    //writeCentroidsToFile("outputs/centroids.txt", centroids);
    //writeDataPointPartitionsToFile("outputs/partitions.txt", dataPoints);

    return finalResultMse;
}

/**
 * @brief Runs the k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, calculates the MSE,
 * and returns the best MSE obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the MSE values.
 * @param fileName The name of the file to write the results to.
 * @param outputDirectory The directory where the results file is located.
 */
void runKMeansAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory)
{
    Statistics stats;
    initializeStatistics(&stats);

    clock_t start, end;
    double duration;
    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    printf("K-means\n");

    for (size_t i = 0; i < loopCount; ++i)
    {
        Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

        start = clock();

        generateRandomCentroids(numCentroids, dataPoints, &centroids);

        /*if (LOGGING >= 3)
        {
            size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);
            printf("(K-means)Initial Centroid Index (CI): %zu\n", centroidIndex);
        }*/

        double resultMse = runKMeans(dataPoints, maxIterations, &centroids, groundTruth);
        //if(LOGGING >= 3) printf("(K-means)Best Centroid Index (CI): %d and Best Sum-of-Squared Errors (MSE): %.4f\n", result.centroidIndex, result.mse / 10000000);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;

        //if (LOGGING >= 3) printf("(K-means)Time taken: %.2f seconds\n\n", duration);

        size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

        stats.mseSum += resultMse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;

        //if (LOGGING >= 3) printf("Round %zu\n", i + 1);

        if (centroidIndex != 0 && savedNonZeroResults == false)
        {
            writeCentroidsToFile("kMeans_centroids_failed.txt", &centroids, outputDirectory);
            writeDataPointPartitionsToFile("kMeans_partitions_failed.txt", dataPoints, outputDirectory);
            savedNonZeroResults = true;
        }
        else if (centroidIndex == 0 && savedNonZeroResults == false)
        {
            writeCentroidsToFile("kMeans_centroids_perfect.txt", &centroids, outputDirectory);
            writeDataPointPartitionsToFile("kMeans_partitions_perfect.txt", dataPoints, outputDirectory);
			savedNonZeroResults = true;
        }

        freeCentroids(&centroids);
    }

    printStatistics("K-means", stats, loopCount, numCentroids, scaling);

    writeResultsToFile(fileName, stats, numCentroids, "K-means", loopCount, scaling, outputDirectory);
}

/**
 * @brief Runs the repeated k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, calculates the MSE,
 * and returns the best MSE obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param maxRepeats The maximum number of repeats for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the MSE values.
 * @param fileName The name of the file to write the results to.
 * @param outputDirectory The directory where the results file is located.
 */
void runRepeatedKMeansAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t maxRepeats, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory)
{
    Statistics stats;
    initializeStatistics(&stats);

    clock_t start, end;
    double duration;

    printf("Repeated K-means\n");

    for (size_t i = 0; i < loopCount; ++i)
    {
		double bestMse = DBL_MAX;

        Centroids bestCentroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

        start = clock();

        for (size_t j = 0; j < maxRepeats; ++j)
        {
            Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);
            generateRandomCentroids(numCentroids, dataPoints, &centroids);

            /*if (LOGGING >= 3)
            {
                size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);
                printf("(Repeated K-means)Initial Centroid Index (CI): %zu\n", centroidIndex);
            }*/

            double resultMse = runKMeans(dataPoints, maxIterations, &centroids, groundTruth);

            if (resultMse < bestMse)
            {
                bestMse = resultMse;
				deepCopyCentroids(&centroids, &bestCentroids, numCentroids);
            }

            freeCentroids(&centroids);
        }

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;

        //if (LOGGING >= 3) printf("(Repeated K-means)Time taken: %.2f seconds\n\n", duration);

        size_t centroidIndex = calculateCentroidIndex(&bestCentroids, groundTruth);

        stats.mseSum += bestMse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;

        //if (LOGGING >= 3) printf("Round %zu\n", i + 1);

        if (i == 0)
        {
            writeCentroidsToFile("repeatedKMeans_centroids.txt", &bestCentroids, outputDirectory);
            writeDataPointPartitionsToFile("repeatedKMeans_partitions.txt", dataPoints, outputDirectory);
        }

        freeCentroids(&bestCentroids);
    }

    printStatistics("Repeated K-means", stats, loopCount, numCentroids, scaling);

    writeResultsToFile(fileName, stats, numCentroids, "Repeated K-means", loopCount, scaling, outputDirectory);
}

/**
 * @brief Runs the repeated k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, calculates the MSE,
 * and returns the best MSE obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param maxRepeats The maximum number of repeats for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the MSE values.
 * @param fileName The name of the file to write the results to.
 * @param outputDirectory The directory where the results file is located.
 */
void runRandomSwapAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxSwaps, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory)
{
    Statistics stats;
    initializeStatistics(&stats);

    clock_t start, end;
    double duration;

    printf("Random swap\n");

    for (size_t i = 0; i < loopCount; ++i)
    {
        Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

        start = clock();

        generateRandomCentroids(numCentroids, dataPoints, &centroids);

        // Random Swap
        double resultMse = randomSwap(dataPoints, &centroids, maxSwaps, groundTruth);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;

        size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

        //if (LOGGING >= 3) printf("(Random Swap)Time taken: %.2f seconds\n\n", duration);

        stats.mseSum += resultMse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;

        //if (LOGGING >= 3) printf("Round %zu\n", i + 1);

        if (i == 0)
        {
            writeCentroidsToFile("randomSwap_centroids.txt", &centroids, outputDirectory);
            writeDataPointPartitionsToFile("randomSwap_partitions.txt", dataPoints, outputDirectory);
        }

        freeCentroids(&centroids);
    }

    printStatistics("Random Swap", stats, loopCount, numCentroids, scaling);

    writeResultsToFile(fileName, stats, numCentroids, "Random swap", loopCount, scaling, outputDirectory);
}

/**
 * @brief Runs the split k-means algorithm with random splitting.
 *
 * This function iterates through partition and centroid steps, randomly selects a cluster to split,
 * and updates the centroids and partitions based on the results of the local k-means.
 * It returns the best mean squared error (MSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the MSE values.
 * @param fileName The name of the file to write the results to.
 * @param outputDirectory The directory where the results file is located.
 */
void runRandomSplitAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory)
{
    Statistics stats;
    initializeStatistics(&stats);

    clock_t start, end;
    double duration;

    printf("Random Split k-means\n");

    for (size_t i = 0; i < loopCount; ++i)
    {
        Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

        start = clock();

        generateRandomCentroids(centroids.size, dataPoints, &centroids);

        double resultMse = runRandomSplit(dataPoints, &centroids, numCentroids, maxIterations, groundTruth);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        //if (LOGGING >= 3) printf("(Split1)Time taken: %.2f seconds\n\n", duration);

        size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

        stats.mseSum += resultMse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;

        //if (LOGGING >= 3) printf("Round %zu\n", i + 1);

		//TODO: kaikki tälläiset pois lopullisesta versiosta
        if (i == 0)
        {
            writeCentroidsToFile("randomSplit_centroids.txt", &centroids, outputDirectory);
            writeDataPointPartitionsToFile("randomSplit_partitions.txt", dataPoints, outputDirectory);
        }

        freeCentroids(&centroids);
    }

    printStatistics("Random Split", stats, loopCount, numCentroids, scaling);

    writeResultsToFile(fileName, stats, numCentroids, "Random Split", loopCount, scaling, outputDirectory);
}

/**
 * @brief Runs the split k-means algorithm with tentative splitting (choosing to split the one that reduces the MSE the most).
 *
 * This function iterates through partition and centroid steps, selects a cluster to split based on the MSE drop,
 * and updates the centroids and partitions based on the results of the local k-means.
 * It returns the best mean squared error (MSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the MSE values.
 * @param fileName The name of the file to write the results to.
 * @param outputDirectory The directory where the results file is located.
 * @param splitType The type of split to perform (0 = intra-cluster, 1 = global, 2 = local repartition).
 */
void runMseSplitAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, int splitType, bool trackProgress, bool trackTime)
{
    Statistics stats;
    initializeStatistics(&stats);

    clock_t start, end;
    double duration;

    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    const char* splitTypeName = getSplitTypeName(splitType);

    printf("%s\n", splitTypeName);

    //Tracker helpers
    size_t totalIterations = loopCount * numCentroids;
    double* timeList = malloc(totalIterations * sizeof(double));
    handleMemoryError(timeList);
    size_t timeIndex = 0;

    for (size_t i = 0; i < loopCount; ++i)
    {
        resetPartitions(dataPoints);

        Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

        start = clock();

        generateRandomCentroids(centroids.size, dataPoints, &centroids);

        double resultMse = runMseSplit(dataPoints, &centroids, numCentroids, maxIterations, groundTruth, splitType, outputDirectory, (i == 0 && trackProgress), timeList, &timeIndex, start, trackTime, trackProgress);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        //if (LOGGING >= 3) printf("(Split%d)Time taken: %.2f seconds\n\n", splitType + 1, duration);

        size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

        stats.mseSum += resultMse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;

        //if (LOGGING >= 3) printf("Round %zu\n", i + 1);

        if (centroidIndex != 0 && savedNonZeroResults == false)
        {
            char centroidsFile[256];
            char partitionsFile[256];
            snprintf(centroidsFile, sizeof(centroidsFile), "%s_centroids_failed.txt", splitTypeName);
            snprintf(partitionsFile, sizeof(partitionsFile), "%s_partitions_failed.txt", splitTypeName);
            writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
            writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
            savedNonZeroResults = true;
        }
        else if (centroidIndex == 0 && savedZeroResults == false)
        {
            char centroidsFile[256];
            char partitionsFile[256];
            snprintf(centroidsFile, sizeof(centroidsFile), "%s_centroids_perfect.txt", splitTypeName);
            snprintf(partitionsFile, sizeof(partitionsFile), "%s_partitions_perfect.txt", splitTypeName);

            writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
            writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
            savedZeroResults = true;
        }

        freeCentroids(&centroids);
    }

    printStatistics(splitTypeName, stats, loopCount, numCentroids, scaling);

    writeResultsToFile(fileName, stats, numCentroids, splitTypeName, loopCount, scaling, outputDirectory);

    if (trackTime)
    {
        // Write iteration times to a new text file
        char timesFile[256];
        snprintf(timesFile, sizeof(timesFile), "%s/times.txt", outputDirectory);
        FILE* timesFilePtr = fopen(timesFile, "w");
        if (timesFilePtr == NULL)
        {
            handleFileError(timesFile);
        }
        for (size_t i = 0; i < timeIndex; ++i)
        {
            fprintf(timesFilePtr, "%.5f\n", timeList[i]);
        }
        fclose(timesFilePtr);

        free(timeList);
    }    
}

/**
 * @brief Runs the Bisecting k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, selects a cluster to split based on the SSE,
 * and updates the centroids and partitions based on the results of the local k-means.
 * It returns the best mean squared error (MSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the MSE values.
 * @param fileName The name of the file to write the results to.
 * @param outputDirectory The directory where the results file is located.
 */
void runBisectingKMeansAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory)
{
    Statistics stats;
    initializeStatistics(&stats);

    clock_t start, end;
    double duration;

    printf("Bisecting k-means\n");

    for (size_t i = 0; i < loopCount; ++i)
    {
        resetPartitions(dataPoints);

        Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

        start = clock();

        generateRandomCentroids(centroids.size, dataPoints, &centroids);

        //if (LOGGING >= 3) printDataPointsPartitions(dataPoints, centroids.size);

        double resultMse = runBisectingKMeans(dataPoints, &centroids, numCentroids, maxIterations, groundTruth);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_SEC;
        //if (LOGGING >= 3) printf("(Bisecting)Time taken: %.2f seconds\n\n", duration);
        //printf("Sizeeeee %zu\n", centroids.size);
        size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

        stats.mseSum += resultMse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;
        //printf("CI %zu\n", centroidIndex);
        //if (LOGGING >= 3) printf("Round %zu\n", i + 1);

        if (i == 0)
        {
            writeCentroidsToFile("bisectingKMeans_centroids.txt", &centroids, outputDirectory);
            writeDataPointPartitionsToFile("bisectingKMeans_partitions.txt", dataPoints, outputDirectory);
        }

        freeCentroids(&centroids);
    }

    printStatistics("Bisecting", stats, loopCount, numCentroids, scaling);

    writeResultsToFile(fileName, stats, numCentroids, "Bisecting k-means", loopCount, scaling, outputDirectory);
}


///////////
// Main //
/////////

/**
 * @brief Initializes the dataset, ground truth lists, and number of clusters.
 *
 * This function initializes the dataset and ground truth lists with the file names,
 * and initializes the number of clusters for each dataset.
 *
 * @param datasetList A pointer to the list of dataset file names.
 * @param gtList A pointer to the list of ground truth file names.
 * @param kNumList A pointer to the array of number of clusters for each dataset.
 * @param datasetCount The number of datasets.
 */
static void initializeLists(char** datasetList, char** gtList, size_t* kNumList, size_t datasetCount)
{
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

    kNumList[0] = 20;   // A1
    kNumList[1] = 35;   // A2
    kNumList[2] = 50;   // A3
    kNumList[3] = 15;   // S1
    kNumList[4] = 15;   // S2
    kNumList[5] = 15;   // S3
    kNumList[6] = 15;   // S4
    kNumList[7] = 8;    // Unbalance    
    kNumList[8] = 100;  // Birch1
    kNumList[9] = 100;  // Birch2
    kNumList[10] = 100; // Birch3
    kNumList[11] = 16;  // Dim (high)
    kNumList[12] = 16;  // Dim (high)
    kNumList[13] = 2;   // G2
    kNumList[14] = 2;   // G2
}

//END: disabloi false positive varoitukset kommenttien kera
//     Vai poistetaanko suppressit ja antaa varoitusten olla?
//END: kommentoi kaikki muistintarkastukset ja iffit pois lopullisesta versiosta <-tehokkuus
//END: credits ja alkupuhe koodin alkuun
//     edellisen alle voisi lisätä lokin, että kuka on päivittänyt ja milloin

//TODO: "static " sellaisten funktioiden eteen jotka eivät muuta mitään ja joita kutsutaan samasta tiedostosta?
//TODO: koodin palastelu eri tiedostoihin

//EXTRA: konsolikysymykset, kuten "do you want to run split k-means?" ja "do you want to run random swap?" jne?
//      ja/vai/tai komentoriviargumentit, kuten "split k-means" ja "random swap" jne? (eli että voi ajaa suoraan powershellistä)

/**
 * @brief Main function to run various clustering algorithms on multiple datasets.
 *
 * This function initializes the datasets, ground truth files, and clustering parameters.
 * It then runs different clustering algorithms (K-means, Random Swap, Random Split, MSE Split (3 variants), Bisecting K-means)
 * on each dataset and writes the results to output files.
 *
 * @return int Returns 0 on successful execution.
 */
int main()
{
	// Number of datasets
    size_t datasetCount = 15;

    // List of dataset file names, ground truth file names, and number of clusters
    const char** datasetList = createStringList(datasetCount);
    const char** gtList = createStringList(datasetCount);
    size_t* kNumList = malloc(datasetCount * sizeof(size_t));
    handleMemoryError(kNumList);	
    initializeLists(datasetList, gtList, kNumList, datasetCount);

    char outputDirectory[256]; // Buffer size = 256, increase if needed 
    createUniqueDirectory(outputDirectory, sizeof(outputDirectory));

    //TODO: muista laittaa loopin rajat oikein
    for (size_t i = 4; i < 6; ++i)
    {
        //Settings
        size_t loopCount = 10; // Number of loops to run the algorithms
        size_t scaling = 10000; // Scaling factor for the MSE values
		size_t maxIterations = 1000; // Maximum number of iterations for the k-means algorithm //TODO lopulliseen 1000(?)
		size_t maxRepeats = 10; // Maximum number of repeats for the repeated k-means algorithm //TODO lopulliseen 100(?)
		size_t maxSwaps = 1000; // Maximum number of swaps for the random swap algorithm //TODO lopulliseen 1000(?)		
		bool trackProgress = true; // Track progress for the algorithms
		bool trackTime = true; // Track time for the algorithms

        size_t numCentroids = kNumList[i];
        const char* fileName = datasetList[i];
        char dataFile[256]; // Buffer size = 256, increase if needed
        snprintf(dataFile, sizeof(dataFile), "data/%s", fileName);
		const char* gtName = gtList[i];
        char gtFile[256];
        snprintf(gtFile, sizeof(gtFile), "gt/%s", gtName);

        // Create a subdirectory for the dataset
        char datasetDirectory[256];
        createDatasetDirectory(outputDirectory, fileName, datasetDirectory, sizeof(datasetDirectory));

        // Seeding the random number generator
        srand((unsigned int)time(NULL));

        printf("Starting the process\n");
        printf("File name: %s\n", dataFile);

        size_t numDimensions = getNumDimensions(dataFile);

        if (numDimensions > 0)
        {
            printf("Number of dimensions in the data: %zu\n", numDimensions);

            DataPoints dataPoints = readDataPoints(dataFile);
            printf("Dataset size: %zu\n", dataPoints.size);

            printf("Number of clusters in the data: %zu\n", numCentroids);

            Centroids groundTruth = readCentroids(gtFile);

            printf("Number of loops: %zu\n\n", loopCount);

            // Run K-means
            runKMeansAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, fileName, datasetDirectory);

            // Run Repeated K-means
			// Too slow to keep it enabled
            //runRepeatedKMeansAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, maxRepeats, loopCount, scaling, fileName, datasetDirectory);

            // Run Random Swap
            //runRandomSwapAlgorithm(&dataPoints, &groundTruth, numCentroids, maxSwaps, loopCount, scaling, fileName, datasetDirectory);
            
            // Run Random Split
            //runRandomSplitAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, fileName, datasetDirectory);

            // Run MSE Split (Intra-cluster)
            //runMseSplitAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, fileName, datasetDirectory, 0, trackProgress);
                        
            // Run MSE Split (Global)
            //runMseSplitAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, fileName, datasetDirectory, 1, trackProgress);
                        
            // Run MSE Split (Local Repartition)
            runMseSplitAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, fileName, datasetDirectory, 2, trackProgress, trackTime);
                        
            // Run Bisecting K-means
            //runBisectingKMeansAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, fileName, datasetDirectory);

            // Clean up
            freeDataPoints(&dataPoints);
            freeCentroids(&groundTruth);
        }
    }

    freeStringList(datasetList, datasetCount);
    freeStringList(gtList, datasetCount);

    return 0;
}