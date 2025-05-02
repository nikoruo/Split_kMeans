/* platform.h  -----------------------------------------------------------
 * Minimal portability layer: lets the same C source compile on
 *   • Windows / MSVC            (Annex-K “*_s”, rand_s, _mkdir)
 *   • Linux (glibc >= 2.25)      getrandom(2)
 *   • BSD / macOS               arc4random()
 *   • Older POSIX               random_r()
 * ---------------------------------------------------------------------- */

#include <string.h>

#ifndef PLATFORM_H
#define PLATFORM_H
#pragma once

 /* --------------------------------------------------------------------- */
 /* Feature macros – must precede *any* system header on POSIX -----------*/
#if !defined(_MSC_VER)
#  ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#  endif
#endif
/* --------------------------------------------------------------------- */

/* ------------------------------------------------------------------ */
/*  COMMON helper: qsort comparator for char*                         */
/* ------------------------------------------------------------------ */
static int cmp_charptr(const void* a, const void* b)
{
    const char* sa = *(const char* const*)a;
    const char* sb = *(const char* const*)b;
#if defined(_MSC_VER)
    return _stricmp(sa, sb); // WIN
#else
    return strcmp(sa, sb); // POSIX
#endif
}

/* ==========================  WINDOWS / MSVC  ========================= */
#ifdef _MSC_VER
  /* secure CRT wrappers */
# define STRTOK(str,delim,ctx)   strtok_s((str),(delim),(ctx))
# define STRCPY(dest,dstsz,src)  strcpy_s((dest),(dstsz),(src))
# define FOPEN(fp,name,mode) ((((fp)=fopen((name),(mode)))==NULL) ? errno : 0 )

  /* stat / mkdir */
# include <direct.h>
# define STAT(path,buf)          _stat((path),(buf))
# define MAKE_DIR(path)          _mkdir(path)

  /* secure random */
# include <stdlib.h>
# define RANDOMIZE(rv)                                                     \
     do { if (rand_s(&(rv)) != 0) {                                         \
              fprintf(stderr,"rand_s failed\n"); exit(EXIT_FAILURE); }      \
     } while(0)

# include <limits.h>     /* pulls in _MAX_PATH via stdlib/win headers       */
# ifndef PATH_MAX
#   define PATH_MAX _MAX_PATH   /* 260 by default, matches Windows “MAX_PATH” */
# endif

  /* other MSVCisms */
# include <sal.h>
# define LOCALTIME(out,timep)  localtime_s((out),(timep))

  /* path separator */
# define PATHSEP '\\'

/* ============================  POSIX  ================================= */
#else   /* gcc / clang -------------------------------------------------- */

# include <string.h>
# include <stdio.h>
# include <errno.h>
# include <pthread.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <stdint.h>
# include <time.h>
# include <unistd.h>
# include <limits.h>
#ifndef PATH_MAX
/* Fallback if the platform does not define a hard upper bound.
   4096 is the historical Linux maximum for most filesystems.  */
#define PATH_MAX 4096
#endif
# include <stdlib.h>


/* safe tokeniser / copy / fopen */
# define STRTOK(str,delim,ctx)  strtok_r((str),(delim),(ctx))
# define STRCPY(dest,dstsz,src)                                             \
     do {                                                                   \
         strncpy((dest),(src),(dstsz));                                     \
         (dest)[(dstsz) > 0 ? (dstsz)-1 : 0] = '\0';                        \
     } while(0)
# define FOPEN(fp,name,mode) ( (((fp)=fopen((name),(mode)))==NULL) ? errno : 0 )

/* stat / mkdir */
# define STAT(path,buf)     stat((path),(buf))
# define MAKE_DIR(path)     mkdir((path),0755)

/* helper: did mkdir succeed or directory already exist? */
# define MKDIR_OK(err)      ((err)==0 || (err)==EEXIST)

/* silence MSVC-only annotations / pragmas */
# define _Analysis_assume_(expr)  ((void)0)
# define PRAGMA_MSVC(x)

/* -------- secure RANDOMIZE() --------------------------------------- */
# if   defined(__linux__)
      /* glibc 2.25+: getrandom(2) */
#   include <sys/random.h>
static inline unsigned int _rand32_getrandom(void)
{
    unsigned int v;
    ssize_t n;
    do { n = getrandom(&v, sizeof v, GRND_NONBLOCK); } while (n == -1 && errno == EINTR);
    if (n != sizeof v) { perror("getrandom"); exit(EXIT_FAILURE); }
    return v;
}
#   define RANDOMIZE(rv)   (rv) = _rand32_getrandom()

# elif defined(__OpenBSD__) || defined(__APPLE__)
      /* BSD / macOS */
#   include <stdlib.h>
#   define RANDOMIZE(rv)   (rv) = arc4random()

# else
      /* portable fallback: thread-local random_r() */
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
        fprintf(stderr, "random_r failed\n"); exit(EXIT_FAILURE);
    }
    return (unsigned int)out;
}
#   define RANDOMIZE(rv)   (rv) = _rand32_random_r()
# endif  /*random source selection */

/* path separator */
# define PATHSEP '/'

  /* thread-safe localtime */
# define LOCALTIME(out,timep)  localtime_r((timep),(out))

#endif /* _MSC_VER */

#endif /* PLATFORM_H */


/* ------------------------------------------------------------------ */
/* Simple directory listing – POSIX dirent or Windows FindFirstFile   */
/* Returns number of regular files; allocates *out with strdup’ed     */
/* basenames in lexicographic order (caller must free).               */

#ifdef _MSC_VER           /* ----------  Windows  ------------------ */
#include <windows.h>
static size_t list_files(const char *dir, char ***out)
{
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof pattern, "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    char **v = NULL;
    size_t n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        v = realloc(v, (n + 1) * sizeof *v);
        v[n++] = _strdup(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    qsort(v, n, sizeof * v, cmp_charptr);
    *out = v;
    return n;
}

#else                       /* --------- POSIX / dirent ------------ */
#include <dirent.h>
static size_t list_files(const char *dir, char ***out)
{
    DIR *d = opendir(dir);
    if (!d) return 0;

    struct dirent *e;
    char **v = NULL;
    size_t n = 0;
    while ((e = readdir(d))) {
        if (e->d_type == DT_DIR) continue;
        v = realloc(v, (n + 1) * sizeof *v);
        v[n++] = strdup(e->d_name);
    }
    closedir(d);

    qsort(v, n, sizeof * v, cmp_charptr);
    *out = v;
    return n;
}
#endif