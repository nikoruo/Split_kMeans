/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define LINE_BUFSZ 16384

extern int logLevel;

void handleMemoryError(void* ptr);
void handleFileError(const char* filename);

/* The caller owns the list and its strings; release them with freeStringList. */
char** createStringList(size_t size);
void freeStringList(char** list, size_t size);

/* Closes the stream and exits on read errors or overlong lines. */
char* readDataLine(char* buffer, size_t size, FILE* file, const char* filename, size_t lineNumber);
bool parseFiniteDouble(const char* token, double* value);
bool fileExists(const char* filePath);
void joinPath(char* out, size_t size, const char* dir, const char* name);
/* The returned buffer is overwritten by the next call. */
char* removeExtension(const char* filename);

#endif /* UTIL_H */
