/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"

#include "util.h"
#include "metrics.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int logLevel = 1;
bool decimalComma = false;

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

void closeInputFile(FILE* file, const char* filename)
{
    const bool readFailed = ferror(file) != 0;
    const bool closeFailed = fclose(file) != 0;
    if (readFailed) fprintf(stderr, "Error: Unable to read from file '%s'\n", filename);
    if (closeFailed) fprintf(stderr, "Error: Unable to close file '%s'\n", filename);
    if (readFailed || closeFailed) exit(EXIT_FAILURE);
}

static void finishOutputFile(FILE* file, const char* filename, bool writeFailed)
{
    writeFailed = writeFailed || ferror(file) != 0;
    if (fflush(file) != 0) writeFailed = true;
    const bool closeFailed = fclose(file) != 0;
    if (writeFailed) fprintf(stderr, "Error: Unable to write to file '%s'\n", filename);
    if (closeFailed) fprintf(stderr, "Error: Unable to close file '%s'\n", filename);
    if (writeFailed || closeFailed) exit(EXIT_FAILURE);
}

void checkFileWrite(FILE* file, const char* filename, int result)
{
    if (result < 0) finishOutputFile(file, filename, true);
}

void closeOutputFile(FILE* file, const char* filename)
{
    finishOutputFile(file, filename, false);
}

bool fileIsEmpty(FILE* file, const char* filename)
{
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Error: Unable to seek file '%s'\n", filename);
        closeOutputFile(file, filename);
        exit(EXIT_FAILURE);
    }
    const long position = ftell(file);
    if (position < 0)
    {
        fprintf(stderr, "Error: Unable to determine position in file '%s'\n", filename);
        closeOutputFile(file, filename);
        exit(EXIT_FAILURE);
    }
    return position == 0;
}

/**
 * @brief Reads one line into `buffer` with fgets; NULL at end of file.
 *
 * Exits when the line does not fit in `size - 1` characters or a read fails.
 * `lineNumber` is 1-based and only used in the error message.
 */
char* readDataLine(char* buffer, size_t size, FILE* file, const char* filename, size_t lineNumber)
{
    if (fgets(buffer, (int)size, file) == NULL)
    {
        if (ferror(file))
        {
            closeInputFile(file, filename);
        }
        return NULL;
    }

    if (strchr(buffer, '\n') == NULL && !feof(file))
    {
        // The buffer is full: fine only if the line ends right here
        const int next = fgetc(file);
        if (ferror(file))
        {
            closeInputFile(file, filename);
        }
        if (next != '\n' && next != EOF)
        {
            closeInputFile(file, filename);
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
    if (stat(filePath, &buffer) == 0) return true;
    if (errno == ENOENT || errno == ENOTDIR) return false;
    fprintf(stderr, "Error: Unable to inspect '%s': %s\n", filePath, strerror(errno));
    exit(EXIT_FAILURE);
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

bool validateCsvName(const char* name, const char* kind)
{
    const char* invalid = strpbrk(name, ";\"\r\n");
    if (invalid == NULL) return true;

    fprintf(stderr, "Error: %s '", kind);
    for (const unsigned char* p = (const unsigned char*)name; *p != '\0'; ++p)
    {
        switch (*p)
        {
            case '\r':
                fputs("\\r", stderr);
                break;
            case '\n':
                fputs("\\n", stderr);
                break;
            case '\\':
                fputs("\\\\", stderr);
                break;
            case '\'':
                fputs("\\'", stderr);
                break;
            default:
                fputc(*p, stderr);
                break;
        }
    }
    const char* character = *invalid == ';' ? ";" : *invalid == '"' ? "\"" : *invalid == '\r' ? "\\r" : "\\n";
    fprintf(stderr, "' contains forbidden character '%s'\n", character);
    return false;
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

/**
   * @brief Writes `value` with `decimals` fixed decimals into `buffer` and returns it (Excel-facing files).
   *
   * The program never calls setlocale, so printf writes a decimal point; with --decimal-comma it becomes a comma.
   * Output needing NUMBER_TEXT_SIZE or more characters is truncated.
   */
  const char* formatFixed(double value, int decimals, char buffer[NUMBER_TEXT_SIZE])
  {
      snprintf(buffer, NUMBER_TEXT_SIZE, "%.*f", decimals, value);
      if (decimalComma)
      {
          char* point = strchr(buffer, '.');
          if (point != NULL)
          {
              *point = ',';
          }
      }
      return buffer;
  }

/** @brief Parses a decimal unsigned 64-bit integer; false on anything else. */
  bool parseUint64(const char* text, uint64_t* out)
  {
      if (text[0] < '0' || text[0] > '9')
      {
          return false;
      }
      char* end = NULL;
      errno = 0;
      const unsigned long long value = strtoull(text, &end, 10);
      if (errno != 0 || *end != '\0')
      {
          return false;
      }
      *out = (uint64_t)value;
      return true;
  }

/** @brief Reads the value after option `argv[*i]` as an integer of at least `minimum` and advances `*i`; exits otherwise. */
  size_t countOption(int argc, char* argv[], int* i, uint64_t minimum)
  {
      const char* name = argv[*i];
      uint64_t value = 0;
      if (*i + 1 >= argc || !parseUint64(argv[*i + 1], &value) || value < minimum || (uint64_t)(size_t)value != value)
      {
          fprintf(stderr, "Error: %s requires an integer of at least %" PRIu64 "\n", name, minimum);
          fprintf(stderr, "Use -h for help\n");
          exit(EXIT_FAILURE);
      }
      ++*i;
      return (size_t)value;
  }

/** @brief Returns the value after option `argv[*i]` and advances `*i`; exits when it is missing. */
  const char* textOption(int argc, char* argv[], int* i)
  {
      if (*i + 1 >= argc)
      {
          fprintf(stderr, "Error: %s requires a value\n", argv[*i]);
          fprintf(stderr, "Use -h for help\n");
          exit(EXIT_FAILURE);
      }
      ++*i;
      return argv[*i];
  }
