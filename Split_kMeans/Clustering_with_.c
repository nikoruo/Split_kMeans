/**
 * @brief Suppresses warnings about unsafe functions in Visual Studio.
 *
 * This macro disables warnings for functions like `strcpy` and `sprintf`
 * when using the Microsoft C compiler. It is ignored by other compilers.
 */
#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS //TODO: tarkista tarvitaanko enää
#endif

 // Platform-specific includes
#include "platform.h"

// Language-specific includes
#include "locale_utils.h"

// General includes
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <float.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stddef.h>
#include <locale.h>

// Macros
#define LINE_BUFSZ 512 /* tweak if needed */


// Change logs
// 29-04-2025: Initial 1.0v release by Niko Ruohonen

// Introduction
/**
 * Project Name: Clustering_with_c
 * 
 * Description: 
 * This project focuses on the development and implementation of various clustering algorithms.
 * The primary goal was to create a novel clustering algorithm, the SSE Split Algorithm, 
 * and also to implement existing algorithms. All algorithms were designed and optimized 
 * to ensure maximum efficiency and effectiveness when applied to multi-dimensional data points.
  * 
 * Author: Niko Ruohonen
 * Date: April 29, 2025
 * Version: 1.0.0
 *
 * Details:
 * - Implements multiple clustering algorithms: K-means, Repeated K-means, Random Swap, Random Split, SSE Split (Intra-cluster, Global, Local Repartition), and Bisecting K-means.
 * - Provides detailed logging options for debugging and performance analysis.
 * - Includes memory management functions to handle dynamic allocation and deallocation of data structures.
 * - Supports reading and writing data points and centroids from/to files.
 * - Calculates various metrics such as Mean Squared Error (SSE) and Centroid Index (CI) to evaluate clustering performance.
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
 * - Some of the functions write to files using locale "fi_FI" to format numbers with commas and some with "C" to format numbers with dots. This was used to ensure compatibility with different systems (e.g., Excel and existing MATLAB).
 */

//////////////
// DEFINES //
////////////

/**
 * @brief Converts clock ticks to milliseconds.
 *
 * This macro defines the number of clock ticks per millisecond
 * based on the platform-specific `CLOCKS_PER_SEC` constant.
 * It is used to simplify time calculations when measuring durations
 * in milliseconds using `clock()`.
 */
#define CLOCKS_PER_MS ((double)CLOCKS_PER_SEC / 1000.0)

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
    DataPoint* points;   /**< Array of DataPoint structures representing the centroids. */
    size_t size;         /**< Number of centroids in the array. */
} Centroids;

/**
 * @brief Represents the result of a clustering algorithm.
 *
 * This struct contains the mean squared error (SSE) of the clustering result,
 * an array of partition indices indicating the cluster assignment for each data point,
 * an array of centroids representing the cluster centers, and the Centroid Index (CI) value.
 */
typedef struct
{
    double sse;           /**< Mean squared error (SSE) of the clustering result. */
	size_t* partition;    /**< Array of partition indices indicating the cluster assignment for each data point. */
    DataPoint* centroids; /**< Array of DataPoint structures representing the centroids. */
    size_t centroidIndex; /**< Centroid Index value. */
} ClusteringResult;

/**
 * @brief Represents statistical data collected during the execution of clustering algorithms.
 *
 * This struct contains the sum of mean squared errors (SSE), the sum of Centroid Index (CI) values,
 * the total time taken for the clustering process, and the success rate of the clustering algorithm.
 */
typedef struct
{
    double sseSum;       /**< Sum of mean squared errors (SSE) values. */
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
    for (size_t i = 0; i < size; ++i)
    {
		freeDataPoint(&points[i]);
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

    const size_t stringSize = PATH_MAX;

    for (size_t i = 0; i < size; ++i)
    {
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
     result.sse = DBL_MAX;
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

     stats->sseSum = 0.0;
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
    FILE* file;
    if (FOPEN(file, filename, "r") != 0)
    {
        handleFileError(filename);
    }

    // For disabling the CS6387 warning,
    // inform the static analyzer that 'file' is not NULL.
    // Safe to use as we actually check for NULL earlier
    _Analysis_assume_(file != NULL);

	char firstLine[LINE_BUFSZ];
    if (fgets(firstLine, sizeof(firstLine), file) == NULL)
    {
        handleFileReadError(filename);
    }

    size_t dimensions = 0;
    char* context = NULL;
	char* token = STRTOK(firstLine, " \t\r\n", &context); //Delimiter = " ", tabs "\t", newlines "\n", carriage return "\r"
    while (token != NULL)
    {
        dimensions++;
        token = STRTOK(NULL, " \t\r\n", &context); //Delimiter = " ", tabs "\t", newlines "\n", carriage return "\r" 
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
    FILE* file;
    if (FOPEN(file, filename, "r") != 0)
    {
        handleFileError(filename);
    }

    // First pass to count actual lines in the file
    size_t lineCount = 0;
    char countBuffer[1024];
    while (fgets(countBuffer, sizeof(countBuffer), file) != NULL) {
        // Skip empty lines
        if (strlen(countBuffer) > 1) { // More than just a newline character
            lineCount++;
        }
    }

    // Reset file position to beginning
    rewind(file);

    // Allocate data points based on actual line count
    DataPoints dataPoints;
    dataPoints.size = lineCount;
    dataPoints.points = malloc(lineCount * sizeof(DataPoint));
    handleMemoryError(dataPoints.points);

    // Read the file line by line
    char line[1024]; // Buffer size = 512, increase if needed
    size_t currentPoint = 0;

    while (fgets(line, sizeof(line), file) && currentPoint < lineCount)
    {
        // Skip empty lines
        if (strlen(line) <= 1) {
            continue;
        }

        // Initialize the data point
        DataPoint* point = &dataPoints.points[currentPoint];

        // Count tokens first to allocate the right size
        size_t tokenCount = 0;
        char* countContext = NULL;
        char lineCopy[1024];
        strcpy(lineCopy, line);

        char* countToken = STRTOK(lineCopy, " \t\r\n", &countContext);
        while (countToken != NULL) {
            tokenCount++;
            countToken = STRTOK(NULL, " \t\r\n", &countContext);
        }

        // Allocate memory for attributes
        point->attributes = malloc(tokenCount * sizeof(double));
        handleMemoryError(point->attributes);
        point->dimensions = 0;
        point->partition = SIZE_MAX;

        // Parse the actual values
        char* context = NULL;
        char* token = STRTOK(line, " \t\r\n", &context);

        while (token != NULL && point->dimensions < tokenCount)
        {
            point->attributes[point->dimensions++] = strtod(token, NULL);
            token = STRTOK(NULL, " \t\r\n", &context);
        }

        currentPoint++;
    }

    fclose(file);

    // Verify we read the expected number of points
    if (currentPoint != lineCount) {
        fprintf(stderr, "Error: Expected to read %zu points but only read %zu points\n", lineCount, currentPoint);
        free(dataPoints.points); // Free allocated memory
        exit(EXIT_FAILURE);
    }

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

    //printf("Read %zu centroids from %s, each with %zu dimensions\n", centroids.size, filename, centroids.points[0].dimensions);
        
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

/**
 * @brief Reads the value of K from a file.
 *
 * This function reads the value of K from the specified file. It expects the file to contain
 * a single line with a positive integer representing the number of clusters (K).
 *
 * @param path The path to the file containing the value of K.
 * @return The value of K as a size_t.
 */
static size_t read_k_from_file(const char* path)
{
    FILE* fp;
    if (FOPEN(fp, path, "r") != 0) {
        handleFileError(path);
    }
    long k = 0;
    if (fscanf(fp, "%ld", &k) != 1 || k <= 0) {
        fprintf(stderr, "Bad K in %s\n", path);
        exit(EXIT_FAILURE);
    }
    fclose(fp);
    return (size_t)k;
}

/**
 * @brief Appends a log entry to a CSV file.
 *
 * This function appends a log entry to the specified CSV file with the given parameters.
 * The log entry includes the number of centroids, iteration number, and SSE value.
 *
 * @param filePath The path to the CSV file.
 * @param iteration The current iteration number.
 * @param ci The number of centroids.
 * @param sse The sum of squared errors (SSE) value.
 */
void appendLogCsv(const char* filePath, size_t iteration, size_t ci, double sse)
{
    FILE* file;
    if (FOPEN(file, filePath, "a") != 0) {
        handleFileError(filePath);
        return;
    }
    // Write time, number of centroids, centroid index (CI) and sse
    fprintf(file, "%zu;%zu;%.0f\n", ci, iteration, sse);
    fclose(file);
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
    char outputFilePath[PATH_MAX];
    snprintf(outputFilePath, sizeof(outputFilePath), "%s%c%s", outputDirectory, PATHSEP, filename);

    FILE* centroidFile;
    if (FOPEN(centroidFile, outputFilePath, "w") != 0)
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
    char outputFilePath[PATH_MAX];
    snprintf(outputFilePath, sizeof(outputFilePath), "%s%c%s", outputDirectory, PATHSEP, filename);

    FILE* file;
    if (FOPEN(file, outputFilePath, "w") != 0)
    {
        handleFileError(outputFilePath);
        return;
    }

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        fprintf(file, "%zu\n", dataPoints->points[i].partition);
    }

    fclose(file);
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
 * @brief Writes the results of clustering algorithms to a CSV file.
 *
 * This function appends the results of clustering algorithms to a CSV file.
 * It includes the average Centroid Index (CI), Mean Squared Error (SSE),
 * relative CI, average time taken, and success rate.
 *
 * @param filename The name of the file to write the results to.
 * @param stats A Statistics structure containing the results to be written.
 * @param numCentroids The number of centroids used in the clustering algorithm.
 * @param algorithm The name of the clustering algorithm used.
 * @param loopCount The number of loops performed.
 * @param scaling A scaling factor for the SSE values.
 * @param outputDirectory The directory where the output file will be created.
 * @param dataPointsSize The size of the data points used in the clustering algorithm.
 */
void writeResultsToFile(const char* filename, Statistics stats, size_t numCentroids, const char* algorithm, size_t loopCount, size_t scaling, const char* outputDirectory, size_t dataPointsSize)
{
    char csvFileName[PATH_MAX];
    snprintf(csvFileName, sizeof(csvFileName), "%s.csv", filename);
    char outputFilePath[PATH_MAX];
    snprintf(outputFilePath, sizeof(outputFilePath), "%s%c%s", outputDirectory, PATHSEP, csvFileName);

    FILE* file;
    if (FOPEN(file, outputFilePath, "a+") != 0) {
        handleFileError(outputFilePath);
        return;
    }

    // Headers
    fseek(file, 0, SEEK_END);
    if (ftell(file) == 0)
    {
        fprintf(file, "Algorithm;Average CI;SSE;Relative CI;MS;Success Rate\n");
    }

    double avgCI = (double)stats.ciSum / loopCount;
    double sse = ((double)stats.sseSum / (double)loopCount);
    sse /= (double)scaling;
    double relCI = avgCI / (double)numCentroids;
    double avgTime = stats.timeSum / (double)loopCount;
    double succRate = (stats.successRate / (double)loopCount);
    
    fprintf(file, "%s;%.2f;%.0f;%.2f;%.0f;%.2f\n", algorithm, avgCI, sse, relCI, avgTime, succRate);
    
    fclose(file);
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
    struct tm t;
    LOCALTIME(&t, &now);

    char datebuf[32];

    if (strftime(datebuf, sizeof datebuf,
        "%Y-%m-%d_%H-%M-%S", &t) == 0)
    {
        fprintf(stderr, "strftime failed\n");
        exit(EXIT_FAILURE);
    }

    if (MAKE_DIR("outputs") != 0 && errno != EEXIST) {
        perror("mkdir outputs"); exit(EXIT_FAILURE);
    }

    if (snprintf(outputDirectory, size,
        "outputs%c%s", PATHSEP, datebuf) >= (int)size)
    {
        fprintf(stderr, "Buffer too small in createUniqueDirectory\n");
        exit(EXIT_FAILURE);
    }

    if (MAKE_DIR(outputDirectory) != 0 && errno != EEXIST)
    {
        perror("Error: Unable to create directory");
		exit(EXIT_FAILURE);
    }
}

/**
 * @brief Creates a dataset directory based on the base directory and dataset name.
 *
 * This function generates a dataset directory name by combining the base directory
 * and the dataset name. It attempts to create the directory and handles any errors.
 *
 * @param baseDirectory The base directory where the dataset directory will be created.
 * @param datasetName The name of the dataset.
 * @param datasetDirectory A buffer to store the generated dataset directory name.
 * @param size The size of the datasetDirectory buffer.
 */
void createDatasetDirectory(const char* baseDirectory, const char* datasetName, char* datasetDirectory, size_t size)
{
    snprintf(datasetDirectory, size, "%s%c%s", baseDirectory, PATHSEP, datasetName);

    if (MAKE_DIR(datasetDirectory) != 0)
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
const char* getAlgorithmName(size_t aName)
{
    switch (aName) //TODO: katso lopulliset nimet Pasin spostista
    {
    case 0:
        return "IntraCluster";
    case 1:
        return "Global";
    case 2:
        return "LocalRepartition";
	case 3:
        return "RandomSwap";
	case 4:
		return "Bisecting";
	case 5:
		return "RandomSplit";
    case 6:
		return "KMeans";
    case 7:
        return "RKM";
    default:
        fprintf(stderr, "Error: Invalid algorithm type provided\n");
        return NULL;
    }
}

/**
 * @brief Prints the statistics of a clustering algorithm.
 *
 * This function prints the average Centroid Index (CI), Mean Squared Error (SSE),
 * relative CI, average time taken, and success rate of a clustering algorithm.
 *
 * @param algorithmName The name of the clustering algorithm.
 * @param stats A Statistics structure containing the results to be printed.
 * @param loopCount The number of loops performed.
 * @param numCentroids The number of centroids used in the clustering algorithm.
 * @param scaling A scaling factor for the SSE values.
 */
void printStatistics(const char* algorithmName, Statistics stats, size_t loopCount, size_t numCentroids, size_t scaling, size_t dataSize)
{
    printf("(%s) Average CI: %.2f and SSE: %.0f\n", algorithmName, (double)stats.ciSum / loopCount, stats.sseSum / loopCount / scaling);
    printf("(%s) Relative CI: %.2f\n", algorithmName, (double)stats.ciSum / loopCount / numCentroids);
    printf("(%s) Average time taken: %.0f ms\n", algorithmName, stats.timeSum / loopCount);
    printf("(%s) Success rate: %.2f%%\n\n", algorithmName, stats.successRate / loopCount * 100);
}

/**
 * @brief Removes the file extension from a filename.
 *
 * This function removes the file extension from the given filename and returns the base filename.
 *
 * @param filename The name of the file from which to remove the extension.
 * @return The base filename without the extension.
 */
char* removeExtension(const char* filename) {
    static char baseFileName[PATH_MAX];

    strncpy(baseFileName, filename, sizeof(baseFileName) - 1);
    baseFileName[sizeof(baseFileName) - 1] = '\0';

    char* dot = strrchr(baseFileName, '.');
    if (dot) {
        *dot = '\0';
    }

    return baseFileName;
}

/**
 * @brief Initializes a CSV file for logging and returns the file path.
 *
 * This function creates a CSV file based on the algorithm type in the output directory.
 * If the file does not exist, it creates the file and writes the header.
 *
 * @param splitType The type of algorithm used.
 * @param outputDirectory The directory where the CSV file will be created.
 * @param csvFilePath Buffer to store the created file path.
 * @param csvFilePathSize Size of the csvFilePath buffer.
 */
void initializeCsvFile(size_t splitType, const char* outputDirectory, char* csvFilePath, size_t csvFilePathSize)
{
    const char* algorithmName = getAlgorithmName(splitType);
    snprintf(csvFilePath, csvFilePathSize, "%s%c%s_log.csv", outputDirectory, PATHSEP, algorithmName);

    if (!fileExists(csvFilePath))
    {
        FILE* csvFile;
        if (FOPEN(csvFile, csvFilePath, "w") != 0) {
            handleFileError(csvFilePath);
        }
        fprintf(csvFile, "%s\n", "ci;iteration;sse");
        fclose(csvFile);
    }
}

/**
 * @brief Writes time tracking data to a file.
 *
 * This function writes the time tracking data collected during algorithm execution
 * to a file for later analysis.
 *
 * @param outputDirectory The directory where the file will be created.
 * @param splitType The algorithm type index.
 * @param timeList An array containing the time data points.
 * @param timeIndex The number of time data points in the array.
 */
void writeTimeTrackingData(const char* outputDirectory, size_t splitType, const double* timeList, size_t timeIndex)
{
    if (timeIndex == 0 || timeList == NULL)
        return;

    const char* algorithmName = getAlgorithmName(splitType);
    
    char timesFile[PATH_MAX];
    snprintf(timesFile, sizeof(timesFile), "%s%c%s_times.txt", outputDirectory, PATHSEP, algorithmName);

    FILE* timesFilePtr;
    if (FOPEN(timesFilePtr, timesFile, "w") != 0) {
        handleFileError(timesFile);
		return; //TODO: tää on turha, koska handleFileError lopettaa ohjelman. Mutta tän avulla saa warningit pois koska timefileprt ei voikaan olla null alhaalla
    }

    for (size_t i = 0; i < timeIndex; ++i) {
        fprintf(timesFilePtr, "%.0f\n", timeList[i]);
    }

    fclose(timesFilePtr);
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
    /*if (dataPoints->size < numCentroids)
    {
        fprintf(stderr, "Error: There are less data points than the required number of clusters\n");
        exit(EXIT_FAILURE);
    }*/

    size_t* indices = malloc(sizeof(size_t) * dataPoints->size);
    handleMemoryError(indices);
    
    unsigned int randomValue;

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        indices[i] = i;
    }

    for (size_t i = 0; i < numCentroids; ++i)
    {
        RANDOMIZE(randomValue);
        size_t j = i + randomValue % (dataPoints->size - i);
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
 * @brief Generates KMeans++ centroids from the data points.
 *
 * This function selects initial centroids for KMeans clustering using the KMeans++ algorithm.
 *
 * @param numCentroids The number of centroids to generate.
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure to store the generated centroids.
 */
void generateKMeansPlusPlusCentroids(size_t numCentroids, const DataPoints* dataPoints, Centroids* centroids)
{
    // 1. Choose the first centroid at random
    unsigned int randomValue;
    RANDOMIZE(randomValue);
    size_t firstIndex = (size_t)(randomValue % dataPoints->size);
    deepCopyDataPoint(&centroids->points[0], &dataPoints->points[firstIndex]);

    // Distance cache
    double* dist2 = malloc(sizeof(double) * dataPoints->size);
    handleMemoryError(dist2);

    // Initialise the cache with distances to the first centroid
    for (size_t i = 0; i < dataPoints->size; ++i) {
        dist2[i] = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[0]);
    }

    size_t chosen = 1;

    while (chosen < numCentroids)
    {
        // 2. Compute the normalising constant distSum
        double distSum = 0.0;
        for (size_t i = 0; i < dataPoints->size; ++i)
        {
            distSum += dist2[i];
        }

        // 3. Select a new centroid with probability r
        RANDOMIZE(randomValue);
        double r = (double)randomValue / ((double)UINT_MAX + 1.0) * distSum;
        double cumulative = 0.0;
        size_t picked = dataPoints->size - 1;   /* fallback to last point */

        for (size_t i = 0; i < dataPoints->size; ++i) {
            cumulative += dist2[i];
            if (cumulative >= r) {
                picked = i;
                break;
            }
        }

        // Debug: If the chosen point is already a centroid, skip and choose again
        if (dist2[picked] == 0.0)
            continue;

        // 4. Add the selected point as the next centroid
        deepCopyDataPoint(&centroids->points[chosen++], &dataPoints->points[picked]);

        /* 5. Update the distance cache:
         *    for every data point, keep the minimum distance to any
         *    centroid selected so far.  Only values that improve
         *    (i.e. become smaller) are updated. */
        for (size_t i = 0; i < dataPoints->size; ++i) {
            double d = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[chosen - 1]);
            if (d < dist2[i])
            {
                dist2[i] = d;
            }
        }
    }

    free(dist2);
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

        /*if (cIndex >= centroids->size)
        {
            fprintf(stderr, "Error: Invalid partition index %zu for data point %zu\n", cIndex, i);
            exit(EXIT_FAILURE);
        }*/

        sse += calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[cIndex]);
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
 * @brief Calculates the sum of squared errors (SSE) for a specific cluster.
 *
 * This function computes the SSE for a specific cluster by summing the squared Euclidean distances
 * between each data point in the cluster and its assigned centroid.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param clusterLabel The label of the cluster for which to calculate the SSE.
 * @return The sum of squared errors (SSE) for the specified cluster.
 */
double calculateClusterSSE(const DataPoints* dataPoints, const Centroids* centroids, size_t clusterLabel)
{
    double sse = 0.0;
    size_t count = 0;

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterLabel)
        {
            sse += calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[clusterLabel]);
            count++;
        }
    }

    return sse;
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
    /*if (dataPoints->size == 0 || centroids->size == 0)
    {
        fprintf(stderr, "Error: Cannot perform optimal partition with empty data or centroids\n");
        exit(EXIT_FAILURE);
    }*/

	size_t nearestCentroidId = 0;

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        nearestCentroidId = findNearestCentroid(&dataPoints->points[i], centroids);
        dataPoints->points[i].partition = nearestCentroidId;
    }
}

/**
 * @brief Updates the centroids based on the assigned data points.
 *
 * This function calculates the new centroid for each cluster by averaging the attributes
 * of all data points assigned to that cluster.
 *
 * @param centroids A pointer to the Centroids structure containing the centroids to be updated.
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 */
void centroidStep(Centroids* centroids, const DataPoints* dataPoints)
{
    size_t numClusters = centroids->size;
    size_t dimensions = dataPoints->points[0].dimensions;

    // Use a single allocation for the sums
    double* sums = calloc(numClusters * dimensions, sizeof(double));
    size_t* counts = calloc(numClusters, sizeof(size_t));
    handleMemoryError(sums);
    handleMemoryError(counts);

    // Accumulate sums and counts for each cluster
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        DataPoint* point = &dataPoints->points[i];
        size_t clusterLabel = point->partition;

        for (size_t dim = 0; dim < dimensions; ++dim)
        {
            sums[clusterLabel * dimensions + dim] += point->attributes[dim];
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
                centroids->points[clusterLabel].attributes[dim] =
                    sums[clusterLabel * dimensions + dim] / counts[clusterLabel];
            }
        }
        /*else
        {
            if(LOGGING >= 3) fprintf(stderr, "Warning: Cluster %zu has no points assigned.\n", clusterLabel);
        }*/        
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
    bool* hasClosest = calloc(centroids2->size, sizeof(bool));
    handleMemoryError(hasClosest);

    for (size_t i = 0; i < centroids1->size; ++i)
    {
        size_t closestIndex = findNearestCentroid(&centroids1->points[i], centroids2);
        hasClosest[closestIndex] = true;
    }

    for (size_t i = 0; i < centroids2->size; ++i)
    {
        if (!hasClosest[i])
        {
            countFrom1to2++;
        }
    }

    free(hasClosest);

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
    /*if (LOGGING >= 2)
    {
        printf("centroids1: %zu AND 2: %zu\n\n", centroids1->size, centroids2->size);
        printf("centroids1: %f AND 2: %f\n\n", centroids1->points->attributes[0], centroids2->points->attributes[0]);
    }*/
    
    size_t countFrom1to2 = countOrphans(centroids1, centroids2);
    size_t countFrom2to1 = countOrphans(centroids2, centroids1);

    /*if (LOGGING >= 2)
    {
        printf("Count from 1 to 2: %zu AND 2 to 1: %zu\n\n", countFrom1to2, countFrom2to1);
    }*/

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
    char centroidsFileName[PATH_MAX];
    char partitionsFileName[PATH_MAX];

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
 * @param sse The mean squared error at this iteration.
 * @param splitCluster The cluster that was split at this iteration (if applicable).
 * @param outputDirectory The directory where the file should be created.
 * @param algorithmName A name identifier for the algorithm being run.
 */
void writeIterationStats(const DataPoints* dataPoints, const Centroids* centroids,
    const Centroids* groundTruth, size_t iteration, double sse,
    size_t splitCluster, const char* outputDirectory, const char* algorithmName)
{
    char statsFileName[PATH_MAX];
    snprintf(statsFileName, sizeof(statsFileName), "%s_iteration_stats.txt", algorithmName);

    char outputFilePath[PATH_MAX];
    snprintf(outputFilePath, sizeof(outputFilePath), "%s%c%s", outputDirectory, PATHSEP, statsFileName);

    // Create the file with headers if it doesn't exist
    FILE* statsFile = NULL;
    if (iteration == 0) {
        if (FOPEN(statsFile, outputFilePath, "w") != 0) {
            handleFileError(outputFilePath);
            return;
        }
        fprintf(statsFile, "Iteration;NumCentroids;SSE;CI;SplitCluster\n");
    }
    else {
        if (FOPEN(statsFile, outputFilePath, "a") != 0) {
            handleFileError(outputFilePath);
            return;
        }
    }

    size_t ci = calculateCentroidIndex(centroids, groundTruth);
    fprintf(statsFile, "%zu;%zu;%.0f;%zu;%zu\n",
        iteration, centroids->size, sse, ci, splitCluster);

    fclose(statsFile);
}

/**
 * @brief Tracks the progress of the algorithm by writing statistics and saving state.
 *
 * This function tracks the progress of the algorithm by writing statistics to a file
 * and saving the current state of centroids and partitions at each iteration.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param iteration The current iteration number.
 * @param clusterToSplit The cluster that was split at this iteration (if applicable).
 * @param splitType The type of split performed.
 * @param outputDirectory The directory where the files should be created.
 */
static void trackProgressState(const DataPoints* dataPoints, const Centroids* centroids, const Centroids* groundTruth, size_t iteration, size_t clusterToSplit, size_t splitType, const char* outputDirectory)
{
	const char* splitTypeName = getAlgorithmName(splitType); 
    double currentSse = calculateSSE(dataPoints, centroids);
    writeIterationStats(dataPoints, centroids, groundTruth, iteration, currentSse, clusterToSplit, outputDirectory, splitTypeName);
    saveIterationState(dataPoints, centroids, iteration, outputDirectory, splitTypeName);
}

/**
 * @brief Updates the time tracking data and logs it to a file.
 *
 * This function updates the time tracking data by calculating the elapsed time
 * since the start of the algorithm and appending it to a CSV file.
 *
 * @param trackTime A boolean indicating whether to track time.
 * @param start The starting clock time.
 * @param timeList An array to store the time data points.
 * @param timeIndex The index for the next time data point.
 */
static void updateTimeTracking(bool trackTime, clock_t start, double* timeList, size_t* timeIndex)
{
    clock_t iterEnd = clock();
    double iterDuration = ((double)(iterEnd - start)) / CLOCKS_PER_MS;
    timeList[(*timeIndex)++] = iterDuration;
}

/**
 * @brief Updates the CSV logging with the current iteration statistics.
 *
 * This function appends the current iteration statistics to a CSV file,
 * including the Centroid Index (CI) and sum of squared errors (SSE).
 *
 * @param createCsv A boolean indicating whether to create a CSV file.
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param csvFile The name of the CSV file to log the data.
 * @param iterationNumber The current iteration number.
 */
static void updateCsvLogging(bool createCsv, const DataPoints* dataPoints, const Centroids* centroids, const Centroids* groundTruth, const char* csvFile, size_t iterationNumber)
{
    size_t currentCi = calculateCentroidIndex(centroids, groundTruth);
    double currentSse = calculateSSE(dataPoints, centroids);
    appendLogCsv(csvFile, iterationNumber, currentCi, currentSse);
}

/**
 * @brief Handles logging and tracking of the algorithm's progress.
 *
 * This function manages the logging and tracking of the algorithm's progress,
 * including time tracking, progress tracking, and CSV logging.
 *
 * @param trackTime A boolean indicating whether to track time.
 * @param start The starting clock time.
 * @param timeList An array to store the time data points.
 * @param timeIndex The index for the next time data point.
 * @param trackProgress A boolean indicating whether to track progress.
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param iterationCount The current iteration number.
 * @param outputDirectory The directory where the files should be created.
 * @param createCsv A boolean indicating whether to create a CSV file.
 * @param csvFile The name of the CSV file to log the data.
 */
static void handleLoggingAndTracking(bool trackTime, clock_t start, double* timeList, size_t* timeIndex, bool trackProgress, const DataPoints* dataPoints,
    const Centroids* centroids, const Centroids* groundTruth, size_t iterationCount, const char* outputDirectory, bool createCsv, FILE* csvFile,
    size_t clusterToSplit, size_t splitType)
{
    if (trackTime)
    {
        updateTimeTracking(trackTime, start, timeList, timeIndex);
    }

    //TODO: tämä tehdään nyt vain iteraatiolle 0. Halutaanko tehdä esim niin monta kertaa että CI=0 tmv? Tai kommentoida kokonaan pois?
    if (trackProgress)
    {
        trackProgressState(dataPoints, centroids, groundTruth, iterationCount, clusterToSplit, splitType, outputDirectory);
    }

    if (createCsv)
    {
        updateCsvLogging(createCsv, dataPoints, centroids, groundTruth, csvFile, iterationCount);
    }
}

/**
 * @brief Repartitions data points after a centroid has been removed.
 *
 * This function updates the partition of each data point based on the nearest centroid
 * after a centroid has been removed from the clustering process.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param removed The index of the removed centroid.
 */
void localRepartitionForRS(DataPoints* dataPoints, Centroids* centroids, size_t removed)
{
    // from removed -> to existing
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == removed)
        {
            size_t nearestCentroid = findNearestCentroid(&dataPoints->points[i], centroids);

            dataPoints->points[i].partition = nearestCentroid;
        }
    }

    // from existing -> to created
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition != removed)
        {
            size_t currentPartition = dataPoints->points[i].partition;

            // Calculate distance to the current centroid
            double distanceToCurrent = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[currentPartition]);

            // Calculate distance to the removed centroid
            double distanceToRemoved = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[removed]);

            // Assign to the closer centroid
            if (distanceToRemoved < distanceToCurrent)
            {
                dataPoints->points[i].partition = removed;
            }
        }
    }

    centroidStep(centroids, dataPoints);
}

/**
 * @brief Runs the k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, calculates the SSE,
 * and returns the best SSE obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param iterations The maximum number of iterations to run the k-means algorithm.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @return The best mean squared error (SSE) obtained during the iterations.
 */
double runKMeans(DataPoints* dataPoints, size_t iterations, Centroids* centroids, const Centroids* groundTruth)
{
    double bestSse = DBL_MAX;
    double sse = DBL_MAX;

    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        partitionStep(dataPoints, centroids);

        centroidStep(centroids, dataPoints);
        
        sse = calculateSSE(dataPoints, centroids);

        /*if (LOGGING >= 3)
        {
			size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
            printf("(runKMeans)After iteration %zu: CI = %zu and SSE = %.0f\n", iteration + 1, centroidIndex, sse);
        }*/

        if (sse < bestSse)
        {
			//if (LOGGING >= 3) printf("Best SSE so far: %.5f\n", sse);
            bestSse = sse;
        }
        else
        {
            break; // Exit the loop if the SSE does not improve
        }
    }

    return bestSse;
}

/**
 * @brief Runs the k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, calculates the SSE,
 * and returns the best SSE obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param iterations The maximum number of iterations to run the k-means algorithm.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @return The best mean squared error (SSE) obtained during the iterations.
 */
double runKMeansWithTracking(DataPoints* dataPoints, size_t iterations, Centroids* centroids, const Centroids* groundTruth, const char* outputDirectory,
    bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv, size_t* iterationCount, bool firstRun, FILE* csvFile)
{
    double bestSse = DBL_MAX;
    double sse = DBL_MAX;

    for (size_t iteration = 0; iteration < iterations; ++iteration)
    {
        partitionStep(dataPoints, centroids);

        centroidStep(centroids, dataPoints);

        if (firstRun) {
            handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
                dataPoints, centroids, groundTruth, *iterationCount, outputDirectory, createCsv, csvFile, SIZE_MAX, 7);
        }

        sse = calculateSSE(dataPoints, centroids);

        /*if (LOGGING >= 3)
        {
            size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
            printf("(runKMeans)After iteration %zu: CI = %zu and SSE = %.0f\n", iteration + 1, centroidIndex, sse);
        }*/

        if (sse < bestSse)
        {
            //if (LOGGING >= 3) printf("Best SSE so far: %.5f\n", sse);
            bestSse = sse;
        }
        else
        {
            break; // Exit the loop if the SSE does not improve
        }

		(*iterationCount)++;
    }

    return bestSse;
}

/**
 * @brief Performs random swaps of centroids and evaluates the resulting clustering using k-means.
 *
 * This function performs random swaps of centroids with data points, runs k-means on the modified centroids,
 * and keeps the changes if the mean squared error (SSE) improves. If the SSE does not improve, it reverses the swap.
 * The function returns the best mean squared error (SSE) obtained during the swaps.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param maxSwaps The maximum number of attempted swaps.
 * @return The best mean squared error (SSE) obtained during the swaps.
 */
double randomSwap(DataPoints* dataPoints, Centroids* centroids, size_t maxSwaps, const Centroids* groundTruth, const char* outputDirectory,
    bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv)
{
    double bestSse = DBL_MAX;
    size_t kMeansIterations = 2;
    size_t numCentroids = centroids->size;
    size_t dimensions = centroids->points[0].dimensions;

    size_t totalAttributes = numCentroids * dimensions;

    double* backupAttributes = malloc(totalAttributes * sizeof(double));
    handleMemoryError(backupAttributes);

    double* backupPartitions = malloc(dataPoints->size * sizeof(size_t));
    handleMemoryError(backupPartitions);

    char csvFile[PATH_MAX];
    if (createCsv)
    {
        initializeCsvFile(3, outputDirectory, csvFile, sizeof(csvFile));
    }

    handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
        dataPoints, centroids, groundTruth, 0, outputDirectory, createCsv, csvFile, SIZE_MAX, 3);

    size_t iterationCount = 1; //Helper for tracking progress

    for (size_t i = 0; i < maxSwaps; ++i)
    {
        printf("Swap %zu\n", i + 1);

        //Backup
        size_t offset = 0;
        for (size_t j = 0; j < numCentroids; ++j)
        {
            memcpy(&backupAttributes[offset], centroids->points[j].attributes, dimensions * sizeof(double));
            offset += dimensions;
        }


        for (size_t j = 0; j < dataPoints->size; ++j)
         {
            backupPartitions[j] = dataPoints->points[j].partition;
        }

        //Swap
        unsigned int randomValue;
        
        RANDOMIZE(randomValue);
        size_t randomCentroidId = randomValue % centroids->size;

        RANDOMIZE(randomValue);
        size_t randomDataPointId = randomValue % dataPoints->size;

        memcpy(centroids->points[randomCentroidId].attributes, dataPoints->points[randomDataPointId].attributes, dimensions * sizeof(double));
        
		localRepartitionForRS(dataPoints, centroids, randomCentroidId);

        //K-means
        double resultSse = runKMeans(dataPoints, kMeansIterations, centroids, groundTruth);

        //If 1) SSE improves, we keep the change
        //if not, 2) we reverse the swap
        if (resultSse < bestSse)
        {
            /*if (LOGGING >= 3)
            {
				size_t centroidIndex = calculateCentroidIndex(centroids, groundTruth);
                printf("(RS) Round %zu: Best CI: %zu and Best SSE: %.5f\n", i + 1, centroidIndex, resultSse);
            }*/
            
            bestSse = resultSse;

            handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
                dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, SIZE_MAX, 3);
        }
        else
        {
            offset = 0;
            for (size_t j = 0; j < numCentroids; ++j)
            {
                memcpy(centroids->points[j].attributes, &backupAttributes[offset], dimensions * sizeof(double));
                offset += dimensions;
            }

            for (size_t j = 0; j < dataPoints->size; ++j)
            {
                dataPoints->points[j].partition = backupPartitions[j];
            }
        }

        iterationCount++;
    }

    free(backupAttributes);

    return bestSse;
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
    unsigned int randomValue;
    RANDOMIZE(randomValue);
    size_t c1 = randomValue % clusterSize;
    size_t c2 = c1;
    while (c2 == c1)
    {
        RANDOMIZE(randomValue);
        c2 = randomValue % clusterSize;
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
    runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, groundTruth);

    // Update partitions
    for (size_t i = 0; i < clusterSize; ++i)
    {
        size_t originalIndex = clusterIndices[i];
        dataPoints->points[originalIndex].partition = (pointsInCluster.points[i].partition == 0) ? clusterToSplit : centroids->size;
    }

    // Update centroids
    //#1
    deepCopyDataPoint(&centroids->points[clusterToSplit], &localCentroids.points[0]);
    
    //#2
    centroids->size++;
    centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint)); //TODO: periaatteessa aina tiedetään lopullinen koko, niin pitäisikö reallocoinnit poistaa ja lisätä jonnekin aikaisemmin se oikea koko näille
    handleMemoryError(centroids->points);
	centroids->points[centroids->size - 1] = allocateDataPoint(dataPoints->points[0].dimensions);
    deepCopyDataPoint(&centroids->points[centroids->size - 1], &localCentroids.points[1]);
    

    // Cleanup
    free(clusterIndices);
    free(pointsInCluster.points);
	freeCentroids(&localCentroids);
}

double splitClusterGlobalV2(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t localMaxIterations, const Centroids* groundTruth)
{
    size_t clusterSize = 0;
    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterToSplit)
        {
            clusterSize++;
        }
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
    unsigned int randomValue;
    RANDOMIZE(randomValue);
    size_t c1 = randomValue % clusterSize;
    size_t c2 = c1;
    while (c2 == c1)
    {
        RANDOMIZE(randomValue);
        c2 = randomValue % clusterSize;
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
    runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, groundTruth);

    // Update partitions
    for (size_t i = 0; i < clusterSize; ++i)
    {
        size_t originalIndex = clusterIndices[i];
        dataPoints->points[originalIndex].partition = (pointsInCluster.points[i].partition == 0) ? clusterToSplit : centroids->size;
    }

    // Update centroids
    //#1
    deepCopyDataPoint(&centroids->points[clusterToSplit], &localCentroids.points[0]);

    //#2
    centroids->size++;
    centroids->points = realloc(centroids->points, centroids->size * sizeof(DataPoint)); //TODO: periaatteessa aina tiedetään lopullinen koko, niin pitäisikö reallocoinnit poistaa ja lisätä jonnekin aikaisemmin se oikea koko näille
    handleMemoryError(centroids->points);
    centroids->points[centroids->size - 1] = allocateDataPoint(dataPoints->points[0].dimensions);
    deepCopyDataPoint(&centroids->points[centroids->size - 1], &localCentroids.points[1]);

	double sse = runKMeans(dataPoints, localMaxIterations, centroids, groundTruth);


    // Cleanup
    free(clusterIndices);
    free(pointsInCluster.points);
    freeCentroids(&localCentroids);

    return sse;
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
double splitClusterGlobal(DataPoints* dataPoints, Centroids* centroids, size_t clusterToSplit, size_t globalMaxIterations, const Centroids* groundTruth)
{
    size_t clusterSize = 0;

    for (size_t i = 0; i < dataPoints->size; ++i)
    {
        if (dataPoints->points[i].partition == clusterToSplit)
        {
            clusterSize++;
        }
    }

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
    unsigned int randomValue;
    RANDOMIZE(randomValue);
    size_t c1 = randomValue % clusterSize;
    size_t c2 = c1;
    while (c2 == c1)
    {
        RANDOMIZE(randomValue);
        c2 = randomValue % clusterSize;
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
    double resultSse = runKMeans(dataPoints, globalMaxIterations, centroids, groundTruth);

    // Cleanup
    free(clusterIndices);

    return resultSse;
}

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

    // TODO: Selvitä, että tarvitaanko tätä? Oma oletus on, että ei tarvita
    // new clusters -> old clusters
    /*for (size_t i = 0; i < dataPoints->size; ++i)
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

		// Calculate distance to the current centroid
		double currentDistance = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[currentCluster]);

        // Calculate distance to the cluster to be split
        double distanceToSplit = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[clusterToSplit]);

        // Calculate distance to the new cluster
        double distanceToNew = calculateSquaredEuclideanDistance(&dataPoints->points[i], &centroids->points[newClusterIndex]);

        if (distanceToSplit < currentDistance || distanceToNew < currentDistance)
        {
            clustersAffected[currentCluster] = true;    // Mark the old cluster as affected
            dataPoints->points[i].partition = distanceToSplit <= distanceToNew ? clusterToSplit : newClusterIndex;
        }
    }
}

/**
 * @brief Calculates the SSE drop for a tentative split of a cluster.
 *
 * This function calculates the SSE drop by running local k-means on a cluster and comparing
 * the new SSE with the original SSE. It returns the difference between the original SSE and the new SSE.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param clusterLabel The label of the cluster to split.
 * @param localMaxIterations The maximum number of iterations for the local k-means.
 * @param originalClusterSSE The original SSE of the cluster before the split.
 * @return The SSE drop for the tentative split of the cluster.
 */
double tentativeSseDrop(DataPoints* dataPoints, size_t clusterLabel, size_t localMaxIterations, double originalClusterSSE)
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
        return 0.0;
    }

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
    unsigned int randomValue;
    RANDOMIZE(randomValue);
    size_t idx1 = randomValue % clusterSize;
    size_t idx2 = idx1;
    while (idx2 == idx1)
    {
        RANDOMIZE(randomValue);
        idx2 = randomValue % clusterSize;
    }

    Centroids localCentroids = allocateCentroids(2,dataPoints->points[0].dimensions);

    deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[idx1]);
    deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[idx2]);

    // k-means
    double resultSse = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, NULL);

    double sseDrop = originalClusterSSE - resultSse;

    freeDataPoints(&pointsInCluster);
    freeCentroids(&localCentroids);

    return sseDrop;
}

/**
 * @brief Calculates the SSE drop for a tentative split of a cluster.
 *
 * This function calculates the SSE drop by running local k-means on a cluster and comparing
 * the new SSE with the original SSE. It returns the difference between the original SSE and the new SSE.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param clusterLabel The label of the cluster to split.
 * @param localMaxIterations The maximum number of iterations for the local k-means.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @return A ClusteringResult structure containing the new centroids and the SSE drop.
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

    /*if (clusterSize < 2)
    {
        printf("Not enough points to split the cluster\n");
        exit(EXIT_FAILURE);
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
    unsigned int randomValue;
    RANDOMIZE(randomValue);
    size_t idx1 = randomValue % clusterSize;
    size_t idx2 = idx1;
    while (idx2 == idx1)
    {
        RANDOMIZE(randomValue);
        idx2 = randomValue % clusterSize;
    }

    Centroids localCentroids = allocateCentroids(2, dataPoints->points[0].dimensions);
    deepCopyDataPoint(&localCentroids.points[0], &pointsInCluster.points[idx1]);
    deepCopyDataPoint(&localCentroids.points[1], &pointsInCluster.points[idx2]);

    ClusteringResult localResult = allocateClusteringResult(dataPoints->size, 2, dataPoints->points[0].dimensions);

    // k-means
    localResult.sse = runKMeans(&pointsInCluster, localMaxIterations, &localCentroids, groundTruth);

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
 * It returns the best mean squared error (SSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param maxCentroids The maximum number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @return The best mean squared error (SSE) obtained during the iterations.
 */
double runRandomSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, const Centroids* groundTruth, const char* outputDirectory,
    bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv)
{
    char csvFile[PATH_MAX];
    if (createCsv)
    {
        initializeCsvFile(5, outputDirectory, csvFile, sizeof(csvFile));
    }

    handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
        dataPoints, centroids, groundTruth, 0, outputDirectory, createCsv, csvFile, SIZE_MAX, 5);

    size_t iterationCount = 1; //Helper for tracking progress

    unsigned int randomValue;

    while(centroids->size < maxCentroids)
    {
        RANDOMIZE(randomValue);
        size_t clusterToSplit = randomValue % centroids->size;

        splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, groundTruth);

        /*if (LOGGING >= 3)
        {
            size_t ci = calculateCentroidIndex(centroids, groundTruth);
            double sse = SSE(dataPoints, centroids);
            printf("(RandomSplit) Number of centroids: %zu, CI: %zu, and SSE: %.0f \n", centroids->size, ci, sse);
        }*/

        handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
            dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, clusterToSplit, 5);

		iterationCount++;
    }	
 
    double finalResultSse = runKMeans(dataPoints, maxIterations, centroids, groundTruth);

    handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
        dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, SIZE_MAX, 5);

    return finalResultSse;
}

/**
 * @brief Runs the split k-means algorithm with tentative splitting (choosing to split the one that reduces the SSE the most).
 *
 * This function iterates through partition and centroid steps, selects a cluster to split based on the SSE drop,
 * and updates the centroids and partitions based on the results of the local k-means.
 * It returns the best mean squared error (SSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param maxCentroids The maximum number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param splitType The type of split to perform (0 = intra-cluster, 1 = global, 2 = local repartition).
 * @return The best mean squared error (SSE) obtained during the iterations.
 */
double runSseSplit(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, const Centroids* groundTruth, size_t splitType, const char* outputDirectory, 
    bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv)
{
    double finalResultSse = DBL_MAX;

	double* clusterSSEs = malloc(maxCentroids * sizeof(double));
    handleMemoryError(clusterSSEs);

    double* SseDrops = calloc(maxCentroids, sizeof(double));
    handleMemoryError(SseDrops);

    bool* clustersAffected = calloc(maxCentroids, sizeof(bool));
    handleMemoryError(clustersAffected);

    char csvFile[PATH_MAX];
    if (createCsv)
    {
        initializeCsvFile(splitType, outputDirectory, csvFile, sizeof(csvFile));
    }

    handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
        dataPoints, centroids, groundTruth, 0, outputDirectory, createCsv, csvFile, SIZE_MAX, splitType);

    //Only 1 cluster, so no need for decision making
    size_t initialClusterToSplit = 0;
    splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, maxIterations, groundTruth);

    handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
        dataPoints, centroids, groundTruth, 1, outputDirectory, createCsv, csvFile, initialClusterToSplit, splitType);

    for (size_t i = 0; i < centroids->size; ++i)
    {
        clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
        SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i]);
    }

    size_t iterationCount = 2; //Helper for tracking progress

    while (centroids->size < maxCentroids)
    {
		// Choose the cluster that reduces the SSE the most
        size_t clusterToSplit = 0;
        double maxSseDrop = SseDrops[0];

        for (size_t i = 1; i < centroids->size; ++i)
        {
            if (SseDrops[i] > maxSseDrop)
            {
                maxSseDrop = SseDrops[i];
                clusterToSplit = i;
            }
        }

        if(splitType == 0) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, groundTruth);
		else if (splitType == 1) finalResultSse = splitClusterGlobalV2(dataPoints, centroids, clusterToSplit, maxIterations, groundTruth);
		else if (splitType == 2) splitClusterIntraCluster(dataPoints, centroids, clusterToSplit, maxIterations, groundTruth);

        if(centroids->size < maxCentroids){
	        if (splitType == 0) // Intra-cluster
            {
                // Recalculate SSE for the affected clusters
                clusterSSEs[clusterToSplit] = calculateClusterSSE(dataPoints, centroids, clusterToSplit);
                clusterSSEs[centroids->size - 1] = calculateClusterSSE(dataPoints, centroids, centroids->size - 1);

                // Update SseDrops for the affected clusters
                SseDrops[clusterToSplit] = tentativeSseDrop(dataPoints, clusterToSplit, maxIterations, clusterSSEs[clusterToSplit]);
                SseDrops[centroids->size - 1] = tentativeSseDrop(dataPoints, centroids->size - 1, maxIterations, clusterSSEs[centroids->size - 1]);
            }
	        else if (splitType == 1) // Global
            {            
                for (size_t i = 0; i < centroids->size; ++i)
                {
                    clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
                    SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i]);
                }
            }
	        else if (splitType == 2) // Local repartition
            {            
                localRepartition(dataPoints, centroids, clusterToSplit, clustersAffected);
            
		        clustersAffected[clusterToSplit] = true;
		        clustersAffected[centroids->size - 1] = true;

		        // Recalculate SseDrop for affected clusters (old and new)
                for (size_t i = 0; i < centroids->size; ++i)
                {
                    if (clustersAffected[i])
                    {
                        clusterSSEs[i] = calculateClusterSSE(dataPoints, centroids, i);
                        SseDrops[i] = tentativeSseDrop(dataPoints, i, maxIterations, clusterSSEs[i]);
                    }
                }

                memset(clustersAffected, 0, (maxCentroids) * sizeof(bool));
            }
        }
        handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
            dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, clusterToSplit, splitType);

        iterationCount++;
        
        /*if (LOGGING >= 3 && splitType == 2)
        {
            size_t ci = calculateCentroidIndex(centroids, groundTruth);
            double sse = calculateSSE(dataPoints, centroids);
            printf("(SseSplit) Number of centroids: %zu, CI: %zu, and SSE: %.0f \n", centroids->size, ci, sse);
        }*/
    }

    free(clusterSSEs);
	free(SseDrops);
	free(clustersAffected);
 
    if (splitType != 1) {
        finalResultSse = runKMeans(dataPoints, maxIterations, centroids, groundTruth);
        handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
            dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, SIZE_MAX, splitType);
    }

    return finalResultSse;
}

/**
 * @brief Runs the Bisecting k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, selects a cluster to split based on the SSE,
 * and updates the centroids and partitions based on the results of the local k-means.
 * It returns the best mean squared error (SSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param centroids A pointer to the Centroids structure containing the centroids.
 * @param maxCentroids The maximum number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @return The best mean squared error (SSE) obtained during the iterations.
 */
double runBisectingKMeans(DataPoints* dataPoints, Centroids* centroids, size_t maxCentroids, size_t maxIterations, const Centroids* groundTruth, const char* outputDirectory,
    bool trackProgress, double* timeList, size_t* timeIndex, clock_t start, bool trackTime, bool createCsv, size_t bisectingIterations)
{
    double* SseList = malloc(maxCentroids * sizeof(double));
    handleMemoryError(SseList);
    double bestSse = DBL_MAX;

    DataPoint newCentroid1 = allocateDataPoint(dataPoints->points[0].dimensions);
    DataPoint newCentroid2 = allocateDataPoint(dataPoints->points[0].dimensions);

    char csvFile[PATH_MAX];
    if (createCsv)
    {
        initializeCsvFile(4, outputDirectory, csvFile, sizeof(csvFile));
    }

    handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
        dataPoints, centroids, groundTruth, 0, outputDirectory, createCsv, csvFile, SIZE_MAX, 4);

	//Step 0: Only 1 cluster, so no need for decision making
    size_t initialClusterToSplit = 0;
    splitClusterIntraCluster(dataPoints, centroids, initialClusterToSplit, maxIterations, groundTruth);

	SseList[0] = calculateClusterSSE(dataPoints, centroids, 0);
    SseList[1] = calculateClusterSSE(dataPoints, centroids, 1);

    handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
        dataPoints, centroids, groundTruth, 1, outputDirectory, createCsv, csvFile, initialClusterToSplit, 4);

    size_t iterationCount = 2; //Helper for tracking progress

    //Repeat until we have K clusters
    for (size_t i = 2; centroids->size < maxCentroids; ++i)
    {
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

            //if (LOGGING >= 3) printf("(RKM) Round %d: Latest CI: %zu and Latest SSE: %.0f\n", repeat, result1.centroidIndex, result1.sse);

			// If the result is better than the best result so far, update the best result
            if (curr.sse < bestSse)
            {
                bestSse = curr.sse;
                
                // Save the two new centroids
				deepCopyDataPoint(&newCentroid1, &curr.centroids[0]);
                deepCopyDataPoint(&newCentroid2, &curr.centroids[1]);
            }

            freeClusteringResult(&curr, 2);
        }

		// Replace the old centroid with the new centroid1
        deepCopyDataPoint(&centroids->points[clusterToSplit], &newCentroid1);

		// Increase the size of the centroids array and add the new centroid2
        centroids->points = realloc(centroids->points, (centroids->size + 1) * sizeof(DataPoint));
        handleMemoryError(centroids->points);
        centroids->points[centroids->size] = allocateDataPoint(newCentroid2.dimensions);
        deepCopyDataPoint(&centroids->points[centroids->size], &newCentroid2);
        centroids->size++;

		partitionStep(dataPoints, centroids); //partition step, vai pitäisikö tallentaa ne loopissa ja tehdä tässä sitten muutokset? (esim kaikki 0->clusterToSplit, 1->centroids->size-1)

		//Step 3: Update the SSE list
        SseList[clusterToSplit] = calculateClusterSSE(dataPoints, centroids, clusterToSplit);
        SseList[centroids->size - 1] = calculateClusterSSE(dataPoints, centroids, centroids->size - 1);
        
        bestSse = DBL_MAX;

        handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
            dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, clusterToSplit, 4);

        iterationCount++;
    }

	//Step 4: Run the final k-means
    double finalResultSse = runKMeans(dataPoints, maxIterations, centroids, groundTruth); //TODO: Pitäisikö poistaa? Final K-means ei taida olla algoritmia

    handleLoggingAndTracking(trackTime, start, timeList, timeIndex, trackProgress,
        dataPoints, centroids, groundTruth, iterationCount, outputDirectory, createCsv, csvFile, SIZE_MAX, 4);
    
    // Cleanup
    free(SseList);
	freeDataPoint(&newCentroid1);
	freeDataPoint(&newCentroid2);

    return finalResultSse;
}

/**
 * @brief Runs the k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, calculates the SSE,
 * and returns the best SSE obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the SSE values.
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
		printf("Round %zu\n", i + 1);

        Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

        start = clock();

        //generateRandomCentroids(numCentroids, dataPoints, &centroids);
		generateKMeansPlusPlusCentroids(numCentroids, dataPoints, &centroids);

        double resultSse = runKMeans(dataPoints, maxIterations, &centroids, groundTruth);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_MS;

        size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

        stats.sseSum += resultSse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;

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

    printStatistics("K-means", stats, loopCount, numCentroids, scaling, dataPoints->size);
    writeResultsToFile(fileName, stats, numCentroids, "K-means", loopCount, scaling, outputDirectory, dataPoints->size);
}

/**
 * @brief Runs the repeated k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, calculates the SSE,
 * and returns the best SSE obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param maxRepeats The maximum number of repeats for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the SSE values.
 * @param fileName The name of the file to write the results to.
 * @param outputDirectory The directory where the results file is located.
 */
void runRepeatedKMeansAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t maxRepeats, size_t loopCount, size_t scaling, 
    const char* fileName, const char* outputDirectory, bool trackProgress, bool trackTime)
{
    Statistics stats;
    initializeStatistics(&stats);

    clock_t start, end;
    double duration;

    size_t totalIterations = loopCount * maxRepeats * 5 + 100; //TODO: mietin parempi limit kuin *5
    double* timeList = malloc(totalIterations * sizeof(double)); //TODO: vois varmaan olla myös size_t koska millisekunteja
    handleMemoryError(timeList);
    size_t timeIndex = 0;

    char csvFile[PATH_MAX];
    if (trackProgress)
    {
        initializeCsvFile(7, outputDirectory, csvFile, sizeof(csvFile));

    }

    printf("Repeated K-means\n");

    for (size_t i = 0; i < loopCount; ++i)
    {
		printf("Round %zu\n", i + 1);

		double bestSse = DBL_MAX;
		size_t bestCI = SIZE_MAX;
        
        size_t iterationCount = 0; //Helper for tracking progress
        bool firstRun = true;

        Centroids bestCentroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

        start = clock();

        for (size_t j = 0; j < maxRepeats; ++j)
        {
			printf("Repeat %zu\n", j + 1);
            Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);
            generateRandomCentroids(numCentroids, dataPoints, &centroids);
			//generateKMeansPlusPlusCentroids(numCentroids, dataPoints, &centroids);
            double resultSse = runKMeansWithTracking(dataPoints, maxIterations, &centroids, groundTruth, outputDirectory, (i == 0 && trackProgress), timeList, &timeIndex, start, trackTime, trackProgress, &iterationCount, firstRun, csvFile);

            if (resultSse < bestSse)
            {
                bestSse = resultSse;
				deepCopyCentroids(&centroids, &bestCentroids, numCentroids);

                if(!firstRun)
                {
                    size_t currentCi = calculateCentroidIndex(&centroids, groundTruth);
                    bestCI = currentCi;

                    appendLogCsv(csvFile, iterationCount, currentCi, resultSse);
                    updateTimeTracking(trackTime, start, timeList, &timeIndex);
                }
                else
                {
					firstRun = false;
                }
            }
            else {
                appendLogCsv(csvFile, iterationCount, bestCI, bestSse);
                updateTimeTracking(trackTime, start, timeList, &timeIndex);
            }

            freeCentroids(&centroids);
        }

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_MS;

        size_t centroidIndex = calculateCentroidIndex(&bestCentroids, groundTruth);

        stats.sseSum += bestSse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;

        if (i == 0)
        {
            writeCentroidsToFile("repeatedKMeans_centroids.txt", &bestCentroids, outputDirectory);
            writeDataPointPartitionsToFile("repeatedKMeans_partitions.txt", dataPoints, outputDirectory);
        }

        freeCentroids(&bestCentroids);
    }

    printStatistics("Repeated K-means", stats, loopCount, numCentroids, scaling, dataPoints->size);
    writeResultsToFile(fileName, stats, numCentroids, "Repeated K-means", loopCount, scaling, outputDirectory, dataPoints->size);

    if (trackTime)
    {
        writeTimeTrackingData(outputDirectory, 7, timeList, timeIndex);
    }

    free(timeList);
}

/**
 * @brief Runs the repeated k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, calculates the SSE,
 * and returns the best SSE obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param maxRepeats The maximum number of repeats for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the SSE values.
 * @param fileName The name of the file to write the results to.
 * @param outputDirectory The directory where the results file is located.
 */
void runRandomSwapAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxSwaps, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, bool trackProgress, bool trackTime)
{
    Statistics stats;
    initializeStatistics(&stats);

    clock_t start, end;
    double duration;

    //Tracker helpers
    size_t totalIterations = loopCount * maxSwaps + loopCount;
    double* timeList = malloc(totalIterations * sizeof(double));
    handleMemoryError(timeList);
    size_t timeIndex = 0;

    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    printf("Random swap\n");

    for (size_t i = 0; i < loopCount; ++i)
    {
        printf("Round %zu\n", i + 1);

        Centroids centroids = allocateCentroids(numCentroids, dataPoints->points[0].dimensions);

        start = clock();

        generateRandomCentroids(numCentroids, dataPoints, &centroids);
		//generateKMeansPlusPlusCentroids(numCentroids, dataPoints, &centroids);
        if (trackProgress) partitionStep(dataPoints, &centroids);

        double resultSse = randomSwap(dataPoints, &centroids, maxSwaps, groundTruth, outputDirectory, (i == 0 && trackProgress), timeList, &timeIndex, start, trackTime, trackProgress);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_MS;

        size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

        stats.sseSum += resultSse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;

        if (centroidIndex != 0 && savedNonZeroResults == false)
        {
            char centroidsFile[PATH_MAX];
            char partitionsFile[PATH_MAX];
            snprintf(centroidsFile, sizeof(centroidsFile), "RandomSwap_centroids_failed.txt");
            snprintf(partitionsFile, sizeof(partitionsFile), "RandomSwap_partitions_failed.txt");

            writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
            writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
            savedNonZeroResults = true;
        }
        else if (centroidIndex == 0 && savedZeroResults == false)
        {
            char centroidsFile[PATH_MAX];
            char partitionsFile[PATH_MAX];
            snprintf(centroidsFile, sizeof(centroidsFile), "RandomSwap_centroids_perfect.txt");
            snprintf(partitionsFile, sizeof(partitionsFile), "RandomSwap_partitions_perfect.txt");

            writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
            writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
            savedZeroResults = true;
        }

        freeCentroids(&centroids);
    }

    printStatistics("Random Swap", stats, loopCount, numCentroids, scaling, dataPoints->size);
    writeResultsToFile(fileName, stats, numCentroids, "Random swap", loopCount, scaling, outputDirectory, dataPoints->size);

    if (trackTime) 
    {
        writeTimeTrackingData(outputDirectory, 3, timeList, timeIndex);
    }

    free(timeList);
}

/**
 * @brief Runs the split k-means algorithm with random splitting.
 *
 * This function iterates through partition and centroid steps, randomly selects a cluster to split,
 * and updates the centroids and partitions based on the results of the local k-means.
 * It returns the best mean squared error (SSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the SSE values.
 * @param fileName The name of the file to write the results to.
 * @param outputDirectory The directory where the results file is located.
 */
void runRandomSplitAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, bool trackProgress, bool trackTime)
{
    Statistics stats;
    initializeStatistics(&stats);

    clock_t start, end;
    double duration;

    //Tracker helpers
	size_t failSafety = 0.5 * loopCount; //TODO: ei käytössä // Note: It may randomly choose a cluster with just 1 data point -> No split. I started with 0.1 multiplier, but 0.5 seems to be a good balance
    size_t totalIterations = loopCount * numCentroids * 2 + loopCount;
    double* timeList = malloc(totalIterations * sizeof(double));
    handleMemoryError(timeList);
    size_t timeIndex = 0;

    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    printf("Random Split k-means\n");

    for (size_t i = 0; i < loopCount; ++i)
    {
		printf("Round %zu\n", i + 1);

		resetPartitions(dataPoints);

        Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

        start = clock();

        generateRandomCentroids(centroids.size, dataPoints, &centroids);

        double resultSse = runRandomSplit(dataPoints, &centroids, numCentroids, maxIterations, groundTruth, outputDirectory, (i == 0 && trackProgress), timeList, &timeIndex, start, trackTime, trackProgress);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_MS;

        size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

        stats.sseSum += resultSse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;

        if (centroidIndex != 0 && savedNonZeroResults == false)
        {
            char centroidsFile[PATH_MAX];
            char partitionsFile[PATH_MAX];
            snprintf(centroidsFile, sizeof(centroidsFile), "RandomSplit_centroids_failed.txt");
            snprintf(partitionsFile, sizeof(partitionsFile), "RandomSplit_partitions_failed.txt");

            writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
            writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
            savedNonZeroResults = true;
        }
        else if (centroidIndex == 0 && savedZeroResults == false)
        {
            char centroidsFile[PATH_MAX];
            char partitionsFile[PATH_MAX];
            snprintf(centroidsFile, sizeof(centroidsFile), "RandomSplit_centroids_perfect.txt");
            snprintf(partitionsFile, sizeof(partitionsFile), "RandomSplit_partitions_perfect.txt");

            writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
            writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
            savedZeroResults = true;
        }

        freeCentroids(&centroids);
    }

    printStatistics("Random Split", stats, loopCount, numCentroids, scaling, dataPoints->size);
    writeResultsToFile(fileName, stats, numCentroids, "Random Split", loopCount, scaling, outputDirectory, dataPoints->size);
    
    if (trackTime) 
    {
        writeTimeTrackingData(outputDirectory, 5, timeList, timeIndex);
    }

    free(timeList);
}

/**
 * @brief Runs the split k-means algorithm with tentative splitting (choosing to split the one that reduces the SSE the most).
 *
 * This function iterates through partition and centroid steps, selects a cluster to split based on the SSE drop,
 * and updates the centroids and partitions based on the results of the local k-means.
 * It returns the best mean squared error (SSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the SSE values.
 * @param fileName The name of the file to write the results to.
 * @param outputDirectory The directory where the results file is located.
 * @param splitType The type of split to perform (0 = intra-cluster, 1 = global, 2 = local repartition).
 */
void runSseSplitAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, size_t splitType, bool trackProgress, bool trackTime)
{
    Statistics stats;
    initializeStatistics(&stats);

    clock_t start, end;
    double duration;

    const char* splitTypeName = getAlgorithmName(splitType);

    //Tracker helpers
    size_t totalIterations = loopCount * numCentroids + loopCount;
    double* timeList = malloc(totalIterations * sizeof(double));
    handleMemoryError(timeList);
    size_t timeIndex = 0;

    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    printf("%s\n", splitTypeName);

    for (size_t i = 0; i < loopCount; ++i)
    {
		printf("Round %zu\n", i + 1);

        resetPartitions(dataPoints);

        Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

        start = clock();

        generateRandomCentroids(centroids.size, dataPoints, &centroids);

        double resultSse = runSseSplit(dataPoints, &centroids, numCentroids, maxIterations, groundTruth, splitType, outputDirectory, (i == 0 && trackProgress), timeList, &timeIndex, start, trackTime, trackProgress);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_MS;

        size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

        stats.sseSum += resultSse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;

        if (centroidIndex != 0 && savedNonZeroResults == false)
        {
            char centroidsFile[PATH_MAX];
            char partitionsFile[PATH_MAX];
            snprintf(centroidsFile, sizeof(centroidsFile), "%s_centroids_failed.txt", splitTypeName);
            snprintf(partitionsFile, sizeof(partitionsFile), "%s_partitions_failed.txt", splitTypeName);
            
            writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
            writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
            savedNonZeroResults = true;
        }
        else if (centroidIndex == 0 && savedZeroResults == false)
        {
            char centroidsFile[PATH_MAX];
            char partitionsFile[PATH_MAX];
            snprintf(centroidsFile, sizeof(centroidsFile), "%s_centroids_perfect.txt", splitTypeName);
            snprintf(partitionsFile, sizeof(partitionsFile), "%s_partitions_perfect.txt", splitTypeName);

            writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
            writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
            savedZeroResults = true;
        }

        freeCentroids(&centroids);
    }

    printStatistics(splitTypeName, stats, loopCount, numCentroids, scaling, dataPoints->size);
    writeResultsToFile(fileName, stats, numCentroids, splitTypeName, loopCount, scaling, outputDirectory, dataPoints->size);

    if (trackTime)
    {
        writeTimeTrackingData(outputDirectory, splitType, timeList, timeIndex);
    }

    free(timeList);
}

/**
 * @brief Runs the Bisecting k-means algorithm on the given data points and centroids.
 *
 * This function iterates through partition and centroid steps, selects a cluster to split based on the SSE,
 * and updates the centroids and partitions based on the results of the local k-means.
 * It returns the best mean squared error (SSE) obtained during the iterations.
 *
 * @param dataPoints A pointer to the DataPoints structure containing the data points.
 * @param groundTruth A pointer to the Centroids structure containing the ground truth centroids.
 * @param numCentroids The number of centroids to generate.
 * @param maxIterations The maximum number of iterations for the k-means algorithm.
 * @param loopCount The number of loops to run the k-means algorithm.
 * @param scaling A scaling factor for the SSE values.
 * @param fileName The name of the file to write the results to.
 * @param outputDirectory The directory where the results file is located.
 */
void runBisectingKMeansAlgorithm(DataPoints* dataPoints, const Centroids* groundTruth, size_t numCentroids, size_t maxIterations, size_t loopCount, size_t scaling, const char* fileName, const char* outputDirectory, bool trackProgress, bool trackTime, size_t bisectingIterations)
{
    Statistics stats;
    initializeStatistics(&stats);

    clock_t start, end;
    double duration;

    //Tracker helpers
    size_t totalIterations = loopCount * numCentroids + loopCount;
    double* timeList = malloc(totalIterations * sizeof(double));
    handleMemoryError(timeList);
    size_t timeIndex = 0;

    bool savedZeroResults = false;
    bool savedNonZeroResults = false;

    printf("Bisecting k-means\n");

    for (size_t i = 0; i < loopCount; ++i)
    {
		printf("Round %zu\n", i + 1);

        resetPartitions(dataPoints);

        Centroids centroids = allocateCentroids(1, dataPoints->points[0].dimensions);

        start = clock();

        generateRandomCentroids(centroids.size, dataPoints, &centroids);

        double resultSse = runBisectingKMeans(dataPoints, &centroids, numCentroids, maxIterations, groundTruth, outputDirectory, (i == 0 && trackProgress), timeList, &timeIndex, start, trackTime, trackProgress, bisectingIterations);

        end = clock();
        duration = ((double)(end - start)) / CLOCKS_PER_MS;

        size_t centroidIndex = calculateCentroidIndex(&centroids, groundTruth);

        stats.sseSum += resultSse;
        stats.ciSum += centroidIndex;
        stats.timeSum += duration;
        if (centroidIndex == 0) stats.successRate++;

        if (centroidIndex != 0 && savedNonZeroResults == false)
        {
            char centroidsFile[PATH_MAX];
            char partitionsFile[PATH_MAX];
            snprintf(centroidsFile, sizeof(centroidsFile), "Bisecting_centroids_failed.txt");
            snprintf(partitionsFile, sizeof(partitionsFile), "Bisecting_partitions_failed.txt");

            writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
            writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
            savedNonZeroResults = true;
        }
        else if (centroidIndex == 0 && savedZeroResults == false)
        {
            char centroidsFile[PATH_MAX];
            char partitionsFile[PATH_MAX];
            snprintf(centroidsFile, sizeof(centroidsFile), "Bisecting_centroids_perfect.txt");
            snprintf(partitionsFile, sizeof(partitionsFile), "Bisecting_partitions_perfect.txt");

            writeCentroidsToFile(centroidsFile, &centroids, outputDirectory);
            writeDataPointPartitionsToFile(partitionsFile, dataPoints, outputDirectory);
            savedZeroResults = true;
        }

        freeCentroids(&centroids);
    }

    printStatistics("Bisecting", stats, loopCount, numCentroids, scaling, dataPoints->size);
    writeResultsToFile(fileName, stats, numCentroids, "Bisecting k-means", loopCount, scaling, outputDirectory, dataPoints->size);

    if (trackTime)
    {
        writeTimeTrackingData(outputDirectory, 4, timeList, timeIndex);
    }

    free(timeList);
}

/////////////
// Random //
///////////

/**
 * @brief Generates the ground truth centroids based on the data points and partition file.
 *
 * This function reads the data points from a file, assigns partition indices to each point,
 * calculates the centroids for each cluster, and writes the centroids to an output file.
 *
 * @param dataFileName The name of the data file containing the data points.
 * @param partitionFileName The name of the partition file containing the partition indices.
 * @param outputFileName The name of the output file to write the centroids to.
 * @return 0 on success, non-zero on failure.
 */
int generateGroundTruthCentroids(const char* dataFileName, const char* partitionFileName, const char* outputFileName) {
    // Read the data points
    DataPoints dataPoints = readDataPoints(dataFileName);

    if (dataPoints.size == 0) {
        printf("Error: No data points read from file %s\n", dataFileName);
        return 1;
    }

    printf("Read %zu data points from %s\n", dataPoints.size, dataFileName);

    // Read the partition indices
    FILE* partitionFile;

    if (FOPEN(partitionFile, partitionFileName, "r") != 0) {
        fprintf(stderr, "Error: Unable to open partition file '%s'\n", partitionFileName);
        freeDataPoints(&dataPoints);
        return 1;
    }

    // Assign the partition indices to the data points
    size_t maxPartitionIndex = 0;
    size_t validDataPoints = 0;

    // Determine if we have a mismatch between dataPoints size and partition file entries
    for (size_t i = 0; i < dataPoints.size; ++i) {
        int partitionIndex;
        if (fscanf(partitionFile, "%d", &partitionIndex) != 1) {
            // If we can't read more partitions but still have data points, we have a mismatch
            if (i < dataPoints.size - 1) {
                printf("Warning: Partition file has fewer entries (%zu) than data points (%zu).\n",
                    i, dataPoints.size);

                // Adjust the dataPoints size to match the valid entries we read
                dataPoints.size = i;
                break;
            }

            // Normal end of file
            break;
        }

        // Subtract 1 to adjust for partitions starting from 1 instead of 0
        partitionIndex -= 1;

        // Make sure partition index is non-negative
        if (partitionIndex < 0) {
            fprintf(stderr, "Error: Negative partition index found for data point %zu\n", i);
            fclose(partitionFile);
            freeDataPoints(&dataPoints);
            return 1;
        }

        dataPoints.points[i].partition = (size_t)partitionIndex;

        if (dataPoints.points[i].partition > maxPartitionIndex) {
            maxPartitionIndex = dataPoints.points[i].partition;
        }

        validDataPoints++;
    }

    // Check if there are more partitions in the file than data points
    int extraPartition;
    if (fscanf(partitionFile, "%d", &extraPartition) == 1) {
        printf("Warning: Partition file has more entries than data points. Data file has %zu points.\n",
            dataPoints.size);

        // Count how many extra partitions there are
        size_t extraPartitions = 1;  // We already read one
        while (fscanf(partitionFile, "%d", &extraPartition) == 1) {
            extraPartitions++;
        }
        printf("Found %zu extra partition entries.\n", extraPartitions);
    }

    fclose(partitionFile);

    printf("Processing %zu valid data points with partitions ranging from 0 to %zu\n",
        validDataPoints, maxPartitionIndex);

    // The number of centroids is maxPartitionIndex + 1 (assuming 0-based indexing)
    size_t numCentroids = maxPartitionIndex + 1;
    printf("Will calculate %zu centroids\n", numCentroids);

    // Allocate memory for the centroids
    Centroids centroids = allocateCentroids(numCentroids, dataPoints.points[0].dimensions);

    // Initialize count array to track how many points are in each cluster
    size_t* clusterCounts = calloc(numCentroids, sizeof(size_t));
    handleMemoryError(clusterCounts);

    // Sum up the attribute values for each cluster
    for (size_t i = 0; i < dataPoints.size; ++i) {
        size_t clusterIndex = dataPoints.points[i].partition;

        // Skip any invalid cluster indices (this is a safety check)
        if (clusterIndex >= numCentroids) {
            fprintf(stderr, "Error: Invalid cluster index %zu for data point %zu\n", clusterIndex, i);
            continue;
        }

        // Add the attribute values to the centroid sum
        for (size_t j = 0; j < dataPoints.points[i].dimensions; ++j) {
            centroids.points[clusterIndex].attributes[j] += dataPoints.points[i].attributes[j];
        }

        clusterCounts[clusterIndex]++;
    }

    // Calculate the mean values (centroids)
    for (size_t i = 0; i < numCentroids; ++i) {
        if (clusterCounts[i] > 0) {
            for (size_t j = 0; j < centroids.points[i].dimensions; ++j) {
                centroids.points[i].attributes[j] /= clusterCounts[i];
            }
            printf("Cluster %zu has %zu points\n", i, clusterCounts[i]);
        }
        else {
            printf("Warning: Cluster %zu has no points assigned to it\n", i);
        }
    }

    // Write the centroids to the output file
    FILE* outputFile;
    if (FOPEN(outputFile, outputFileName, "w") != 0) {
        fprintf(stderr, "Error: Unable to open output file '%s'\n", outputFileName);
        free(clusterCounts);
        freeCentroids(&centroids);
        freeDataPoints(&dataPoints);
        return 1;
    }

    for (size_t i = 0; i < numCentroids; ++i) {
        for (size_t j = 0; j < centroids.points[i].dimensions; ++j) {
            fprintf(outputFile, "%f ", centroids.points[i].attributes[j]);
        }
        fprintf(outputFile, "\n");
    }

    fclose(outputFile);

    printf("Successfully wrote %zu centroids to %s\n", numCentroids, outputFileName);

    // Clean up
    free(clusterCounts);
    freeCentroids(&centroids);
    freeDataPoints(&dataPoints);

    return 0;
}

/**
 * @brief Debug function that calculates CI between a debug centroid file and ground truth.
 *
 * This temporary function reads centroids from a debug file and compares them with
 * ground truth centroids to calculate the Centroid Index (CI).
 */
void debugCalculateCI(const char* debugCentroidsFile, const char* groundTruthFile)
{
    printf("Debugging CI calculation between:\n");
    printf("  Debug file: %s\n", debugCentroidsFile);
    printf("  Ground truth: %s\n", groundTruthFile);

    // Read centroids from both files
    Centroids debugCentroids = readCentroids(debugCentroidsFile);
    Centroids groundTruth = readCentroids(groundTruthFile);

    printf("Debug centroids: %zu with %zu dimensions\n",
        debugCentroids.size, debugCentroids.points[0].dimensions);
    printf("Ground truth centroids: %zu with %zu dimensions\n",
        groundTruth.size, groundTruth.points[0].dimensions);

    // Calculate Centroid Index
    size_t ci = calculateCentroidIndex(&debugCentroids, &groundTruth);
    printf("Centroid Index (CI): %zu\n\n", ci);

    // Clean up
    freeCentroids(&debugCentroids);
    freeCentroids(&groundTruth);
}

/**
 * @brief Debug function that calculates SSE between centroids and data points.
 *
 * This function reads centroids from a file and data points from another file,
 * assigns each data point to the nearest centroid, and calculates the Sum of
 * Squared Errors (SSE).
 *
 * @param centroidsFile The path to the file containing the centroids.
 * @param dataFile The path to the file containing the data points.
 */
void debugCalculateSSE(const char* debugCentroidsFile, const char* dataFile)
{
    printf("Debugging SSE calculation:\n");
    printf("  Centroids file: %s\n", debugCentroidsFile);
    printf("  Data file: %s\n", dataFile);

    // Read centroids and data points
    Centroids centroids = readCentroids(debugCentroidsFile);
    DataPoints dataPoints = readDataPoints(dataFile);

    printf("Centroids: %zu with %zu dimensions\n",
        centroids.size, centroids.points[0].dimensions);
    printf("Data points: %zu with %zu dimensions\n",
        dataPoints.size, dataPoints.points[0].dimensions);

    // Assign each data point to the nearest centroid
    partitionStep(&dataPoints, &centroids);

    // Calculate SSE
    double sse = calculateSSE(&dataPoints, &centroids);
    printf("Sum of Squared Errors (SSE): %.2f\n\n", sse);

    // Clean up
    freeCentroids(&centroids);
    freeDataPoints(&dataPoints);
}

void runDebuggery()
{
	debugCalculateCI("debuggery/output_worms_64d.txt", "gt/worms_64d-gt.txt");
	debugCalculateSSE("debuggery/output_worms_64d.txt", "data/worms_64d.txt");
}


///////////
// Main //
/////////

//END: kommentoi kaikki muistintarkastukset ja iffit pois lopullisesta versiosta <-tehokkuus
//END: credits ja alkupuhe koodin alkuun
//     edellisen alle voisi lisätä lokin, että kuka on päivittänyt ja milloin

//TODO: "static " sellaisten funktioiden eteen jotka eivät muuta mitään ja joita kutsutaan vain samasta tiedostosta?
//TODO: koodin palastelu eri tiedostoihin

int main()
{
    //runDebuggery();
    //return 0;
    
    set_numeric_locale_finnish();

    char outputDirectory[PATH_MAX];
    createUniqueDirectory(outputDirectory, sizeof(outputDirectory));

    char** dataNames = NULL, ** gtNames = NULL, ** kNames = NULL;
    size_t dataCount = list_files("data", &dataNames);
    size_t gtCount = list_files("gt", &gtNames);
    size_t kCount = list_files("centroids", &kNames);

    if (dataCount == 0 || dataCount != gtCount || dataCount != kCount) {
        fprintf(stderr,
            "Directory mismatch: data=%zu, gt=%zu, centroids=%zu\n",
            dataCount, gtCount, kCount);
        exit(EXIT_FAILURE);
    }

    if (LOGGING == 3) {
        for (size_t i = 0; i < dataCount; ++i) {
            printf("Dataset %zu: %s\n", i + 1, dataNames[i]);
        }
    }

    for (size_t i = 0; i < dataCount; ++i)
    {
        char dataFile[PATH_MAX];
        char gtFile[PATH_MAX];
        char kFile[PATH_MAX];
        snprintf(dataFile, sizeof dataFile, "data%c%s", PATHSEP, dataNames[i]);
        snprintf(gtFile, sizeof gtFile, "gt%c%s", PATHSEP, gtNames[i]);
        snprintf(kFile, sizeof kFile, "centroids%c%s", PATHSEP, kNames[i]);
        char* baseName = removeExtension(dataNames[i]);

        // Creates a subdirectory for each the dataset
        char datasetDirectory[PATH_MAX];
        createDatasetDirectory(outputDirectory, baseName, datasetDirectory, sizeof(datasetDirectory));

        //Settings
        size_t loops                = 100;        // Number of loops to run the algorithms //todo lopulliseen 100
        size_t scaling              = 1;        // Scaling factor for the printed values //TODO: Ei käytössä
		size_t maxIterations        = SIZE_MAX; // Maximum number of iterations for the k-means algorithm
		size_t maxRepeats           = 1000;     // Number of "repeats" for the repeated k-means algorithm //TODO lopulliseen 1000
		size_t maxSwaps             = 5000;     // Number of trial swaps for the random swap algorithm //TODO lopulliseen 5000
		size_t bisectingIterations  = 5;        // Number of tryouts for the bisecting k-means algorithm
		bool trackProgress          = true;     // Track progress of the algorithms
		bool trackTime              = true;     // Track time of the algorithms

        DataPoints dataPoints = readDataPoints(dataFile);
        Centroids  groundTruth = readCentroids(gtFile);
        size_t loopCount = loops;
        size_t swaps = maxSwaps;
        size_t numDimensions = getNumDimensions(dataFile);
        size_t numCentroids = read_k_from_file(kFile);

        printf("Starting the process\n");
        printf("Dataset: %s\n", baseName);

        if (numDimensions == 0) {
            fprintf(stderr, "--> Skipping (couldn’t read dimensions)\n");
            continue;
        }
        printf("Number of dimensions in the data: %zu\n", numDimensions);
        printf("Dataset size: %zu\n", dataPoints.size);
        printf("Number of clusters in the data: %zu\n", numCentroids);
        printf("Number of loops: %zu\n\n", loopCount);

        // Run K-means
        //runKMeansAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, baseName, datasetDirectory);

        loopCount = 5;
        // Run Repeated K-means
        //runRepeatedKMeansAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, maxRepeats, loopCount, scaling, baseName, datasetDirectory, trackProgress, trackTime);        

        loopCount = 1;
        swaps = maxSwaps;
        // Run Random Swap
        //runRandomSwapAlgorithm(&dataPoints, &groundTruth, numCentroids, swaps, loopCount, scaling, baseName, datasetDirectory, trackProgress, trackTime);               
        
        loopCount = loops;
        // Run Random Split
        //runRandomSplitAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, baseName, datasetDirectory, trackProgress, trackTime);

        // Run SSE Split (Intra-cluster)
        //runSseSplitAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, baseName, datasetDirectory, 0, trackProgress, trackTime);

        // Run SSE Split (Global)
        //runSseSplitAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, baseName, datasetDirectory, 1, trackProgress, trackTime);

        // Run SSE Split (Local Repartition)
        runSseSplitAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, baseName, datasetDirectory, 2, trackProgress, trackTime);
                        
        // Run Bisecting K-means
        //runBisectingKMeansAlgorithm(&dataPoints, &groundTruth, numCentroids, maxIterations, loopCount, scaling, baseName, datasetDirectory, trackProgress, trackTime, bisectingIterations);
            
        // Clean up
        freeDataPoints(&dataPoints);
        freeCentroids(&groundTruth);
    }

    for (size_t i = 0; i < dataCount; ++i) {
        free(dataNames[i]);
        free(gtNames[i]);
        free(kNames[i]);
    }

    free(dataNames);
    free(gtNames);
    free(kNames);

    return 0;
}