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

/* ASCII case-insensitive, then byte order, so the result is the same on every OS and locale. */
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
    if (snprintf(pattern, sizeof pattern, "%s\\*", dir) >= (int)sizeof pattern)
    {
        fprintf(stderr, "Error: Folder path too long: %s\n", dir);
        return 0;
    }

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    do
    {
        const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (wantDirs ? (isDir && !is_dot_entry(fd.cFileName)) : (!isDir && has_txt_extension(fd.cFileName)))
        {
            append_name(&list, &count, &capacity, fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    DIR* d = opendir(dir);
    if (d == NULL)
    {
        return 0;
    }

    struct dirent* e;
    while ((e = readdir(d)) != NULL)
    {
        if (wantDirs ? is_dot_entry(e->d_name) : !has_txt_extension(e->d_name))
        {
            continue;
        }
        /* d_type is not portable (MinGW has none); stat the entry instead */
        char path[PATH_MAX];
        struct stat st;
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path)
        {
            fprintf(stderr, "Warning: Skipping %s/%s (path too long)\n", dir, e->d_name);
            continue;
        }
        if (stat(path, &st) != 0 || (S_ISDIR(st.st_mode) != 0) != wantDirs)
        {
            continue;
        }
        append_name(&list, &count, &capacity, e->d_name);
    }

    closedir(d);
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
    fclose(file);
}
#endif

/** @brief Creates `path` and its missing parent folders (as mkdir -p); false with errno set on failure. */
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
          if (MAKE_DIR(partial) != 0 && errno != EEXIST)
          {
              return false;
          }
      }
      return true;
  }
