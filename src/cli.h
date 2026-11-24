/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef CLI_H
#define CLI_H

/* argv includes the executable name; returns a status or exits on parsing and input errors. */
int cliMain(int argc, char* argv[]);
/* Writes means from 1..N labels separated by spaces, tabs or line breaks; returns 0/1, while data parsing errors may exit. */
int generateGroundTruthCentroids(const char* dataFileName, const char* partitionFileName, const char* outputFileName);
/* Debug helpers require nonempty, dimension-compatible files and do not validate them. */
void debugCalculateCI(const char* debugCentroidsFile, const char* groundTruthFile);
void debugCalculateSSE(const char* debugCentroidsFile, const char* dataFile);
void runDebuggery(void);

#endif /* CLI_H */
