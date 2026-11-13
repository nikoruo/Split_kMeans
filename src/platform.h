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
*   - Time:     LOCALTIME
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
#include <string.h>
#include <stdio.h>   /* needed for snprintf/fprintf used in this header */

/* --------------------------------------------------------------------
* Macro reference (public API)
* --------------------------------------------------------------------
* STRTOK(str,delim,ctx)  Thread-safe tokenizer (Windows: strtok_s, POSIX: strtok_r).
* STRCPY(dest,dstsz,src) Copy with guaranteed NUL-termination; POSIX path truncates if needed.
* FOPEN(fp,name,mode)    Open file; assigns to fp; returns 0 on success or errno on failure.
* MAKE_DIR(path)         Create directory; POSIX mode 0755. Check errno == EEXIST on failure.
* LOCALTIME(out,timep)   Thread-safe localtime variant (localtime_s/localtime_r).
* PATHSEP                Directory separator ('\\' on Windows, '/' on POSIX).
* -------------------------------------------------------------------- */

/* ================================ WINDOWS ============================ */
#ifdef _MSC_VER

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
# include <sys/stat.h>  /* stat() in main.c */
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

# include <errno.h>
# include <limits.h>
# include <stdint.h>
# include <stdio.h>
# include <stdlib.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <time.h>
# include <unistd.h>

# ifdef _REENTRANT
#   include <pthread.h>
# endif

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
 * @brief Lists the *.txt files (not folders) in `dir`, sorted ASCII case-insensitively, then by byte order.
 *
 * Returns the count; `*out` receives a malloc'd array of malloc'd names, or NULL when there are none
 * or the folder cannot be read. The caller frees each name and the array. Exits on allocation failure.
 */
size_t list_files(const char* dir, char*** out);

#endif /* PLATFORM_H */

/* End of platform.h */
