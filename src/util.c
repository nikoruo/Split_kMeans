/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"

#include "util.h"
#include "metrics.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int logLevel = 1;

/**
 * @brief Aborts the process if an allocation returned NULL.
 *
 * Centralizes out-of-memory handling so callers remain branchless.
 * Only use after allocating a non-zero number of bytes; for size 0,
 * malloc/calloc/realloc may legally return NULL.
 * Takes a non-const pointer: gcc -Wmaybe-uninitialized assumes a const pointer argument is read.
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
 * @brief Allocates an array of `size` writable PATH_MAX strings, zero-initialized.
 */
char** createStringList(size_t size)
{
    if (size == 0) return NULL;

    char** list = calloc(size, sizeof(char*));
    handleMemoryError(list);

    const size_t stringSize = PATH_MAX;

    for (size_t i = 0; i < size; ++i)
    {
        list[i] = calloc(stringSize, sizeof(char));
        handleMemoryError(list[i]);
    }

    return list;
}

/**
 * @brief Frees each string then the list container.
 */
void freeStringList(char** list, size_t size)
{
    if (list == NULL) return;

    for (size_t i = 0; i < size; ++i)
    {
        free(list[i]);
    }
    free(list);
}

/**
 * @brief Handles file opening errors and terminates the program.
 */
void handleFileError(const char* filename)
{
    fprintf(stderr, "Error: Unable to open file '%s'\n", filename);
    exit(EXIT_FAILURE);
}

/**
 * @brief Handles file read errors and terminates the program.
 */
static void handleFileReadError(const char* filename)
{
    fprintf(stderr, "Error: Unable to read from file '%s'\n", filename);
    exit(EXIT_FAILURE);
}

/**
 * @brief Reads one line into `buffer` with fgets; NULL at end of file.
 *
 * Exits when the line does not fit in `size - 1` characters (a split line would be read as two rows)
 * or on a read error. `lineNumber` is 1-based and only used in the error message.
 */
char* readDataLine(char* buffer, size_t size, FILE* file, const char* filename, size_t lineNumber)
{
    if (fgets(buffer, (int)size, file) == NULL)
    {
        if (ferror(file))
        {
            fclose(file);
            handleFileReadError(filename);
        }
        return NULL;
    }

    if (strchr(buffer, '\n') == NULL && !feof(file))
    {
        // The buffer is full: fine only if the line ends right here
        const int next = fgetc(file);
        if (next != '\n' && next != EOF)
        {
            fclose(file);
            fprintf(stderr, "Error: Line %zu in '%s' is longer than %zu characters\n", lineNumber, filename, size - 1);
            exit(EXIT_FAILURE);
        }
    }
    return buffer;
}

/**
 * @brief Parses a whole token as a finite double; false on trailing characters, NaN, Inf or overflow.
 */
bool parseFiniteDouble(const char* token, double* value)
{
    char* end = NULL;
    const double parsed = strtod(token, &end);
    if (end == token || *end != '\0' || !isfinite(parsed))
    {
        return false;
    }
    *value = parsed;
    return true;
}

/**
 * @brief Returns true if the path exists (file or directory).
 */
bool fileExists(const char* filePath)
{
    struct stat buffer;
    return (stat(filePath, &buffer) == 0);
}

/**
 * @brief Writes `dir` + separator + `name` into `out`; exits when it does not fit in `size` bytes.
 */
void joinPath(char* out, size_t size, const char* dir, const char* name)
{
    const int length = snprintf(out, size, "%s%c%s", dir, PATHSEP, name);
    if (length < 0 || (size_t)length >= size)
    {
        fprintf(stderr, "Error: Path too long: %s%c%s\n", dir, PATHSEP, name);
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Returns a copy of filename without its trailing extension.
 *
 * Uses an internal static buffer. Removes the last '.' only if it appears
 * after the last path separator; preserves leading-dot names (e.g., ".env").
 * Not thread-safe.
 */
char* removeExtension(const char* filename)
{
    static char baseFileName[PATH_MAX];

    /* Copy input with guaranteed NUL-termination */
    STRCPY(baseFileName, sizeof(baseFileName), filename);

    /* Find last path separator (support both styles) */
    char* lastSep = strrchr(baseFileName, '/');
    char* lastSep2 = strrchr(baseFileName, '\\');
    if (lastSep2 && (!lastSep || lastSep2 > lastSep)) lastSep = lastSep2;

    /* Find last dot and ensure it is within the basename and not the very first basename char */
    char* lastDot = strrchr(baseFileName, '.');
    char* basenameStart = lastSep ? lastSep + 1 : baseFileName;

    if (lastDot && lastDot > basenameStart)
    {
        *lastDot = '\0';
    }

    return baseFileName;
}

/**
   * @brief Writes `ci` as decimal text, or "NA" for CI_NA, into `buffer`; returns `buffer`.
   */
  const char* formatCi(size_t ci, char buffer[CI_TEXT_SIZE])
  {
      if (ci == CI_NA)
      {
          snprintf(buffer, CI_TEXT_SIZE, "NA");
      }
      else
      {
          snprintf(buffer, CI_TEXT_SIZE, "%zu", ci);
      }
      return buffer;
  }
