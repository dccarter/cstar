//
// Created by Carter Mbotho on 2023-08-03.
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

#include <string.h>

static void preCheckMembers(AstVisitor *visitor,
                            AstNode *node,
                            NamedTypeMember *members)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->structDecl.members;

    for (u64 i = 0; member; member = member->next, i++) {
        const Type *type;
        if (nodeIs(member, FuncDecl)) {
            type = checkFunctionSignature(visitor, member);
            if (member->funcDecl.operatorOverload == opDeinitialize)
                node->flags |= flgImplementsDelete;
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

void generateStructDelete(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    format(state, "\nattr(always_inline)\nstatic void ", NULL);
    writeTypename(context, type);
    format(state, "__builtin_destructor(void *ptr) {{{>}\n", NULL);
    writeTypename(context, type);
    format(state, " *this = ptr;\n", NULL);

    if (hasFlag(type, ImplementsDelete)) {
        writeTypename(context, type);
        format(state, "__op__delete(this);", NULL);
    }
    else {
        format(state, "\n", NULL);
        u64 y = 0;
        for (u64 i = 0; i < type->tStruct.members->count; i++) {
            const NamedTypeMember *field = &type->tStruct.members->members[i];
            if (typeIs(field->type, Func) || typeIs(field->type, Generic) ||
                typeIs(field->type, Struct))
                continue;

            const Type *unwrapped = unwrapType(field->type, NULL);
            const Type *stripped = stripAll(field->type);

            if (typeIs(unwrapped, Pointer) || typeIs(unwrapped, String) ||
                isSliceType(unwrapped)) {
                if (y++ != 0)
                    format(state, "\n", NULL);

                format(state,
                       "CXY__free((void *)this->{s});",
                       (FormatArg[]){{.s = field->name}});
            }
            else if (typeIs(stripped, Struct) || typeIs(stripped, Array) ||
                     typeIs(stripped, Tuple)) {
                if (y++ != 0)
                    format(state, "\n", NULL);

                writeTypename(context, stripped);
                if (typeIs(unwrapped, Pointer))
                    format(state,
                           "__builtin_destructor(this->{s});",
                           (FormatArg[]){{.s = field->name}});
                else
                    format(state,
                           "__builtin_destructor(&this->{s});",
                           (FormatArg[]){{.s = field->name}});
            }
        }
    }

    format(state, "{<}\n}\n", NULL);
}

void buildStringOperatorForMember(CodegenContext *context,
                                  const Type *type,
                                  cstring name,
                                  u64 deref)
{
    FormatState *state = context->state;
    const Type *unwrapped = unwrapType(type, NULL);
    const Type *stripped = stripAll(type);

    deref += isSliceType(type);

    format(state,
           "CXY__builtins_string_builder_append_cstr0(sb->sb, "
           "\"{s}: \", {u64});\n",
           (FormatArg[]){{.s = name}, {.u64 = strlen(name) + 2}});
    if (typeIs(unwrapped, Pointer)) {
        format(state,
               "if (this->{s} == NULL) {{{>}\n"
               "CXY__builtins_string_builder_append_cstr0(sb->sb, \"null\", "
               "4);\n",
               (FormatArg[]){{.s = name}});

        format(state,
               "CXY__builtins_string_builder_append_char(sb->sb, "
               "'&');{<}\n}\nelse{>}\n",
               NULL);
        deref = pointerLevels(unwrapped);
    }

    switch (stripped->tag) {
    case typNull:
        format(state,
               "CXY__builtins_string_builder_append_cstr0(sb->sb, "
               "\"null\", 4);",
               NULL);
        break;
    case typPrimitive:
        switch (stripped->primitive.id) {
        case prtBool:
            format(state,
                   "CXY__builtins_string_builder_append_bool(sb->sb, "
                   "{cl}this->{s});",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = name}});
            break;
        case prtChar:
            format(state,
                   "CXY__builtins_string_builder_append_char(sb->sb, "
                   "{cl}this->{s});",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = name}});
            break;
#define f(I, ...) case prt##I:
            INTEGER_TYPE_LIST(f)
            format(state,
                   "CXY__builtins_string_builder_append_int(sb->sb, "
                   "(i64)({cl}this->{s}));",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = name}});
            break;
#undef f
#define f(I, ...) case prt##I:
            FLOAT_TYPE_LIST(f)
            format(state,
                   "CXY__builtins_string_builder_append_float(sb->sb, "
                   "(f64)({cl}this->{s}));",
                   (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = name}});
            break;
#undef f
        default:
            unreachable("UNREACHABLE");
        }
        break;

    case typString:
        format(state,
               "CXY__builtins_string_builder_append_cstr1(sb->sb, "
               "{cl}this->{s});",
               (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = name}});
        break;
    case typArray:
    case typStruct:
    case typTuple:
        writeTypename(context, stripped);
        format(state,
               "__toString({cl}this->{s}, sb);",
               (FormatArg[]){{.c = deref ? '*' : '&'},
                             {.len = deref ? deref - 1 : 1},
                             {.s = name}});
        break;
    case typOpaque:
        format(state,
               "CXY__builtins_string_builder_append_cstr1(sb->sb, "
               "\"<opaque:",
               NULL);
        writeTypename(context, type);
        format(state, ">\");", NULL);
        break;
    case typEnum:
        format(
            state, "CXY__builtins_string_builder_append_cstr1(sb->sb, ", NULL);
        writeEnumPrefix(context, type);
        format(state,
               "__get_name({cl}this->{s}));",
               (FormatArg[]){{.c = '*'}, {.len = deref}, {.s = name}});
        break;
    default:
        unreachable("UNREACHABLE");
    }

    if (typeIs(unwrapped, Pointer))
        format(state, "{<}", NULL);
    format(state, "\n", NULL);
}

static const Type *getBuiltinStringBuilderType()
{
    static const Type *sb = NULL;
    if (sb == NULL)
        sb = findBuiltinType(S_StringBuilder);

    return sb;
}

static bool structHasToString(const Type *type)
{
    const Type *sb = getBuiltinStringBuilderType();
    if (sb == NULL || stripAll(type) == sb)
        return true;

    const NamedTypeMember *toString = findStructMember(type, "toString");
    if (toString == NULL)
        return false;

    const AstNode *symbol = toString->decl;
    while (symbol) {
        const Type *nodeType = symbol->type;
        if (!hasFlags(symbol, flgBuiltinMember) &&
            nodeType->func.paramsCount == 1 &&
            stripAll(nodeType->func.params[0]) == sb)
            return true;
        symbol = symbol->list.link;
    }

    return false;
}

static void generateStructToString(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    if (structHasToString(type))
        return;

    format(state, "\nstatic void ", NULL);
    writeTypename(context, type);
    format(state, "__toString(", NULL);
    writeTypename(context, type);
    format(state, " *this, StringBuilder *sb) {{{>}\n", NULL);
    format(state, "CXY__builtins_string_builder_append_cstr1(sb->sb, \"", NULL);
    writeTypename(context, type);
    format(state, "{{\");\n", NULL);

    u64 y = 0;
    for (u64 i = 0; i < type->tStruct.members->count; i++) {
        const NamedTypeMember *field = &type->tStruct.members->members[i];
        if (typeIs(field->type, Func) || typeIs(field->type, Generic) ||
            typeIs(field->type, Struct))
            continue;

        if (y++ != 0)
            format(state,
                   "CXY__builtins_string_builder_append_cstr0(sb->sb, "
                   "\", \", 2);\n",
                   NULL);

        buildStringOperatorForMember(context, field->type, field->name, 0);
    }

    format(
        state, "CXY__builtins_string_builder_append_char(sb->sb, '}');", NULL);

    format(state, "{<}\n}\n", NULL);
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
        format(ctx->state,
               ".{s}{s} = ",
               (FormatArg[]){{.s = hasFlag(field, Inherited) ? "super." : ""},
                             {.s = field->fieldExpr.name}});

        if (isSliceType(field->type) &&
            !isSliceType(field->fieldExpr.value->type))
            generateArrayToSlice(visitor, field->type, field->fieldExpr.value);
        else
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
    format(state, "void *__mgmt;\n", NULL);

    u64 y = 0;
    for (u64 i = 0; i < type->tStruct.members->count; i++) {
        const NamedTypeMember *field = &type->tStruct.members->members[i];
        if (typeIs(field->type, Func) || typeIs(field->type, Generic))
            continue;

        if (y++ != 0)
            format(state, "\n", NULL);

        generateTypeUsage(context, field->type);
        format(state, " {s};", (FormatArg[]){{.s = field->name}});
    }

    format(state, "{<}\n};\n", NULL);

    generateStructDelete(context, type);
    generateStructToString(context, type);
}

void generateStructDecl(ConstAstVisitor *visitor, const AstNode *node)
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

void generateStructTypedef(CodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->state;
    format(state, "typedef struct ", NULL);
    writeTypename(ctx, type);
    format(state, " ", NULL);
    writeTypename(ctx, type);
    format(state, ";\n", NULL);
}

bool isExplicitConstructableFrom(TypingContext *ctx,
                                 const Type *type,
                                 const Type *from)
{
    if (!typeIs(type, Struct))
        return isTypeAssignableFrom(type, from);

    const Type *constructor = findStructMemberType(type, S_Initializer);
    if (constructor == NULL)
        return false;

    constructor = matchOverloadedFunction(
        ctx, constructor, (const Type *[]){from}, 1, NULL, flgNone);

    if (constructor == NULL ||
        findAttribute(constructor->func.decl, S_explicit))
        return false;

    if (constructor->func.paramsCount != 1)
        return false;

    const Type *param = constructor->func.params[0];
    if (!typeIs(param, Struct))
        return isTypeAssignableFrom(param, from);

    if (!isExplicitConstructableFrom(ctx, param, from))
        return false;

    return true;
}

bool evalExplicitConstruction(AstVisitor *visitor,
                              const Type *type,
                              AstNode *node)
{
    const Type *source = node->type ?: checkType(visitor, node);
    TypingContext *ctx = getAstVisitorContext(visitor);

    if (isTypeAssignableFrom(type, source))
        return true;

    if (!typeIs(type, Struct))
        return false;

    const NamedTypeMember *member = findStructMember(type, S_Initializer);
    if (member == NULL)
        return false;

    const Type *constructor =
        matchOverloadedFunction(ctx,
                                member->type,
                                (const Type *[]){node->type},
                                1,
                                &node->loc,
                                flgNone);
    if (constructor == NULL ||
        findAttribute(constructor->func.decl, "explicit"))
        return false;

    if (constructor->func.paramsCount != 1)
        return false;

    const Type *param = constructor->func.params[0];
    if (!evalExplicitConstruction(visitor, param, node))
        return false;

    AstNode *args = copyAstNode(ctx->pool, node);

    node->tag = astCallExpr;
    node->type = NULL;
    node->flags = flgNone;
    node->callExpr.callee =
        makePath(ctx->pool, &node->loc, type->name, flgNone, type);
    node->callExpr.args = args;

    type = transformToConstructCallExpr(visitor, node);
    return !typeIs(type, Error);
}

void checkStructExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = checkType(visitor, node->structExpr.left);
    if (typeIs(target, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!typeIs(target, Struct)) {
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
        callocOrDie(1, sizeof(bool) * target->tStruct.members->count);

    for (; field; field = field->next) {
        prev = field;
        const NamedTypeMember *member =
            findStructMember(target, field->fieldExpr.name);
        if (member == NULL) {
            logError(
                ctx->L,
                &field->loc,
                "field '{s}' does not exist in target struct type '{t}'",
                ((FormatArg[]){{.s = field->fieldExpr.name}, {.t = target}}));
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        if (!nodeIs(member->decl, Field)) {
            logError(
                ctx->L,
                &field->loc,
                "member '{s}' is not a field, only struct can be initialized",
                (FormatArg[]){{.s = field->fieldExpr.name}});
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        const Type *type = checkType(visitor, field->fieldExpr.value);
        if (!isTypeAssignableFrom(member->type, type)) {
            logError(ctx->L,
                     &field->fieldExpr.value->loc,
                     "value type '{t}' is not assignable to field type '{t}'",
                     (FormatArg[]){{.t = type}, {.t = member->type}});
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        field->type = member->type;
        initialized[member->decl->structField.index] = true;
    }

    if (node->type == ERROR_TYPE(ctx))
        return;

    for (u64 i = 0; i < target->tStruct.members->count; i++) {
        const AstNode *targetField = target->tStruct.members->members[i].decl;
        if (initialized[i] || !nodeIs(targetField, Field))
            continue;

        if (targetField->structField.value == NULL) {
            logError(
                ctx->L,
                &node->loc,
                "initializer expression missing struct required member '{s}'",
                (FormatArg[]){{.s = targetField->structField.name}});
            logNote(
                ctx->L, &targetField->loc, "struct field declared here", NULL);
            node->type = ERROR_TYPE(ctx);
            continue;
        }
        AstNode *temp = makeAstNode(
            ctx->pool,
            &(prev ?: node)->loc,
            &(AstNode){.tag = astFieldExpr,
                       .type = targetField->type,
                       .flags = targetField->flags,
                       .fieldExpr = {.name = targetField->structField.name,
                                     .value = targetField->structField.value}});
        if (prev)
            prev = prev->next = temp;
        else
            prev = node->structExpr.fields = temp;
    }

    if (node->type != ERROR_TYPE(ctx))
        node->type = target;
}

void checkStructDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);

    const Type **implements = NULL;
    u64 implementsCount = countAstNodes(node->structDecl.implements);
    if (implementsCount) {
        u64 count = countAstNodes(node->structDecl.implements);
        implements = mallocOrDie(sizeof(Type *) * count);
        checkImplements(visitor, node, implements, count);
    }

    if (typeIs(node->type, Error))
        goto checkStructInterfacesError;

    u64 membersCount = countAstNodes(node->structDecl.members);
    NamedTypeMember *members =
        mallocOrDie(sizeof(NamedTypeMember) * membersCount);
    node->structDecl.thisType =
        node->structDecl.thisType
            ?: makeThisType(ctx->types, node->structDecl.name, flgNone);
    const Type *this = node->structDecl.thisType;

    ctx->currentStruct = node;
    preCheckMembers(visitor, node, members);
    ctx->currentStruct = NULL;

    if (typeIs(node->type, Error))
        goto checkStructMembersError;

    ((Type *)this)->this.that = makeStructType(ctx->types,
                                               getDeclarationName(node),
                                               members,
                                               membersCount,
                                               node,
                                               implements,
                                               implementsCount,
                                               node->flags & flgTypeApplicable);
    node->type = this;

    ctx->currentStruct = node;
    if (checkMemberFunctions(visitor, node, members)) {
        node->type = replaceStructType(ctx->types,
                                       this->this.that,
                                       members,
                                       membersCount,
                                       node,
                                       implements,
                                       implementsCount,
                                       node->flags & flgTypeApplicable);
        ((Type *)this)->this.that = node->type;
    }
    else
        node->type = this->this.that;

    ctx->currentStruct = NULL;

    if (!checkTypeImplementsAllMembers(ctx, node))
        node->type = ERROR_TYPE(ctx);

checkStructMembersError:
    if (members)
        free(members);

checkStructInterfacesError:
    if (implements)
        free(implements);
}
