/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef CLI_H
#define CLI_H

int cliMain(int argc, char* argv[]);
int generateGroundTruthCentroids(const char* dataFileName, const char* partitionFileName, const char* outputFileName);
void debugCalculateCI(const char* debugCentroidsFile, const char* groundTruthFile);
void debugCalculateSSE(const char* debugCentroidsFile, const char* dataFile);
void runDebuggery(void);

#endif /* CLI_H */
