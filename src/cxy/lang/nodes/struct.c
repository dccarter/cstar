//
// Created by Carter on 2023-04-18.
//

#include "lang/codegen.h"
#include "lang/semantics.h"

#include "lang/ttable.h"

#include "core/alloc.h"

static inline void hookStructEnvironments(SemanticsContext *ctx,
                                          const Type *base,
                                          Env *root)
{
    if (base) {
        environmentAttachUp((Env *)base->tStruct.env, root);
        environmentAttachUp(&ctx->env, (Env *)base->tStruct.env);
    }
    else {
        environmentAttachUp(&ctx->env, root);
    }
}

static inline void unHookStructEnvironments(SemanticsContext *ctx,
                                            const Type *base)
{
    if (base) {
        environmentDetachUp((Env *)base->tStruct.env);
        environmentDetachUp(&ctx->env);
    }
    else {
        environmentDetachUp(&ctx->env);
    }
}

static inline AstNode *findStructField(const Type *type, cstring name)
{
    Env env = {.first = type->tStruct.env->scope,
               .scope = type->tStruct.env->scope};
    return findSymbolOnly(&env, name);
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

    for (u64 i = 0; i < type->tStruct.fieldsCount; i++) {
        const StructField *field = &type->tStruct.fields[i];
        if (typeIs(field->type, Func) || typeIs(field->type, Generic))
            continue;

        if (i != 0)
            format(state, "\n", NULL);

        generateTypeUsage(context, field->type);
        format(state, " {s};", (FormatArg[]){{.s = field->name}});
    }
    format(state, "{<}\n}", NULL);
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
        for (u64 i = 0; i < target->tStruct.fieldsCount; i++) {
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
        type = value;
    }

    node->type = type;
    defineSymbol(&ctx->env, ctx->L, node->structField.name, node);
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
    defineSymbol(&ctx->env, ctx->L, node->structDecl.name, node);
    addModuleExport(ctx, node, node->structDecl.name);

    Env env = ctx->env;
    environmentInit(&ctx->env);
    pushScope(&ctx->env, node);
    hookStructEnvironments(ctx, base, &env);

    u64 i = 0;
    for (; member; member = member->next, i++) {
        member->parentScope = node;
        const Type *type;
        if (member->tag == astFuncDecl) {
            type = checkMethodDeclSignature(visitor, member);
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

    unHookStructEnvironments(ctx, base);

    Env structEnv = {NULL};
    releaseScope(&ctx->env, &structEnv);

    node->type = makeStruct(ctx->typeTable,
                            &(Type){.tag = typStruct,
                                    .flags = node->flags,
                                    .name = node->structDecl.name,
                                    .tStruct = {.env = &structEnv,
                                                .base = base,
                                                .fields = members,
                                                .fieldsCount = i}});
    ((Type *)this)->this.that = node->type;

    environmentFree(&ctx->env);

    ctx->env = structEnv;
    hookStructEnvironments(ctx, base, &env);

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

    if (ctx->env.scope && ctx->env.scope->next) {
        Env tmp = {.first = ctx->env.first->next};
        ctx->env.first->next = NULL;
        ctx->env = tmp;
    }
    else {
        ctx->env = (Env){.first = NULL};
    }

checkStructDecl_cleanup:
    unHookStructEnvironments(ctx, base);

    environmentFree(&ctx->env);
    ctx->env = env;

    free(members);
}
