//
// Created by Carter on 2023-07-06.
//

#include "stats.h"

#include "driver.h"

#define BYTES_TO_GB(B) (((double)(B)) / 1000000000)
#define BYTES_TO_MB(B) (((double)(B)) / 1000000)
#define BYTES_TO_KB(B) (((double)(B)) / 1000)

void compilerStatsSnapshot(CompilerDriver *driver)
{
    getMemPoolStats(&driver->pool, &driver->stats.snapshot.poolStats);
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
    getMemPoolStats(&driver->pool, &stats);
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
        printf("| %-14s|%14llu |%12zu |%15g |%15g |\n",
               getCompilerStageDescription(stage),
               stats->duration,
               stats->pool.numberOfBlocks,
               BYTES_TO_KB(stats->pool.totalAllocated),
               BYTES_TO_KB(stats->pool.totalUsed));
    }

    printf(cBOLD);
    // clang-format off
    printf("+---------------+---------------+-------------+----------------+----------------+\n");
    // clang-format on

    printf("| Total         |%14llu |%12zu |%15g |%15g |\n",
           driver->stats.duration,
           driver->stats.snapshot.poolStats.numberOfBlocks,
           BYTES_TO_KB(driver->stats.snapshot.poolStats.totalAllocated),
           BYTES_TO_KB(driver->stats.snapshot.poolStats.totalUsed));

    // clang-format off
    printf("+---------------+---------------+-------------+----------------+----------------+\n");
    // clang-format on
    printf(cDEF);
}