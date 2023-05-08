//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

#include <string.h>

static inline void hookStructEnvironments(SemanticsContext *ctx,
                                          const Type *base,
                                          Env *root)
{
    if (base) {
        environmentAttachUp((Env *)base->tStruct.env, root);
        environmentAttachUp(ctx->env, (Env *)base->tStruct.env);
    }
    else {
        environmentAttachUp(ctx->env, root);
    }
}

static inline void unHookStructEnvironments(SemanticsContext *ctx,
                                            const Type *base)
{
    if (base) {
        environmentDetachUp((Env *)base->tStruct.env);
        environmentDetachUp(ctx->env);
    }
    else {
        environmentDetachUp(ctx->env);
    }
}

static inline AstNode *findStructField(const Type *type, cstring name)
{
    Env env = {.first = type->tStruct.env->scope,
               .scope = type->tStruct.env->scope};
    return findSymbolOnly(&env, name);
}

bool evalExplicitConstruction(AstVisitor *visitor,
                              const Type *type,
                              AstNode *node)
{
    const Type *source = node->type ?: evalType(visitor, node);
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    if (isTypeAssignableFrom(type, source))
        return true;

    if (!typeIs(type, Struct))
        return false;

    AstNode *constructor = findSymbolOnly(type->tStruct.env, "op_new");
    if (constructor == NULL || findAttribute(constructor, "explicit"))
        return false;

    if (constructor->type->func.paramsCount != 1)
        return false;

    const Type *param = constructor->type->func.params[0];
    if (!evalExplicitConstruction(visitor, param, node))
        return false;

    AstNode *callee = makeAstNode(
        ctx->pool,
        &node->loc,
        &(AstNode){
            .tag = astPath,
            .path = {.elements = makeAstNode(
                         ctx->pool,
                         &node->loc,
                         &(AstNode){.tag = astPathElem,
                                    .pathElement = {.name = type->name}})}});

    return evalConstructorCall(
               visitor, type, node, callee, copyAstNode(ctx->pool, node)) !=
           NULL;
}

const Type *evalConstructorCall(AstVisitor *visitor,
                                const Type *type,
                                AstNode *node,
                                AstNode *callee,
                                AstNode *args)
{
    // Struct(100)
    // -> ({ var x = Struct{}; x.op_new(&x, 100), x; })
    SemanticsContext *ctx = getAstVisitorContext(visitor);

    // turn new S(...) => ({ var tmp = new S{}; tmp.init(...); })
    cstring name = makeAnonymousVariable(ctx->strPool, "_new_tmp");
    // S{}
    AstNode *newExpr =
        makeAstNode(ctx->pool,
                    &callee->loc,
                    &(AstNode){.tag = astStructExpr,
                               .flags = callee->flags,
                               .structExpr = {.left = callee, .fields = NULL}});
    // var name = new S{}
    AstNode *varDecl = makeAstNode(
        ctx->pool,
        &callee->loc,
        &(AstNode){
            .tag = astVarDecl,
            .flags = callee->flags | flgImmediatelyReturned,
            .varDecl = {.names = makeAstNode(ctx->pool,
                                             &callee->loc,
                                             &(AstNode){.tag = astIdentifier,
                                                        .ident.value = name}),
                        .init = newExpr}});

    // tmp.init
    AstNode *newCallee = makeAstNode(
        ctx->pool,
        &callee->loc,
        &(AstNode){
            .tag = astMemberExpr,
            .flags = callee->flags,
            .memberExpr = {
                .target = makeAstNode(ctx->pool,
                                      &node->loc,
                                      &(AstNode){.tag = astIdentifier,
                                                 .flags = callee->flags,
                                                 .ident.value = name}),
                .member = makeAstNode(ctx->pool,
                                      &node->loc,
                                      &(AstNode){.tag = astIdentifier,
                                                 .flags = callee->flags,
                                                 .ident.value = "op_new"})}});

    AstNode *ret =
        makeAstNode(ctx->pool,
                    &node->loc,
                    &(AstNode){.tag = astExprStmt,
                               .flags = node->flags,
                               .exprStmt.expr = makeAstNode(
                                   ctx->pool,
                                   &node->loc,
                                   &(AstNode){.tag = astIdentifier,
                                              .flags = node->flags,
                                              .ident.value = name})});

    //     name.init
    varDecl->next =
        makeAstNode(ctx->pool,
                    &node->loc,
                    &(AstNode){.tag = astCallExpr,
                               .flags = node->flags,
                               .callExpr = {.callee = newCallee, .args = args},
                               .next = ret});

    AstNode *block = makeAstNode(
        ctx->pool,
        &node->loc,
        &(AstNode){.tag = astBlockStmt, .blockStmt.stmts = varDecl});

    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
    node->tag = astStmtExpr;
    node->stmtExpr.stmt = block;

    return evalType(visitor, node);
}

void generateStructExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *field = node->structExpr.fields;

    format(ctx->state, "(", NULL);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, ")", NULL);

    format(ctx->state, "{{", NULL);
    for (u64 i = 0; field; field = field->next, i++) {
        if (i != 0)
            format(ctx->state, ", ", NULL);
        format(
            ctx->state,
            ".{s}{s} = ",
            (FormatArg[]){{.s = (field->flags & flgAddSuper) ? "super." : ""},
                          {.s = field->fieldExpr.name}});
        astConstVisit(visitor, field->fieldExpr.value);
    }

    format(ctx->state, "}", NULL);
}

static void generateStructDelete(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "attr(always_inline)\nstatic void ", NULL);
    writeTypename(context, type);
    format(state, "__op_delete(", NULL);
    writeTypename(context, type);
    format(state, " *this) {{{>}\n", NULL);

    u64 y = 0;
    for (u64 i = 0; i < type->tStruct.fieldsCount; i++) {
        const StructField *field = &type->tStruct.fields[i];
        if (typeIs(field->type, Func) || typeIs(field->type, Generic) ||
            (isBuiltinType(field->type) && !typeIs(field->type, String)))
            continue;

        const Type *raw = stripPointer(field->type);
        if (y++ != 0)
            format(state, "\n", NULL);

        if ((typeIs(field->type, Pointer) && isBuiltinType(raw)) ||
            typeIs(field->type, String)) {
            format(state,
                   "cxy_free((void *)this->{s});",
                   (FormatArg[]){{.s = field->name}});
        }
        else {
            writeTypename(context, raw);
            format(
                state,
                "__op_delete({s}this->{s});",
                (FormatArg[]){{.s = !typeIs(field->type, Pointer) ? "&" : ""},
                              {.s = field->name}});
        }
    }

    format(state, "{<}\n}", NULL);
}

void generateStructDefinition(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "struct ", NULL);
    writeTypename(context, type);
    format(state, " {{{>}\n", NULL);
    if (type->tStruct.base) {
        writeTypename(context, type->tStruct.base);
        format(state, " super;\n", NULL);
    }

    u64 y = 0;
    for (u64 i = 0; i < type->tStruct.fieldsCount; i++) {
        const StructField *field = &type->tStruct.fields[i];
        if (typeIs(field->type, Func) || typeIs(field->type, Generic))
            continue;

        if (y++ != 0)
            format(state, "\n", NULL);

        generateTypeUsage(context, field->type);
        format(state, " {s};", (FormatArg[]){{.s = field->name}});
    }

    format(state, "{<}\n}", NULL);
    if (!hasFlag(type, ImplementsDelete)) {
        format(state, ";\n", NULL);
        generateStructDelete(context, type);
    }
}

void generateStructTypedef(CodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->state;
    format(state, "typedef struct ", NULL);
    writeTypename(ctx, type);
    format(state, " ", NULL);
    writeTypename(ctx, type);
    format(state, ";\n", NULL);
}

void checkStructExpr(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *target = evalType(visitor, node->structExpr.left);
    if (target->tag != typStruct) {
        logError(ctx->L,
                 &node->structExpr.left->loc,
                 "unsupported type used with struct initializer, '{t}' is not "
                 "a struct",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *field = node->structExpr.fields, *prev = node->structExpr.fields;
    bool *initialized =
        callocOrDie(1, sizeof(bool) * target->tStruct.fieldsCount);

    for (; field; field = field->next) {
        prev = field;
        AstNode *decl = findStructField(target, field->fieldExpr.name);
        if (decl == NULL && target->tStruct.base) {
            decl = findStructField(target->tStruct.base, field->fieldExpr.name);
            if (decl)
                field->flags |= flgAddSuper;
        }

        if (decl == NULL) {
            logError(
                ctx->L,
                &field->loc,
                "field '{s}' does not exist in target struct type '{t}'",
                ((FormatArg[]){{.s = field->fieldExpr.name}, {.t = target}}));
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        const Type *type = evalType(visitor, field->fieldExpr.value);
        if (!isTypeAssignableFrom(decl->type, type)) {
            logError(ctx->L,
                     &field->fieldExpr.value->loc,
                     "value type '{t}' is not assignable to field type '{t}'",
                     (FormatArg[]){{.t = type}, {.t = decl->type}});
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        initialized[decl->structField.index] = true;
    }

    if (node->type != ERROR_TYPE(ctx)) {
        for (u64 i = 0; prev && i < target->tStruct.fieldsCount; i++) {
            const AstNode *targetField = target->tStruct.fields[i].decl;
            if (initialized[i] || targetField->type->tag == typFunc ||
                targetField->structField.value == NULL)
                continue;

            prev->next = makeAstNode(
                ctx->pool,
                &prev->loc,
                &(AstNode){
                    .tag = astFieldExpr,
                    .type = targetField->type,
                    .flags = targetField->flags,
                    .fieldExpr = {.name = targetField->structField.name,
                                  .value = targetField->structField.value}});
            prev = prev->next;
        }
        node->type = target;
    }
}

void checkStructField(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    const Type *type = node->structField.type
                           ? evalType(visitor, node->structField.type)
                           : makeAutoType(ctx->typeTable);
    if (node->structField.value) {
        const Type *value = evalType(visitor, node->structField.value);
        if (!isTypeAssignableFrom(type, value)) {
            logError(ctx->L,
                     &node->structField.value->loc,
                     "field initializer of type '{t}' not compatible with "
                     "field type '{t}'",
                     (FormatArg[]){{.t = value}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        type = typeIs(type, Auto) ? value : type;
    }

    node->type = type;
    defineSymbol(ctx->env, ctx->L, node->structField.name, node);
}

void checkStructDecl(AstVisitor *visitor, AstNode *node)
{
    SemanticsContext *ctx = getAstVisitorContext(visitor);
    u64 numMembers = countAstNodes(node->structDecl.members);

    const Type *base = NULL;
    if (node->structDecl.base) {
        base = evalType(visitor, node->structDecl.base);
        if (base->tag != typStruct) {
            logError(ctx->L,
                     &node->structDecl.base->loc,
                     "a struct can only extend another struct type, got "
                     "unexpected type '{t}'",
                     (FormatArg[]){{.t = base}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    StructField *members = mallocOrDie(sizeof(StructField) * numMembers);
    AstNode *member = node->structDecl.members;
    const Type *this = makeThisType(
        ctx->typeTable, node->structDecl.name, flgConst & node->flags);

    node->type = this;
    defineSymbol(ctx->env, ctx->L, node->structDecl.name, node);
    addModuleExport(ctx, node, node->structDecl.name);

    Env *env = ctx->env;
    ctx->env = makeEnvironment(ctx->pool, NULL);
    pushScope(ctx->env, node);
    hookStructEnvironments(ctx, base, env);
    defineSymbol(ctx->env, ctx->L, "This", node);
    u64 i = 0;
    for (; member; member = member->next, i++) {
        member->parentScope = node;
        const Type *type;
        if (member->tag == astFuncDecl) {
            type = checkMethodDeclSignature(visitor, member);
            if (member->funcDecl.operatorOverload == opDelete)
                node->flags |= flgImplementsDelete;
        }
        else {
            type = evalType(visitor, member);
        }

        if (type == ERROR_TYPE(ctx)) {
            node->type = ERROR_TYPE(ctx);
            goto checkStructDecl_cleanup;
        }

        if (member->tag == astFuncDecl) {
            members[i] = (StructField){
                .name = member->funcDecl.name, .type = type, .decl = member};
        }
        else {
            members[i] = (StructField){
                .name = member->structField.name, .type = type, .decl = member};
            member->structField.index = i;
        }
    }

    node->type = makeStruct(ctx->typeTable,
                            &(Type){.tag = typStruct,
                                    .flags = node->flags,
                                    .name = node->structDecl.name,
                                    .tStruct = {.env = ctx->env,
                                                .base = base,
                                                .fields = members,
                                                .fieldsCount = i,
                                                .decl = node}});
    ((Type *)this)->this.that = node->type;

    member = node->structDecl.members;
    AstNode *prev = member;
    for (; member; member = member->next) {
        if (member->tag == astFuncDecl) {
            checkMethodDeclBody(visitor, member);
            if (member == node->structDecl.members) {
                node->structDecl.members = member->next;
                prev = node->structDecl.members;
            }
            else {
                prev->next = member->next;
            }

            member->next = NULL;
            addTopLevelDecl(ctx, NULL, member);
            member = prev;
            if (member == NULL)
                break;
        }
        else {
            prev = member;
        }
    }

    if (ctx->env->scope->next) {
        Env tmp = {.first = ctx->env->scope->next};
        environmentFree(&tmp);
        ctx->env->scope->next = NULL;
    }

checkStructDecl_cleanup:
    ctx->env = env;
    free(members);
}
