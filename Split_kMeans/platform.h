/* platform.h ------------------------------------------------------------
 * Minimal portability layer for building the same C source on
 *   - Windows / MSVC        (Annex-K *_s, rand_s, _mkdir)
 *   - Linux (glibc >= 2.25) getrandom(2)
 *   - BSD / macOS           arc4random()
 *   - Older POSIX           random_r()
 * -------------------------------------------------------------------- */

 /* Update log
  * --------------------------------------------------------------------
  * Version 1.0 - 2025-05-02 by Niko Ruohonen
  * - Initial release.
  * - Added support for platform-specific random number generation:
  *   - Windows: rand_s
  *   - Linux: getrandom(2)
  *   - BSD/macOS: arc4random
  *   - Older POSIX: random_r
  * - Added macros for safe string operations (e.g., STRTOK, STRCPY).
  * - Added directory creation macros (MAKE_DIR).
  * - Added cross-platform file listing (list_files).
  * --------------------------------------------------------------------
  * Update 1.1...
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
#endif

  /* --------------------------------------------------------------------
   * Common includes
   * -------------------------------------------------------------------- */
#include <string.h>

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
# define FOPEN(fp, name, mode)    ((((fp) = fopen((name), (mode))) == NULL) ? errno : 0)

/* Filesystem */
# include <direct.h>
# define STAT(path, buf)          _stat((path), (buf))
# define MAKE_DIR(path)           _mkdir(path)

/* Random */
# include <stdlib.h>
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
# include <pthread.h>
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

/* Safe wrappers */
# define STRTOK(str, delim, ctx) strtok_r((str), (delim), (ctx))
# define STRCPY(dest, dstsz, src)                                       \
    do {                                                                \
        strncpy((dest), (src), (dstsz));                                \
        (dest)[((dstsz) > 0 ? (dstsz) - 1 : 0)] = '\0';                \
    } while (0)
# define FOPEN(fp, name, mode) ((((fp) = fopen((name), (mode))) == NULL) ? errno : 0)

# define STAT(path, buf)    stat((path), (buf))
# define MAKE_DIR(path)     mkdir((path), 0755)
# define MKDIR_OK(err)      ((err) == 0 || (err) == EEXIST)

/* Silence MSVC-only annotations */
# define _Analysis_assume_(expr)  ((void)0)
# define PRAGMA_MSVC(x)

/* ---------- secure RANDOMIZE() ---------- */
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

# else  /* portable random_r() fallback */
#   include <stdlib.h>
static inline unsigned int _rand32_random_r(void)
{
    static __thread struct random_data rd;
    static __thread char statebuf[64];
    static __thread int ready = 0;

    if (!ready) {
        initstate_r((unsigned)time(NULL) ^ (unsigned)getpid(),
            statebuf, sizeof statebuf, &rd);
        ready = 1;
    }

    int32_t out;
    if (random_r(&rd, &out) != 0) {
        fprintf(stderr, "random_r failed\n");
        exit(EXIT_FAILURE);
    }
    return (unsigned int)out;
}
#   define RANDOMIZE(rv)  (rv) = _rand32_random_r()
# endif /* random source */

# define PATHSEP '/'
# define LOCALTIME(out, timep) localtime_r((timep), (out))

#endif /* _MSC_VER */

#endif /* PLATFORM_H */

/* --------------------------------------------------------------------
 * Simple directory listing (outside the header guard so it can be
 * included in multiple translation units without violating ODR).
 *   - Returns the number of regular files.
 *   - Allocates *out with strdup'ed basenames in lexicographic order.
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
