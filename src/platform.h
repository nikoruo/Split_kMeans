/* SPDX-License-Identifier: AGPL-3.0-only
* Copyright (C) 2025 Niko Ruohonen and contributors
*/

/* platform.h ------------------------------------------------------------
* Minimal portability layer for building the same C/C++ source on:
*   - Windows / MSVC        (Annex-K *_s, _mkdir)
*   - Windows / MinGW       (custom strtok_r, mkdir)
*   - Linux / POSIX
*
* Exposes small cross-platform shims:
*   - String:   STRTOK, STRCPY
*   - Files:    FOPEN, MAKE_DIR, list_files (in platform.c)
*   - Time:     LOCALTIME; platform_ticks, the monotonic stopwatch clock (in platform.c)
*   - Paths:    PATHSEP, PATH_MAX (fallback on POSIX if undefined)
*
* Notes:
*   - list_files returns the *.txt files of a folder, sorted the same way on
*     every OS (ASCII case-insensitive, then byte order).
*   - Randomness lives in rng.h (seeded PCG32), not here.
* -------------------------------------------------------------------- */

/* Update log
* --------------------------------------------------------------------
* Version 1.0.0 - 26-10-2025 by Niko Ruohonen
* - Initial release.
* --------------------------------------------------------------------
* Update 1.1...
* -...
*/

#ifndef PLATFORM_H
#define PLATFORM_H
#pragma once

/* --------------------------------------------------------------------
* Feature macros - must precede any system header on POSIX
* -------------------------------------------------------------------- */
#if !defined(_MSC_VER)
#  ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#  endif
/* MinGW needs POSIX visibility in C11 mode */
#  if defined(__MINGW32__) || defined(__MINGW64__)
#    ifndef _POSIX_C_SOURCE
#      define _POSIX_C_SOURCE 200809L
#    endif
#  endif
#endif

/* --------------------------------------------------------------------
* Common includes
* -------------------------------------------------------------------- */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>   /* needed for snprintf/fprintf used in this header */

/* --------------------------------------------------------------------
* Macro reference (public API)
* --------------------------------------------------------------------
* STRTOK(str,delim,ctx)  Thread-safe tokenizer (Windows: strtok_s, POSIX: strtok_r).
* STRCPY(dest,dstsz,src) Requires non-overlap, positive capacity and room for src; undersized behavior differs by CRT.
* FOPEN(fp,name,mode)    Open file; assigns to fp; returns 0 on success or errno on failure.
* MAKE_DIR(path)         Create directory; POSIX mode 0755. Check errno == EEXIST on failure.
* LOCALTIME(out,timep)   Writes local time on success; return types differ, and MinGW thread-safety is CRT-dependent.
* PATHSEP                Directory separator ('\\' on Windows, '/' on POSIX).
* SKM_HOT_INLINE         Inline hot helpers (forced for MSVC).
* -------------------------------------------------------------------- */

/* ================================ WINDOWS ============================ */
#ifdef _MSC_VER

# define SKM_HOT_INLINE __forceinline

/* "Secure" CRT wrappers */
# define STRTOK(str, delim, ctx)   strtok_s((str), (delim), (ctx))
# define STRCPY(dest, dstsz, src)  strcpy_s((dest), (dstsz), (src))
/**
 * @brief Open a file with errno-return semantics.
 *
 * Assigns the resulting FILE* to the provided lvalue and returns 0 on success;
 * on failure, leaves the lvalue NULL and returns errno.
 *
 * @param fp   FILE* lvalue to receive the opened stream.
 * @param name Path to the file.
 * @param mode fopen mode string, e.g. "rb".
 * @return int 0 on success, or errno on failure.
 */
# define FOPEN(fp, name, mode)    ((((fp) = fopen((name), (mode))) == NULL) ? errno : 0)

/* Filesystem */
# include <direct.h>
# include <sys/stat.h>  /* stat() in fileExists (util.c) */
# define MAKE_DIR(path)           _mkdir(path)

/* CRT */
# include <stdlib.h>
# include <errno.h>     /* errno used in FOPEN() */
# include <time.h>      /* localtime_s used via LOCALTIME() */

/* PATH_MAX */
# include <limits.h> /* pulls in _MAX_PATH */
# ifndef PATH_MAX
#   define PATH_MAX _MAX_PATH
# endif

/* Misc */
# define LOCALTIME(out, timep)  localtime_s((out), (timep))
# define PATHSEP '\\'

/* ================================ POSIX ============================= */
#else  /* gcc / clang */

# define SKM_HOT_INLINE inline

# include <errno.h>
# include <limits.h>
# include <stdint.h>
# include <stdio.h>
# include <stdlib.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <time.h>
# include <unistd.h>

/* Provide a fallback when PATH_MAX is not defined */
# ifndef PATH_MAX
#   define PATH_MAX 4096
# endif

/* Ensure strdup is declared (may not be visible with strict C11) */
#if defined(__MINGW32__) || defined(__MINGW64__)
    extern char* strdup(const char*);
    
    /* MinGW doesn't provide strtok_r, so we implement it */
    static inline char* _mingw_strtok_r(char* str, const char* delim, char** saveptr)
    {
        char* token;
        if (str == NULL) str = *saveptr;
        
        /* Skip leading delimiters */
        str += strspn(str, delim);
        if (*str == '\0') {
            *saveptr = str;
            return NULL;
        }
        
        /* Find end of token */
        token = str;
        str = strpbrk(token, delim);
        if (str == NULL) {
            /* No more delimiters; point saveptr to end */
            *saveptr = token + strlen(token);
        } else {
            /* Terminate token and update saveptr */
            *str = '\0';
            *saveptr = str + 1;
        }
        return token;
    }
#endif

/* Safe wrappers */
#if defined(__MINGW32__) || defined(__MINGW64__)
/* MinGW-specific implementations (GCC/Clang on Windows) */
# define STRTOK(str, delim, ctx) _mingw_strtok_r((str), (delim), (ctx))
# define MAKE_DIR(path)     mkdir(path)

/* MinGW localtime_r wrapper */
static inline struct tm* _mingw_localtime_r(const time_t* timep, struct tm* result)
{
    struct tm* tmp = localtime(timep);
    if (tmp) *result = *tmp;
    return tmp;
}
# define LOCALTIME(out, timep) _mingw_localtime_r((timep), (out))
#else
/* Standard POSIX */
# define STRTOK(str, delim, ctx) strtok_r((str), (delim), (ctx))
# define MAKE_DIR(path)     mkdir((path), 0755)
# define LOCALTIME(out, timep) localtime_r((timep), (out))
#endif

# define STRCPY(dest, dstsz, src)                                       \
    do {                                                                \
        strncpy((dest), (src), (dstsz));                                \
        (dest)[((dstsz) > 0 ? (dstsz) - 1 : 0)] = '\0';                \
    } while (0)

# define FOPEN(fp, name, mode) ((((fp) = fopen((name), (mode))) == NULL) ? errno : 0)

# define PATHSEP '/'

#endif /* _MSC_VER */

/**
 * @brief Lists the *.txt files (not folders) in `dir`, case-folded in the current C locale, then by byte order.
 *
 * Returns the count; `*out` receives a malloc'd array of malloc'd names, or NULL when there are none
 * or the folder cannot be opened. The caller frees each name and the array. Exits on allocation failure;
 * enumeration and stat failures can leave a partial list without an error.
 */
size_t list_files(const char* dir, char*** out);

/**
 * @brief Lists the subfolders of `dir` (without `.` and `..`), sorted like list_files.
 *
 * Returns the count with the same ownership, ordering and partial-error limitations as list_files.
 */
size_t list_dirs(const char* dir, char*** out);

/* Creates a directory and missing parents; accepts EEXIST without checking type, else returns false with errno. */
bool platform_make_directories(const char* path);

/**
 * @brief Monotonic wall-clock ticks from an arbitrary origin; only differences are meaningful.
 *
 * Windows: QueryPerformanceCounter. POSIX: clock_gettime(CLOCK_MONOTONIC) in nanoseconds.
 */
uint64_t platform_ticks(void);

/** @brief Ticks per second of platform_ticks. */
uint64_t platform_ticks_per_second(void);

/** @brief Name of the clock behind platform_ticks (for run_config.txt). */
const char* platform_clock_name(void);

/** @brief Compiler name and version (for run_config.txt). */
const char* platform_compiler(void);

/** @brief Writes the OS and the number of logical processors into `buffer` (for run_config.txt; no host name, the file is published in results/). */
void platform_machine(char* buffer, size_t size);

/** @brief Writes the CPU model name into `buffer`, or "unknown" (for run_config.txt). */
void platform_cpu(char* buffer, size_t size);

#endif /* PLATFORM_H */

/* End of platform.h */
