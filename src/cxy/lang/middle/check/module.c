//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"

static u64 addDefineToModuleMembers(NamedTypeMember *members,
                                    u64 index,
                                    AstNode *decl,
                                    u64 builtinFlags)
{
    if (decl->define.container) {
        decl->flags |= builtinFlags;
        members[index++] = (NamedTypeMember){
            .decl = decl, .name = getDeclarationName(decl), .type = decl->type};
    }
    else {
        AstNode *name = decl->define.names;
        for (; name; name = name->next) {
            name->flags |= builtinFlags;
            members[index++] = (NamedTypeMember){
                .decl = name,
                .name = name->ident.alias ?: name->ident.value,
                .type = name->type};
        }
    }
    return index;
}

static u64 addModuleTypeMember(NamedTypeMember *members,
                               u64 index,
                               AstNode *decl,
                               u64 builtinFlags)
{
    if (nodeIs(decl, Define)) {
        return addDefineToModuleMembers(members, index, decl, builtinFlags);
    }
    else if (!nodeIs(decl, CCode)) {
        decl->flags |= builtinFlags;
        members[index++] = (NamedTypeMember){
            .decl = decl, .name = getDeclarationName(decl), .type = decl->type};
        return index;
    }

    return index;
}

void buildModuleType(TypingContext *ctx, AstNode *node, bool isBuiltinModule)
{
    u64 builtinsFlags = (isBuiltinModule ? flgBuiltin : flgNone);
    u64 count = countProgramDecls(node->program.decls) +
                countProgramDecls(node->program.top),
        i = 0;

    NamedTypeMember *members = mallocOrDie(sizeof(NamedTypeMember) * count);

    AstNode *decl = node->program.top;
    for (; decl; decl = decl->next) {
        if (nodeIs(decl, ImportDecl))
            continue;
        i = addModuleTypeMember(members, i, decl, builtinsFlags);
    }

    decl = node->program.decls;
    for (; decl; decl = decl->next) {
        i = addModuleTypeMember(members, i, decl, builtinsFlags);
    }

    node->type = makeModuleType(
        ctx->types,
        isBuiltinModule ? S___builtins : node->program.module->moduleDecl.name,
        node->loc.fileName,
        members,
        i);
    free(members);
}

void checkImportDecl(attr(unused) AstVisitor *visitor, attr(unused) AstNode *node) {}