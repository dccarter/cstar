//
// Created by Carter on 2023-09-01.
//

#include "builtins.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

static struct {
    Log *L;
    const Type *module;
    HashTable overrides;
    FileLoc loc;
} cxy_builtins = {NULL};

typedef struct BuiltinOverride {
    cstring name;
    AstNode *node;
} BuiltinOverride;

static inline AstNode *findBuiltinOverride(cstring name)
{
    BuiltinOverride *override =
        findInHashTable(&cxy_builtins.overrides,
                        &(BuiltinOverride){.name = name},
                        hashPtr(hashInit(), name),
                        sizeof(BuiltinOverride),
                        comparePointers);
    return override ? override->node : NULL;
}

static inline void addOverridableBuiltin(cstring name)
{
    insertInHashTable(&cxy_builtins.overrides,
                      &(BuiltinOverride){.name = name},
                      hashPtr(hashInit(), name),
                      sizeof(BuiltinOverride),
                      comparePointers);
}

bool isBuiltinsInitialized() { return cxy_builtins.module != NULL; }

void initializeBuiltins(Log *L, const FileLoc *loc, const Type *module)
{
    if (cxy_builtins.module != NULL) {
        logWarning(L,
                   &cxy_builtins.loc,
                   "re-initializing builtins should not allowed",
                   NULL);
        return;
    }
    cxy_builtins.L = L;
    cxy_builtins.module = module;
    cxy_builtins.overrides = newHashTable(sizeof(BuiltinOverride));
    cxy_builtins.loc = *loc;
#define f(name) addOverridableBuiltin(S_##name);
    OVERRIDABLE_BUILTINS(f)
#undef f
}

AstNode *findBuiltinDecl(cstring name)
{
    csAssert0(cxy_builtins.module);
    AstNode *node = findBuiltinOverride(name);
    if (node == NULL) {
        const NamedTypeMember *member =
            findModuleMember(cxy_builtins.module, name);
        node = member ? (AstNode *)member->decl : NULL;
    }
    return node;
}

const Type *findBuiltinType(cstring name)
{
    if (cxy_builtins.module) {
        const NamedTypeMember *member =
            findModuleMember(cxy_builtins.module, name);
        return member ? member->type : NULL;
    }
    return NULL;
}

bool isBuiltinString(const Type *type)
{
    return findBuiltinType(S_String) == type;
}

bool overrideBuiltin(cstring name, AstNode *node)
{
    BuiltinOverride *override =
        findInHashTable(&cxy_builtins.overrides,
                        &(BuiltinOverride){.name = name},
                        hashPtr(hashInit(), name),
                        sizeof(BuiltinOverride),
                        comparePointers);
    if (override == NULL) {
        logError(cxy_builtins.L,
                 &node->loc,
                 "builtin override not supported `{s}` is not a builtin",
                 (FormatArg[]){{.s = name}});
        return false;
    }

    if (override->node != NULL) {
        logError(cxy_builtins.L,
                 &node->loc,
                 "builtin `{s}` already has an override, overriding again can "
                 "lead to undefined behaviour",
                 (FormatArg[]){{.s = name}});
        logNote(cxy_builtins.L,
                &override->node->loc,
                "override provided here",
                NULL);
        return false;
    }
    node->flags |= flgPublic;
    override->node = node;
    return true;
}
