//
// Created by Carter Mbotho on 2024-12-06.
//

#include "core/array.h"
#include "core/htable.h"
#include "core/utils.h"

#include "driver/driver.h"

#include "toml/toml.h"

#include <errno.h>

typedef struct NamedEntity {
    cstring name;
} NamedEntity;

static bool compareNamedEntity(const void *lhs, const void *rhs)
{
    return ((NamedEntity *)lhs)->name == ((NamedEntity *)rhs)->name;
}

#define REQUIRE_DATUM(NAME, MSG, ...)                                          \
    if (!NAME.ok) {                                                            \
        logError(cc->L, builtinLoc(), MSG, (FormatArg[]){__VA_ARGS__});        \
        return false;                                                          \
    }

#define LOAD_DATUM(TARGET, NAME, T)                                            \
    if (NAME.ok) {                                                             \
        if (#T[0] == 's') {                                                    \
            (TARGET)->NAME = makeString(cc->strings, NAME.u.s);                \
            free(NAME.u.s);                                                    \
        }                                                                      \
        else                                                                   \
            (TARGET)->NAME = NAME.u.T;                                         \
    }

#define LOAD_REQUIRED_DATUM(TARGET, NAME, T, MSG, ...)                         \
    LOAD_DATUM(TARGET, NAME, T)                                                \
    else                                                                       \
    {                                                                          \
        if (#T[0] == 's')                                                      \
            free(NAME.u.s);                                                    \
        logError(cc->L, builtinLoc(), MSG, (FormatArg[]){__VA_ARGS__});        \
        return false;                                                          \
    }

#define READ_DATUM(TARGET, NAME, T)                                            \
    if (NAME.ok) {                                                             \
        if (#T[0] == 's') {                                                    \
            TARGET = makeString(cc->strings, NAME.u.s);                        \
            free(NAME.u.s);                                                    \
        }                                                                      \
        else                                                                   \
            TARGET = NAME.u.T;                                                 \
    }

#define READ_REQUIRED_DATUM(TARGET, NAME, T, MSG, ...)                         \
    READ_DATUM(TARGET, NAME, T)                                                \
    else                                                                       \
    {                                                                          \
        if (#T[0] == 's')                                                      \
            free(NAME.u.s);                                                    \
        logError(cc->L, builtinLoc(), MSG, (FormatArg[]){__VA_ARGS__});        \
        return false;                                                          \
    }

static bool loadCxyfileStringArray(CompilerDriver *cc,
                                   DynArray *array,
                                   toml_array_t *source)
{
    int count = toml_array_nelem(source);
    for (int i = 0; i < count; i++) {
        toml_datum_t entry = toml_string_at(source, i);
        cstring str = NULL;
        READ_REQUIRED_DATUM(
            str,
            entry,
            s,
            "invalid authors entry at index {i32}, expecting a string",
            {.i32 = i})
        pushOnDynArray(array, &str);
    }
    return true;
}

static bool loadCxyfileBinary(CompilerDriver *cc,
                              CxyPackage *package,
                              toml_table_t *table)
{
    CxyBinary binary = {};
    toml_datum_t name = toml_string_in(table, "name");
    LOAD_REQUIRED_DATUM(
        &binary, name, s, "binary declaration requires a binary name");

    if (findInHashTable(&package->binaries,
                        &binary,
                        hashStr(hashInit(), binary.name),
                        sizeof(binary),
                        compareNamedEntity)) //
    {
        logError(cc->L,
                 builtinLoc(),
                 "binary with name '{s}' already defined in Cxyfile",
                 (FormatArg[]){{.s = binary.name}});
        return false;
    }

    toml_datum_t main = toml_string_in(table, "main");
    LOAD_REQUIRED_DATUM(
        &binary, main, s, "invalid or missing application `main` file");

    toml_array_t *tests = toml_array_in(table, "tests");
    if (tests && !loadCxyfileStringArray(cc, &binary.tests, tests)) {
        return false;
    }

    toml_array_t *defines = toml_array_in(table, "defines");
    if (tests && !loadCxyfileStringArray(cc, &binary.defines, defines)) {
        return false;
    }

    toml_array_t *cIncludeDirs = toml_array_in(table, "c-inc-dirs");
    if (tests &&
        !loadCxyfileStringArray(cc, &binary.cIncludeDirs, cIncludeDirs)) {
        return false;
    }

    toml_array_t *cLibDirs = toml_array_in(table, "c-lib-dirs");
    if (tests && !loadCxyfileStringArray(cc, &binary.cLibDirs, cLibDirs)) {
        return false;
    }

    toml_array_t *cLibs = toml_array_in(table, "c-libs");
    if (tests && !loadCxyfileStringArray(cc, &binary.cLibs, cLibs)) {
        return false;
    }

    toml_array_t *cDefines = toml_array_in(table, "c-defines");
    if (tests && !loadCxyfileStringArray(cc, &binary.cDefines, cDefines)) {
        return false;
    }

    toml_array_t *cFlags = toml_array_in(table, "c-flags");
    if (tests && !loadCxyfileStringArray(cc, &binary.cFlags, cFlags)) {
        return false;
    }

    insertInHashTable(&package->binaries,
                      &binary,
                      hashStr(hashInit(), binary.name),
                      sizeof(binary),
                      compareNamedEntity);
    return true;
}

static bool loadCxyfilePackage(CompilerDriver *cc,
                               CxyPackage *package,
                               toml_table_t *table)
{
    toml_datum_t name = toml_string_in(table, "name");
    LOAD_REQUIRED_DATUM(
        package, name, s, "invalid or missing package 'name' in Cxyfile")

    toml_datum_t version = toml_string_in(table, "version");
    LOAD_REQUIRED_DATUM(
        package, version, s, "invalid or missing package 'version' in Cxyfile")

    toml_datum_t description = toml_string_in(table, "description");
    LOAD_DATUM(package, description, s);

    toml_datum_t repo = toml_string_in(table, "repo");
    LOAD_DATUM(package, repo, s);

    toml_datum_t defaultRun = toml_string_in(table, "default-run");
    LOAD_DATUM(package, defaultRun, s);

    toml_array_t *authors = toml_array_in(table, "authors");
    if (authors && !loadCxyfileStringArray(cc, &package->authors, authors)) {
        return false;
    }
    return true;
}

bool loadCxyfile(CompilerDriver *cc, cstring path)
{
    FILE *fp = fopen(path, "r");
    char except[1024];
    if (fp == NULL) {
        logError(cc->L,
                 NULL,
                 "opening Cxyfile '{s}' failed: '{s}'",
                 (FormatArg[]){{.s = path}, {.s = strerror(errno)}});
        return false;
    }

    toml_table_t *toml = toml_parse_file(fp, except, sizeof(except));
    if (toml == NULL) {
        logError(cc->L,
                 NULL,
                 "parsing '{s}' failed: {s}",
                 (FormatArg[]){{.s = except}});
        return false;
    }

    toml_table_t *pkgToml = toml_table_in(toml, "package");
    if (pkgToml == NULL) {
        logError(cc->L,
                 NULL,
                 "invalid Cxyfile, file is missing 'package' definition",
                 (FormatArg[]){{.s = except}});
        return false;
    }

    if (!loadCxyfilePackage(cc, &cc->package, pkgToml))
        return false;

    toml_array_t *binaries = toml_array_in(toml, "bin");
    if (binaries) {
        int len = toml_array_nelem(binaries);
        for (int i = 0; i < len; i++) {
            toml_table_t *bin = toml_table_at(binaries, i);
            if (bin == NULL) {
                logError(cc->L,
                         builtinLoc(),
                         "invalid binary declaration #{i32}",
                         (FormatArg[]){{.i = i}});
                return false;
            }
            if (!loadCxyfileBinary(cc, &cc->package, bin))
                return false;
        }
    }
    return true;
}
