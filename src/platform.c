/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#include "platform.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
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

size_t list_files(const char* dir, char*** out)
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
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && has_txt_extension(fd.cFileName))
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
        if (!has_txt_extension(e->d_name))
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
        if (stat(path, &st) != 0 || S_ISDIR(st.st_mode))
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
