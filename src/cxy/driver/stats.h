//
// Created by Carter on 2023-07-06.
//
#pragma once

#include <driver/stages.h>

#include <time.h>

struct CompilerDriver;

typedef struct {
    struct timespec at;
    MemPoolStats poolStats;
} StatsSnapshot;

typedef struct CompilerStats {
    struct {
        bool captured;
        u64 duration;
        MemPoolStats pool;
    } stages[ccsCOUNT];
    struct timespec start;
    u64 duration;
    StatsSnapshot snapshot;
} CompilerStats;

void startCompilerStats(struct CompilerDriver *driver);
void stopCompilerStats(struct CompilerDriver *driver);
void compilerStatsSnapshot(struct CompilerDriver *driver);
void compilerStatsRecord(struct CompilerDriver *driver, CompilerStage stage);
void compilerStatsPrint(const struct CompilerDriver *driver);
