/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#define _CRT_SECURE_NO_WARNINGS
#include "platform.h"
#include "report.h"
#include "algorithms.h"
#include "experiment.h"
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
    size_t k;                                    /**< From dataset.txt; 0 when unknown. */
    ReportAlgorithm algorithms[ALGORITHM_COUNT]; /**< Indexed like algorithmOptions. */
} ReportDataset;

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
 * Skips the header line (a first line that is not the header is read as data: a file emptied by an interrupted
 * call gets no header on the next append); exits on a malformed line. For `_runs.csv`, a run number that is
 * already in `rows` (the same run twice, in one or more run folders) is an error.
 */
static void reportReadCsv(const char* path, bool isLog, ReportRows* rows)
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
        ReportRow row;
        row.ci = CI_NA;
        bool ok = fieldCount == 5 && field == NULL && parseUint64(fields[0], &run) && run >= 1 &&
                  parseUint64(fields[1], &second) && reportNumber(fields[2], &row.sse) &&
                  reportNumber(fields[4], &row.ms);
        if (ok && strcmp(fields[3], "NA") != 0)
        {
            uint64_t ci = 0;
            ok = parseUint64(fields[3], &ci);
            row.ci = (size_t)ci;
        }
        if (!ok)
        {
            fclose(file);
            fprintf(stderr, "Error: %s line %zu is not a %s row\n", path, lineNumber, header);
            exit(EXIT_FAILURE);
        }
        row.run = (size_t)run;
        row.count = isLog ? (size_t)second : 0;

        if (!isLog)
        {
            for (size_t i = 0; i < rows->count; ++i)
            {
                if (rows->rows[i].run == row.run)
                {
                    fclose(file);
                    fprintf(stderr, "Error: run %zu appears twice (in %s, or in it and an earlier run folder)\n",
                            row.run, path);
                    exit(EXIT_FAILURE);
                }
            }
        }
        reportPush(rows, &row);
    }
    fclose(file);
}

/** @brief Reads the shared header of `<directory>/run_config.txt` (the lines up to "calls:") into `header`; exits when
 * missing. */
static void reportReadHeader(const char* directory, char* header, size_t size)
{
    char path[PATH_MAX];
    joinPath(path, sizeof(path), directory, "run_config.txt");
    FILE* file;
    if (FOPEN(file, path, "r") != 0)
    {
        fprintf(stderr, "Error: %s not found; report needs output folders written by version 2.0.0 or later\n", path);
        exit(EXIT_FAILURE);
    }
    const size_t length = fread(header, 1, size - 1, file);
    header[length] = '\0';
    fclose(file);

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
        exit(EXIT_FAILURE);
    }
    calls[strlen("\ncalls:\n")] = '\0';
}

/** @brief K from `<directory>/dataset.txt`, or 0 when the file or a valid K is missing. */
static size_t reportReadK(const char* directory)
{
    char path[PATH_MAX];
    joinPath(path, sizeof(path), directory, "dataset.txt");
    FILE* file;
    if (FOPEN(file, path, "r") != 0)
    {
        return 0;
    }
    char line[PATH_MAX + 32];
    size_t k = 0;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        uint64_t value = 0;
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "k: ", 3) == 0 && parseUint64(line + 3, &value))
        {
            k = (size_t)value;
        }
    }
    fclose(file);
    return k;
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
 * @brief Sorts the `_log.csv` rows of one algorithm by run and count and checks them against its `_runs.csv`; exits on
 * a mismatch.
 *
 * Every row must belong to a run of `_runs.csv`; every run must have rows, or none may (tracked and untracked calls
 * in one report would mix the curves); RS must have them (its results end at the last improvement of each run);
 * with rows, the RS and RKM curve run must exist.
 */
static void reportPrepareLog(const char* dataset, ReportAlgorithm* data, Algorithm algorithm, size_t curveRun)
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
            exit(EXIT_FAILURE);
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
            exit(EXIT_FAILURE);
        }
        return;
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
            exit(EXIT_FAILURE);
        }
    }
    if ((algorithm == ALGO_RS || algorithm == ALGO_RKM) && reportRunRows(log, curveRun, &first) == 0)
    {
        fprintf(stderr, "Error: --curve-run %zu: %s %s has no run %zu\n", curveRun, dataset, name, curveRun);
        exit(EXIT_FAILURE);
    }
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
static void reportCell(FILE* file, const char* text, bool last)
{
    fputs(text, file);
    fputc(last ? '\n' : ';', file);
}

/** @brief Opens `<directory>/<name>` for writing; exits on failure. */
static FILE* reportOpen(const char* directory, const char* name)
{
    char path[PATH_MAX];
    joinPath(path, sizeof(path), directory, name);
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
            if (curves[a][i].sse > 0.0 && curves[a][i].sse < smallest)
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
    FILE* file = reportOpen(outputDirectory, name);

    char text[NUMBER_TEXT_SIZE + 64];
    for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
    {
        if (dataset->algorithms[a].runs.count == 0) continue;
        const char* display = reportAlgorithmName(algorithmOptions[a].id);
        snprintf(text, sizeof(text), "%s count", display);
        reportCell(file, text, false);
        if (!options->noTime)
        {
            snprintf(text, sizeof(text), "%s time (ms)", display);
            reportCell(file, text, false);
        }
        snprintf(text, sizeof(text), "%s SSE (10^%d)", display, exponent);
        reportCell(file, text, false);
        snprintf(text, sizeof(text), "%s CI", display);
        reportCell(file, text, a == lastAlgorithm);
    }

    // The end point: the means from _runs.csv (RS at the last improvement), the same values as the tables
    for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
    {
        if (dataset->algorithms[a].runs.count == 0) continue;
        reportCell(file, "end", false);
        if (!options->noTime)
        {
            reportCell(file, formatFixed(summaries[a].msMean, 3, text), false);
        }
        reportCell(file, formatFixed(summaries[a].sseMean / scale, 6, text), false);
        reportCell(file, summaries[a].ciAvailable ? formatFixed(summaries[a].ciMean, 4, text) : "NA",
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
                reportCell(file, "", false);
                if (!options->noTime) reportCell(file, "", false);
                reportCell(file, "", false);
                reportCell(file, "", last);
                continue;
            }
            const CurvePoint* point = &curves[a][i];
            snprintf(text, sizeof(text), "%zu", point->count);
            reportCell(file, text, false);
            if (!options->noTime)
            {
                reportCell(file, formatFixed(point->ms, 3, text), false);
            }
            reportCell(file, formatFixed(point->sse / scale, 6, text), false);
            reportCell(file, point->ciAvailable ? formatFixed(point->ci, 4, text) : "NA", last);
        }
    }
    fclose(file);

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
    FILE* file = reportOpen(outputDirectory, name);
    reportCell(file, "Algorithm", datasetCount == 0);
    for (size_t d = 0; d < datasetCount; ++d)
    {
        reportCell(file, datasets[order[d]].display, d + 1 == datasetCount);
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

        reportCell(file, reportAlgorithmName(algorithmOptions[a].id), datasetCount == 0);
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
            reportCell(file, cell, d + 1 == datasetCount);
        }
    }
    fclose(file);
}

/**
 * @brief Writes the summary.csv row of one algorithm on one dataset; `fullMs` (RS only, else NULL) is the mean time of
 * the whole runs, every swap included. The standard deviations are NA with fewer than 2 runs.
 */
static void reportWriteSummaryRow(FILE* file, const ReportDataset* dataset, Algorithm algorithm,
                                  const ReportSummary* summary, const double* fullMs, bool noTime)
{
    char text[NUMBER_TEXT_SIZE];
    const bool sd = summary->runs >= 2;
    fprintf(file, "%s;%s;%zu", dataset->display, reportAlgorithmName(algorithm), summary->runs);
    fprintf(file, ";%s", formatFixed(summary->sseMean, 6, text));
    fprintf(file, ";%s", sd ? formatFixed(summary->sseSd, 6, text) : "NA");
    fprintf(file, ";%s", summary->ciAvailable ? formatFixed(summary->ciMean, 4, text) : "NA");
    fprintf(file, ";%s",
            summary->ciAvailable && dataset->k > 0 ? formatFixed(summary->ciMean / (double)dataset->k, 4, text) : "NA");
    fprintf(file, ";%s", summary->ciAvailable ? formatFixed(summary->successPct, 1, text) : "NA");
    if (!noTime)
    {
        fprintf(file, ";%s", formatFixed(summary->msMean, 3, text));
        fprintf(file, ";%s", sd ? formatFixed(summary->msSd, 3, text) : "NA");
        fprintf(file, ";%s", fullMs != NULL ? formatFixed(*fullMs, 3, text) : "");
    }
    fputc('\n', file);
}

/** @brief Reads run folders into `*datasets` and returns their count; the caller owns the array and its row arrays. */
static size_t reportCollect(const char* const* directories, size_t directoryCount, ReportDataset** datasets)
{
    size_t datasetCount = 0;
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
            for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
            {
                char path[PATH_MAX];
                algorithmFilePath(algorithmOptions[a].id, folder, "_runs.csv", path, sizeof(path));
                if (!fileExists(path))
                {
                    continue;
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
                    ++datasetCount;
                }
                ReportDataset* dataset = &(*datasets)[d];
                reportReadCsv(path, false, &dataset->algorithms[a].runs);
                algorithmFilePath(algorithmOptions[a].id, folder, "_log.csv", path, sizeof(path));
                if (fileExists(path))
                {
                    reportReadCsv(path, true, &dataset->algorithms[a].log);
                }
                const size_t k = reportReadK(folder);
                if (dataset->k == 0)
                {
                    dataset->k = k;
                }
                else if (k != 0 && k != dataset->k)
                {
                    fprintf(stderr, "Error: %s has K = %zu, another run folder K = %zu\n", folder, k, dataset->k);
                    exit(EXIT_FAILURE);
                }
            }
        }
        freeStringList(names, nameCount);
    }
    return datasetCount;
}

/**
 * @brief Dataset order and display names: the `namesFile` lines ("<dataset> [display name]") first, the rest by name.
 *
 * Fills `order` (datasetCount entries). Unknown or repeated names in the file are warnings.
 */
static void reportOrder(const char* namesFile, ReportDataset* datasets, size_t datasetCount, size_t* order)
{
    bool* placed = calloc(datasetCount, sizeof(bool));
    handleMemoryError(placed);
    size_t ordered = 0;
    if (namesFile != NULL)
    {
        FILE* file;
        if (FOPEN(file, namesFile, "r") != 0)
        {
            handleFileError(namesFile);
        }
        char line[512];
        while (fgets(line, sizeof(line), file) != NULL)
        {
            line[strcspn(line, "\r\n")] = '\0';
            char* name = line + strspn(line, " \t");
            if (*name == '\0')
            {
                continue;
            }
            char* rest = name + strcspn(name, " \t");
            if (*rest != '\0')
            {
                *rest++ = '\0';
                rest += strspn(rest, " \t");
            }
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
                    exit(EXIT_FAILURE);
                }
                STRCPY(datasets[d].display, sizeof(datasets[d].display), rest);
            }
            placed[d] = true;
            order[ordered++] = d;
        }
        fclose(file);
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

    // The folders must come from one configuration
    char first[RUN_CONFIG_SIZE];
    char other[RUN_CONFIG_SIZE];
    reportReadHeader(directories[0], first, sizeof(first));
    for (size_t f = 1; f < directoryCount; ++f)
    {
        reportReadHeader(directories[f], other, sizeof(other));
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
            return EXIT_FAILURE;
        }
    }

    ReportDataset* datasets = NULL;
    const size_t datasetCount = reportCollect(directories, directoryCount, &datasets);
    if (datasetCount == 0)
    {
        fprintf(stderr, "Error: no <Algo>_runs.csv files in the dataset folders of %s\n", directories[0]);
        return EXIT_FAILURE;
    }
    for (size_t d = 0; d < datasetCount; ++d)
    {
        for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
        {
            if (datasets[d].algorithms[a].runs.count > 0)
            {
                reportPrepareLog(datasets[d].name, &datasets[d].algorithms[a], algorithmOptions[a].id,
                                 options.curveRun);
            }
        }
    }

    size_t* order = malloc(datasetCount * sizeof(size_t));
    handleMemoryError(order);
    reportOrder(namesFile, datasets, datasetCount, order);

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
        perror("Error: Unable to create the report folder");
        exit(EXIT_FAILURE);
    }

    // summary.csv, and the summaries behind the tables and the curve end points
    ReportSummary* summaries = calloc(datasetCount * ALGORITHM_COUNT, sizeof(ReportSummary));
    handleMemoryError(summaries);
    FILE* summaryFile = reportOpen(outputDirectory, "summary.csv");
    fputs(options.noTime ? "dataset;algorithm;runs;sse_mean;sse_sd;ci_mean;relative_ci;success_pct\n"
                         : "dataset;algorithm;runs;sse_mean;sse_sd;ci_mean;relative_ci;success_pct;time_ms_mean;time_"
                           "ms_sd;full_time_ms_mean\n",
          summaryFile);
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
                reportWriteSummaryRow(summaryFile, &datasets[d], algorithm, summary, &fullMs, options.noTime);
            }
            else
            {
                reportWriteSummaryRow(summaryFile, &datasets[d], algorithm, summary, NULL, options.noTime);
            }
        }
        reportWriteCurves(outputDirectory, &datasets[d], &summaries[d * ALGORITHM_COUNT], &options);
    }
    fclose(summaryFile);

    reportWriteTable(outputDirectory, "table_ci.csv", TABLE_CI, datasets, order, datasetCount, summaries);
    reportWriteTable(outputDirectory, "table_success.csv", TABLE_SUCCESS, datasets, order, datasetCount, summaries);
    reportWriteTable(outputDirectory, "table_sse.csv", TABLE_SSE, datasets, order, datasetCount, summaries);
    if (!options.noTime)
    {
        reportWriteTable(outputDirectory, "table_time.csv", TABLE_TIME, datasets, order, datasetCount, summaries);
    }

    printf("Report: %zu dataset%s from %zu folder%s -> %s\n", datasetCount, datasetCount == 1 ? "" : "s",
           directoryCount, directoryCount == 1 ? "" : "s", outputDirectory);

    for (size_t d = 0; d < datasetCount; ++d)
    {
        for (size_t a = 0; a < ALGORITHM_COUNT; ++a)
        {
            free(datasets[d].algorithms[a].runs.rows);
            free(datasets[d].algorithms[a].log.rows);
        }
    }
    free(datasets);
    free(order);
    free(summaries);
    return EXIT_SUCCESS;
}
