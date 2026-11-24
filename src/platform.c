/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#include "platform.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#endif
#ifdef _MSC_VER
/* RegGetValueA (platform_cpu); MinGW and the Visual Studio project link advapi32 by default, clang with the MSVC target does not */
#  pragma comment(lib, "advapi32.lib")
#endif
#ifndef _MSC_VER
#  include <dirent.h>
#  include <sys/stat.h>
#endif
#ifndef _WIN32
#  include <sys/utsname.h>
#  include <unistd.h>
#endif

/* Case-fold in the current C locale, then use byte order; the program leaves the default C locale. */
static int compare_file_names(const void* a, const void* b)
{
    const unsigned char* sa = *(const unsigned char* const*)a;
    const unsigned char* sb = *(const unsigned char* const*)b;

    for (size_t i = 0;; ++i)
    {
        const int ca = tolower(sa[i]);
        const int cb = tolower(sb[i]);
        if (ca != cb)
        {
            return ca - cb;
        }
        if (ca == '\0')
        {
            break;
        }
    }
    return strcmp((const char*)sa, (const char*)sb);
}

static bool has_txt_extension(const char* name)
{
    const size_t length = strlen(name);
    if (length <= 4)
    {
        return false;
    }
    const char* ext = name + length - 4;
    return ext[0] == '.' && tolower((unsigned char)ext[1]) == 't' &&
        tolower((unsigned char)ext[2]) == 'x' && tolower((unsigned char)ext[3]) == 't';
}

static void out_of_memory(void)
{
    fprintf(stderr, "Error: Unable to allocate memory\n");
    exit(EXIT_FAILURE);
}

/* Appends a copy of `name` to the growing list; exits on allocation failure. */
static void append_name(char*** list, size_t* count, size_t* capacity, const char* name)
{
    if (*count == *capacity)
    {
        const size_t grown = *capacity ? *capacity * 2 : 16;
        char** resized = realloc(*list, grown * sizeof(**list));
        if (resized == NULL)
        {
            out_of_memory();
        }
        *list = resized;
        *capacity = grown;
    }

    const size_t size = strlen(name) + 1;
    char* copy = malloc(size);
    if (copy == NULL)
    {
        out_of_memory();
    }
    memcpy(copy, name, size);
    (*list)[(*count)++] = copy;
}

static void free_names(char** list, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        free(list[i]);
    free(list);
}

/* True for "." and "..". */
static bool is_dot_entry(const char* name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

/* The *.txt files of `dir` (wantDirs false) or its subfolders (wantDirs true), sorted with compare_file_names. */
static size_t list_entries(const char* dir, char*** out, bool wantDirs)
{
    char** list = NULL;
    size_t count = 0;
    size_t capacity = 0;
    *out = NULL;

#ifdef _MSC_VER
    char pattern[PATH_MAX];
    const int patternLength = snprintf(pattern, sizeof pattern, "%s\\*", dir);
    if (patternLength < 0 || (size_t)patternLength >= sizeof pattern)
    {
        fprintf(stderr, "Error: Folder path too long: %s\n", dir);
        exit(EXIT_FAILURE);
    }

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) return 0;
        fprintf(stderr, "Error: Unable to enumerate directory '%s' (Windows error %lu)\n", dir, (unsigned long)error);
        exit(EXIT_FAILURE);
    }

    do
    {
        const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (wantDirs ? (isDir && !is_dot_entry(fd.cFileName)) : (!isDir && has_txt_extension(fd.cFileName)))
        {
            append_name(&list, &count, &capacity, fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));

    const DWORD enumerationError = GetLastError();
    const BOOL closed = FindClose(h);
    const DWORD closeError = closed ? ERROR_SUCCESS : GetLastError();
    if (enumerationError != ERROR_NO_MORE_FILES || !closed)
    {
        free_names(list, count);
        if (enumerationError != ERROR_NO_MORE_FILES)
            fprintf(stderr, "Error: Unable to enumerate directory '%s' (Windows error %lu)\n", dir,
                    (unsigned long)enumerationError);
        if (!closed)
            fprintf(stderr, "Error: Unable to close directory '%s' (Windows error %lu)\n", dir,
                    (unsigned long)closeError);
        exit(EXIT_FAILURE);
    }
#else
    DIR* d = opendir(dir);
    if (d == NULL)
    {
        fprintf(stderr, "Error: Unable to enumerate directory '%s': %s\n", dir, strerror(errno));
        exit(EXIT_FAILURE);
    }

    int error = 0;
    const char* operation = "enumerate directory";
    const char* errorPath = dir;
    char path[PATH_MAX];
    for (;;)
    {
        errno = 0;
        struct dirent* e = readdir(d);
        if (e == NULL)
        {
            error = errno;
            break;
        }
        if (wantDirs ? is_dot_entry(e->d_name) : !has_txt_extension(e->d_name))
        {
            continue;
        }
        /* d_type is not portable (MinGW has none); stat the entry instead */
        struct stat st;
        const int length = snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (length < 0 || (size_t)length >= sizeof path)
        {
            fprintf(stderr, "Error: Directory entry path too long: %s/%s\n", dir, e->d_name);
            error = ENAMETOOLONG;
            operation = NULL;
            break;
        }
        if (stat(path, &st) != 0)
        {
            error = errno;
            operation = "inspect directory entry";
            errorPath = path;
            break;
        }
        if ((S_ISDIR(st.st_mode) != 0) != wantDirs)
        {
            continue;
        }
        append_name(&list, &count, &capacity, e->d_name);
    }

    const int closeResult = closedir(d);
    const int closeError = errno;
    if (error != 0 || closeResult != 0)
    {
        free_names(list, count);
        if (error != 0 && operation != NULL)
            fprintf(stderr, "Error: Unable to %s '%s': %s\n", operation, errorPath, strerror(error));
        if (closeResult != 0) fprintf(stderr, "Error: Unable to close directory '%s': %s\n", dir, strerror(closeError));
        exit(EXIT_FAILURE);
    }
#endif

    if (count > 1)
    {
        qsort(list, count, sizeof(*list), compare_file_names);
    }
    *out = list;
    return count;
}

size_t list_files(const char* dir, char*** out)
{
    return list_entries(dir, out, false);
}

size_t list_dirs(const char* dir, char*** out)
{
    return list_entries(dir, out, true);
}
#ifdef _WIN32
uint64_t platform_ticks(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)counter.QuadPart;
}

uint64_t platform_ticks_per_second(void)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)frequency.QuadPart;
}

const char* platform_clock_name(void)
{
    return "QueryPerformanceCounter (monotonic wall clock)";
}
#else
uint64_t platform_ticks(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

uint64_t platform_ticks_per_second(void)
{
    return UINT64_C(1000000000);
}

const char* platform_clock_name(void)
{
    return "clock_gettime(CLOCK_MONOTONIC) (monotonic wall clock)";
}
#endif

#define PLATFORM_STRING2(x) #x
#define PLATFORM_STRING(x) PLATFORM_STRING2(x)

const char* platform_compiler(void)
{
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC " PLATFORM_STRING(_MSC_FULL_VER);
#else
    return "unknown";
#endif
}

/* Removes leading and trailing spaces and the line end in place. */
static void trim_text(char* text)
{
    size_t start = 0;
    while (text[start] == ' ' || text[start] == '\t')
    {
        ++start;
    }
    size_t length = strlen(text + start);
    memmove(text, text + start, length + 1);
    while (length > 0 && isspace((unsigned char)text[length - 1]))
    {
        text[--length] = '\0';
    }
}

#ifdef _WIN32
void platform_machine(char* buffer, size_t size)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    snprintf(buffer, size, "Windows, %lu logical processors", (unsigned long)info.dwNumberOfProcessors);
}

void platform_cpu(char* buffer, size_t size)
{
    DWORD bytes = (DWORD)size;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            "ProcessorNameString", RRF_RT_REG_SZ, NULL, buffer, &bytes) != ERROR_SUCCESS)
    {
        snprintf(buffer, size, "unknown");
    }
    trim_text(buffer);
}
#else
void platform_machine(char* buffer, size_t size)
{
    struct utsname host;
    if (uname(&host) != 0)
    {
        snprintf(buffer, size, "unknown");
        return;
    }
    snprintf(buffer, size, "%s %s, %ld logical processors", host.sysname, host.release, sysconf(_SC_NPROCESSORS_ONLN));
}

void platform_cpu(char* buffer, size_t size)
{
    snprintf(buffer, size, "unknown");
    FILE* file = fopen("/proc/cpuinfo", "r");
    if (file == NULL)
    {
        return;
    }
    char line[512];
    while (fgets(line, sizeof line, file) != NULL)
    {
        const char* colon = strchr(line, ':');
        if (strncmp(line, "model name", 10) == 0 && colon != NULL)
        {
            snprintf(buffer, size, "%s", colon + 1);
            trim_text(buffer);
            break;
        }
    }
    const bool readFailed = ferror(file) != 0;
    const bool closeFailed = fclose(file) != 0;
    if (readFailed) fprintf(stderr, "Error: Unable to read from file '/proc/cpuinfo'\n");
    if (closeFailed) fprintf(stderr, "Error: Unable to close file '/proc/cpuinfo'\n");
    if (readFailed || closeFailed) exit(EXIT_FAILURE);
}
#endif

bool platform_make_directory(const char* path, bool* created)
{
    if (MAKE_DIR(path) == 0)
    {
        if (created != NULL) *created = true;
        return true;
    }
    if (errno != EEXIST) return false;

    struct stat status;
    if (stat(path, &status) != 0) return false;
#ifdef _MSC_VER
    const bool isDirectory = (status.st_mode & _S_IFMT) == _S_IFDIR;
#else
    const bool isDirectory = S_ISDIR(status.st_mode) != 0;
#endif
    if (!isDirectory)
    {
        errno = ENOTDIR;
        return false;
    }
    if (created != NULL) *created = false;
    return true;
}

bool platform_make_directories(const char* path)
{
    char partial[PATH_MAX];
    const size_t length = strlen(path);
    if (length == 0 || length >= sizeof(partial))
    {
        errno = ENAMETOOLONG;
        return false;
    }
    for (size_t i = 1; i <= length; ++i)
    {
        if (i < length && path[i] != '/' && path[i] != '\\')
        {
            continue;
        }
        memcpy(partial, path, i);
        partial[i] = '\0';
        if (i == 2 && partial[1] == ':')
        {
            continue; // drive root such as C:
        }
        if (!platform_make_directory(partial, NULL))
        {
            return false;
        }
    }
    return true;
}

bool platform_localtime(struct tm* out, const time_t* timep)
{
#ifdef _MSC_VER
    return LOCALTIME(out, timep) == 0;
#else
    return LOCALTIME(out, timep) != NULL;
#endif
}
