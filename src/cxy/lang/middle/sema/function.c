//
// Created by Carter on 2023-08-27.
//
#include "check.h"

#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"
#include "lang/middle/builtins.h"

static inline bool isInlineFunction(const AstNode *node)
{
    return findAttribute(node, S_inline) != NULL;
}

static inline bool isIteratorFunction(const Type *type)
{
    if (!typeIs(type, Struct) || !hasFlag(type, Closure))
        return false;

    const Type *iter = findStructMemberType(type, S_CallOverload);
    if (iter == NULL)
        return false;

    const Type *ret = iter->func.retType;
    return typeIs(ret, Struct) && hasFlag(ret, Optional);
}

static bool validateMainFunction(TypingContext *ctx,
                                 AstNode *node,
                                 const Type *type)
{
    if (type->func.paramsCount == 0)
        return true;

    if (type->func.paramsCount > 1) {
        logError(ctx->L,
                 &node->loc,
                 "main function can only have on argument, a slice of strings",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return false;
    }

    const Type *param = type->func.params[0];
    if (!hasFlag(param, Slice)) {
        logError(ctx->L,
                 &node->loc,
                 "main function parameter must be a slice of strings",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return false;
    }
    param = param->tStruct.members->members[0].type;
    csAssert0(typeIs(param, Pointer));
    param = param->pointer.pointed;
    if (!typeIs(param, String)) {
        logError(ctx->L,
                 &node->loc,
                 "main function parameter must be a slice of strings",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return false;
    }
    return true;
}

const Type *matchOverloadedFunctionPerfectMatch(Log *L,
                                                const Type *callee,
                                                const Type **argTypes,
                                                u64 argsCount,
                                                const FileLoc *loc,
                                                u64 flags,
                                                bool perfectMatch)
{
    AstNode *decls = callee->func.decl->list.first ?: callee->func.decl,
            *decl = decls;
    const Type *found = NULL;
    u64 maxScore = argsCount * 2, matchScore = 0, declarations = 0;

    for (; decl; decl = decl->list.link) {
        const Type *type = decl->type;
        if (type == NULL)
            continue;

        if ((decl->flags & flags) != flags)
            continue;

        bool isVariadic = hasFlag(decl, Variadic);
        u64 defaultCount = type->func.defaultValuesCount,
            paramsCount = type->func.paramsCount,
            requiredCount = paramsCount - defaultCount - isVariadic;

        declarations++;

        argsCount =
            isVariadic ? MIN(argsCount, paramsCount - isVariadic) : argsCount;
        if (argsCount < requiredCount || argsCount > paramsCount)
            continue;

        u64 score = maxScore;
        bool compatible = true;
        for (u64 i = 0; i < argsCount; i++) {
            const Type *paramType = type->func.params[i];
            compatible = paramType == argTypes[i];
            if (compatible)
                continue;
            if (!perfectMatch) {
                compatible = isTypeAssignableFrom(paramType, argTypes[i]);
                if (compatible) {
                    score--;
                    continue;
                }
                compatible =
                    isExplicitConstructableFrom(L, paramType, argTypes[i]);
                if (compatible) {
                    score--;
                    continue;
                }
                break;
            }
            else
                break;
        }

        if (!compatible)
            continue;

        if (score == maxScore)
            return type;

        if (score >= matchScore) {
            matchScore = score;
            found = type;
        }
    }

    if (found)
        return found;

    if (L && loc) {
        Type tAuto = {.tag = typAuto};
        Type type = {.tag = typFunc,
                     .flags = flags,
                     .name = callee->func.decl->funcDecl.name,
                     .func = {.params = argTypes,
                              .paramsCount = argsCount,
                              .retType = &tAuto}};

        logError(
            L,
            loc,
            "incompatible function reference ({u64} functions declared did "
            "not match function with signature {t})",
            (FormatArg[]){{.u64 = declarations}, {.t = &type}});

        decl = decls;
        while (decl) {
            if (decl->type)
                logError(L,
                         &decl->loc,
                         "found declaration with incompatible signature {t}",
                         (FormatArg[]){{.t = decl->type}});
            else
                logError(L,
                         &decl->loc,
                         "found declaration with incompatible signature "
                         "`unresolved`",
                         NULL);
            decl = decl->list.link;
        }
    }
    return NULL;
}

bool checkMemberFunctions(AstVisitor *visitor,
                          AstNode *node,
                          NamedTypeMember *members)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->structDecl.members;

    bool retype = false;
    for (u64 i = hasFlag(node, Virtual); member; member = member->next, i++) {
        if (nodeIs(member, FuncDecl)) {
            if (member->funcDecl.this_) {
                member->funcDecl.this_->type =
                    nodeIs(node, ClassDecl)
                        ? makeReferenceType(
                              ctx->types, node->type, member->flags & flgConst)
                        : makePointerType(
                              ctx->types, node->type, member->flags & flgConst);
            }

            const Type *type = member->type;
            if (!hasFlag(member, Abstract)) {
                type = checkFunctionBody(visitor, member);
                if (typeIs(type, Error)) {
                    node->type = ERROR_TYPE(ctx);
                    return false;
                }
            }

            if (member->funcDecl.operatorOverload == opRange &&
                !isIteratorFunction(type->func.retType)) //
            {
                logError(ctx->L,
                         &member->loc,
                         "expecting an iterator function overload to return an "
                         "iterator, got '{t}'",
                         (FormatArg[]){{.t = type->func.retType}});
                node->type = ERROR_TYPE(ctx);
                return false;
            }

            retype = members[i].type != type;
            if (members[i].name == member->funcDecl.name)
                members[i].type = type;
        }
    }

    return retype;
}

void checkFunctionParam(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *type = node->funcParam.type, *def = node->funcParam.def;
    AstNode *parent = node->parentScope;

    const Type *type_ = checkType(visitor, type), *def_ = NULL;
    if (typeIs(type_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (hasFlag(type, Const) && !hasFlag(type_, Const))
        type_ = makeWrappedType(ctx->types, type_, flgConst);

    if (typeIs(type_, Func) && !hasFlags(parent, flgPure | flgExtern)) {
        type = deepCloneAstNode(ctx->pool, type_->func.decl);
        type->funcType.params = makeFunctionParam(
            ctx->pool,
            &type->loc,
            makeString(ctx->strings, "_"),
            makeTypeReferenceNode(ctx->pool,
                                  makeVoidPointerType(ctx->types, flgNone),
                                  &type->loc),
            NULL,
            flgNone,
            type->funcType.params);
        type->type = NULL;
        type_ = checkType(visitor, type);
        if (typeIs(type_, Error))
            return;

        type = node->funcParam.type = makeTupleTypeAst(
            ctx->pool,
            &type->loc,
            flgNone,
            makeTypeReferenceNode2(ctx->pool,
                                   makeVoidPointerType(ctx->types, flgNone),
                                   &type->loc,
                                   type),
            NULL,
            makeTupleType(ctx->types,
                          (const Type *[]){
                              makeVoidPointerType(ctx->types, flgNone), type_},
                          2,
                          flgFuncTypeParam));
        node->type = type->type;
        node->flags |= flgFuncTypeParam;
        return;
    }

    if (def == NULL) {
        node->type = type_;
        return;
    }

    def->parentScope = node;
    def_ = checkType(visitor, def);
    if (typeIs(def_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isTypeAssignableFrom(type_, def_)) {
        logError(ctx->L,
                 &def->loc,
                 "parameter default value type '{t}' is incompatible with "
                 "parameter '{t}'",
                 (FormatArg[]){{.t = def_}, {.t = type_}});
        node->type = ERROR_TYPE(ctx);
    }
    else
        node->type = type_;
}

const Type *checkFunctionSignature(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *params = node->funcDecl.signature->params, *param = params,
            *ret = node->funcDecl.signature->ret, *body = node->funcDecl.body;
    u64 paramsCount = countAstNodes(params);
    node->flags |=
        findAttribute(node, S_transient) == NULL ? flgNone : flgReference;

    const Type **params_ = mallocOrDie(sizeof(Type *) * paramsCount);
    const Type *ret_ = ret ? checkType(visitor, ret) : makeAutoType(ctx->types);

    const Type *type = ret_;
    u64 defaultValues = 0;
    for (u64 i = 0; param; param = param->next, i++) {
        params_[i] = param->type ?: checkType(visitor, param);
        if (typeIs(params_[i], Error))
            type = params_[i];

        defaultValues += (param->funcParam.def != NULL);
    }

    if (typeIs(type, Error)) {
        free(params_);
        return node->type = ERROR_TYPE(ctx);
    }

    if (node->list.first && node->list.first != node) {
        type = node->list.first->type;
        if (typeIs(type, Error)) {
            return node->type = type;
        }

        const Type *found =
            matchOverloadedFunctionPerfectMatch(ctx->L,
                                                type,
                                                params_,
                                                paramsCount,
                                                NULL,
                                                node->flags & flgConst,
                                                true);
        if (found && found->func.decl != node) {
            // conflict
            logError(
                ctx->L,
                &node->loc,
                "function '{s}' overload with signature {t} already declared",
                (FormatArg[]){{.s = node->funcDecl.name}, {.t = found}});
            logNote(ctx->L,
                    &found->func.decl->loc,
                    "previous declaration found here",
                    NULL);
            free(params_);

            return node->type = ERROR_TYPE(ctx);
        }
    }

    node->type =
        makeFuncType(ctx->types,
                     &(Type){.tag = typFunc,
                             .flags = node->flags & (flgConst | flgVariadic),
                             .name = getDeclarationName(node),
                             .func = {.paramsCount = paramsCount,
                                      .params = params_,
                                      .retType = ret_,
                                      .defaultValuesCount = defaultValues,
                                      .decl = node}});
    free(params_);
    return node->type;
}

const Type *checkFunctionBody(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *body = node->funcDecl.body;

    const Type *type = node->type;
    const Type *ret_ = type->func.retType;

    const Type *body_ = checkType(visitor, body);

    if (typeIs(body_, Error))
        return node->type = ERROR_TYPE(ctx);

    if (nodeIs(body, BlockStmt)) {
        AstNode *ret = node->funcDecl.signature->ret;
        // Can be updated by return statement
        body_ = ret ? ret->type : body_;
    }

    if (typeIs(body_, Auto))
        body_ = makeVoidType(ctx->types);

    if (ret_ == body_)
        return type;

    if (!isTypeAssignableFrom(ret_, body_)) {
        logError(ctx->L,
                 &node->loc,
                 "return type '{t}' of function declaration doesn't match type "
                 "'{t}' returned in body.",
                 (FormatArg[]){{.t = ret_}, {.t = body_}});
        return node->type = ERROR_TYPE(ctx);
    }

    if (typeIs(ret_, Auto))
        node->type = changeFunctionRetType(ctx->types, type, body_);

    //    if (hasFlag(node, Async)) {
    //        makeCoroutineEntry(visitor, node);
    //    }

    return node->type;
}

void checkFunctionType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);

    const Type *ret = checkType(visitor, node->funcType.ret);
    u64 count = countAstNodes(node->funcType.params);
    const Type **params = mallocOrDie(sizeof(Type *) * count);

    AstNode *param = node->funcType.params;
    u64 defaultValuesCount = 0;
    for (u64 i = 0; param; param = param->next, i++) {
        param->parentScope = node;
        params[i] = checkType(visitor, param);
        if (params[i] == ERROR_TYPE(ctx)) {
            node->type = ERROR_TYPE(ctx);
        }
        else if (param->funcParam.def) {
            defaultValuesCount++;
        }
    }

    if (node->type == NULL) {
        node->type = makeFuncType(
            ctx->types,
            &(Type){.tag = typFunc,
                    .name = NULL,
                    .flags = node->flags,
                    .func = {.retType = ret,
                             .params = params,
                             .paramsCount = count,
                             .defaultValuesCount = defaultValuesCount,
                             .decl = node}});
    }

    free(params);
}

void checkFunctionDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node->funcDecl.name == S_main) {
        node->flags |= flgMain;
        ctx->root.program->flags |= flgExecutable;
    }

    const Type *type = checkFunctionSignature(visitor, node);
    if (typeIs(type, Error))
        return;
    if (hasFlag(node, Main)) {
        validateMainFunction(ctx, node, type);
        if (typeIs(node->type, Error))
            return;
    }

    if (node->funcDecl.body)
        checkFunctionBody(visitor, node);

    if (typeIs(node->type, Error))
        return;
}
