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
    insertInHashTable(                                                         \
        warnings,                                                              \
        &(Warning) { #name, strlen(#name), (u64)1 << IDX },                    \
        hashStr(hashInit(), #name),                                            \
        sizeof(Warning),                                                       \
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

Log newLog(DiagnosticHandler handler, void *ctx)
{
    handler = handler ?: printDiagnosticToConsole;

    Log L = (Log){.fileCache = newHashTable(sizeof(FileEntry)),
                  .showDiagnostics = true,
                  .handler = handler,
                  .handlerCtx = ctx,
                  .maxErrors = SIZE_MAX,
                  .enabledWarnings.num = wrnAll & ~(BIT(wrnMissingStage) |
                                                    BIT(wrnCMacroRedefine))};
    if (handler == printDiagnosticToConsole) {
        L.handlerCtx = NULL;
        L.handler = NULL;
    }
    return L;
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

static void printDiagnostic(Log *log,
                            FormatState *state,
                            FormatStyle style,
                            const FileLoc *fileLoc)
{
    FILE *file =
        fileLoc->fileName ? getCachedFile(log, fileLoc->fileName) : NULL;

    if (!file)
        return;

    size_t lineNumberLen = countDigits(fileLoc->end.row);
    size_t beginOffset = findLineBegin(file, fileLoc->begin.byteOffset);

    printEmptySpace(state, lineNumberLen + 1);
    format(state,
           "{$}|{$}\n",
           (FormatArg[]){{.style = style}, {.style = resetStyle}});

    printEmptySpace(state, lineNumberLen - countDigits(fileLoc->begin.row));
    format(state,
           "{$}{u}{$} {$}|{$}",
           (FormatArg[]){{.style = locStyle},
                         {.u = fileLoc->begin.row},
                         {.style = resetStyle},
                         {.style = style},
                         {.style = resetStyle}});
    printFileLine(state, beginOffset, file);

    printEmptySpace(state, lineNumberLen + 1);
    format(state,
           "{$}|{$}",
           (FormatArg[]){{.style = style}, {.style = resetStyle}});

    printEmptySpace(state, fileLoc->begin.byteOffset - beginOffset);
    if (isMultilineFileLoc(fileLoc)) {
        printLineMarkers(state,
                         style,
                         findLineEnd(file, fileLoc->begin.byteOffset) -
                             fileLoc->begin.byteOffset);

        if (fileLoc->begin.row + 1 < fileLoc->end.row) {
            printEmptySpace(state, lineNumberLen);
            format(state,
                   "{$}...{$}\n",
                   (FormatArg[]){{.style = locStyle}, {.style = resetStyle}});
        }

        format(state,
               "{$}{u}{$} {$}|{$}",
               (FormatArg[]){{.style = locStyle},
                             {.u = fileLoc->end.row},
                             {.style = resetStyle},
                             {.style = style},
                             {.style = resetStyle}});
        size_t end_offset = findLineBegin(file, fileLoc->end.byteOffset);
        printFileLine(state, end_offset, file);

        printEmptySpace(state, lineNumberLen + 1);
        format(state,
               "{$}|{$}",
               (FormatArg[]){{.style = style}, {.style = resetStyle}});

        printLineMarkers(state, style, fileLoc->end.byteOffset - end_offset);
    }
    else
        printLineMarkers(
            state, style, fileLoc->end.byteOffset - fileLoc->begin.byteOffset);
}

static void printDiagnosticToState(FormatState *state,
                                   const Diagnostic *diag,
                                   void *ctx)
{
    Log *log = ctx;
    if (diag->kind == dkError)
        log->errorCount++;
    else if (diag->kind == dkWarning)
        log->warningCount++;

    if (log->errorCount >= log->maxErrors)
        return;

    if ((diag->kind == dkError || diag->kind == dkWarning) &&
        log->errorCount + log->warningCount > 1)
        format(state, "\n", NULL);

    static const FormatStyle header_styles[] = {{STYLE_BOLD, COLOR_RED},
                                                {STYLE_BOLD, COLOR_YELLOW},
                                                {STYLE_BOLD, COLOR_CYAN}};
    static const char *headers[] = {"error", "warning", "note"};

    format(state,
           "{$}{s}{$}: ",
           (FormatArg[]){{.style = header_styles[diag->kind]},
                         {.s = headers[diag->kind]},
                         {.style = resetStyle}});
    format(state, diag->fmt, diag->args);
    format(state, "\n", NULL);
    if (diag->loc.fileName) {
        format(state,
               memcmp(&diag->loc.begin, &diag->loc.end, sizeof(diag->loc.begin))
                   ? "  in {$}{s}:{u32}:{u32} (to {u32}:{u32}){$}\n"
                   : "  in {$}{s}:{u32}:{u32}{$}\n",
               (FormatArg[]){{.style = locStyle},
                             {.s = diag->loc.fileName},
                             {.u32 = diag->loc.begin.row},
                             {.u32 = diag->loc.begin.col},
                             {.u32 = diag->loc.end.row},
                             {.u32 = diag->loc.end.col},
                             {.style = resetStyle}});
        if (log->showDiagnostics)
            printDiagnostic(log, state, header_styles[diag->kind], &diag->loc);
    }
}

static void printMessage(Log *log,
                         DiagnosticKind msg_type,
                         const FileLoc *fileLoc,
                         const char *format_str,
                         const FormatArg *args)
{
    Diagnostic diag = {.loc = fileLoc ? *fileLoc : (FileLoc){},
                       .kind = msg_type,
                       .fmt = format_str,
                       .args = args};
    if (log->handler) {
        log->handler(&diag, log->handlerCtx);
    }
    else {
        printDiagnosticToConsole(&diag, log);
    }
}

void logError(Log *log,
              const FileLoc *fileLoc,
              const char *format_str,
              const FormatArg *args)
{
    printMessage(log, dkError, fileLoc, format_str, args);
}

void logWarning(Log *log,
                const FileLoc *fileLoc,
                const char *format_str,
                const FormatArg *args)
{
    printMessage(log, dkWarning, fileLoc, format_str, args);
}

void logWarningWithId(Log *log,
                      u8 warningId,
                      const FileLoc *fileLoc,
                      const char *format_str,
                      const FormatArg *args)
{
    if (log->enabledWarnings.num & (1 << warningId))
        printMessage(log, dkWarning, fileLoc, format_str, args);
}

void printDiagnosticToConsole(const Diagnostic *diag, void *ctx)
{
    Log *log = ctx;
    FormatState state = newFormatState("", log->ignoreStyles);
    printDiagnosticToState(&state, diag, ctx);
    char *msg = formatStateToString(&state);
    if (diag->kind == dkError)
        fputs(msg, stderr);
    else
        fputs(msg, stdout);
    freeFormatState(&state);
    free(msg);
}

void printDiagnosticToMemory(const Diagnostic *diag, void *ctx)
{
    DiagnosticMemoryPrintCtx *memoryPrintCtx = ctx;
    printDiagnosticToState(memoryPrintCtx->state, diag, memoryPrintCtx->L);
}

u64 parseWarningLevels(Log *L, cstring str)
{
    WarningId warnings = L->enabledWarnings.num;
    if (str == NULL)
        return warnings;
    char *copy = strdup(str);
    char *start = copy, *end = strchr(str, '|');

    while (start && start[0] != '\0') {
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
        if (warning == wrn_Error || warning == wrnNone || warning == wrnAll) {
            free(copy);
            return warning;
        }

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
    printMessage(log, dkNote, fileLoc, format_str, args);
}

const FileLoc *builtinLoc(void)
{
    static FileLoc builtin = {
        .fileName = NULL, .begin = {0, 0, 0}, .end = {0, 0, 0}};
    return &builtin;
}
