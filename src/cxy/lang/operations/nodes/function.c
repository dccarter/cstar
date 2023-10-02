//
// Created by Carter on 2023-08-27.
//
#include "../check.h"
#include "../codegen.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

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

void generateFuncParam(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (isIgnoreVar(node->funcParam.name)) {
        format(ctx->state, "attr(unused) ", NULL);
        generateTypeUsage(ctx, node->type);
        format(ctx->state,
               "/* unused {u32} */",
               (FormatArg[]){{.u32 = node->funcParam.index}});
    }
    else {
        generateTypeUsage(ctx, node->type);
        format(ctx->state, " {s}", (FormatArg[]){{.s = node->funcParam.name}});
    }
}

void generateFunctionDefinition(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    TypeTable *table = (ctx)->types;

    const AstNode *parent = node->parentScope;
    bool isMember =
        parent && (nodeIs(parent, StructDecl) || nodeIs(parent, ClassDecl));

    format(ctx->state, "\n", NULL);

    if (hasFlag(node, Native)) {
        // Generated on prologue statement
        if (ctx->namespace) {
            format(ctx->state, "#define ", NULL);
            writeNamespace(ctx, NULL);
            format(
                ctx->state, "{s}", (FormatArg[]){{.s = node->funcDecl.name}});

            if (node->funcDecl.index != 0)
                format(ctx->state,
                       "_{u32}",
                       (FormatArg[]){{.u32 = node->funcDecl.index}});

            format(
                ctx->state, " {s}", (FormatArg[]){{.s = node->funcDecl.name}});
        }
        const char *name = getNativeDeclarationAliasName(node);
        if (name) {
            format(ctx->state, "\n#define ", NULL);
            writeNamespace(ctx, NULL);
            format(ctx->state, "{s}", (FormatArg[]){{.s = name}});

            if (node->funcDecl.index != 0)
                format(ctx->state,
                       "_{u32}",
                       (FormatArg[]){{.u32 = node->funcDecl.index}});

            format(
                ctx->state, " {s}", (FormatArg[]){{.s = node->funcDecl.name}});
        }
        return;
    }

    cstring namespace = ctx->namespace;
    if (hasFlag(node, Generated))
        ctx->namespace = node->type->namespace;

    if (!isMember && hasFlag(node, Main)) {
        format(ctx->state, "typedef ", NULL);
        writeTypename(ctx, node->type->func.params[0]);
        format(ctx->state, " CXY__Main_Args_t;\n", NULL);
        if (isIntegerType(node->type->func.retType)) {
            format(ctx->state, "#define CXY_MAIN_INVOKE_RETURN\n\n", NULL);
        }
        else {
            format(ctx->state, "#define CXY_MAIN_INVOKE\n\n", NULL);
        }
    }

    if (isInlineFunction(node))
        format(ctx->state, "attr(always_inline)\n", NULL);

    const Type *ret = node->type->func.retType;
    generateTypeUsage(ctx, ret);

    if (isMember) {
        format(ctx->state, " ", NULL);
        writeTypename(ctx, parent->type);
        format(ctx->state, "__{s}", (FormatArg[]){{.s = node->funcDecl.name}});
    }
    else if (hasFlag(node, Main)) {
        format(ctx->state, " CXY__main", NULL);
    }
    else {
        format(ctx->state, " ", NULL);
        if (hasFlag(node, Generated))
            writeDeclNamespace(ctx, node->type->namespace, NULL);
        else
            writeNamespace(ctx, NULL);

        format(ctx->state, "{s}", (FormatArg[]){{.s = node->funcDecl.name}});
    }

    if (node->funcDecl.index != 0)
        format(
            ctx->state, "_{u32}", (FormatArg[]){{.u32 = node->funcDecl.index}});

    if (isMember && !hasFlag(node, Pure)) {
        format(ctx->state, "(", NULL);
        if (hasFlag(node->type, Const))
            format(ctx->state, "const ", NULL);

        generateTypeUsage(ctx, parent->type);
        if (typeIs(parent->type, Struct))
            format(ctx->state, "* this", NULL);
        else
            format(ctx->state, " this", NULL);

        if (node->funcDecl.signature->params)
            format(ctx->state, ", ", NULL);

        generateManyAstsWithDelim(
            visitor, "", ", ", ")", node->funcDecl.signature->params);
    }
    else {
        generateManyAstsWithDelim(
            visitor, "(", ", ", ")", node->funcDecl.signature->params);
    }

    format(ctx->state, " ", NULL);
    if (node->funcDecl.body->tag == astBlockStmt) {
        astConstVisit(visitor, node->funcDecl.body);
    }
    else {
        format(ctx->state, "{{{>}\n", NULL);
        if (node->type->func.retType != makeVoidType(table)) {
            format(ctx->state, "return ", NULL);
        }
        astConstVisit(visitor, node->funcDecl.body);
        format(ctx->state, ";", NULL);
        format(ctx->state, "{<}\n}", NULL);
    }

    //    if (node->funcDecl.operatorOverload == opDelete) {
    //        generateStructDelete(ctx, parent->type);
    //    }
    format(ctx->state, "\n", NULL);
    ctx->namespace = namespace;
}

void generateFuncDeclaration(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    const AstNode *parent = type->func.decl->parentScope;
    bool isPure = hasFlag(type->func.decl, Pure);
    u32 index = type->func.decl->funcDecl.index;
    if (hasFlag(type->func.decl, BuiltinMember))
        return;

    generateTypeUsage(context, type->func.retType);
    format(state, " ", NULL);

    writeTypename(context, parent->type);
    format(state, "__{s}", (FormatArg[]){{.s = type->name}});
    if (index)
        format(state, "_{u32}", (FormatArg[]){{.u32 = index}});

    format(state, "(", NULL);
    if (!isPure) {
        if (hasFlag(type, Const))
            format(state, "const ", NULL);
        generateTypeUsage(context, parent->type);
        if (typeIs(parent->type, Struct))
            format(state, " *", NULL);
    }

    for (u64 i = 0; i < type->func.paramsCount; i++) {
        if (!isPure || i != 0)
            format(state, ", ", NULL);
        generateTypeUsage(context, type->func.params[i]);
    }
    format(state, ");\n", NULL);
}

void generateFuncGeneratedDeclaration(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    u32 index = type->func.decl->funcDecl.index;
    const Type *ret = type->func.retType;
    if (hasFlag(type->func.decl, BuiltinMember))
        return;

    generateTypeUsage(context, type->func.retType);
    format(state, " ", NULL);

    if (hasFlag(type->func.decl, Generated))
        writeDeclNamespace(context, type->namespace, NULL);
    else
        writeNamespace(context, NULL);
    format(state, "{s}", (FormatArg[]){{.s = type->name}});
    if (index)
        format(state, "_{u32}", (FormatArg[]){{.u32 = index}});

    format(state, "(", NULL);
    for (u64 i = 0; i < type->func.paramsCount; i++) {
        if (i)
            format(state, ", ", NULL);
        generateTypeUsage(context, type->func.params[i]);
    }
    format(state, ");\n", NULL);
}

void generateFunctionTypedef(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    const AstNode *decl = type->func.decl,
                  *parent = decl ? type->func.decl->parentScope : NULL;
    bool isMember =
        parent && (nodeIs(parent, StructDecl) || nodeIs(parent, ClassDecl));

    format(state, "typedef ", NULL);
    generateTypeUsage(context, type->func.retType);
    format(state, "(*", NULL);
    if (isMember) {
        writeTypename(context, parent->type);
        format(state, "__", NULL);
    }
    writeTypename(context, type);

    format(state, ")(", NULL);
    if (isMember) {
        if (hasFlag(type, Const))
            format(state, "const ", NULL);
        writeTypename(context, parent->type);
        format(state, " *this", NULL);
    }

    for (u64 i = 0; i < type->func.paramsCount; i++) {
        if (isMember || i != 0)
            format(state, ", ", NULL);
        generateTypeUsage(context, type->func.params[i]);
    }
    format(state, ");\n", NULL);
    if (isMember)
        generateFuncDeclaration(context, type);
    else if (hasFlag(decl, Generated))
        generateFuncGeneratedDeclaration(context, type);
}

const Type *matchOverloadedFunction(TypingContext *ctx,
                                    const Type *callee,
                                    const Type **argTypes,
                                    u64 argsCount,
                                    const FileLoc *loc,
                                    u64 flags)
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

        u64 defaultCount = type->func.defaultValuesCount,
            paramsCount = type->func.paramsCount,
            requiredCount = paramsCount - defaultCount;

        declarations++;

        if (argsCount < requiredCount || argsCount > paramsCount)
            continue;

        u64 score = maxScore;
        bool compatible = true;
        for (u64 i = 0; i < argsCount; i++) {
            const Type *paramType = type->func.params[i];
            compatible = paramType == argTypes[i];
            if (!compatible) {
                compatible = isTypeAssignableFrom(paramType, argTypes[i]);
                if (compatible) {
                    score--;
                    continue;
                }
                compatible =
                    isExplicitConstructableFrom(ctx, paramType, argTypes[i]);
                if (compatible) {
                    score--;
                    continue;
                }
                break;
            }
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

    if (loc) {
        Type type = {.tag = typFunc,
                     .flags = flags,
                     .name = callee->func.decl->funcDecl.name,
                     .func = {.params = argTypes,
                              .paramsCount = argsCount,
                              .retType = makeAutoType(ctx->types)}};

        logError(
            ctx->L,
            loc,
            "incompatible function reference ({u64} functions declared did "
            "not match function with signature {t})",
            (FormatArg[]){{.u64 = declarations}, {.t = &type}});

        decl = decls;
        while (decl) {
            if (decl->type)
                logError(ctx->L,
                         &decl->loc,
                         "found declaration with incompatible signature {t}",
                         (FormatArg[]){{.t = decl->type}});
            else
                logError(ctx->L,
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
    for (u64 i = 0; member; member = member->next, i++) {
        if (nodeIs(member, FuncDecl)) {
            const Type *type = checkFunctionBody(visitor, member);
            if (typeIs(type, Error)) {
                node->type = ERROR_TYPE(ctx);
                return false;
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
            members[i].type = type;
        }
    }

    return retype;
}

void checkFunctionParam(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *type = node->funcParam.type, *def = node->funcParam.def;

    const Type *type_ = checkType(visitor, type), *def_ = NULL;
    if (typeIs(type_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (hasFlag(type, Const) && !hasFlag(type_, Const))
        type_ = makeWrappedType(ctx->types, type_, flgConst);

    if (def == NULL) {
        node->type = type_;
        return;
    }

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
        const Type *found = matchOverloadedFunction(ctx,
                                                    node->list.first->type,
                                                    params_,
                                                    paramsCount,
                                                    NULL,
                                                    node->flags & flgConst);
        if (found) {
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
                             .flags = node->flags & flgConst,
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
        return node->type = changeFunctionRetType(ctx->types, type, body_);

    return node->type;
}

void checkFunctionType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);

    const Type *ret = checkType(visitor, node->funcType.ret);
    u64 count = countAstNodes(node->funcType.params);
    const Type **params = mallocOrDie(sizeof(Type *) * count);

    AstNode *param = node->funcType.params;
    for (u64 i = 0; param; param = param->next, i++) {
        param->parentScope = node;
        params[i] = checkType(visitor, param);
        if (params[i] == ERROR_TYPE(ctx))
            node->type = ERROR_TYPE(ctx);
    }

    if (node->type == NULL) {
        node->type = makeFuncType(ctx->types,
                                  &(Type){.tag = typFunc,
                                          .name = NULL,
                                          .flags = node->flags,
                                          .func = {.retType = ret,
                                                   .params = params,
                                                   .paramsCount = count,
                                                   .decl = node}});
    }

    free(params);
}

void checkFunctionDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node->funcDecl.name == S_main) {
        node->flags |= flgMain;
        node->funcDecl.name = S_CXY__main;
    }

    const Type *type = checkFunctionSignature(visitor, node);
    if (!typeIs(type, Error) && node->funcDecl.body)
        checkFunctionBody(visitor, node);
}
