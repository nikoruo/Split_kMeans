/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"
#include "input_identity.h"
#include "algorithms.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void escapeInputPath(char* output, size_t size, const char* path)
{
    static const char digits[] = "0123456789abcdef";
    size_t used = 0;
    for (const unsigned char* p = (const unsigned char*)path; *p != '\0'; ++p)
    {
        char escaped[4];
        size_t count = 1;
        escaped[0] = (char)*p;
        if (*p == '\\' || *p == '\n' || *p == '\r' || *p == '\t')
        {
            escaped[0] = '\\';
            escaped[1] = *p == '\\' ? '\\' : (*p == '\n' ? 'n' : (*p == '\r' ? 'r' : 't'));
            count = 2;
        }
        else if (*p < 0x20 || *p == 0x7f || *p == '\'')
        {
            escaped[0] = '\\';
            escaped[1] = 'x';
            escaped[2] = digits[*p >> 4];
            escaped[3] = digits[*p & 0x0f];
            count = 4;
        }
        if (count >= size - used)
        {
            fprintf(stderr, "Error: Escaped input path is too long\n");
            exit(EXIT_FAILURE);
        }
        memcpy(output + used, escaped, count);
        used += count;
    }
    output[used] = '\0';
}

static bool validEscapedPath(const char* path)
{
    if (*path == '\0') return false;
    for (const unsigned char* p = (const unsigned char*)path; *p != '\0'; ++p)
    {
        if (*p < 0x20 || *p == 0x7f || *p == '\'') return false;
        if (*p != '\\') continue;
        ++p;
        if (*p == '\\' || *p == 'n' || *p == 'r' || *p == 't') continue;
        if (*p != 'x' || p[1] == '\0' || p[2] == '\0' || !isxdigit(p[1]) || !isxdigit(p[2]) ||
            (p[1] == '0' && p[2] == '0'))
            return false;
        p += 2;
    }
    return true;
}

static bool copyDigest(char output[SHA256_HEX_SIZE], const char* input, bool allowNone)
{
    if (allowNone && strcmp(input, "none") == 0)
    {
        STRCPY(output, SHA256_HEX_SIZE, "none");
        return true;
    }
    if (strlen(input) != SHA256_HEX_SIZE - 1) return false;
    for (size_t i = 0; i < SHA256_HEX_SIZE - 1; ++i)
    {
        const unsigned char value = (unsigned char)input[i];
        if (!isxdigit(value)) return false;
        output[i] = (char)tolower(value);
    }
    output[SHA256_HEX_SIZE - 1] = '\0';
    return true;
}

void makeDatasetInfo(DatasetInfo* info, const char* dataFile, const char* groundTruthFile,
                     const char* dataSha256, const char* groundTruthSha256, size_t k, size_t n, size_t d)
{
    *info = (DatasetInfo){0};
    escapeInputPath(info->dataFile, sizeof(info->dataFile), dataFile);
    escapeInputPath(info->groundTruthFile, sizeof(info->groundTruthFile),
                    groundTruthFile[0] != '\0' ? groundTruthFile : "none");
    if (!copyDigest(info->dataSha256, dataSha256, false) ||
        !copyDigest(info->groundTruthSha256, groundTruthSha256, true) ||
        ((groundTruthFile[0] == '\0') != (strcmp(info->groundTruthSha256, "none") == 0)))
    {
        fprintf(stderr, "Error: Invalid input fingerprint\n");
        exit(EXIT_FAILURE);
    }
    info->k = k;
    info->n = n;
    info->d = d;
    info->hasFingerprints = true;
}

/* Returns 1 for a line, 0 at EOF and -1 for a NUL or overlong line. */
static int metadataLine(FILE* file, char* buffer, size_t size)
{
    size_t used = 0;
    int value;
    while ((value = fgetc(file)) != EOF && value != '\n')
    {
        if (value == '\0' || used == size - 1) return -1;
        buffer[used++] = (char)value;
    }
    if (value == EOF && used == 0) return 0;
    if (used > 0 && buffer[used - 1] == '\r') --used;
    buffer[used] = '\0';
    return 1;
}

static bool parsePositiveSize(const char* text, size_t* result)
{
    uint64_t value = 0;
    if (!parseUint64(text, &value) || value == 0 || value > SIZE_MAX) return false;
    *result = (size_t)value;
    return true;
}

bool readDatasetInfo(const char* directory, DatasetInfo* info)
{
    char path[PATH_MAX];
    joinPath(path, sizeof(path), directory, "dataset.txt");
    FILE* file;
    if (FOPEN(file, path, "rb") != 0)
    {
        fprintf(stderr, "Error: Unable to open required dataset metadata '%s': %s\n", path, strerror(errno));
        return false;
    }

    *info = (DatasetInfo){0};
    enum { DATA = 1, TRUTH = 2, K = 4, N = 8, D = 16, DATA_HASH = 32, TRUTH_HASH = 64 };
    unsigned fields = 0;
    size_t lineNumber = 0;
    bool valid = true;
    char line[IDENTITY_PATH_SIZE + 32];
    int read;
    while ((read = metadataLine(file, line, sizeof(line))) > 0)
    {
        ++lineNumber;
        if (line[0] == '\0') continue;
        unsigned field = 0;
        const char* value = NULL;
        if (strncmp(line, "data: ", 6) == 0) { field = DATA; value = line + 6; }
        else if (strncmp(line, "ground-truth: ", 14) == 0) { field = TRUTH; value = line + 14; }
        else if (strncmp(line, "k: ", 3) == 0) { field = K; value = line + 3; }
        else if (strncmp(line, "n: ", 3) == 0) { field = N; value = line + 3; }
        else if (strncmp(line, "d: ", 3) == 0) { field = D; value = line + 3; }
        else if (strncmp(line, "data-sha256: ", 13) == 0) { field = DATA_HASH; value = line + 13; }
        else if (strncmp(line, "ground-truth-sha256: ", 21) == 0) { field = TRUTH_HASH; value = line + 21; }
        if (field == 0 || (fields & field) != 0)
        {
            valid = false;
            break;
        }
        fields |= field;
        if (field == DATA || field == TRUTH)
        {
            char* destination = field == DATA ? info->dataFile : info->groundTruthFile;
            if (*value == '\0' || strlen(value) >= IDENTITY_PATH_SIZE) valid = false;
            else STRCPY(destination, IDENTITY_PATH_SIZE, value);
        }
        else if (field == K) valid = parsePositiveSize(value, &info->k);
        else if (field == N) valid = parsePositiveSize(value, &info->n);
        else if (field == D) valid = parsePositiveSize(value, &info->d);
        else if (field == DATA_HASH) valid = copyDigest(info->dataSha256, value, false);
        else valid = copyDigest(info->groundTruthSha256, value, true);
        if (!valid) break;
    }
    if (read < 0) valid = false;
    closeInputFile(file, path);

    const unsigned required = DATA | TRUTH | K | N | D;
    const unsigned hashes = fields & (DATA_HASH | TRUTH_HASH);
    if ((fields & required) != required || (hashes != 0 && hashes != (DATA_HASH | TRUTH_HASH)) ||
        info->k > info->n)
        valid = false;
    info->hasFingerprints = hashes == (DATA_HASH | TRUTH_HASH);
    if (info->hasFingerprints &&
        (!validEscapedPath(info->dataFile) || !validEscapedPath(info->groundTruthFile) ||
         (strcmp(info->groundTruthSha256, "none") == 0 && strcmp(info->groundTruthFile, "none") != 0)))
        valid = false;
    if (!valid)
    {
        fprintf(stderr, "Error: Invalid dataset metadata '%s' near line %zu\n", path, lineNumber);
        return false;
    }
    return true;
}

bool compareDatasetInfo(const DatasetInfo* first, const DatasetInfo* second, const char* firstLabel,
                        const char* secondLabel)
{
    if (!first->hasFingerprints || !second->hasFingerprints)
    {
        fprintf(stderr, "Error: Input fingerprints are not recorded in %s or %s\n", firstLabel, secondLabel);
        return false;
    }
    if (strcmp(first->dataSha256, second->dataSha256) == 0 &&
        strcmp(first->groundTruthSha256, second->groundTruthSha256) == 0 &&
        first->k == second->k && first->n == second->n && first->d == second->d)
        return true;

    fprintf(stderr, "Error: Input identity mismatch between %s and %s\n", firstLabel, secondLabel);
    fprintf(stderr, "  %s data: '%s'; SHA-256: %s\n", firstLabel, first->dataFile, first->dataSha256);
    fprintf(stderr, "  %s ground truth: '%s'; SHA-256: %s\n", firstLabel, first->groundTruthFile,
            first->groundTruthSha256);
    fprintf(stderr, "  %s K: %zu; N: %zu; D: %zu\n", firstLabel, first->k, first->n, first->d);
    fprintf(stderr, "  %s data: '%s'; SHA-256: %s\n", secondLabel, second->dataFile, second->dataSha256);
    fprintf(stderr, "  %s ground truth: '%s'; SHA-256: %s\n", secondLabel, second->groundTruthFile,
            second->groundTruthSha256);
    fprintf(stderr, "  %s K: %zu; N: %zu; D: %zu\n", secondLabel, second->k, second->n, second->d);
    return false;
}

void checkDatasetInfo(const char* directory, const DatasetInfo* info)
{
    char path[PATH_MAX];
    joinPath(path, sizeof(path), directory, "dataset.txt");
    if (!fileExists(path)) return;
    DatasetInfo existing;
    if (!readDatasetInfo(directory, &existing) || !compareDatasetInfo(&existing, info, path, "this call"))
        exit(EXIT_FAILURE);
}

void writeDatasetInfo(const char* directory, const DatasetInfo* info)
{
    char path[PATH_MAX];
    joinPath(path, sizeof(path), directory, "dataset.txt");
    if (fileExists(path))
    {
        checkDatasetInfo(directory, info);
        return;
    }
    FILE* file;
    if (FOPEN(file, path, "w") != 0) handleFileError(path);
    checkFileWrite(file, path,
                   fprintf(file, "data: %s\nground-truth: %s\nk: %zu\nn: %zu\nd: %zu\n"
                                 "data-sha256: %s\nground-truth-sha256: %s\n",
                           info->dataFile, info->groundTruthFile, info->k, info->n, info->d, info->dataSha256,
                           info->groundTruthSha256));
    closeOutputFile(file, path);
}

static bool startsWithIgnoreCase(const char* text, const char* prefix)
{
    while (*prefix != '\0')
    {
        if (*text == '\0' || tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) return false;
        ++text;
        ++prefix;
    }
    return true;
}

static bool endsWithIgnoreCase(const char* text, const char* suffix)
{
    const size_t length = strlen(text);
    const size_t suffixLength = strlen(suffix);
    return length >= suffixLength && startsWithIgnoreCase(text + length - suffixLength, suffix);
}

static bool directoryHasResults(const char* directory, const char* datasetName)
{
    char** files = NULL;
    const size_t count = list_all_files(directory, &files);
    bool found = false;
    for (size_t i = 0; i < count && !found; ++i)
    {
        const size_t nameLength = strlen(datasetName);
        const size_t fileLength = strlen(files[i]);
        if (fileLength >= nameLength && fileLength - nameLength == 4 &&
            startsWithIgnoreCase(files[i], datasetName) && endsWithIgnoreCase(files[i], ".csv"))
            found = true;
        for (size_t a = 0; a < ALGORITHM_COUNT && !found; ++a)
        {
            const char* prefix = getAlgorithmName(algorithmOptions[a].id);
            const size_t prefixLength = strlen(prefix);
            if (startsWithIgnoreCase(files[i], prefix) && files[i][prefixLength] == '_' &&
                (endsWithIgnoreCase(files[i], ".csv") || endsWithIgnoreCase(files[i], ".txt")))
                found = true;
        }
    }
    freeStringList(files, count);
    return found;
}

static bool directoryHasReportResults(const char* directory)
{
    char** files = NULL;
    const size_t count = list_all_files(directory, &files);
    bool found = false;
    for (size_t i = 0; i < count && !found; ++i)
    {
        found = (strlen(files[i]) == strlen("summary.csv") && startsWithIgnoreCase(files[i], "summary.csv")) ||
                (startsWithIgnoreCase(files[i], "table_") && endsWithIgnoreCase(files[i], ".csv")) ||
                (startsWithIgnoreCase(files[i], "curves_") && endsWithIgnoreCase(files[i], ".csv"));
    }
    freeStringList(files, count);
    return found;
}

void checkOutputRoot(const char* directory)
{
    char configPath[PATH_MAX];
    joinPath(configPath, sizeof(configPath), directory, "run_config.txt");
    const bool hasConfig = fileExists(configPath);
    if (!hasConfig && (directoryHasResults(directory, platform_basename(directory)) ||
                       directoryHasReportResults(directory)))
    {
        fprintf(stderr, "Error: Existing results in '%s' lack required %s\n", directory, configPath);
        exit(EXIT_FAILURE);
    }

    char** names = NULL;
    const size_t count = list_dirs(directory, &names);
    for (size_t i = 0; i < count; ++i)
    {
        char folder[PATH_MAX];
        joinPath(folder, sizeof(folder), directory, names[i]);
        if (!directoryHasResults(folder, names[i])) continue;
        if (!hasConfig)
        {
            fprintf(stderr, "Error: Existing results in '%s' lack required %s\n", folder, configPath);
            freeStringList(names, count);
            exit(EXIT_FAILURE);
        }
        DatasetInfo info;
        if (!readDatasetInfo(folder, &info))
        {
            freeStringList(names, count);
            exit(EXIT_FAILURE);
        }
        if (!info.hasFingerprints)
        {
            fprintf(stderr, "Error: Existing results in '%s' have no input fingerprints; use a new output folder\n",
                    folder);
            freeStringList(names, count);
            exit(EXIT_FAILURE);
        }
    }
    freeStringList(names, count);
}

bool checkReportDatasets(const char* directory, bool allowLegacy, bool* identityMissing)
{
    char** names = NULL;
    const size_t count = list_dirs(directory, &names);
    for (size_t i = 0; i < count; ++i)
    {
        char folder[PATH_MAX];
        joinPath(folder, sizeof(folder), directory, names[i]);
        if (!directoryHasResults(folder, names[i])) continue;
        DatasetInfo info;
        if (!readDatasetInfo(folder, &info))
        {
            freeStringList(names, count);
            return false;
        }
        if (!info.hasFingerprints)
        {
            if (!allowLegacy)
            {
                fprintf(stderr, "Error: %s has no input fingerprints; multi-folder reports require them\n", folder);
                freeStringList(names, count);
                return false;
            }
            fprintf(stderr, "Warning: %s has no input fingerprints; historical identity is not recorded\n", folder);
            *identityMissing = true;
        }
    }
    freeStringList(names, count);
    return true;
}
