//
// Created by Carter on 2023-07-06.
//

#include "stats.h"

#include "driver.h"

#include <inttypes.h>

#define BYTES_1KB 1000.0
#define BYTES_1MB (1000.0 * BYTES_1KB)
#define BYTES_1GB (1000.0 * BYTES_1MB)

#define BYTES_TO_GB(B) (((double)(B)) / BYTES_1GB)
#define BYTES_TO_MB(B) (((double)(B)) / BYTES_1MB)
#define BYTES_TO_KB(B) (((double)(B)) / BYTES_1KB)

static void compilerPrintSummarySize(const CompilerStats *stats)
{
    if (stats->snapshot.poolStats.totalAllocated < BYTES_1KB) {
        printf("%zu bytes (%zu bytes / %zu blocks)",
               stats->snapshot.poolStats.totalUsed,
               stats->snapshot.poolStats.totalAllocated,
               stats->snapshot.poolStats.numberOfBlocks);
    }
    else if (stats->snapshot.poolStats.totalAllocated < BYTES_1MB) {
        printf("%g Kb (%g Kb / %zu blocks)",
               BYTES_TO_KB(stats->snapshot.poolStats.totalUsed),
               BYTES_TO_KB(stats->snapshot.poolStats.totalAllocated),
               stats->snapshot.poolStats.numberOfBlocks);
    }
    else if (stats->snapshot.poolStats.totalAllocated < BYTES_1GB) {
        printf("%g Mb (%g Mb / %zu blocks)",
               BYTES_TO_MB(stats->snapshot.poolStats.totalUsed),
               BYTES_TO_MB(stats->snapshot.poolStats.totalAllocated),
               stats->snapshot.poolStats.numberOfBlocks);
    }
    else {
        printf("%g Gb (%g Gb / %zu blocks)",
               BYTES_TO_GB(stats->snapshot.poolStats.totalUsed),
               BYTES_TO_GB(stats->snapshot.poolStats.totalAllocated),
               stats->snapshot.poolStats.numberOfBlocks);
    }
}

void compilerStatsSnapshot(CompilerDriver *driver)
{
    getMemPoolStats(driver->pool, &driver->stats.snapshot.poolStats);
    timespec_get(&driver->stats.snapshot.at, TIME_UTC);
}

void startCompilerStats(struct CompilerDriver *driver)
{
    timespec_get(&driver->stats.start, TIME_UTC);
}

void stopCompilerStats(struct CompilerDriver *driver)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    driver->stats.duration = timespecToMilliseconds(&ts) -
                             timespecToMilliseconds(&driver->stats.start);
}

void compilerStatsRecord(CompilerDriver *driver, CompilerStage stage)
{
    struct timespec ts;
    MemPoolStats stats;
    timespec_get(&ts, TIME_UTC);
    getMemPoolStats(driver->pool, &stats);
    StatsSnapshot *snapshot = &driver->stats.snapshot;

    driver->stats.stages[stage].duration =
        timespecToMilliseconds(&ts) - timespecToMilliseconds(&snapshot->at);
    driver->stats.stages[stage].pool.totalUsed =
        stats.totalUsed - snapshot->poolStats.totalUsed;
    driver->stats.stages[stage].pool.numberOfBlocks =
        stats.numberOfBlocks - snapshot->poolStats.numberOfBlocks;
    driver->stats.stages[stage].pool.totalAllocated =
        stats.totalAllocated - snapshot->poolStats.totalAllocated;

    driver->stats.stages[stage].captured = true;
}

void compilerStatsPrint(const struct CompilerDriver *driver)
{
    const Options *options = &driver->options;
    if ((options->cmd == cmdDev && options->dev.cleanAst) ||
        options->dsmMode == dsmNONE)
        return;

    if (options->dsmMode == dsmFULL) {
        // clang-format off
    printf("+---------------+---------------+-----------------------------------------------+\n");
    printf("|               |               |              Memory Usage                     |\n");
    printf("| Stage         | Duration (ms) |-------------+----------------+----------------|\n");
    printf("|               |               | # of Blocks | Allocated (Kb) | Used (Kb)      |\n");
        // clang-format on
        for (CompilerStage stage = ccsInvalid + 1; stage != ccsCOUNT; stage++) {
            __typeof(driver->stats.stages[stage]) *stats =
                &driver->stats.stages[stage];
            if (!stats->captured)
                continue;
            // clang-format off
        printf("|---------------+---------------+-------------+----------------+----------------|\n");
            // clang-format on
            printf("| %-14s|%14" PRIu64 " |%12zu |%15g |%15g |\n",
                   getCompilerStageDescription(stage),
                   stats->duration,
                   stats->pool.numberOfBlocks,
                   BYTES_TO_KB(stats->pool.totalAllocated),
                   BYTES_TO_KB(stats->pool.totalUsed));
        }

        printf(cBWHT);
        // clang-format off
        printf("+---------------+---------------+-------------+----------------+----------------+\n");
        // clang-format on

        printf("| Total         |%14" PRIu64 " |%12zu |%15g |%15g |\n",
               driver->stats.duration,
               driver->stats.snapshot.poolStats.numberOfBlocks,
               BYTES_TO_KB(driver->stats.snapshot.poolStats.totalAllocated),
               BYTES_TO_KB(driver->stats.snapshot.poolStats.totalUsed));

        // clang-format off
        printf("+---------------+---------------+-------------+----------------+----------------+\n");
        // clang-format on
        printf(cDEF);
    }
    else {
        printf(cBWHT);
        printf("   memory: ");
        compilerPrintSummarySize(&driver->stats);
        printf("\n   duration: %" PRIu64 " ms\n", driver->stats.duration);
        printf(cDEF);
    }
}
