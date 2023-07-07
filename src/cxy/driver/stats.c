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
    for (CompilerStage stage = ccsInvalid + 1; stage != ccsCOUNT; stage++) {
        __typeof(driver->stats.stages[stage]) *stats =
            &driver->stats.stages[stage];
        if (!stats->captured)
            continue;

        printf("\t%s -> duration: %llu ms,  memory usage: (blocks: %zu, "
               "allocated: "
               "%f kb, used: %f kb)\n",
               getCompilerStageDescription(stage),
               stats->duration,
               stats->pool.numberOfBlocks,
               BYTES_TO_KB(stats->pool.totalAllocated),
               BYTES_TO_KB(stats->pool.totalUsed));
    }
}