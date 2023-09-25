//
// Created by Carter on 2023-09-21.
//

#include "../check.h"
#include "../codegen.h"

#include "lang/ast.h"
#include "lang/builtins.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/types.h"
#include "lang/visitor.h"

#include "core/alloc.h"

static void checkClassBaseDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *base = checkType(visitor, node->classDecl.base);
    csAssert0(base);
    if (typeIs(base, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!typeIs(base, Class)) {
        logError(ctx->L,
                 &node->classDecl.base->loc,
                 "base of type of '{t}' is not supported, base must be a class",
                 (FormatArg[]){{.t = base}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const AstNode *finalized = findAttribute(base->tClass.decl, S_final);
    if (finalized != NULL) {
        logError(
            ctx->L,
            &node->classDecl.base->loc,
            "base class {t} cannot be extended, base class marked as final",
            (FormatArg[]){{.t = base}});
        node->type = ERROR_TYPE(ctx);
        return;
    }
}

static void preCheckClassMembers(AstVisitor *visitor,
                                 AstNode *node,
                                 NamedTypeMember *members)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->classDecl.members;

    for (u64 i = 0; member; member = member->next, i++) {
        const Type *type;
        if (nodeIs(member, FuncDecl)) {
            type = checkFunctionSignature(visitor, member);
        }
        else {
            type = checkType(visitor, member);
        }

        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        if (nodeIs(member, Field)) {
            members[i] = (NamedTypeMember){
                .name = member->structField.name, .type = type, .decl = member};
            member->structField.index = i;
        }
        else {
            members[i] = (NamedTypeMember){.name = getDeclarationName(member),
                                           .type = type,
                                           .decl = member};
        }
    }

    if (typeIs(node->type, Error))
        return;
}

void generateClassDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "struct ", NULL);
    writeTypename(context, type);
    format(state, " {{{>}\n", NULL);
    format(state, "void *__mgmt;", NULL);

    format(state, "\n", NULL);
    for (u64 i = 0; i < type->tStruct.members->count; i++) {
        const NamedTypeMember *field = &type->tStruct.members->members[i];
        if (typeIs(field->type, Func) || typeIs(field->type, Generic))
            continue;

        format(state, "\n", NULL);

        generateTypeUsage(context, field->type);
        format(state, " {s};", (FormatArg[]){{.s = field->name}});
    }
    format(state, "{<}\n};\n", NULL);
}

void generateClassDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const Type *type = node->type;

    for (u64 i = 0; i < type->tStruct.members->count; i++) {
        const NamedTypeMember *member = &type->tStruct.members->members[i];
        if (typeIs(member->type, Func)) {
            astConstVisit(visitor, member->decl);
        }
    }
}

void generateClassTypedef(CodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->state;
    format(state, "typedef struct ", NULL);
    writeTypename(ctx, type);
    format(state, " ", NULL);
    writeTypename(ctx, type);
    format(state, ";\n", NULL);
}

AstNode *makeNewClassCall(TypingContext *ctx, AstNode *node)
{
    const Type *type = node->type;
    csAssert0(typeIs(type, Class));
    AstNode *new = findBuiltinDecl(S_newClass);
    csAssert0(new);

    return makeCallExpr(
        ctx->pool,
        &node->loc,
        makeResolvedPathWithArgs(ctx->pool,
                                 &node->loc,
                                 getDeclarationName(new),
                                 flgNone,
                                 new,
                                 makeResolvedPath(ctx->pool,
                                                  &node->loc,
                                                  type->name,
                                                  flgNone,
                                                  type->tClass.decl,
                                                  NULL,
                                                  type),
                                 NULL),
        NULL,
        flgNone,
        NULL,
        NULL);
}

AstNode *makeDropReferenceCall(TypingContext *ctx,
                               AstNode *member,
                               const FileLoc *loc)
{
    const Type *type = member->type;
    csAssert0(typeIs(type, Class));
    AstNode *drop = findBuiltinDecl(S_dropref);
    csAssert0(drop);
    return makeCallExpr(
        ctx->pool,
        loc,
        makePathWithElements(
            ctx->pool,
            loc,
            flgNone,
            makeResolvedPathElement(
                ctx->pool, loc, S_dropref, drop->flags, drop, NULL, NULL),
            NULL),
        makeResolvedPath(ctx->pool,
                         loc,
                         member->structField.name,
                         member->flags | flgMember,
                         member,
                         NULL,
                         member->type),
        flgNone,
        NULL,
        NULL);
}

AstNode *makeGetReferenceCall(TypingContext *ctx,
                              AstNode *member,
                              const FileLoc *loc)
{
    const Type *type = member->type;
    csAssert0(typeIs(type, Class));
    AstNode *get = findBuiltinDecl(S_getref);
    csAssert0(get);
    return makeCallExpr(
        ctx->pool,
        loc,
        makePathWithElements(
            ctx->pool,
            loc,
            flgNone,
            makeResolvedPathElement(
                ctx->pool, loc, S_getref, get->flags, get, NULL, NULL),
            NULL),
        makeResolvedPath(ctx->pool,
                         loc,
                         member->structField.name,
                         member->flags | flgMember,
                         member,
                         NULL,
                         member->type),
        flgNone,
        NULL,
        NULL);
}

void checkClassDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type **implements = NULL, *base = NULL;

    if (node->classDecl.base) {
        checkClassBaseDecl(visitor, node);
        if (typeIs(node->type, Error))
            return;
        base = node->classDecl.base->type;
    }

    u64 implementsCount = countAstNodes(node->classDecl.implements);
    if (implementsCount) {
        u64 count = countAstNodes(node->classDecl.implements);
        implements = mallocOrDie(sizeof(Type *) * count);
        checkImplements(visitor, node, implements, count);

        if (typeIs(node->type, Error))
            goto checkClassInterfacesError;
    }

    u64 membersCount = countAstNodes(node->classDecl.members);
    NamedTypeMember *members =
        mallocOrDie(sizeof(NamedTypeMember) * membersCount);
    node->classDecl.thisType =
        node->classDecl.thisType
            ?: makeThisType(ctx->types, node->classDecl.name, flgNone);
    const Type *this = node->classDecl.thisType;

    node->type = this;
    ctx->currentClass = node;
    preCheckClassMembers(visitor, node, members);
    ctx->currentClass = NULL;

    if (typeIs(node->type, Error))
        goto checkClassMembersError;

    ((Type *)this)->this.that = makeClassType(ctx->types,
                                              getDeclarationName(node),
                                              members,
                                              membersCount,
                                              node,
                                              base,
                                              implements,
                                              implementsCount,
                                              node->flags & flgTypeApplicable);
    node->type = this;

    implementClassOrStructBuiltins(visitor, node);
    if (typeIs(node->type, Error))
        goto checkClassMembersError;

    ctx->currentClass = node;
    if (checkMemberFunctions(visitor, node, members)) {
        node->type = replaceClassType(ctx->types,
                                      this->this.that,
                                      members,
                                      membersCount,
                                      node,
                                      base,
                                      implements,
                                      implementsCount,
                                      node->flags & flgTypeApplicable);
        ((Type *)this)->this.that = node->type;
    }
    else
        node->type = this->this.that;

    ctx->currentClass = NULL;

    if (!checkTypeImplementsAllMembers(ctx, node))
        node->type = ERROR_TYPE(ctx);

checkClassMembersError:
    if (members)
        free(members);

checkClassInterfacesError:
    if (implements)
        free(implements);
}
