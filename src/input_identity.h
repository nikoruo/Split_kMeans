/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef INPUT_IDENTITY_H
#define INPUT_IDENTITY_H

#include "platform.h"
#include "sha256.h"

#define IDENTITY_PATH_SIZE (4 * PATH_MAX + 1)

typedef struct
{
    /* Paths are escaped for single-line metadata and diagnostics. */
    char dataFile[IDENTITY_PATH_SIZE];
    char groundTruthFile[IDENTITY_PATH_SIZE];
    char dataSha256[SHA256_HEX_SIZE];
    char groundTruthSha256[SHA256_HEX_SIZE];
    size_t k;
    size_t n;
    size_t d;
    bool hasFingerprints;
} DatasetInfo;

void makeDatasetInfo(DatasetInfo* info, const char* dataFile, const char* groundTruthFile,
                     const char* dataSha256, const char* groundTruthSha256, size_t k, size_t n, size_t d);
/* Missing or malformed metadata is reported and returns false. */
bool readDatasetInfo(const char* directory, DatasetInfo* info);
bool compareDatasetInfo(const DatasetInfo* first, const DatasetInfo* second, const char* firstLabel,
                        const char* secondLabel);
void checkDatasetInfo(const char* directory, const DatasetInfo* info);
void writeDatasetInfo(const char* directory, const DatasetInfo* info);
void checkOutputRoot(const char* directory);
bool checkReportDatasets(const char* directory, bool allowLegacy, bool* identityMissing);

#endif /* INPUT_IDENTITY_H */
