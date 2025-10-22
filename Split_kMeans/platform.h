/* SPDX-License-Identifier: AGPL-3.0-only
* Copyright (C) 2025 Niko Ruohonen and contributors
*/

/* platform.h ------------------------------------------------------------
* Minimal portability layer for building the same C/C++ source on
*   - Windows / MSVC        (Annex-K *_s, rand_s, _mkdir)
*   - Linux (glibc >= 2.25) getrandom(2)
*   - BSD / macOS           arc4random()
*   - Fallback (others)     random_r() (GNU extension)
*
* Exposes small cross-platform shims:
*   - String:   STRTOK, STRCPY
*   - Files:    FOPEN, STAT, MAKE_DIR, MKDIR_OK
*   - Time:     LOCALTIME
*   - Paths:    PATHSEP, PATH_MAX (fallback on POSIX if undefined)
*   - RNG:      RANDOMIZE(unsigned int rv) -> fills rv with 32 bits
*   - Utils:    cmp_charptr (qsort comparator for const char* arrays)
*
* Notes:
*   - cmp_charptr is case-insensitive on Windows (_stricmp) and
*     case-sensitive on POSIX (strcmp).
*   - RANDOMIZE uses system RNG where available; the random_r fallback
*     uses thread-local state (GNU extension).
* -------------------------------------------------------------------- */

/* Update log
* --------------------------------------------------------------------
* Version 1.0.0 - 22-10-2025 by Niko Ruohonen
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
* STAT(path,buf)         Wrapper for stat/_stat; see platform headers for struct differences.
* MAKE_DIR(path)         Create directory; POSIX mode 0755. Use MKDIR_OK(errno) to test existence.
* MKDIR_OK(err)          True if mkdir succeeded or already existed (POSIX only).
* LOCALTIME(out,timep)   Thread-safe localtime variant (localtime_s/localtime_r).
* PATHSEP                Directory separator ('\\' on Windows, '/' on POSIX).
* RANDOMIZE(rv)          Fill unsigned int lvalue with 32 bits of system randomness; exits on failure.
* -------------------------------------------------------------------- */

/* --------------------------------------------------------------------
* Helper: qsort comparator for (const char *) arrays.
*   Windows: case-insensitive via _stricmp
*   POSIX  : case-sensitive via strcmp
* -------------------------------------------------------------------- */
static int cmp_charptr(const void* a, const void* b)
{
    const char* sa = *(const char* const*)a;
    const char* sb = *(const char* const*)b;

#if defined(_MSC_VER)
    return _stricmp(sa, sb); /* Windows */
#else
    return strcmp(sa, sb);   /* POSIX  */
#endif
}

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
# include <sys/stat.h>  /* _stat used via STAT() */
# define STAT(path, buf)          _stat((path), (buf))
# define MAKE_DIR(path)           _mkdir(path)

/* Random */
# include <stdlib.h>
# include <errno.h>     /* errno used in FOPEN() */
# include <time.h>      /* localtime_s used via LOCALTIME() */
/**
 * @brief Fill a 32-bit unsigned lvalue with system randomness.
 *
 * Uses rand_s on Windows. On failure, prints an error to stderr and exits
 * the process with EXIT_FAILURE. Thread-safe.
 *
 * @param rv unsigned int lvalue that receives 32 random bits.
 */
# define RANDOMIZE(rv)                                                  \
    do { if (rand_s(&(rv)) != 0) {                                       \
             fprintf(stderr, "rand_s failed\n");                        \
             exit(EXIT_FAILURE);                                         \
         } } while (0)

/* PATH_MAX */
# include <limits.h> /* pulls in _MAX_PATH */
# ifndef PATH_MAX
#   define PATH_MAX _MAX_PATH
# endif

/* Misc */
# include <sal.h>
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
# include <windows.h>  /* Needed for rand_s and mkdir fallback */

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

/* MinGW RNG: Use rand_s() to match MSVC quality and Linux getrandom() */
static inline unsigned int _mingw_rand32(void)
{
    unsigned int v;
    if (rand_s(&v) != 0) {
        fprintf(stderr, "rand_s failed on MinGW\n");
        exit(EXIT_FAILURE);
    }
    return v;
}

# define RANDOMIZE(rv)  (rv) = _mingw_rand32()
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
# define STAT(path, buf)    stat((path), (buf))
# define MKDIR_OK(err)      ((err) == 0 || (err) == EEXIST)

/* Silence MSVC-only annotations */
# define _Analysis_assume_(expr)  ((void)0)
# define PRAGMA_MSVC(x)

/* Platform-specific RNG (MinGW handled above) */
#if !defined(__MINGW32__) && !defined(__MINGW64__)
# if defined(__linux__)
#   include <sys/random.h>
static inline unsigned int _rand32_getrandom(void)
{
    unsigned int v;
    ssize_t n;
    do {
        n = getrandom(&v, sizeof v, GRND_NONBLOCK);
    } while (n == -1 && errno == EINTR);

    if (n != sizeof v) { perror("getrandom"); exit(EXIT_FAILURE); }
    return v;
}
#   define RANDOMIZE(rv)  (rv) = _rand32_getrandom()
# elif defined(__OpenBSD__) || defined(__APPLE__)
#   include <stdlib.h>
#   define RANDOMIZE(rv)  (rv) = arc4random()
# else
/* Fallback for other systems */
#   define RANDOMIZE(rv) do { srand((unsigned)time(NULL)); (rv) = (unsigned)rand() | ((unsigned)rand() << 16); } while(0)
# endif
#endif

# define PATHSEP '/'

#endif /* _MSC_VER */

#endif /* PLATFORM_H */

/* --------------------------------------------------------------------
 * Simple directory listing (outside the header guard).
 *   - Returns the number of entries that are not directories.
 *   - Allocates *out with strdup'ed basenames, sorted:
 *       - Windows: case-insensitive order
 *       - POSIX  : case-sensitive order
 *   - Notes (POSIX): relies on d_type where available; entries with
 *     DT_UNKNOWN are treated as non-directories and included. Symlinks
 *     and special files may be included.
 *   - Ownership: caller owns *out and each string; free all on completion.
 * -------------------------------------------------------------------- */

#ifdef _MSC_VER /* ------------------------- Windows ----------------------- */
# include <windows.h>
static size_t list_files(const char* dir, char*** out)
{
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof pattern, "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    char** v = NULL;
    size_t n = 0;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        v = realloc(v, (n + 1) * sizeof * v);
        v[n++] = _strdup(fd.cFileName);
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    qsort(v, n, sizeof * v, cmp_charptr);
    *out = v;
    return n;
}

#else /* -------------------------- POSIX ------------------------------ */
# include <dirent.h>
static size_t list_files(const char* dir, char*** out)
{
    DIR* d = opendir(dir);
    if (!d) return 0;

    struct dirent* e;
    char** v = NULL;
    size_t n = 0;

    while ((e = readdir(d))) {
        if (e->d_type == DT_DIR) continue;
        v = realloc(v, (n + 1) * sizeof * v);
        v[n++] = strdup(e->d_name);
    }

    closedir(d);

    qsort(v, n, sizeof * v, cmp_charptr);
    *out = v;
    return n;
}
#endif /* list_files */