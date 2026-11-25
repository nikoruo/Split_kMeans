/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"
#include "report.h"
#include "algorithms.h"
#include "experiment.h"
#include "input_identity.h"
#include "metrics.h"
#include "tracking.h"
#include "util.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief One `_runs.csv` row or one `_log.csv` row (`count` is 0 for runs). */
typedef struct
{
    size_t run; /**< 1-based. */
    size_t count;
    double sse;
    bool sseAvailable;
    size_t ci; /**< CI_NA when not available. */
    double ms;
    size_t order; /**< Position in its ReportRows (set by reportPush), so sorting keeps the file order. */
} ReportRow;

/** @brief Growing array of report rows; free `rows`. */
typedef struct
{
    ReportRow* rows;
    size_t count;
    size_t capacity;
} ReportRows;

/** @brief The runs and tracked states of one algorithm on one dataset, merged over run folders. */
typedef struct
{
    ReportRows runs; /**< `_runs.csv`. */
    ReportRows log;  /**< `_log.csv`. */
} ReportAlgorithm;

/** @brief One dataset of the report. */
typedef struct
{
    char name[256];                              /**< Folder name. */
    char display[256];                           /**< Name in the tables (--names). */
    size_t k;                                    /**< From dataset.txt. */
    DatasetInfo identity;
    char identitySource[PATH_MAX];
    ReportAlgorithm algorithms[ALGORITHM_COUNT]; /**< Indexed like algorithmOptions. */
} ReportDataset;

typedef struct ReportName
{
    struct ReportName* next;
    char* display;
    char name[];
} ReportName;

/** @brief Means over the runs of one algorithm on one dataset. */
typedef struct
{
    size_t runs;
    double sseMean;
    double sseSd;
    double ciMean;
    double successPct;
    double msMean;
    double msSd;
    bool ciAvailable; /**< False when a run has no CI. */
} ReportSummary;

/** @brief One curve point: the time, SSE and CI at a count (means over runs for the mean curves). */
typedef struct
{
    size_t count;
    double ms;
    double sse;
    bool sseAvailable;
    double ci;
    bool ciAvailable;
} CurvePoint;

/** @brief `report` options. */
typedef struct
{
    size_t curveRun; /**< Run of the RS and RKM curves (1-based). */
    bool noTime;     /**< Leave out the time columns (make check compares the rest). */
} ReportOptions;

/** @brief The seminar tables. */
typedef enum
{
    TABLE_CI,
    TABLE_SUCCESS,
    TABLE_TIME,
    TABLE_SSE
} ReportTable;

/** @brief Display name of an algorithm in the report tables, in the style of the seminar slides. */
static const char* reportAlgorithmName(Algorithm algorithm)
{
    switch (algorithm)
    {
        case ALGO_KM:
            return "KM";
        case ALGO_RKM:
            return "RKM";
        case ALGO_RS:
            return "RS";
        case ALGO_SKM_RANDOM:
            return "SKM - Random";
        case ALGO_SKM_INTRA:
            return "SKM - Intra";
        case ALGO_SKM_GLOBAL:
            return "SKM - Global";
        case ALGO_SKM_GLOBAL_BASIC:
            return "SKM - Global basic";
        case ALGO_SKM_LOCAL:
            return "SKM - Local";
        case ALGO_BISECTING:
            return "Bisecting KM";
        default:
            return "Unknown";
    }
}

static void reportPush(ReportRows* rows, const ReportRow* row)
{
    if (rows->count == rows->capacity)
    {
        const size_t grown = rows->capacity ? rows->capacity * 2 : 64;
        ReportRow* resized = realloc(rows->rows, grown * sizeof(ReportRow));
        handleMemoryError(resized);
        rows->rows = resized;
        rows->capacity = grown;
    }
    rows->rows[rows->count] = *row;
    rows->rows[rows->count].order = rows->count;
    rows->count++;
}

/** @brief Parses a number field; accepts a decimal comma (files written with --decimal-comma). */
static bool reportNumber(const char* text, double* value)
{
    char buffer[NUMBER_TEXT_SIZE];
    if (strlen(text) >= sizeof(buffer))
    {
        return false;
    }
    STRCPY(buffer, sizeof(buffer), text);
    char* comma = strchr(buffer, ',');
    if (comma != NULL)
    {
        *comma = '.';
    }
    return parseFiniteDouble(buffer, value);
}

/**
 * @brief Appends the rows of a `_runs.csv` (run;seed;sse;ci;ms) or `_log.csv` (run;count;sse;ci;ms) file to `rows`.
 *
 * Skips the header line; a missing header is read as data. RKM logs may have unavailable SSE and CI.
 * Returns false for malformed or duplicate runs.
 */
static bool reportReadCsv(const char* path, bool isLog, bool allowUnavailableSse, ReportRows* rows)
{
    FILE* file;
    if (FOPEN(file, path, "r") != 0)
    {
        handleFileError(path);
    }

    const char* header = isLog ? "run;count;sse;ci;ms" : "run;seed;sse;ci;ms";
    char line[1024];
    size_t lineNumber = 0;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        ++lineNumber;
        line[strcspn(line, "\r\n")] = '\0';
        if ((lineNumber == 1 && strcmp(line, header) == 0) || line[0] == '\0')
        {
            continue;
        }

        char* fields[5];
        size_t fieldCount = 0;
        char* field = line;
        while (field != NULL && fieldCount < 5)
        {
            fields[fieldCount++] = field;
            char* next = strchr(field, ';');
            if (next != NULL)
            {
                *next++ = '\0';
            }
            field = next;
        }

        uint64_t run = 0;
        uint64_t second = 0;
        ReportRow row = {0};
        row.ci = CI_NA;
        row.sseAvailable = true;
        bool ok = fieldCount == 5 && field == NULL && parseUint64(fields[0], &run) && run >= 1 &&
                  parseUint64(fields[1], &second) && reportNumber(fields[4], &row.ms);
        if (ok && allowUnavailableSse && strcmp(fields[2], "NA") == 0)
            row.sseAvailable = false;
        else if (ok)
            ok = reportNumber(fields[2], &row.sse);
        if (ok && strcmp(fields[3], "NA") != 0)
        {
            uint64_t ci = 0;
            ok = parseUint64(fields[3], &ci);
            row.ci = (size_t)ci;
        }
        if (!row.sseAvailable && strcmp(fields[3], "NA") != 0) ok = false;
        if (!ok)
        {
            closeInputFile(file, path);
            fprintf(stderr, "Error: %s line %zu is not a %s row\n", path, lineNumber, header);
            return false;
        }
        row.run = (size_t)run;
        row.count = isLog ? (size_t)second : 0;

        if (!isLog)
        {
            for (size_t i = 0; i < rows->count; ++i)
            {
                if (rows->rows[i].run == row.run)
                {
                    closeInputFile(file, path);
                    fprintf(stderr, "Error: run %zu appears twice (in %s, or in it and an earlier run folder)\n",
                            row.run, path);
                    return false;
                }
            }
        }
        reportPush(rows, &row);
    }
    closeInputFile(file, path);
    return true;
}

/** @brief Reads the shared header of `<directory>/run_config.txt` (the lines up to "calls:") into `header`. */
static bool reportReadHeader(const char* directory, char* header, size_t size)
{
    char path[PATH_MAX];
    joinPath(path, sizeof(path), directory, "run_config.txt");
    FILE* file = fopen(path, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Error: %s not found; report needs output folders written by version 2.0.0 or later\n", path);
        return false;
    }
    const size_t length = fread(header, 1, size - 1, file);
    header[length] = '\0';
    closeInputFile(file, path);

    // A folder written on Windows and copied (not checked out) to Linux keeps its CRLF line ends
    size_t kept = 0;
    for (size_t i = 0; i < length; ++i)
    {
        if (header[i] != '\r')
        {
            header[kept++] = header[i];
        }
    }
    header[kept] = '\0';

    char* calls = strstr(header, "\ncalls:\n");
    if (calls == NULL)
    {
        fprintf(stderr, "Error: %s has no calls: line\n", path);
        return false;
    }
    calls[strlen("\ncalls:\n")] = '\0';
    return true;
}

static bool reportReadIdentity(const char* directory, DatasetInfo* info)
{
    return readDatasetInfo(directory, info);
}

/** @brief Means and sample standard deviations of SSE and time, mean CI and success rate of `rows`. */
static ReportSummary reportSummarize(const ReportRow* rows, size_t count)
{
    ReportSummary summary;
    memset(&summary, 0, sizeof(summary));
    summary.runs = count;
    summary.ciAvailable = count > 0;
    if (count == 0)
    {
        return summary;
    }

    const double sseOrigin = rows[0].sse;
    const double msOrigin = rows[0].ms;
    double sseOffset = 0.0;
    double msOffset = 0.0;
    size_t successes = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const double n = (double)(i + 1);
        sseOffset += ((rows[i].sse - sseOrigin) - sseOffset) / n;
        msOffset += ((rows[i].ms - msOrigin) - msOffset) / n;
        if (rows[i].ci == CI_NA)
        {
            summary.ciAvailable = false;
        }
        else
        {
            summary.ciMean += (double)rows[i].ci;
            successes += rows[i].ci == 0;
        }
    }
    summary.sseMean = sseOrigin + sseOffset;
    summary.msMean = msOrigin + msOffset;
    summary.ciMean /= (double)count;
    summary.successPct = 100.0 * (double)successes / (double)count;

    if (count > 1)
    {
        const double divisor = sqrt((double)(count - 1));
        for (size_t i = 0; i < count; ++i)
        {
            summary.sseSd = hypot(summary.sseSd, ((rows[i].sse - sseOrigin) - sseOffset) / divisor);
            summary.msSd = hypot(summary.msSd, ((rows[i].ms - msOrigin) - msOffset) / divisor);
        }
    }
    if (!isfinite(summary.sseMean) || !isfinite(summary.msMean) || !isfinite(summary.sseSd) ||
        !isfinite(summary.msSd))
    {
        fprintf(stderr, "Error: report statistics exceed the numeric range\n");
        exit(EXIT_FAILURE);
    }
    return summary;
}

/** @brief Order of `_log.csv` rows: by run, then count, then file order (qsort). */
static int compareReportRows(const void* a, const void* b)
{
    const ReportRow* x = a;
    const ReportRow* y = b;
    if (x->run != y->run)
    {
        return (x->run > y->run) - (x->run < y->run);
    }
    if (x->count != y->count)
    {
        return (x->count > y->count) - (x->count < y->count);
    }
    return (x->order > y->order) - (x->order < y->order);
}

/** @brief Number of the `_log.csv` rows of `run` and, in `*first`, the index of the first; the log is sorted
 * (reportPrepareLog). */
static size_t reportRunRows(const ReportRows* log, size_t run, size_t* first)
{
    size_t i = 0;
    while (i < log->count && log->rows[i].run < run)
    {
        ++i;
    }
    *first = i;
    size_t rows = 0;
    while (i + rows < log->count && log->rows[i + rows].run == run)
    {
        ++rows;
    }
    return rows;
}

/**
 * @brief Sorts and checks `_log.csv` against `_runs.csv`; returns false on a mismatch.
 *
 * Every row must belong to a run of `_runs.csv`; every run must have rows, or none may (tracked and untracked calls
 * in one report would mix the curves); RS must have them (its results end at the last improvement of each run);
 * with rows, the RS and RKM curve run must exist. Unavailable RKM scores may only precede a finite score.
 */
static bool reportPrepareLog(const char* dataset, ReportAlgorithm* data, Algorithm algorithm, size_t curveRun)
{
    ReportRows* log = &data->log;
    const char* name = getAlgorithmName(algorithm);
    if (log->count > 1)
    {
        qsort(log->rows, log->count, sizeof(ReportRow), compareReportRows);
    }

    for (size_t i = 0; i < log->count; ++i)
    {
        if (i > 0 && log->rows[i].run == log->rows[i - 1].run)
        {
            continue;
        }
        bool found = false;
        for (size_t r = 0; r < data->runs.count && !found; ++r)
        {
            found = (data->runs.rows[r].run == log->rows[i].run);
        }
        if (!found)
        {
            fprintf(stderr, "Error: %s %s: _log.csv has rows of run %zu, which is not in _runs.csv\n", dataset, name,
                    log->rows[i].run);
            return false;
        }
    }

    if (log->count == 0)
    {
        if (algorithm == ALGO_RS)
        {
            fprintf(stderr,
                    "Error: %s RS has no _log.csv rows; its results end at the last SSE improvement of each run, "
                    "so run RS with tracking (batch mode or --track-progress)\n",
                    dataset);
            return false;
        }
        return true;
    }

    size_t first = 0;
    for (size_t r = 0; r < data->runs.count; ++r)
    {
        if (reportRunRows(log, data->runs.rows[r].run, &first) == 0)
        {
            fprintf(stderr,
                    "Error: %s %s: run %zu has no _log.csv rows but other runs have (calls with and without tracking "
                    "are mixed)\n",
                    dataset, name, data->runs.rows[r].run);
            return false;
        }
    }
    if ((algorithm == ALGO_RS || algorithm == ALGO_RKM) && reportRunRows(log, curveRun, &first) == 0)
    {
        fprintf(stderr, "Error: --curve-run %zu: %s %s has no run %zu\n", curveRun, dataset, name, curveRun);
        return false;
    }
    if (algorithm == ALGO_RKM)
    {
        bool haveSse = false;
        for (size_t i = 0; i < log->count; ++i)
        {
            if (i == 0 || log->rows[i].run != log->rows[i - 1].run) haveSse = false;
            if (log->rows[i].sseAvailable)
                haveSse = true;
            else if (haveSse)
            {
                fprintf(stderr, "Error: %s RKM: run %zu has unavailable SSE after a finite repeat\n", dataset,
                        log->rows[i].run);
                return false;
            }
            if ((i + 1 == log->count || log->rows[i + 1].run != log->rows[i].run) && !haveSse)
            {
                fprintf(stderr, "Error: %s RKM: run %zu has no finite SSE in _log.csv\n", dataset, log->rows[i].run);
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief RS values at the last SSE improvement of each run: the time of the run's last `_log.csv` row with the
 * `_runs.csv` SSE and CI (Random Swap keeps its best solution, the one found at that swap).
 *
 * The log is prepared (reportPrepareLog), so every run has rows. The caller frees the rows.
 */
static ReportRows reportRsAtLastImprovement(const ReportAlgorithm* rs)
{
    ReportRows cut = {NULL, 0, 0};
    for (size_t r = 0; r < rs->runs.count; ++r)
    {
        ReportRow value = rs->runs.rows[r];
        size_t first = 0;
        const size_t rows = reportRunRows(&rs->log, value.run, &first);
        assert(rows > 0);
        value.ms = rs->log.rows[first + rows - 1].ms;
        reportPush(&cut, &value);
    }
    return cut;
}

/** @brief Ascending order of counts (qsort). */
static int compareCounts(const void* a, const void* b)
{
    const size_t x = *(const size_t*)a;
    const size_t y = *(const size_t*)b;
    return (x > y) - (x < y);
}

/** @brief Copies a `_log.csv` or `_runs.csv` row into a curve point with `count`. */
static void reportPoint(CurvePoint* point, const ReportRow* row, size_t count)
{
    point->count = count;
    point->ms = row->ms;
    point->sse = row->sse;
    point->sseAvailable = row->sseAvailable;
    point->ciAvailable = row->ci != CI_NA;
    point->ci = point->ciAvailable ? (double)row->ci : 0.0;
}

/**
 * @brief The curve of one algorithm into `points` (capacity: the log row count); returns its length. The log is
 * prepared.
 *
 * RS and RKM: the `_log.csv` rows of run `curveRun` (RS: the initial solution and each improvement, so the curve ends
 * at the last improvement; RKM: every repeat). Other algorithms: per count, the means over all runs of the state at
 * that count; a run that has ended keeps its last row (KM runs stop at different iterations).
 */
static size_t reportCurve(const ReportAlgorithm* data, Algorithm algorithm, size_t curveRun, CurvePoint* points)
{
    const ReportRows* log = &data->log;
    size_t length = 0;
    if (algorithm == ALGO_RS || algorithm == ALGO_RKM)
    {
        size_t first = 0;
        const size_t rows = reportRunRows(log, curveRun, &first);
        for (size_t i = 0; i < rows; ++i)
        {
            const ReportRow* row = &log->rows[first + i];
            reportPoint(&points[length++], row, row->count);
        }
        return length;
    }

    if (log->count == 0)
    {
        return 0;
    }

    // The rows of each run: [start, end) of the log, which is sorted by run and count; cursor = the run's state so far
    size_t* start = malloc(log->count * sizeof(size_t));
    size_t* end = malloc(log->count * sizeof(size_t));
    size_t* cursor = malloc(log->count * sizeof(size_t));
    size_t* counts = malloc(log->count * sizeof(size_t));
    handleMemoryError(start);
    handleMemoryError(end);
    handleMemoryError(cursor);
    handleMemoryError(counts);
    size_t runCount = 0;
    for (size_t i = 0; i < log->count; ++i)
    {
        if (i == 0 || log->rows[i].run != log->rows[i - 1].run)
        {
            start[runCount] = i;
            cursor[runCount] = i;
            ++runCount;
        }
        end[runCount - 1] = i + 1;
        counts[i] = log->rows[i].count;
    }
    qsort(counts, log->count, sizeof(size_t), compareCounts);

    for (size_t i = 0; i < log->count; ++i)
    {
        if (i > 0 && counts[i] == counts[i - 1])
        {
            continue;
        }
        CurvePoint* point = &points[length++];
        point->count = counts[i];
        point->ms = 0.0;
        point->sse = 0.0;
        point->sseAvailable = true;
        point->ci = 0.0;
        point->ciAvailable = true;
        size_t members = 0;
        for (size_t s = 0; s < runCount; ++s)
        {
            while (cursor[s] + 1 < end[s] && log->rows[cursor[s] + 1].count <= counts[i])
            {
                ++cursor[s];
            }
            const ReportRow* row = &log->rows[cursor[s]];
            if (row->count > counts[i])
            {
                continue; // the run starts at a later count
            }
            ++members;
            point->ms += row->ms;
            point->sse += row->sse;
            if (row->ci == CI_NA)
            {
                point->ciAvailable = false;
            }
            else
            {
                point->ci += (double)row->ci;
            }
        }
        assert(members > 0);
        point->ms /= (double)members;
        point->sse /= (double)members;
        point->ci /= (double)members;
    }
    free(start);
    free(end);
    free(cursor);
    free(counts);
    return length;
}

/** @brief Writes `text` to `file`, then `;`, or a line end when `last`. */
static void reportCell(FILE* file, const char* path, const char* text, bool last)
{
    checkFileWrite(file, path, fputs(text, file));
    checkFileWrite(file, path, fputc(last ? '\n' : ';', file));
}

/** @brief Opens `<directory>/<name>` for writing; exits on failure. */
static FILE* reportOpen(const char* directory, const char* name, char path[PATH_MAX])
{
    joinPath(path, PATH_MAX, directory, name);
    FILE* file;
    if (FOPEN(file, path, "w") != 0)
    {
        handleFileError(path);
    }
    return file;
}

/** @brief Writes `curves_<dataset>.csv`: per algorithm count, time, scaled SSE and CI; time is omitted with noTime.
 * The first data row is `end`, followed by the curve points. */
static void reportWriteCurves(const char* outputDirectory, const ReportDataset* dataset, const ReportSummary* summaries,
                              const ReportOptions* options)
{
    CurvePoint* curves[ALGORITHM_COUNT];
    size_t lengths[ALGORITHM_COUNT];
    size_t longest = 0;
    size_t lastAlgorithm = 0;
    double smallest = DBL_MAX;
    for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
    {
        const ReportAlgorithm* data = &dataset->algorithms[a];
        curves[a] = NULL;
        lengths[a] = 0;
        if (data->runs.count == 0)
        {
            continue;
        }
        lastAlgorithm = a;
        curves[a] = malloc((data->log.count + 1) * sizeof(CurvePoint));
        handleMemoryError(curves[a]);
        lengths[a] = reportCurve(data, algorithmOptions[a].id, options->curveRun, curves[a]);
        if (lengths[a] > longest)
        {
            longest = lengths[a];
        }
        if (summaries[a].sseMean > 0.0 && summaries[a].sseMean < smallest)
        {
            smallest = summaries[a].sseMean;
        }
        for (size_t i = 0; i < lengths[a]; ++i)
        {
            if (curves[a][i].sseAvailable && curves[a][i].sse > 0.0 && curves[a][i].sse < smallest)
            {
                smallest = curves[a][i].sse;
            }
        }
    }

    // SSE in units of 10^exponent, as the v1 graph axes "SSE (10^10)"
    const int exponent = smallest < DBL_MAX ? (int)floor(log10(smallest)) : 0;
    const double scale = pow(10.0, (double)exponent);

    char name[PATH_MAX];
    if (snprintf(name, sizeof(name), "curves_%s.csv", dataset->name) >= (int)sizeof(name))
    {
        fprintf(stderr, "Error: dataset name too long: %s\n", dataset->name);
        exit(EXIT_FAILURE);
    }
    char path[PATH_MAX];
    FILE* file = reportOpen(outputDirectory, name, path);

    char text[NUMBER_TEXT_SIZE + 64];
    for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
    {
        if (dataset->algorithms[a].runs.count == 0) continue;
        const char* display = reportAlgorithmName(algorithmOptions[a].id);
        snprintf(text, sizeof(text), "%s count", display);
        reportCell(file, path, text, false);
        if (!options->noTime)
        {
            snprintf(text, sizeof(text), "%s time (ms)", display);
            reportCell(file, path, text, false);
        }
        snprintf(text, sizeof(text), "%s SSE (10^%d)", display, exponent);
        reportCell(file, path, text, false);
        snprintf(text, sizeof(text), "%s CI", display);
        reportCell(file, path, text, a == lastAlgorithm);
    }

    // The end point: the means from _runs.csv (RS at the last improvement), the same values as the tables
    for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
    {
        if (dataset->algorithms[a].runs.count == 0) continue;
        reportCell(file, path, "end", false);
        if (!options->noTime)
        {
            reportCell(file, path, formatFixed(summaries[a].msMean, 3, text), false);
        }
        reportCell(file, path, formatFixed(summaries[a].sseMean / scale, 6, text), false);
        reportCell(file, path, summaries[a].ciAvailable ? formatFixed(summaries[a].ciMean, 4, text) : "NA",
                   a == lastAlgorithm);
    }

    for (size_t i = 0; i < longest; ++i)
    {
        for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
        {
            if (dataset->algorithms[a].runs.count == 0) continue;
            const bool last = (a == lastAlgorithm);
            if (i >= lengths[a])
            {
                reportCell(file, path, "", false);
                if (!options->noTime) reportCell(file, path, "", false);
                reportCell(file, path, "", false);
                reportCell(file, path, "", last);
                continue;
            }
            const CurvePoint* point = &curves[a][i];
            snprintf(text, sizeof(text), "%zu", point->count);
            reportCell(file, path, text, false);
            if (!options->noTime)
            {
                reportCell(file, path, formatFixed(point->ms, 3, text), false);
            }
            reportCell(file, path, point->sseAvailable ? formatFixed(point->sse / scale, 6, text) : "NA", false);
            reportCell(file, path, point->ciAvailable ? formatFixed(point->ci, 4, text) : "NA", last);
        }
    }
    closeOutputFile(file, path);

    for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
    {
        free(curves[a]);
    }
}

/** @brief Writes one seminar table (algorithms × datasets in `order`) to `<outputDirectory>/<name>`. */
static void reportWriteTable(const char* outputDirectory, const char* name, ReportTable table,
                             const ReportDataset* datasets, const size_t* order, size_t datasetCount,
                             const ReportSummary* summaries)
{
    char path[PATH_MAX];
    FILE* file = reportOpen(outputDirectory, name, path);
    reportCell(file, path, "Algorithm", datasetCount == 0);
    for (size_t d = 0; d < datasetCount; ++d)
    {
        reportCell(file, path, datasets[order[d]].display, d + 1 == datasetCount);
    }

    char text[NUMBER_TEXT_SIZE];
    for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
    {
        bool present = false;
        for (size_t d = 0; d < datasetCount; ++d)
        {
            present = present || summaries[order[d] * ALGORITHM_COUNT + a].runs > 0;
        }
        if (!present) continue;

        reportCell(file, path, reportAlgorithmName(algorithmOptions[a].id), datasetCount == 0);
        for (size_t d = 0; d < datasetCount; ++d)
        {
            const ReportSummary* summary = &summaries[order[d] * ALGORITHM_COUNT + a];
            const char* cell = "";
            if (summary->runs > 0)
            {
                switch (table)
                {
                    case TABLE_CI:
                        cell = summary->ciAvailable ? formatFixed(summary->ciMean, 2, text) : "NA";
                        break;
                    case TABLE_SUCCESS:
                        cell = summary->ciAvailable ? formatFixed(summary->successPct, 1, text) : "NA";
                        break;
                    case TABLE_TIME:
                        cell = formatFixed(summary->msMean, 2, text);
                        break;
                    default:
                        cell = formatFixed(summary->sseMean, 2, text);
                        break;
                }
            }
            reportCell(file, path, cell, d + 1 == datasetCount);
        }
    }
    closeOutputFile(file, path);
}

/**
 * @brief Writes the summary.csv row of one algorithm on one dataset; `fullMs` (RS only, else NULL) is the mean time of
 * the whole runs, every swap included. The standard deviations are NA with fewer than 2 runs.
 */
static void reportWriteSummaryRow(FILE* file, const char* path, const ReportDataset* dataset, Algorithm algorithm,
                                  const ReportSummary* summary, const double* fullMs, bool noTime)
{
    char text[NUMBER_TEXT_SIZE];
    const bool sd = summary->runs >= 2;
    checkFileWrite(file, path,
                   fprintf(file, "%s;%s;%zu", dataset->display, reportAlgorithmName(algorithm), summary->runs));
    checkFileWrite(file, path, fprintf(file, ";%s", formatFixed(summary->sseMean, 6, text)));
    checkFileWrite(file, path, fprintf(file, ";%s", sd ? formatFixed(summary->sseSd, 6, text) : "NA"));
    checkFileWrite(file, path,
                   fprintf(file, ";%s", summary->ciAvailable ? formatFixed(summary->ciMean, 4, text) : "NA"));
    checkFileWrite(file, path,
                   fprintf(file, ";%s",
                           summary->ciAvailable && dataset->k > 0
                               ? formatFixed(summary->ciMean / (double)dataset->k, 4, text)
                               : "NA"));
    checkFileWrite(file, path,
                   fprintf(file, ";%s", summary->ciAvailable ? formatFixed(summary->successPct, 1, text) : "NA"));
    if (!noTime)
    {
        checkFileWrite(file, path, fprintf(file, ";%s", formatFixed(summary->msMean, 3, text)));
        checkFileWrite(file, path, fprintf(file, ";%s", sd ? formatFixed(summary->msSd, 3, text) : "NA"));
        checkFileWrite(file, path, fprintf(file, ";%s", fullMs != NULL ? formatFixed(*fullMs, 3, text) : ""));
    }
    checkFileWrite(file, path, fputc('\n', file));
}

static void reportFreeDatasets(ReportDataset* datasets, size_t count)
{
    for (size_t d = 0; d < count; ++d)
    {
        for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
        {
            free(datasets[d].algorithms[a].runs.rows);
            free(datasets[d].algorithms[a].log.rows);
        }
    }
    free(datasets);
}

/** @brief Reads run folders into `*datasets`; the caller owns the array and its row arrays. */
static bool reportCollect(const char* const* directories, size_t directoryCount, ReportDataset** datasets,
                          size_t* count, bool* identityMissing)
{
    size_t datasetCount = 0;
    *count = 0;
    for (size_t f = 0; f < directoryCount; ++f)
    {
        char** names = NULL;
        const size_t nameCount = list_dirs(directories[f], &names);
        for (size_t n = 0; n < nameCount; ++n)
        {
            char folder[PATH_MAX];
            joinPath(folder, sizeof(folder), directories[f], names[n]);

            size_t d = 0;
            while (d < datasetCount && strcmp((*datasets)[d].name, names[n]) != 0)
            {
                ++d;
            }
            bool identityRead = false;
            for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
            {
                char path[PATH_MAX];
                algorithmFilePath(algorithmOptions[a].id, folder, "_runs.csv", path, sizeof(path));
                if (!fileExists(path))
                {
                    continue;
                }
                if (!identityRead)
                {
                    DatasetInfo identity;
                    if (!validateCsvName(names[n], "dataset name") || !reportReadIdentity(folder, &identity))
                    {
                        freeStringList(names, nameCount);
                        reportFreeDatasets(*datasets, datasetCount);
                        *datasets = NULL;
                        return false;
                    }
                    if (!identity.hasFingerprints)
                    {
                        if (directoryCount != 1)
                        {
                            fprintf(stderr, "Error: %s has no input fingerprints; multi-folder reports require them\n",
                                    folder);
                            freeStringList(names, nameCount);
                            reportFreeDatasets(*datasets, datasetCount);
                            *datasets = NULL;
                            return false;
                        }
                        if (!*identityMissing)
                            fprintf(stderr,
                                    "Warning: %s has no input fingerprints; historical identity is not recorded\n",
                                    folder);
                        *identityMissing = true;
                    }
                    if (d == datasetCount)
                    {
                        if (strlen(names[n]) >= sizeof((*datasets)[0].name))
                        {
                            fprintf(stderr, "Error: dataset name too long: %s\n", names[n]);
                            exit(EXIT_FAILURE);
                        }
                        ReportDataset* grown = realloc(*datasets, (datasetCount + 1) * sizeof(ReportDataset));
                        handleMemoryError(grown);
                        *datasets = grown;
                        memset(&grown[d], 0, sizeof(ReportDataset));
                        STRCPY(grown[d].name, sizeof(grown[d].name), names[n]);
                        STRCPY(grown[d].display, sizeof(grown[d].display), names[n]);
                        grown[d].k = identity.k;
                        grown[d].identity = identity;
                        STRCPY(grown[d].identitySource, sizeof(grown[d].identitySource), folder);
                        ++datasetCount;
                    }
                    else if (!compareDatasetInfo(&(*datasets)[d].identity, &identity,
                                                 (*datasets)[d].identitySource, folder))
                    {
                        freeStringList(names, nameCount);
                        reportFreeDatasets(*datasets, datasetCount);
                        *datasets = NULL;
                        return false;
                    }
                    identityRead = true;
                }
                ReportDataset* dataset = &(*datasets)[d];
                if (!reportReadCsv(path, false, false, &dataset->algorithms[a].runs))
                {
                    freeStringList(names, nameCount);
                    reportFreeDatasets(*datasets, datasetCount);
                    *datasets = NULL;
                    return false;
                }
                algorithmFilePath(algorithmOptions[a].id, folder, "_log.csv", path, sizeof(path));
                if (fileExists(path))
                {
                    if (!reportReadCsv(path, true, algorithmOptions[a].id == ALGO_RKM, &dataset->algorithms[a].log))
                    {
                        freeStringList(names, nameCount);
                        reportFreeDatasets(*datasets, datasetCount);
                        *datasets = NULL;
                        return false;
                    }
                }
            }
        }
        freeStringList(names, nameCount);
    }
    *count = datasetCount;
    return true;
}

static bool reportParseNameLine(char* line, char** name, char** display)
{
    size_t length = strlen(line);
    if (length > 0 && line[length - 1] == '\n')
    {
        line[--length] = '\0';
        if (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
    }
    *name = line + strspn(line, " \t");
    if (**name == '\0') return false;

    char* rest = *name + strcspn(*name, " \t");
    if (*rest != '\0')
    {
        *rest++ = '\0';
        rest += strspn(rest, " \t");
    }
    *display = rest;
    return true;
}

static void reportFreeNames(ReportName* names)
{
    while (names != NULL)
    {
        ReportName* next = names->next;
        free(names);
        names = next;
    }
}

static bool reportReadNamesFile(const char* namesFile, ReportName** names)
{
    *names = NULL;
    if (namesFile == NULL) return true;
    FILE* file;
    /* Preserve embedded CR bytes before recognizing a CRLF line ending. */
    if (FOPEN(file, namesFile, "rb") != 0) handleFileError(namesFile);
    char line[LINE_BUFSZ];
    ReportName** tail = names;
    for (size_t lineNumber = 1; readDataLine(line, sizeof(line), file, namesFile, lineNumber) != NULL; ++lineNumber)
    {
        char* name;
        char* display;
        if (!reportParseNameLine(line, &name, &display)) continue;
        if (!validateCsvName(name, "dataset name") || !validateCsvName(display, "display name"))
        {
            closeInputFile(file, namesFile);
            reportFreeNames(*names);
            *names = NULL;
            return false;
        }
        const size_t nameSize = strlen(name) + 1;
        const size_t displaySize = strlen(display) + 1;
        ReportName* entry = malloc(sizeof(*entry) + nameSize + displaySize);
        if (entry == NULL)
        {
            closeInputFile(file, namesFile);
            reportFreeNames(*names);
            *names = NULL;
            fprintf(stderr, "Error: Unable to allocate memory\n");
            return false;
        }
        entry->next = NULL;
        entry->display = entry->name + nameSize;
        memcpy(entry->name, name, nameSize);
        memcpy(entry->display, display, displaySize);
        *tail = entry;
        tail = &entry->next;
    }
    closeInputFile(file, namesFile);
    return true;
}

/**
 * @brief Dataset order and display names: the names-file entries first, the rest by name.
 *
 * Fills `order` (datasetCount entries). Unknown or repeated names in the file are warnings.
 */
static bool reportOrder(const ReportName* names, ReportDataset* datasets, size_t datasetCount, size_t* order)
{
    bool* placed = calloc(datasetCount, sizeof(bool));
    handleMemoryError(placed);
    size_t ordered = 0;
    for (const ReportName* entry = names; entry != NULL; entry = entry->next)
    {
        const char* name = entry->name;
        const char* rest = entry->display;
        size_t d = 0;
        while (d < datasetCount && strcmp(datasets[d].name, name) != 0)
        {
            ++d;
        }
        if (d == datasetCount || placed[d])
        {
            fprintf(stderr, "Warning: --names: %s is not a dataset of the report or is listed twice\n", name);
            continue;
        }
        if (*rest != '\0')
        {
            if (strlen(rest) >= sizeof(datasets[d].display))
            {
                fprintf(stderr, "Error: --names: the display name of %s is too long\n", name);
                free(placed);
                return false;
            }
            STRCPY(datasets[d].display, sizeof(datasets[d].display), rest);
        }
        placed[d] = true;
        order[ordered++] = d;
    }
    while (ordered < datasetCount)
    {
        size_t next = SIZE_MAX;
        for (size_t d = 0; d < datasetCount; ++d)
        {
            if (!placed[d] && (next == SIZE_MAX || strcmp(datasets[d].name, datasets[next].name) < 0))
            {
                next = d;
            }
        }
        placed[next] = true;
        order[ordered++] = next;
    }
    free(placed);
    return true;
}

/**
 * @brief `report [options] <run-folder>...`: merges run output folders into Excel-ready tables (plan step 3.9).
 *
 * The folders' run_config.txt headers must agree. Writes summary.csv, table_ci.csv, table_success.csv,
 * table_time.csv, table_sse.csv and curves_<dataset>.csv into `-o` (default `<first dir>/report`).
 * RS results end at the last SSE improvement of each run, so RS needs its `_log.csv` rows; for every algorithm,
 * either all runs have rows or none (reportPrepareLog).
 */
int commandReport(int argc, char* argv[])
{
    ReportOptions options = {1, false};
    const char* outputOption = NULL;
    const char* namesFile = NULL;
    const char* directories[64];
    size_t directoryCount = 0;

    for (int i = 0; i < argc; ++i)
    {
        if (strcmp(argv[i], "-o") == 0)
        {
            outputOption = textOption(argc, argv, &i);
        }
        else if (strcmp(argv[i], "--names") == 0)
        {
            namesFile = textOption(argc, argv, &i);
        }
        else if (strcmp(argv[i], "--curve-run") == 0)
        {
            options.curveRun = countOption(argc, argv, &i, 1);
        }
        else if (strcmp(argv[i], "--decimal-comma") == 0)
        {
            decimalComma = true;
        }
        else if (strcmp(argv[i], "--no-time") == 0)
        {
            options.noTime = true;
        }
        else if (argv[i][0] == '-' || directoryCount == sizeof(directories) / sizeof(directories[0]))
        {
            fprintf(stderr, "Error: report: unknown option or too many folders: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
        else
        {
            directories[directoryCount++] = argv[i];
        }
    }
    if (directoryCount == 0)
    {
        fprintf(stderr, "Usage: split_kmeans report [-o dir] [--names file] [--curve-run i] [--decimal-comma] "
                        "[--no-time] <run-folder>...\n");
        return EXIT_FAILURE;
    }
    if (outputOption != NULL && strlen(outputOption) >= PATH_MAX)
    {
        fprintf(stderr, "Error: -o path too long\n");
        return EXIT_FAILURE;
    }
    ReportName* names = NULL;
    if (!reportReadNamesFile(namesFile, &names)) return EXIT_FAILURE;

    // The folders must come from one configuration
    char first[RUN_CONFIG_SIZE];
    char other[RUN_CONFIG_SIZE];
    if (!reportReadHeader(directories[0], first, sizeof(first)))
    {
        reportFreeNames(names);
        return EXIT_FAILURE;
    }
    for (size_t f = 1; f < directoryCount; ++f)
    {
        if (!reportReadHeader(directories[f], other, sizeof(other)))
        {
            reportFreeNames(names);
            return EXIT_FAILURE;
        }
        if (strcmp(first, other) != 0)
        {
            size_t line = 0;
            char ours[1024];
            char theirs[1024];
            do
            {
                textLine(first, line, ours, sizeof(ours));
                textLine(other, line, theirs, sizeof(theirs));
                ++line;
            } while (strcmp(ours, theirs) == 0 && strcmp(ours, "(none)") != 0);
            fprintf(stderr, "Error: %s and %s come from different configurations:\n  %s\n  %s\n", directories[0],
                    directories[f], ours, theirs);
            reportFreeNames(names);
            return EXIT_FAILURE;
        }
    }

    bool identityMissing = false;
    for (size_t f = 0; f < directoryCount; ++f)
    {
        if (!checkReportDatasets(directories[f], directoryCount == 1, &identityMissing))
        {
            reportFreeNames(names);
            return EXIT_FAILURE;
        }
    }

    ReportDataset* datasets = NULL;
    size_t datasetCount = 0;
    if (!reportCollect(directories, directoryCount, &datasets, &datasetCount, &identityMissing))
    {
        reportFreeNames(names);
        return EXIT_FAILURE;
    }
    if (datasetCount == 0)
    {
        fprintf(stderr, "Error: no <Algo>_runs.csv files in the dataset folders of %s\n", directories[0]);
        reportFreeNames(names);
        return EXIT_FAILURE;
    }

    size_t* order = malloc(datasetCount * sizeof(size_t));
    handleMemoryError(order);
    if (!reportOrder(names, datasets, datasetCount, order))
    {
        reportFreeNames(names);
        reportFreeDatasets(datasets, datasetCount);
        free(order);
        return EXIT_FAILURE;
    }
    reportFreeNames(names);

    for (size_t d = 0; d < datasetCount; ++d)
    {
        for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
        {
            if (datasets[d].algorithms[a].runs.count > 0)
            {
                if (!reportPrepareLog(datasets[d].name, &datasets[d].algorithms[a], algorithmOptions[a].id,
                                      options.curveRun))
                {
                    reportFreeDatasets(datasets, datasetCount);
                    free(order);
                    return EXIT_FAILURE;
                }
            }
        }
    }

    char outputDirectory[PATH_MAX];
    if (outputOption != NULL)
    {
        STRCPY(outputDirectory, sizeof(outputDirectory), outputOption);
    }
    else
    {
        joinPath(outputDirectory, sizeof(outputDirectory), directories[0], "report");
    }
    if (!platform_make_directories(outputDirectory))
    {
        // exit, as the other report errors after reading: datasets and order are still allocated
        fprintf(stderr, "Error: Unable to create the report folder '%s': %s\n", outputDirectory, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // summary.csv, and the summaries behind the tables and the curve end points
    ReportSummary* summaries = calloc(datasetCount * ALGORITHM_COUNT, sizeof(ReportSummary));
    handleMemoryError(summaries);
    char summaryPath[PATH_MAX];
    FILE* summaryFile = reportOpen(outputDirectory, "summary.csv", summaryPath);
    checkFileWrite(summaryFile, summaryPath,
                   fputs(options.noTime ? "dataset;algorithm;runs;sse_mean;sse_sd;ci_mean;relative_ci;success_pct\n"
                                        : "dataset;algorithm;runs;sse_mean;sse_sd;ci_mean;relative_ci;success_pct;"
                                          "time_ms_mean;time_ms_sd;full_time_ms_mean\n",
                         summaryFile));
    for (size_t o = 0; o < datasetCount; ++o)
    {
        const size_t d = order[o];
        for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
        {
            const ReportAlgorithm* data = &datasets[d].algorithms[a];
            if (data->runs.count == 0) continue;

            const Algorithm algorithm = algorithmOptions[a].id;
            ReportSummary* summary = &summaries[d * ALGORITHM_COUNT + a];
            *summary = reportSummarize(data->runs.rows, data->runs.count);
            if (algorithm == ALGO_RS)
            {
                // RS results end at the last SSE improvement of each run; full_time_ms_mean keeps the time of every
                // swap
                const double fullMs = summary->msMean;
                ReportRows cut = reportRsAtLastImprovement(data);
                *summary = reportSummarize(cut.rows, cut.count);
                free(cut.rows);
                reportWriteSummaryRow(summaryFile, summaryPath, &datasets[d], algorithm, summary, &fullMs,
                                      options.noTime);
            }
            else
            {
                reportWriteSummaryRow(summaryFile, summaryPath, &datasets[d], algorithm, summary, NULL, options.noTime);
            }
        }
        reportWriteCurves(outputDirectory, &datasets[d], &summaries[d * ALGORITHM_COUNT], &options);
    }
    closeOutputFile(summaryFile, summaryPath);

    reportWriteTable(outputDirectory, "table_ci.csv", TABLE_CI, datasets, order, datasetCount, summaries);
    reportWriteTable(outputDirectory, "table_success.csv", TABLE_SUCCESS, datasets, order, datasetCount, summaries);
    reportWriteTable(outputDirectory, "table_sse.csv", TABLE_SSE, datasets, order, datasetCount, summaries);
    if (!options.noTime)
    {
        reportWriteTable(outputDirectory, "table_time.csv", TABLE_TIME, datasets, order, datasetCount, summaries);
    }

    char identityPath[PATH_MAX];
    FILE* identityFile = reportOpen(outputDirectory, "report_info.txt", identityPath);
    checkFileWrite(identityFile, identityPath,
                   fprintf(identityFile, "input identity: %s\n", identityMissing ? "not recorded" : "recorded"));
    closeOutputFile(identityFile, identityPath);

    printf("Report: %zu dataset%s from %zu folder%s -> %s\n", datasetCount, datasetCount == 1 ? "" : "s",
           directoryCount, directoryCount == 1 ? "" : "s", outputDirectory);

    reportFreeDatasets(datasets, datasetCount);
    free(order);
    free(summaries);
    return EXIT_SUCCESS;
}
