#include "core/log.h"
#include "core/hash.h"
#include "core/utils.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LINE_SEARCH_RANGE 64
#define LINE_BUF_CAPACITY 64

typedef struct {
    const char *fileName;
    FILE *file;
} FileEntry;

typedef struct {
    const char *name;
    size_t len;
    WarningId value;
} Warning;

static bool compareWarningIds(const void *left, const void *right)
{
    return ((Warning *)left)->len == ((Warning *)right)->len &&
           !memcmp(((Warning *)left)->name,
                   ((Warning *)right)->name,
                   ((Warning *)left)->len);
}

static void registerWarnings(HashTable *warnings)
{
#define f(name, IDX)                                                           \
    insertInHashTable(warnings,                                                \
                      &(Warning){#name, strlen(#name), (u64)1 << IDX},         \
                      hashStr(hashInit(), #name),                              \
                      sizeof(Warning),                                         \
                      compareWarningIds);
    CXY_COMPILER_WARNINGS(f)
#undef f
    insertInHashTable(warnings,
                      &(Warning){"All", 3, wrnAll},
                      hashStr(hashInit(), "All"),
                      sizeof(Warning),
                      compareWarningIds);
    insertInHashTable(warnings,
                      &(Warning){"None", 4, wrnNone},
                      hashStr(hashInit(), "None"),
                      sizeof(Warning),
                      compareWarningIds);
}

static WarningId parseNextWarningId(Log *L, char *start, char *end)
{
    static bool initialized = false;
    static HashTable warningIds;
    if (!initialized) {
        initialized = true;
        warningIds = newHashTable(sizeof(Warning));
        registerWarnings(&warningIds);
    }
    char *p = start;
    while (isspace(*p))
        p++;
    if (*p == '0') {
        logError(L,
                 NULL,
                 "parsing warning failed, expecting a warning name (got '{s}')",
                 (FormatArg[]){{.s = start}});
        return wrn_Error;
    }

    u64 len;
    if (end) {
        while (isspace(*end))
            end--;
        end[1] = '\0';
        len = end - p;
    }
    else
        len = strlen(p);

    Warning *warning = findInHashTable(&warningIds,
                                       &(Warning){.name = p, .len = len},
                                       hashRawBytes(hashInit(), p, len),
                                       sizeof(Warning),
                                       compareWarningIds);

    if (warning == NULL) {
        logError(L,
                 NULL,
                 "parsing warning failed, unknown warning name ('{s}')",
                 (FormatArg[]){{.s = start}});
        return wrn_Error;
    }

    return warning->value;
}

Log newLog(FormatState *state)
{
    return (Log){.fileCache = newHashTable(sizeof(FileEntry)),
                 .showDiagnostics = true,
                 .state = state,
                 .maxErrors = SIZE_MAX};
}

void freeLog(Log *log)
{
    FileEntry *entries = log->fileCache.elems;
    for (size_t i = 0; i < log->fileCache.capacity; ++i) {
        if (!isBucketOccupied(&log->fileCache, i))
            continue;
        fclose(entries[i].file);
    }
    freeHashTable(&log->fileCache);
}

static bool compareFileEntries(const void *left, const void *right)
{
    return !strcmp(((FileEntry *)left)->fileName,
                   ((FileEntry *)right)->fileName);
}

static FILE *getCachedFile(Log *log, const char *fileName)
{
    uint32_t hash = hashStr(hashInit(), fileName);
    FileEntry *entry = findInHashTable(&log->fileCache,
                                       &(FileEntry){.fileName = fileName},
                                       hash,
                                       sizeof(FileEntry),
                                       compareFileEntries);
    if (entry)
        return entry->file;
    FILE *file = fopen(fileName, "rb");
    if (!file)
        return NULL;
    if (!insertInHashTable(&log->fileCache,
                           &(FileEntry){.fileName = fileName, .file = file},
                           hash,
                           sizeof(FileEntry),
                           compareFileEntries))
        assert(false && "cannot insert file in what file cache");
    return file;
}

static size_t findLineBegin(FILE *file, size_t offset)
{
    char data[LINE_SEARCH_RANGE];
    while (offset > 0) {
        size_t range = offset > LINE_SEARCH_RANGE ? LINE_SEARCH_RANGE : offset;
        offset -= range;
        fseek(file, offset, SEEK_SET);
        fread(data, 1, range, file);
        for (size_t i = range; i-- > 0;) {
            if (data[i] == '\n')
                return offset + i + 1;
        }
    }
    return 0;
}

static size_t findLineEnd(FILE *file, size_t offset)
{
    char data[LINE_SEARCH_RANGE];
    fseek(file, offset, SEEK_SET);
    while (true) {
        size_t read_count = fread(data, 1, LINE_SEARCH_RANGE, file);
        for (size_t i = 0; i < read_count; ++i) {
            if (data[i] == '\n')
                return offset + i;
        }
        offset += read_count;
        if (read_count < LINE_SEARCH_RANGE)
            return offset;
    }
}

static size_t countDigits(size_t i)
{
    size_t n = 1;
    while (i >= 10) {
        i /= 10;
        n++;
    }
    return n;
}

static void printEmptySpace(FormatState *state, size_t lineNumberLen)
{
    if (lineNumberLen >= 4)
        format(state, "    ", NULL), lineNumberLen -= 4;
    for (size_t i = 0; i < lineNumberLen; ++i)
        format(state, " ", NULL);
}

static void printFileLine(FormatState *state, size_t line_begin, FILE *file)
{
    char line_buf[LINE_BUF_CAPACITY];
    fseek(file, line_begin, SEEK_SET);
    while (true) {
        if (!fgets(line_buf, LINE_BUF_CAPACITY, file))
            break;
        char *end_line = strrchr(line_buf, '\n');
        if (end_line)
            *end_line = '\0';
        format(state, "{s}", (FormatArg[]){{.s = line_buf}});
        if (end_line)
            break;
    }
    format(state, "\n", NULL);
}

static void printLineMarkers(FormatState *state,
                             FormatStyle style,
                             size_t count)
{
    format(state, "{$}", (FormatArg[]){{.style = style}});
    for (size_t i = 0; i < count; ++i)
        format(state, "^", NULL);
    format(state, "{$}\n", (FormatArg[]){{.style = resetStyle}});
}

static bool isMultilineFileLoc(const FileLoc *fileLoc)
{
    return fileLoc->begin.row != fileLoc->end.row;
}

static void printDiagnostic(Log *log, FormatStyle style, const FileLoc *fileLoc)
{
    FILE *file =
        fileLoc->fileName ? getCachedFile(log, fileLoc->fileName) : NULL;

    if (!file)
        return;

    size_t lineNumberLen = countDigits(fileLoc->end.row);
    size_t beginOffset = findLineBegin(file, fileLoc->begin.byteOffset);

    printEmptySpace(log->state, lineNumberLen + 1);
    format(log->state,
           "{$}|{$}\n",
           (FormatArg[]){{.style = style}, {.style = resetStyle}});

    printEmptySpace(log->state,
                    lineNumberLen - countDigits(fileLoc->begin.row));
    format(log->state,
           "{$}{u}{$} {$}|{$}",
           (FormatArg[]){{.style = locStyle},
                         {.u = fileLoc->begin.row},
                         {.style = resetStyle},
                         {.style = style},
                         {.style = resetStyle}});
    printFileLine(log->state, beginOffset, file);

    printEmptySpace(log->state, lineNumberLen + 1);
    format(log->state,
           "{$}|{$}",
           (FormatArg[]){{.style = style}, {.style = resetStyle}});

    printEmptySpace(log->state, fileLoc->begin.byteOffset - beginOffset);
    if (isMultilineFileLoc(fileLoc)) {
        printLineMarkers(log->state,
                         style,
                         findLineEnd(file, fileLoc->begin.byteOffset) -
                             fileLoc->begin.byteOffset);

        if (fileLoc->begin.row + 1 < fileLoc->end.row) {
            printEmptySpace(log->state, lineNumberLen);
            format(log->state,
                   "{$}...{$}\n",
                   (FormatArg[]){{.style = locStyle}, {.style = resetStyle}});
        }

        format(log->state,
               "{$}{u}{$} {$}|{$}",
               (FormatArg[]){{.style = locStyle},
                             {.u = fileLoc->end.row},
                             {.style = resetStyle},
                             {.style = style},
                             {.style = resetStyle}});
        size_t end_offset = findLineBegin(file, fileLoc->end.byteOffset);
        printFileLine(log->state, end_offset, file);

        printEmptySpace(log->state, lineNumberLen + 1);
        format(log->state,
               "{$}|{$}",
               (FormatArg[]){{.style = style}, {.style = resetStyle}});

        printLineMarkers(
            log->state, style, fileLoc->end.byteOffset - end_offset);
    }
    else
        printLineMarkers(log->state,
                         style,
                         fileLoc->end.byteOffset - fileLoc->begin.byteOffset);
}

static void printMessage(Log *log,
                         LogMsgType msg_type,
                         const FileLoc *fileLoc,
                         const char *format_str,
                         const FormatArg *args)
{
    if (msg_type == LOG_ERROR)
        log->errorCount++;
    else if (msg_type == LOG_WARNING)
        log->warningCount++;

    if (log->errorCount >= log->maxErrors)
        return;

    if ((msg_type == LOG_ERROR || msg_type == LOG_WARNING) &&
        log->errorCount + log->warningCount > 1)
        format(log->state, "\n", NULL);

    static const FormatStyle header_styles[] = {{STYLE_BOLD, COLOR_RED},
                                                {STYLE_BOLD, COLOR_YELLOW},
                                                {STYLE_BOLD, COLOR_CYAN}};
    static const char *headers[] = {"error", "warning", "note"};

    format(log->state,
           "{$}{s}{$}: ",
           (FormatArg[]){{.style = header_styles[msg_type]},
                         {.s = headers[msg_type]},
                         {.style = resetStyle}});
    format(log->state, format_str, args);
    format(log->state, "\n", NULL);
    if (fileLoc && fileLoc->fileName) {
        format(log->state,
               memcmp(&fileLoc->begin, &fileLoc->end, sizeof(fileLoc->begin))
                   ? "  in {$}{s}:{u32}:{u32} (to {u32}:{u32}){$}\n"
                   : "  in {$}{s}:{u32}:{u32}{$}\n",
               (FormatArg[]){
                   {.style = locStyle},
                   {.s = fileLoc->fileName ? fileLoc->fileName : "<unknown>"},
                   {.u32 = fileLoc->begin.row},
                   {.u32 = fileLoc->begin.col},
                   {.u32 = fileLoc->end.row},
                   {.u32 = fileLoc->end.col},
                   {.style = resetStyle}});
        if (log->showDiagnostics)
            printDiagnostic(log, header_styles[msg_type], fileLoc);
    }
}

void logError(Log *log,
              const FileLoc *fileLoc,
              const char *format_str,
              const FormatArg *args)
{
    printMessage(log, LOG_ERROR, fileLoc, format_str, args);
}

void logWarning(Log *log,
                const FileLoc *fileLoc,
                const char *format_str,
                const FormatArg *args)
{
    printMessage(log, LOG_WARNING, fileLoc, format_str, args);
}

void logWarningWithId(Log *log,
                      u8 warningId,
                      const FileLoc *fileLoc,
                      const char *format_str,
                      const FormatArg *args)
{
    if (log->enabledWarnings.num & (1 << warningId))
        printMessage(log, LOG_WARNING, fileLoc, format_str, args);
}

u64 parseWarningLevels(Log *L, cstring str)
{
    WarningId warnings = wrnNone;
    char *copy = strdup(str);
    char *start = copy, *end = strchr(str, '|');

    while (start) {
        char *last = end;
        if (last) {
            *last = '\0';
            last--;
            end++;
        }

        while (isspace(*start))
            start++;
        bool flip = start[0] == '~';
        if (flip)
            start++;

        u64 warning = parseNextWarningId(L, start, last);
        if (warning == wrn_Error || warning == wrnNone || warning == wrnAll)
            return warning;

        if (flip)
            warnings &= ~warning;
        else
            warnings |= warning;

        while (end && *end == '|')
            end++;

        start = end;
        end = last ? strchr(last, '|') : NULL;
    }

    free(copy);

    return warnings;
}

void logNote(Log *log,
             const FileLoc *fileLoc,
             const char *format_str,
             const FormatArg *args)
{
    printMessage(log, LOG_NOTE, fileLoc, format_str, args);
}

const FileLoc *builtinLoc(void)
{
    static FileLoc builtin = {
        .fileName = NULL, .begin = {0, 0, 0}, .end = {0, 0, 0}};
    return &builtin;
}