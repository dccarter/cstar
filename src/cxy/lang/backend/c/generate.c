//
// Created by Carter Mbotho on 2024-08-21.
//

#include "driver/driver.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/builtins.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

typedef struct CBackend {
    cstring filename;
    FILE *output;
    bool testMode;
} CBackend;

typedef struct CodegenContext {
    CBackend *backend;
    StrPool *strings;
    FormatState types;
    FormatState state;
    cstring loopUpdate;
    bool loopUpdateUsed;
    bool hasTestCases;
    bool memTraceEnabled;
    bool inLoop;
    struct {
        bool enabled;
        FilePos pos;
    } debug;
} CodegenContext;

#define getState(ctx) &(ctx)->state
#define typeState(ctx) &(ctx)->types

static void generateType(CodegenContext *ctx, const Type *type);
static void generateTypeName(CodegenContext *ctx,
                             FormatState *state,
                             const Type *type);
static void generateFunctionName(FormatState *state, const AstNode *node);

static void addDebugInfo(CodegenContext *ctx, const AstNode *node)
{
    if (node == NULL)
        return;
    FilePos pos = node->loc.begin;
    if (ctx->debug.enabled && (pos.row != ctx->debug.pos.row)) {
        format(getState(ctx),
               "#line {u32} \"{s}\"\n",
               (FormatArg[]){{.u32 = pos.row}, {.s = node->loc.fileName}});
        ctx->debug.pos = pos;
    }
}

static void generateMany(ConstAstVisitor *visitor,
                         const AstNode *nodes,
                         cstring open,
                         cstring sep,
                         cstring close)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (open)
        format(getState(ctx), open, NULL);
    const AstNode *node = nodes;
    for (; node; node = node->next) {
        if (sep && node != nodes)
            format(getState(ctx), sep, NULL);
        astConstVisit(visitor, node);
    }
    if (close)
        format(getState(ctx), close, NULL);
}

static void generateCustomTypeName(CodegenContext *cyx,
                                   FormatState *state,
                                   const Type *type,
                                   cstring alt)
{
    if (!typeIs(type, Func) || type->func.decl == NULL) {
        if (type->ns)
            format(state, "{s}_", (FormatArg[]){{.s = type->ns}});
        if (type->name)
            format(state, "{s}", (FormatArg[]){{.s = type->name}});
        else
            format(state,
                   "{s}{u64}",
                   (FormatArg[]){{.s = alt}, {.u64 = type->index}});
    }
    else {
        AstNode *decl = type->func.decl;
        generateFunctionName(state, decl);
        format(state, "_t", NULL);
    }
}

static void generateStructAttributes(CodegenContext *ctx, const Type *type)
{
    const AstNode *decl = getTypeDecl(type);
    if (decl == NULL)
        return;
    if (findAttribute(decl, S_packed))
        format(typeState(ctx), " __attribute__((packed))", NULL);
    const AstNode *aligned = findAttribute(decl, S_align);
    if (aligned) {
        const AstNode *value = getAttributeArgument(NULL, NULL, aligned, 0);
        if (value)
            format(typeState(ctx),
                   " __attribute__((aligned({u64})))",
                   (FormatArg[]){{.u64 = value->intLiteral.uValue}});
    }
    format(typeState(ctx), " ", NULL);
}

static void generateArrayType(CodegenContext *ctx, const Type *type)
{
    if (!type->array.elementType->codegen)
        generateType(ctx, type->array.elementType);
    format(typeState(ctx), "typedef ", NULL);
    generateTypeName(ctx, typeState(ctx), type->array.elementType);
    format(typeState(ctx), " ", NULL);
    generateTypeName(ctx, typeState(ctx), type);
    format(typeState(ctx), "[", NULL);
    if (type->array.len != UINT64_MAX)
        format(
            typeState(ctx), "{u64}", (FormatArg[]){{.u64 = type->array.len}});
    format(typeState(ctx), "];\n", NULL);
}

static void generateTupleType(CodegenContext *ctx, const Type *type)
{
    for (u64 i = 0; i < type->tuple.count; i++) {
        const Type *member = type->tuple.members[i];
        if (!member->generated)
            generateType(ctx, type->tuple.members[i]);
    }

    format(typeState(ctx), "typedef struct ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Tuple");
    format(typeState(ctx), " {{{>}", NULL);
    for (u64 i = 0; i < type->tuple.count; i++) {
        format(typeState(ctx), "\n", NULL);
        generateTypeName(ctx, typeState(ctx), type->tuple.members[i]);
        format(typeState(ctx), " _{u64};", (FormatArg[]){{.u64 = i}});
    }
    format(typeState(ctx), "{<}\n} ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Tuple");
    format(typeState(ctx), ";\n\n", NULL);
}

static void generateEnumType(CodegenContext *ctx, const Type *type)
{
    if (!type->tEnum.base->generated)
        generateType(ctx, type->tEnum.base);
    format(typeState(ctx), "typedef ", NULL);
    generateTypeName(ctx, typeState(ctx), type->tEnum.base);
    format(typeState(ctx), " ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Enum");
    format(typeState(ctx), ";\nenum {{{>}", NULL);

    for (u64 i = 0; i < type->tEnum.optionsCount; i++) {
        EnumOptionDecl *option = &type->tEnum.options[i];
        format(typeState(ctx), "\n", NULL);
        generateCustomTypeName(ctx, typeState(ctx), type, "Enum");
        if (option->decl->enumOption.value)
            format(typeState(ctx),
                   "_{s} = {i64},",
                   (FormatArg[]){{.s = option->name}, {.i64 = option->value}});
        else
            format(typeState(ctx), "_{s},", (FormatArg[]){{.s = option->name}});
    }
    format(typeState(ctx), "{<}\n};\n\n", NULL);
}

static void generateClassType(CodegenContext *ctx, const Type *type)
{
    format(typeState(ctx), "typedef struct ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Class");
    format(typeState(ctx), " ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Class");
    format(typeState(ctx), ";\n", NULL);

    for (u64 i = 0; i < type->tClass.members->count; i++) {
        const NamedTypeMember *member = &type->tClass.members->members[i];
        if (nodeIs(member->decl, FieldDecl)) {
            if (!member->type->generated)
                generateType(ctx, member->type);
        }
    }

    format(typeState(ctx), "struct ", NULL);
    generateStructAttributes(ctx, type);
    generateCustomTypeName(ctx, typeState(ctx), type, "Class");
    format(typeState(ctx), " {{{>}", NULL);
    for (u64 i = 0; i < type->tClass.members->count; i++) {
        const NamedTypeMember *member = &type->tClass.members->members[i];
        if (!nodeIs(member->decl, FieldDecl))
            continue;
        format(typeState(ctx), "\n", NULL);
        generateTypeName(ctx, typeState(ctx), member->type);
        format(typeState(ctx), " {s};", (FormatArg[]){{.s = member->name}});
    }

    format(typeState(ctx), "{<}\n};\n\n", NULL);
}

static void generateStructType(CodegenContext *ctx, const Type *type)
{
    format(typeState(ctx), "typedef struct ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Struct");
    if (hasFlag(type, Extern))
        format(typeState(ctx), " _", NULL);
    else
        format(typeState(ctx), " ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Struct");
    format(typeState(ctx), ";\n", NULL);
    ((Type *)type)->generating = true;
    for (u64 i = 0; i < type->tStruct.members->count; i++) {
        const NamedTypeMember *member = &type->tStruct.members->members[i];
        if (nodeIs(member->decl, FieldDecl)) {
            if (member->type->generating) {
                ((Type *)type)->generating = false;
                return;
            }
            if (!member->type->generated)
                generateType(ctx, member->type);
        }
    }
    ((Type *)type)->generating = false;

    format(typeState(ctx), "struct ", NULL);
    generateStructAttributes(ctx, type);
    generateCustomTypeName(ctx, typeState(ctx), type, "Struct");
    format(typeState(ctx), " {{{>}", NULL);
    for (u64 i = 0; i < type->tStruct.members->count; i++) {
        const NamedTypeMember *member = &type->tStruct.members->members[i];
        if (!nodeIs(member->decl, FieldDecl))
            continue;
        format(typeState(ctx), "\n", NULL);
        generateTypeName(ctx, typeState(ctx), member->type);
        if (member->decl->structField.bits != 0)
            format(typeState(ctx),
                   " {s}:{u32};",
                   (FormatArg[]){{.s = member->name},
                                 {.u32 = member->decl->structField.bits}});
        else
            format(typeState(ctx), " {s};", (FormatArg[]){{.s = member->name}});
    }
    format(typeState(ctx), "{<}\n};\n\n", NULL);
}

static void generateExceptionType(CodegenContext *ctx, const Type *type)
{
    format(typeState(ctx), "typedef struct ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Exception");
    if (hasFlag(type, Extern))
        format(typeState(ctx), " _", NULL);
    else
        format(typeState(ctx), " ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Exception");
    format(typeState(ctx), ";\n", NULL);
    ((Type *)type)->generating = true;
    const AstNode *member = type->exception.decl->exception.params;
    for (; member; member = member->next) {
        if (!member->type->generated)
            generateType(ctx, member->type);
    }
    ((Type *)type)->generating = false;

    format(typeState(ctx), "struct ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Struct");
    format(typeState(ctx), " {{{>}", NULL);
    member = type->exception.decl->exception.params;
    for (; member; member = member->next) {
        format(typeState(ctx), "\n", NULL);
        generateTypeName(ctx, typeState(ctx), member->type);
        format(typeState(ctx), " {s};", (FormatArg[]){{.s = member->_name}});
    }
    format(typeState(ctx), "{<}\n};\n\n", NULL);
}

static void generateUnionType(CodegenContext *ctx, const Type *type)
{
    if (!type->generating) {
        format(typeState(ctx), "typedef struct ", NULL);
        generateCustomTypeName(ctx, typeState(ctx), type, "Union");
        format(typeState(ctx), " ", NULL);
        generateCustomTypeName(ctx, typeState(ctx), type, "Union");
        format(typeState(ctx), ";\n", NULL);
    }

    ((Type *)type)->generating = true;
    for (u64 i = 0; i < type->tUnion.count; i++) {
        const Type *member = type->tUnion.members[i].type;
        if (member->generating) {
            ((Type *)type)->generated = false;
            return;
        }
        if (!member->generated)
            generateType(ctx, member);
    }
    ((Type *)type)->generating = false;

    format(typeState(ctx), "struct ", NULL);
    generateStructAttributes(ctx, type);
    generateCustomTypeName(ctx, typeState(ctx), type, "Union");
    format(typeState(ctx), "{{{>}\n", NULL);
    format(typeState(ctx), "uint32_t tag;\n", NULL);
    format(typeState(ctx), "union {{{>}", NULL);
    for (u64 i = 0; i < type->tUnion.count; i++) {
        const Type *member = type->tUnion.members[i].type;
        format(typeState(ctx), "\n", NULL);
        generateTypeName(ctx, typeState(ctx), member);
        format(typeState(ctx), " _{u64};", (FormatArg[]){{.u64 = i}});
    }
    format(typeState(ctx), "{<}\n};", NULL);
    format(typeState(ctx), "{<}\n};\n\n", NULL);
}

static void generateResultType(CodegenContext *ctx, const Type *type)
{
    if (!type->generating) {
        format(typeState(ctx), "typedef struct ", NULL);
        generateCustomTypeName(ctx, typeState(ctx), type, "Result");
        format(typeState(ctx), " ", NULL);
        generateCustomTypeName(ctx, typeState(ctx), type, "Result");
        format(typeState(ctx), ";\n", NULL);
    }

    ((Type *)type)->generating = true;
    if (!type->result.target->generated)
        generateType(ctx, type->result.target);
    ((Type *)type)->generating = false;

    format(typeState(ctx), "struct ", NULL);
    generateStructAttributes(ctx, type);
    generateCustomTypeName(ctx, typeState(ctx), type, "Result");
    format(typeState(ctx), "{{{>}\n", NULL);
    format(typeState(ctx), "uint32_t tag;\n", NULL);
    format(typeState(ctx), "union {{{>}", NULL);
    format(typeState(ctx), "\n", NULL);

    if (!isVoidType(type->result.target)) {
        generateTypeName(ctx, typeState(ctx), type->result.target);
        format(typeState(ctx), " _0;\n", NULL);
    }
    format(typeState(ctx), "void *_1;", NULL);

    format(typeState(ctx), "{<}\n};", NULL);
    format(typeState(ctx), "{<}\n};\n\n", NULL);
}

static void generateUntaggedUnionType(CodegenContext *ctx, const Type *type)
{
    format(typeState(ctx), "typedef union ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Union");
    format(typeState(ctx), " ", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Union");
    format(typeState(ctx), ";\n", NULL);

    ((Type *)type)->generating = true;
    for (u64 i = 0; i < type->untaggedUnion.members->count; i++) {
        const Type *member = type->untaggedUnion.members->members[i].type;
        if (member->generating)
            return;
        if (!member->generated)
            generateType(ctx, member);
    }
    ((Type *)type)->generating = false;

    format(typeState(ctx), "union ", NULL);
    generateStructAttributes(ctx, type);
    generateCustomTypeName(ctx, typeState(ctx), type, "Union");
    format(typeState(ctx), " {{{>}", NULL);
    for (u64 i = 0; i < type->untaggedUnion.members->count; i++) {
        const NamedTypeMember *member =
            &type->untaggedUnion.members->members[i];
        format(typeState(ctx), "\n", NULL);
        generateTypeName(ctx, typeState(ctx), member->type);
        format(typeState(ctx), " {s};", (FormatArg[]){{.s = member->name}});
    }
    format(typeState(ctx), "{<}\n};\n\n", NULL);
}

static void generateFunctionType(CodegenContext *ctx, const Type *type)
{
    const AstNode *decl = getTypeDecl(type);
    generateType(ctx, type->func.retType);
    bool isMember = false;
    if (decl && decl->funcDecl.this_) {
        generateType(ctx, decl->funcDecl.this_->type);
        isMember = true;
    }

    for (u64 i = 0; i < type->func.paramsCount; i++) {
        generateType(ctx, type->func.params[i]);
    }

    format(typeState(ctx), "typedef ", NULL);
    generateTypeName(ctx, typeState(ctx), type->func.retType);
    format(typeState(ctx), "(*", NULL);
    generateCustomTypeName(ctx, typeState(ctx), type, "Func");
    format(typeState(ctx), ")(", NULL);

    if (isMember)
        generateTypeName(ctx, typeState(ctx), decl->funcDecl.this_->type);

    for (u64 i = 0; i < type->func.paramsCount; i++) {
        if (isMember || i != 0)
            format(typeState(ctx), ", ", NULL);
        generateTypeName(ctx, typeState(ctx), type->func.params[i]);
    }
    format(typeState(ctx), ");\n", NULL);
}

static void generateType(CodegenContext *ctx, const Type *type)
{
    if (type->generated)
        return;

    ((Type *)type)->generated = true;
    switch (type->tag) {
    case typClass:
        generateClassType(ctx, type);
        break;
    case typStruct:
        generateStructType(ctx, type);
        break;
    case typException:
        generateExceptionType(ctx, type);
        break;
    case typTuple:
        generateTupleType(ctx, type);
        break;
    case typUnion:
        generateUnionType(ctx, type);
        break;
    case typResult:
        generateResultType(ctx, type);
        break;
    case typUntaggedUnion:
        generateUntaggedUnionType(ctx, type);
        break;
    case typFunc:
        generateFunctionType(ctx, type);
        break;
    case typEnum:
        generateEnumType(ctx, type);
        break;
    case typArray:
        generateArrayType(ctx, type);
        break;
    case typThis:
        generateType(ctx, type->_this.that);
        break;
    case typWrapped:
        generateType(ctx, type->wrapped.target);
        break;
    case typAlias:
        generateType(ctx, type->alias.aliased);
        break;
    case typPointer:
        generateType(ctx, type->pointer.pointed);
        break;
    case typReference:
        generateType(ctx, type->reference.referred);
        break;
    default:
        break;
    }
}

static void generateTypeName(CodegenContext *ctx,
                             FormatState *state,
                             const Type *type)
{
    if (!type->generating)
        generateType(ctx, type);

    u64 flags = flgNone;
    type = unwrapType(type, &flags);
    if (flags & flgConst && !typeIs(type, String))
        format(state, "const ", NULL);

    switch (type->tag) {
    case typPrimitive:
        switch (type->primitive.id) {
        case prtBool:
            format(state, "bool", NULL);
            break;
        case prtCChar:
            format(state, "char", NULL);
            break;
        case prtChar:
            format(state, "wchar_t", NULL);
            break;
        case prtI8:
            format(state, "int8_t", NULL);
            break;
        case prtU8:
            format(state, "uint8_t", NULL);
            break;
        case prtI16:
            format(state, "int16_t", NULL);
            break;
        case prtU16:
            format(state, "uint16_t", NULL);
            break;
        case prtI32:
            format(state, "int32_t", NULL);
            break;
        case prtU32:
            format(state, "uint32_t", NULL);
            break;
        case prtI64:
            format(state, "int64_t", NULL);
            break;
        case prtU64:
            format(state, "uint64_t", NULL);
            break;
        case prtF32:
            format(state, "float", NULL);
            break;
        case prtF64:
            format(state, "double", NULL);
            break;
        default:
            break;
        }
        break;
    case typVoid:
        format(state, "void", NULL);
        break;
    case typString:
        format(state, "const char*", NULL);
        break;
    case typNull:
        format(state, "void *", NULL);
        break;
    case typArray:
        format(state, "Array{u64}", (FormatArg[]){{.u64 = type->index}});
        break;
    case typPointer:
        generateTypeName(ctx, state, type->pointer.pointed);
        format(state, " *", NULL);
        break;
    case typReference:
        generateTypeName(ctx, state, type->reference.referred);
        if (!isClassType(type->reference.referred))
            format(state, " *", NULL);
        break;
    case typThis:
        generateTypeName(ctx, state, type->_this.that);
        break;
    case typAlias:
        generateTypeName(ctx, state, type->alias.aliased);
        break;
    case typInfo:
        generateTypeName(ctx, state, type->info.target);
        break;
    case typClass:
        format(state, "struct ", NULL);
        generateCustomTypeName(ctx, state, type, "Class");
        format(state, " *", NULL);
        break;
    case typException:
        format(state, "struct ", NULL);
        generateCustomTypeName(ctx, state, type, "Exception");
        format(state, " *", NULL);
        break;
    case typResult:
        format(state, "struct ", NULL);
        generateCustomTypeName(ctx, state, type, "Result");
        break;
    case typEnum:
        generateCustomTypeName(ctx, state, type, "Enum");
        break;
    case typUnion:
        format(state, "struct ", NULL);
        generateCustomTypeName(ctx, state, type, "Union");
        break;
    case typTuple:
        format(state, "struct ", NULL);
        generateCustomTypeName(ctx, state, type, "Tuple");
        break;
    case typStruct:
        format(state, "struct ", NULL);
        generateCustomTypeName(ctx, state, type, "Struct");
        break;
    case typUntaggedUnion:
        format(state, "union ", NULL);
        generateCustomTypeName(ctx, state, type, "UntaggedUnion");
        break;
    case typFunc:
        generateCustomTypeName(ctx, state, type, "Func");
        break;
    case typOpaque:
        format(state, "struct {s}", (FormatArg[]){{.s = type->name}});
        break;
    default:
        csAssert0(false);
    }
}

static cstring getModuleNamespace(const AstNode *node)
{
    if (!isBuiltinsInitialized() || hasFlag(node, BuiltinsModule))
        return "builtins";
    return node->program.module ? node->program.module->moduleDecl.name : "";
}

static inline void generateModuleName(FormatState *state, const AstNode *node)
{
    csAssert0(nodeIs(node, Program));
    format(state, "{s}", (FormatArg[]){{.s = getModuleNamespace(node)}});
}

static inline void generateStructOrClassName(FormatState *state,
                                             const AstNode *node)
{
    csAssert0(isClassOrStructAstNode(node));
    AstNode *parent = node->parentScope;
    if (parent) {
        generateModuleName(state, parent);
        appendString(state, "_");
    }
    format(state, "{s}", (FormatArg[]){{.s = node->_namedNode.name}});
}

static void generateVariableName(FormatState *state, const AstNode *node)
{
    if (hasFlag(node, TopLevelDecl)) {
        AstNode *parent = node->parentScope;
        if (nodeIs(parent, Program))
            format(state,
                   "{s}_",
                   (FormatArg[]){{.s = getModuleNamespace(parent)}});
    }
    format(state,
           "{s}",
           (FormatArg[]){
               {.s = node->varDecl.name ?: node->varDecl.names->ident.value}});
}

static void generateFunctionName(FormatState *state, const AstNode *node)
{
    bool isPure = hasFlag(node, Pure) && hasFlag(node, TopLevelDecl);
    if (!hasFlags(node, flgExtern) && !isPure) {
        AstNode *parent = node->parentScope;
        if (nodeIs(parent, Program)) {
            generateModuleName(state, parent);
            appendString(state, "_");
        }
        else if (isClassOrStructAstNode(parent)) {
            generateStructOrClassName(state, parent);
            appendString(state, "_");
        }
        else if (node->type->name) {
            csAssert0(false);
        }
    }
    if (nodeIs(node, FuncType)) {
        if (node->type->name)
            format(state, "{s}", (FormatArg[]){{.s = node->type->name}});
        else
            format(
                state, "Func{u64}", (FormatArg[]){{.u64 = node->type->index}});
    }
    else
        format(state, "{s}", (FormatArg[]){{.s = node->_namedNode.name}});
}

static void generateBfiCallWithFunc(ConstAstVisitor *visitor,
                                    const AstNode *func,
                                    const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    generateFunctionName(getState(ctx), func);
    if (isPointerOrReferenceType(node->type)) {
        format(getState(ctx), "(", NULL);
        astConstVisit(visitor, node);
        format(getState(ctx), ")", NULL);
    }
    else {
        format(getState(ctx), "(&(", NULL);
        astConstVisit(visitor, node);
        format(getState(ctx), "))", NULL);
    }
}
static void generateEnumOptionName(CodegenContext *ctx,
                                   FormatState *state,
                                   const AstNode *node)
{
    generateTypeName(ctx, state, node->type);
    if (nodeIs(node, Identifier))
        node = node->ident.resolvesTo;
    csAssert0(nodeIs(node, EnumOptionDecl));
    format(state, "_{s}", (FormatArg[]){{.s = node->enumOption.name}});
}

static void generateMainFunction(CodegenContext *ctx, const Type *type)
{
    const Type *ret = type->func.retType;
    generateTypeName(ctx, getState(ctx), ret);
    format(getState(ctx), " main(int argc, char *argv[]) {{{>}\n", NULL);
    if (type->func.paramsCount) {
        const Type *param = type->func.params[0];
        generateTypeName(ctx, getState(ctx), param);
        format(
            getState(ctx), " args = {{ .data = argv, .len = argc };\n", NULL);
        if (typeIs(ret, Void))
            appendString(getState(ctx), "_main(args);");
        else
            appendString(getState(ctx), "return _main(args);");
    }
    else {
        if (typeIs(ret, Void))
            appendString(getState(ctx), "_main();");
        else
            appendString(getState(ctx), "return _main();");
    }
    format(getState(ctx), "{<}\n}\n", NULL);
}

static inline bool isImportDeclReference(const AstNode *node)
{
    return nodeIs(node, Identifier) &&
           nodeIs(node->ident.resolvesTo, ImportDecl);
}

static void visitNullLit(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx), "nullptr", NULL);
}

static void visitBoolLit(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx), "{b}", (FormatArg[]){{.b = node->boolLiteral.value}});
}

static void visitCharLit(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->type->primitive.id == prtCChar) {
        format(getState(ctx),
               "'{cE}'",
               (FormatArg[]){{.c = node->charLiteral.value}});
    }
    else {
        format(getState(ctx),
               "'{cE}'",
               (FormatArg[]){{.c = node->charLiteral.value}});
    }
}

static void visitIntegerLit(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->intLiteral.isNegative)
        format(getState(ctx),
               "{i64}",
               (FormatArg[]){{.i64 = node->intLiteral.value}});
    else if (isUnsignedType(node->type))
        format(getState(ctx),
               "{u64}u",
               (FormatArg[]){{.u64 = node->intLiteral.uValue}});
    else
        format(getState(ctx),
               "{u64}",
               (FormatArg[]){{.u64 = node->intLiteral.uValue}});
}

static void visitFloatLit(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx),
           "{f64}",
           (FormatArg[]){{.f64 = node->floatLiteral.value}});
}

static void visitStringLit(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    cstring p = node->stringLiteral.value;
    format(getState(ctx), "\"", NULL);
    while (*p != '\0') {
        format(getState(ctx), "{cE}", (FormatArg[]){{.c = *p++}});
    }
    format(getState(ctx), "\"", NULL);
}

static void visitIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    AstNode *resolved = node->ident.resolvesTo;
    if (nodeIs(resolved, ExternDecl))
        resolved = resolved->externDecl.func;

    if (nodeIs(resolved, VarDecl))
        generateVariableName(getState(ctx), resolved);
    else if (nodeIs(resolved, FuncDecl))
        generateFunctionName(getState(ctx), resolved);
    else {
        cstring name = node->ident.value;
        if (name == S_super) {
            format(getState(ctx), "(", NULL);
            generateTypeName(
                ctx, getState(ctx), getTypeBase(stripReference(node->type)));
            format(getState(ctx), ")", NULL);
            name = S_this;
        }
        format(getState(ctx), "{s}", (FormatArg[]){{.s = name}});
    }
}

static void visitArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const Type *type = resolveAndUnThisType(node->type->array.elementType);
    if (!typeIs(type, Auto)) {
        format(getState(ctx), "(", NULL);
        generateTypeName(ctx, getState(ctx), node->type);
        format(getState(ctx), ")", NULL);
    }
    generateMany(visitor, node->arrayExpr.elements, "{{", ", ", "}");
}

static void visitBinaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->binaryExpr.lhs);
    format(getState(ctx),
           " {s} ",
           (FormatArg[]){{.s = getBinaryOpString(node->assignExpr.op)}});
    astConstVisit(visitor, node->binaryExpr.rhs);
}

static void visitAssignExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->assignExpr.lhs);
    format(getState(ctx),
           " {s} ",
           (FormatArg[]){{.s = getAssignOpString(node->assignExpr.op)}});
    astConstVisit(visitor, node->assignExpr.rhs);
}

static void visitTernaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->ternaryExpr.cond);
    format(getState(ctx), "?", NULL);
    if (node->ternaryExpr.body) {
        format(getState(ctx), " ", NULL);
        astConstVisit(visitor, node->ternaryExpr.body);
    }
    format(getState(ctx), ": ", NULL);
    astConstVisit(visitor, node->ternaryExpr.otherwise);
}

static void visitUnaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->unaryExpr.isPrefix) {
        switch (node->unaryExpr.op) {
        case opMove:
            break;
        default:
            format(getState(ctx),
                   "{s}",
                   (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
        }
        astConstVisit(visitor, node->unaryExpr.operand);
    }
    else {
        astConstVisit(visitor, node->unaryExpr.operand);
        format(getState(ctx),
               "{s}",
               (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
    }
}

static void visitTypedExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx), "(", NULL);
    generateTypeName(ctx, getState(ctx), node->type);
    format(getState(ctx), ")", NULL);
    astConstVisit(visitor, node->typedExpr.expr);
}

static void visitCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *expr = node->castExpr.expr;
    const Type *from = unwrapType(expr->type, NULL);
    const Type *to = unThisType(resolveType(node->castExpr.to->type));

    if (to == unThisType(from)) {
        astConstVisit(visitor, expr);
        return;
    }

    if (hasFlag(node, UnionCast) || typeIs(from, Union)) {
        if (isReferenceType(to) && !isClassType(stripReference(to)))
            format(getState(ctx), "&", NULL);
        astConstVisit(visitor, expr);
        if (isPointerOrReferenceType(from))
            format(getState(ctx),
                   "->_{i64}",
                   (FormatArg[]){{.i64 = node->castExpr.idx}});
        else
            format(getState(ctx),
                   "._{i64}",
                   (FormatArg[]){{.i64 = node->castExpr.idx}});
        return;
    }

    if (isReferenceType(from) && !isReferenceType(to) &&
        !isClassReferenceType(from)) {
        format(getState(ctx), "*(", NULL);
        astConstVisit(visitor, expr);
        format(getState(ctx), ")", NULL);
        return;
    }

    format(getState(ctx), "(", NULL);
    generateTypeName(ctx, getState(ctx), to);
    format(getState(ctx), ")", NULL);
    astConstVisit(visitor, expr);
}

static void visitGroupExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx), "(", NULL);
    astConstVisit(visitor, node->groupExpr.expr);
    format(getState(ctx), ")", NULL);
}

static void visitStmtExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx), "(", NULL);
    astConstVisit(visitor, node->stmtExpr.stmt);
    format(getState(ctx), ")", NULL);
}

static void visitCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    astConstVisit(visitor, node->callExpr.callee);
    generateMany(visitor, node->callExpr.args, "(", ", ", ")");
}

static void visitMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *target = node->memberExpr.target,
                  *member = node->memberExpr.member;
    const AstNode *parent = node->parentScope;

    if (nodeIsMemberFunctionReference(node)) {
        generateFunctionName(getState(ctx), getTypeDecl(node->type));
        return;
    }

    if (nodeIsEnumOptionReference(node)) {
        generateEnumOptionName(ctx, getState(ctx), member);
        return;
    }

    if (!isImportDeclReference(target)) {
        astConstVisit(visitor, target);
        if (isPointerType(target->type) || isClassType(target->type) ||
            isReferenceType(target->type))
            format(getState(ctx), "->", NULL);
        else
            format(getState(ctx), ".", NULL);
    }

    if (nodeIs(member, IntegerLit)) {
        if (isUnionType(stripPointerOrReferenceOnce(target->type, NULL))) {
            format(getState(ctx), "tag", NULL);
        }
        else {
            format(getState(ctx),
                   "_{u64}",
                   (FormatArg[]){{.u64 = member->intLiteral.uValue}});
        }
    }
    else {
        astConstVisit(visitor, member);
    }
}

static void visitPointerOfExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx), "&", NULL);
    astConstVisit(visitor, node->unaryExpr.operand);
}

static void visitReferenceOfExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (!isClassType(node->type->reference.referred))
        format(getState(ctx), "&", NULL);

    astConstVisit(visitor, node->unaryExpr.operand);
}

static void visitIndexExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->indexExpr.target);
    format(getState(ctx), "[", NULL);
    astConstVisit(visitor, node->indexExpr.index);
    format(getState(ctx), "]", NULL);
}

static void visitFieldExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(
        getState(ctx), ".{s} = ", (FormatArg[]){{.s = node->fieldExpr.name}});
    astConstVisit(visitor, node->fieldExpr.value);
}

static void visitStructExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx), "(", NULL);
    generateTypeName(ctx, getState(ctx), node->type);
    format(getState(ctx), ")", NULL);
    if (node->structExpr.fields)
        generateMany(
            visitor, node->structExpr.fields, "{{{>}\n", ",\n", "{<}\n}");
    else
        format(getState(ctx), "{{}", NULL);
}

static void visitTupleExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx), "(", NULL);
    generateTypeName(ctx, getState(ctx), node->type);
    format(getState(ctx), "){{{>}\n", NULL);
    AstNode *member = node->tupleExpr.elements;
    for (u64 i = 0; member; member = member->next, i++) {
        if (i)
            format(getState(ctx), ",\n", NULL);
        format(getState(ctx), "._{u64} = ", (FormatArg[]){{.u64 = i}});
        astConstVisit(visitor, member);
    }
    format(getState(ctx), "{<}\n}", NULL);
}

static void visitUnionValue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx), "(", NULL);
    generateTypeName(ctx, getState(ctx), node->type);
    format(getState(ctx),
           "){{.tag = {u32}, ._{u32} = ",
           (FormatArg[]){{.u32 = node->unionValue.idx},
                         {.u32 = node->unionValue.idx}});
    astConstVisit(visitor, node->unionValue.value);
    format(getState(ctx), "}", NULL);
}

static void visitBackendBfiZeromem(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const Type *type = node->type;
    if (typeIs(type, Pointer)) {
        type = type->pointer.pointed;
        if (isClassType(type)) {
            format(getState(ctx), "*(", NULL);
            astConstVisit(visitor, node);
            format(getState(ctx), ") = nullptr", NULL);
        }
        else {
            format(getState(ctx), "memset(", NULL);
            astConstVisit(visitor, node);
            format(getState(ctx), ", 0, sizeof(", NULL);
            generateTypeName(ctx, getState(ctx), type);
            format(getState(ctx), "))", NULL);
        }
    }
    else if (isClassType(type)) {
        format(getState(ctx), "memset(", NULL);
        astConstVisit(visitor, node);
        format(getState(ctx), ", 0, sizeof(", NULL);
        generateCustomTypeName(ctx, getState(ctx), type, "Class");
        format(getState(ctx), "))", NULL);
    }
    else {
        format(getState(ctx), "memset(&(", NULL);
        astConstVisit(visitor, node);
        format(getState(ctx), "), 0, sizeof(", NULL);
        generateTypeName(ctx, getState(ctx), type);
        format(getState(ctx), "))", NULL);
    }
}

static void visitBackendBfiMemAlloc(ConstAstVisitor *visitor,
                                    const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const Type *type = node->type;
    csAssert0(isClassType(type));
    if (ctx->memTraceEnabled) {
        format(getState(ctx), "__smart_ptr_alloc_trace(sizeof(", NULL);
        generateCustomTypeName(ctx, getState(ctx), type, "Class");
        format(getState(ctx), "), ", NULL);
        astConstVisit(visitor, node->next);
        format(getState(ctx),
               ", \"{s}\", {u64})",
               (FormatArg[]){{.s = node->loc.fileName},
                             {.u64 = node->loc.begin.row}});
    }
    else {
        format(getState(ctx), "__smart_ptr_alloc(sizeof(", NULL);
        generateCustomTypeName(ctx, getState(ctx), type, "Class");
        format(getState(ctx), "), ", NULL);
        astConstVisit(visitor, node->next);
        format(getState(ctx), ")", NULL);
    }
}

static void visitBackendBfiCopy(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const Type *type = stripReference(node->type);
    if (isClassType(type)) {
        if (ctx->memTraceEnabled) {
            format(getState(ctx), "__smart_ptr_get_trace(", NULL);
            astConstVisit(visitor, node);
            format(getState(ctx),
                   ", \"{s}\", {u64})",
                   (FormatArg[]){{.s = node->loc.fileName},
                                 {.u64 = node->loc.begin.row}});
        }
        else {
            format(getState(ctx), "__smart_ptr_get(", NULL);
            astConstVisit(visitor, node);
            format(getState(ctx), ")", NULL);
        }
    }
    else if (isTupleType(type)) {
        const AstNode *func = type->tuple.copyFunc->func.decl;
        csAssert0(func);
        generateBfiCallWithFunc(visitor, func, node);
    }
    else if (isUnionType(type)) {
        const AstNode *func = type->tUnion.copyFunc->func.decl;
        csAssert0(func);
        generateBfiCallWithFunc(visitor, func, node);
    }
    else if (isStructType(type)) {
        const AstNode *func = findMemberDeclInType(type, S_CopyOverload);
        csAssert0(func);
        generateBfiCallWithFunc(visitor, func, node);
    }
    else {
        astConstVisit(visitor, node);
    }
}

static void visitBackendBfiDrop(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    AstNode *args = node->backendCallExpr.args;
    const Type *type = unwrapType(stripReference(args->type), NULL);
    const AstNode *dropFlags = node->backendCallExpr.dropFlags;
    VariableState state = node->backendCallExpr.state;
    if (dropFlags)
        format(getState(ctx),
               "__CXY_DROP_WITH_FLAGS({s}, ",
               (FormatArg[]){{.s = dropFlags->_name}});

    if (isClassType(type)) {
        if (ctx->memTraceEnabled) {
            format(getState(ctx), "__smart_ptr_drop_trace(", NULL);
            astConstVisit(visitor, args);
            format(getState(ctx),
                   ", \"{s}\", {u64})",
                   (FormatArg[]){{.s = args->loc.fileName},
                                 {.u64 = args->loc.begin.row}});
        }
        else {
            format(getState(ctx), "__smart_ptr_drop(", NULL);
            astConstVisit(visitor, args);
            format(getState(ctx), ")", NULL);
        }
    }
    else if (isTupleType(type)) {
        const AstNode *func = type->tuple.destructorFunc->func.decl;
        csAssert0(func);
        generateBfiCallWithFunc(visitor, func, args);
    }
    else if (isUnionType(type)) {
        const AstNode *func = type->tUnion.destructorFunc->func.decl;
        csAssert0(func);
        generateBfiCallWithFunc(visitor, func, args);
    }
    else if (isStructType(type)) {
        const AstNode *func = findMemberDeclInType(type, S_DestructorOverload);
        csAssert0(func);
        generateBfiCallWithFunc(visitor, func, args);
    }
    else {
        unreachable();
    }
    if (dropFlags) {
        format(getState(ctx), ")", NULL);
    }
}

static void visitBackendCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    switch (node->backendCallExpr.func) {
    case bfiSizeOf: {
        const Type *type =
            resolveAndUnThisType(node->backendCallExpr.args->type);
        format(getState(ctx), "sizeof(", NULL);
        if (isClassType(type))
            generateCustomTypeName(ctx, getState(ctx), type, "Class");
        else
            generateTypeName(ctx, getState(ctx), type);
        format(getState(ctx), ")", NULL);
        break;
    }
    case bfiAlloca: {
        const Type *type =
            resolveAndUnThisType(node->backendCallExpr.args->type);
        format(getState(ctx), "__builtin_alloca(sizeof(", NULL);
        if (isClassType(type))
            generateCustomTypeName(ctx, getState(ctx), type, "Class");
        else
            generateTypeName(ctx, getState(ctx), type);
        format(getState(ctx), "))", NULL);
        break;
    }
    case bfiZeromem:
        visitBackendBfiZeromem(visitor, node->backendCallExpr.args);
        break;
    case bfiMemAlloc:
        visitBackendBfiMemAlloc(visitor, node->backendCallExpr.args);
        break;
    case bfiCopy:
        visitBackendBfiCopy(visitor, node->backendCallExpr.args);
        break;
    case bfiDrop:
        visitBackendBfiDrop(visitor, node);
        break;
    default:
        // csAssert(false, "Unsupported bfi");
        break;
    }
}

#define EosNl(ctx, node)                                                       \
    if (node->next)                                                            \
        format(getState(ctx), "\n", NULL);

static void visitInlineAssembly(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (findAttribute(node, S_volatile))
        format(getState(ctx), "__asm__ __volatile__(\"", NULL);
    else
        format(getState(ctx), "__asm__(\"", NULL);
    cstring p = node->inlineAssembly.text;
    while (*p != '\0') {
        switch (*p) {
        case '$':
            if (p[1] == '$') {
                format(getState(ctx), "$", NULL);
                p++;
            }
            else
                format(getState(ctx), "%", NULL);
            break;
        case '%':
            format(getState(ctx), "%%", NULL);
            break;
        default:
            format(getState(ctx), "{cE}", (FormatArg[]){{.c = *p}});
            break;
        }
        p++;
    }
    format(getState(ctx), "\"", NULL);

    if (node->inlineAssembly.outputs) {
        AstNode *output = node->inlineAssembly.outputs;
        format(getState(ctx), "\n:", NULL);
        for (; output; output = output->next) {
            format(getState(ctx),
                   "\"{s}\" (",
                   (FormatArg[]){{.s = output->asmOperand.constraint}});
            astConstVisit(visitor, output->asmOperand.operand);
            format(getState(ctx), ")", NULL);
            if (output->next)
                format(getState(ctx), ", ", NULL);
        }
    }
    else if (node->inlineAssembly.inputs || node->inlineAssembly.clobbers) {
        format(getState(ctx), " :", NULL);
    }

    if (node->inlineAssembly.inputs) {
        AstNode *input = node->inlineAssembly.inputs;
        format(getState(ctx), "\n:", NULL);
        for (; input; input = input->next) {
            format(getState(ctx),
                   "\"{s}\" (",
                   (FormatArg[]){{.s = input->asmOperand.constraint}});
            astConstVisit(visitor, input->asmOperand.operand);
            format(getState(ctx), ")", NULL);
            if (input->next)
                format(getState(ctx), ", ", NULL);
        }
    }
    else if (node->inlineAssembly.clobbers) {
        format(getState(ctx), ":", NULL);
    }

    if (node->inlineAssembly.clobbers) {
        AstNode *clobber = node->inlineAssembly.clobbers;
        format(getState(ctx), "\n:", NULL);
        for (; clobber; clobber = clobber->next) {
            astConstVisit(visitor, clobber);
            if (clobber->next)
                format(getState(ctx), ", ", NULL);
        }
    }
    format(getState(ctx), ");\n", NULL);
}

static void visitBasicBlock(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->basicBlock.stmts.first) {
        format(getState(ctx),
               "{{{>}\n__bbCleanup{u64}:\n",
               (FormatArg[]){{.u64 = node->basicBlock.index}});
        astConstVisitManyNodes(visitor, node->basicBlock.stmts.first);
        format(getState(ctx), "{<}\n}\n", NULL);
    }
    else {
        format(getState(ctx),
               "__bbCleanup{u64}:\n",
               (FormatArg[]){{.u64 = node->basicBlock.index}});
    }
}

static void visitBranch(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *target = node->branch.target;
    csAssert0(nodeIs(target, BasicBlock));
    if (node->branch.condition) {
        cstring name = node->branch.condition->_name;
        if (ctx->inLoop)
            format(
                getState(ctx),
                "__CXY_LOOP_CLEANUP({s}, __bbCleanup{u64});",
                (FormatArg[]){{.s = name}, {.u64 = target->basicBlock.index}});
        else
            format(
                getState(ctx),
                "__CXY_BLOCK_CLEANUP({s}, __bbCleanup{u64});",
                (FormatArg[]){{.s = name}, {.u64 = target->basicBlock.index}});
    }
    else {
        format(getState(ctx),
               "goto __bbCleanup{u64}",
               (FormatArg[]){{.u64 = target->basicBlock.index}});
    }
}

static void visitReturnStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    addDebugInfo(ctx, node);

    format(getState(ctx), "return", NULL);
    if (node->returnStmt.expr) {
        format(getState(ctx), " ", NULL);
        astConstVisit(visitor, node->returnStmt.expr);
    }
    format(getState(ctx), ";", NULL);
    EosNl(ctx, node);
}

static void visitBlockStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx), "{{{>}\n", NULL);
    generateMany(visitor, node->blockStmt.stmts, NULL, NULL, NULL);
    format(getState(ctx), "{<}\n}", NULL);
    EosNl(ctx, node);
}

static void visitExprStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    addDebugInfo(ctx, node);
    astConstVisit(visitor, node->exprStmt.expr);
    format(getState(ctx), ";", NULL);
    EosNl(ctx, node);
}

static void visitIfStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    format(getState(ctx), "if (", NULL);
    astConstVisit(visitor, node->ifStmt.cond);
    format(getState(ctx), ") ", NULL);
    astConstVisit(visitor, node->ifStmt.body);
    if (node->ifStmt.otherwise) {
        format(getState(ctx), " else ", NULL);
        astConstVisit(visitor, node->ifStmt.otherwise);
    }
    EosNl(ctx, node);
}

static void visitWhileStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    cstring previousUpdate = ctx->loopUpdate;
    bool previousUpdateUsed = ctx->loopUpdateUsed;
    bool previousInLoop = ctx->inLoop;
    ctx->loopUpdateUsed = false;
    ctx->inLoop = true;
    if (node->whileStmt.update)
        ctx->loopUpdate = makeAnonymousVariable(ctx->strings, "__update");
    else
        ctx->loopUpdate = NULL;

    format(getState(ctx), "while (", NULL);
    astConstVisit(visitor, node->whileStmt.cond);
    format(getState(ctx), ") {{{>}\n", NULL);
    astConstVisit(visitor, node->whileStmt.body);
    if (node->whileStmt.update && ctx->loopUpdateUsed) {
        format(
            getState(ctx), "\n{s}:\n", (FormatArg[]){{.s = ctx->loopUpdate}});
    }
    else {
        format(getState(ctx), "\n", NULL);
    }
    astConstVisit(visitor, node->whileStmt.update);
    format(getState(ctx), "{<}\n}", NULL);
    EosNl(ctx, node);
    ctx->loopUpdate = previousUpdate;
    ctx->loopUpdateUsed = previousUpdateUsed;
    ctx->inLoop = previousInLoop;
}

static void visitSwitchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(getState(ctx), "switch (", NULL);
    astConstVisit(visitor, node->switchStmt.cond);
    format(getState(ctx), ") ", NULL);
    generateMany(visitor, node->switchStmt.cases, "{{\n", "\n", NULL);
    if (node->switchStmt.defaultCase == NULL) {
        format(getState(ctx),
               "\ndefault: {{ unreachable(\"unreachable\"); }\n",
               NULL);
    }
    format(getState(ctx), "}\n", NULL);
    EosNl(ctx, node);
}

static void visitMatchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    format(getState(ctx), "switch (", NULL);
    astConstVisit(visitor, node->matchStmt.expr);
    if (isPointerOrReferenceType(node->matchStmt.expr->type))
        format(getState(ctx), "->tag", NULL);
    else
        format(getState(ctx), ".tag", NULL);
    format(getState(ctx), ") ", NULL);
    generateMany(visitor, node->switchStmt.cases, "{{\n", "\n", "\n}");
    EosNl(ctx, node);
}

static void visitCaseStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    addDebugInfo(ctx, node);
    const AstNode *body = node->caseStmt.body;
    if (node->caseStmt.match) {
        format(getState(ctx), "case ", NULL);
        if (nodeIs(node->parentScope, MatchStmt)) {
            format(getState(ctx),
                   "{u32}: {{{>}\n",
                   (FormatArg[]){{.u32 = node->caseStmt.idx}});
            if (node->caseStmt.variable) {
                astConstVisit(visitor, node->caseStmt.variable);
                format(getState(ctx), "\n", NULL);
            }
        }
        else {
            astConstVisit(visitor, node->caseStmt.match);
            format(getState(ctx), ": {{{>}\n", NULL);
        }
    }
    else {
        format(getState(ctx), "default: {{{>}\n", NULL);
    }
    addDebugInfo(ctx, body);
    astConstVisit(visitor, body);
    if (nodeIs(body, BlockStmt))
        body = getLastAstNode(body->blockStmt.stmts);
    if (!nodeIs(body, ReturnStmt))
        format(getState(ctx), "\nbreak;{<}\n}", NULL);
    else
        format(getState(ctx), "{<}\n}", NULL);
}

static void visitContinueStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    addDebugInfo(ctx, node);
    const AstNode *loop = node->continueExpr.loop;
    if (ctx->loopUpdate) {
        format(
            getState(ctx), "goto {s};", (FormatArg[]){{.s = ctx->loopUpdate}});
        ctx->loopUpdateUsed = true;
    }
    else
        format(getState(ctx), "continue;", NULL);
    EosNl(ctx, node);
}

static void visitBreakStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    addDebugInfo(ctx, node);
    format(getState(ctx), "break;", NULL);
    EosNl(ctx, node);
}

static void visitVariableDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (hasFlag(node, Comptime))
        return;

    ((AstNode *)node)->codegen = (void *)true;
    addDebugInfo(ctx, node);
    const Type *type = unwrapType(node->type, NULL);
    generateTypeName(ctx, getState(ctx), type);

    format(getState(ctx), " ", NULL);
    generateVariableName(getState(ctx), node);
    if (node->varDecl.init) {
        format(getState(ctx), " = ", NULL);
        astConstVisit(visitor, node->varDecl.init);
    }
    else {
        format(getState(ctx), " = {{}", NULL);
    }
    format(getState(ctx), ";", NULL);
    EosNl(ctx, node);
}

static void visitFuncParamDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (hasFlag(node, Variadic) && typeIs(node->type, Auto)) {
        format(getState(ctx), "...", NULL);
    }
    else {
        generateTypeName(ctx, getState(ctx), node->type);
        format(
            getState(ctx), " {s}", (FormatArg[]){{.s = node->funcParam.name}});
    }
}

static void visitExternDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    AstNode *decl = node->externDecl.func;
    if (decl->codegen)
        return;
    if (nodeIs(decl, FuncDecl)) {
        if (hasFlag(decl, Extern))
            decl->codegen = (void *)true;
        addDebugInfo(ctx, decl);

        if (hasFlag(decl, Extern))
            format(getState(ctx), "extern ", NULL);
        else
            format(getState(ctx), "static ", NULL);
        generateTypeName(ctx, getState(ctx), decl->type->func.retType);
        format(getState(ctx), " ", NULL);
        generateFunctionName(getState(ctx), decl);
        generateMany(visitor, decl->funcDecl.signature->params, "(", ", ", ")");
    }
    else if (nodeIs(decl, VarDecl) && hasFlag(decl, Extern)) {
        decl->codegen = (void *)true;
        format(getState(ctx), "extern ", NULL);
        generateTypeName(ctx, getState(ctx), decl->type);
        format(getState(ctx), " ", NULL);
        generateVariableName(getState(ctx), decl);
    }
    format(getState(ctx), ";\n", NULL);
}

static void visitFuncDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    addDebugInfo(ctx, node);
    ((AstNode *)node)->codegen = (void *)true;
    if (hasFlag(node, Abstract))
        return;
    if (!hasFlag(node, Extern)) {
        if (hasFlag(node, Constructor))
            format(getState(ctx), "__attribute__((constructor))\n", NULL);
        if (findAttribute(node, S_inline))
            format(
                getState(ctx), "__attribute__((always_inline)) inline", NULL);
        else if (findAttribute(node, S_noinline))
            format(getState(ctx), "__attribute__((noinline))", NULL);
        const AstNode *opt = findAttribute(node, S_optimize);
        if (opt) {
            const AstNode *value = getAttributeArgument(NULL, NULL, opt, 0);
            format(getState(ctx),
                   " __attribute__((optnone))",
                   (FormatArg[]){{.u64 = value->intLiteral.uValue}});
        }
        format(getState(ctx), "\nstatic ", NULL);
    }
    else {
        format(getState(ctx), "extern ", NULL);
    }
    generateTypeName(ctx, getState(ctx), node->type->func.retType);
    format(getState(ctx), " ", NULL);
    generateFunctionName(getState(ctx), node);
    generateMany(visitor, node->funcDecl.signature->params, "(", ", ", ")");
    if (hasFlag(node, Extern)) {
        format(getState(ctx), ";", NULL);
    }
    else {
        format(getState(ctx), " ", NULL);
        astConstVisit(visitor, node->funcDecl.body);
        EosNl(ctx, node);
    }
    format(getState(ctx), "\n", NULL);

    if (!ctx->backend->testMode && hasFlag(node, Main))
        generateMainFunction(ctx, node->type);
}

static void generateTestMainFunction(CodegenContext *ctx,
                                     cstring testFile,
                                     cstring moduleName)
{
    csAssert0(ctx->backend->testMode);
    char testsVariable[1024] = {0};
    sprintf(testsVariable,
            "%s%sallTestCases",
            moduleName ?: "",
            moduleName ? "_" : "");

    if (ctx->hasTestCases) {
        format(getState(ctx),
               "typedef struct {{ __typeof({s}[0]) *data; uint64_t "
               "len; }"
               " __TestCases;\n",
               (FormatArg[]){{.s = testsVariable}});
    }
    format(getState(ctx),
           "int main(int argc, char *argv[]) {{{>}\n"
           "return builtins_runTests((SliceI_sE){{.data = argv, .len = argc}, "
           "\"{s}\", (SliceI_TsFZ__OptionalI_Tsu64u64_E___E){{",
           (FormatArg[]){{.s = testFile}});
    if (ctx->hasTestCases) {
        format(getState(ctx),
               ".data = {s}, .len = "
               "sizeof({s})/sizeof({s}[0])});\n"
               "{<}\n}\n",
               (FormatArg[]){{.s = testsVariable},
                             {.s = testsVariable},
                             {.s = testsVariable}});
    }
    else {
        format(getState(ctx),
               ".data = nullptr, .len = 0});\n"
               "{<}\n}\n",
               NULL);
    }
}

static void visitProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(typeState(ctx),
           "\n/*-----------{s}-----------*/\n",
           (FormatArg[]){{.s = node->loc.fileName ?: "builtins.cxy"}});

    generateMany(visitor, node->program.decls, NULL, NULL, NULL);
    cstring moduleName =
        node->program.module ? node->program.module->moduleDecl.name : NULL;
    if (ctx->backend->testMode && isBuiltinsInitialized() &&
        !hasFlag(node, ImportedModule))
        generateTestMainFunction(ctx, node->loc.fileName, moduleName);
}

AstNode *generateCode(CompilerDriver *driver, AstNode *node)
{
    CBackend *backend = driver->backend;
    csAssert0(backend);
    CodegenContext context = {
        .backend = backend,
        .strings = driver->strings,
        .state = newFormatState("  ", true),
        .types = newFormatState("  ", true),
        .hasTestCases = driver->hasTestCases,
        .memTraceEnabled = driver->options.withMemoryTrace,
        .debug = {.enabled = driver->options.debug, .pos = {}}};
    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(&context, {
        [astProgram] = visitProgram,
        [astAsm] = visitInlineAssembly,
        [astBasicBlock] = visitBasicBlock,
        [astBranch] = visitBranch,
        [astBackendCall] = visitBackendCallExpr,
        [astIdentifier] = visitIdentifier,
        [astIntegerLit] = visitIntegerLit,
        [astNullLit] = visitNullLit,
        [astBoolLit] = visitBoolLit,
        [astCharLit] = visitCharLit,
        [astFloatLit] = visitFloatLit,
        [astStringLit] = visitStringLit,
        [astArrayExpr] = visitArrayExpr,
        [astBinaryExpr] = visitBinaryExpr,
        [astAssignExpr] = visitAssignExpr,
        [astTernaryExpr] = visitTernaryExpr,
        [astUnaryExpr] = visitUnaryExpr,
        [astTypedExpr] = visitTypedExpr,
        [astCallExpr] = visitCallExpr,
        [astCastExpr] = visitCastExpr,
        [astGroupExpr] = visitGroupExpr,
        [astStmtExpr] = visitStmtExpr,
        [astMemberExpr] = visitMemberExpr,
        [astPointerOf] = visitPointerOfExpr,
        [astReferenceOf] = visitReferenceOfExpr,
        [astIndexExpr] = visitIndexExpr,
        [astFieldExpr] = visitFieldExpr,
        [astStructExpr] = visitStructExpr,
        [astTupleExpr] = visitTupleExpr,
        [astUnionValueExpr] = visitUnionValue,
        [astBlockStmt] = visitBlockStmt,
        [astExprStmt] = visitExprStmt,
        [astReturnStmt] = visitReturnStmt,
        [astIfStmt] = visitIfStmt,
        [astWhileStmt] = visitWhileStmt,
        [astCaseStmt] = visitCaseStmt,
        [astSwitchStmt] = visitSwitchStmt,
        [astMatchStmt] = visitMatchStmt,
        [astContinueStmt] = visitContinueStmt,
        [astBreakStmt] = visitBreakStmt,
        [astVarDecl] = visitVariableDecl,
        [astFuncParamDecl] = visitFuncParamDecl,
        [astExternDecl] = visitExternDecl,
        [astFuncDecl] = visitFuncDecl,
        [astStructDecl] = astConstVisitSkip,
        [astClassDecl] = astConstVisitSkip,
        [astInterfaceDecl] = astConstVisitSkip,
        [astUnionDecl] = astConstVisitSkip,
        [astEnumDecl] = astConstVisitSkip,
        [astTypeRef] = astConstVisitSkip,
        [astTypeDecl] = astConstVisitSkip,
        [astGenericDecl] = astConstVisitSkip,
        [astImportDecl] = astConstVisitSkip,
        [astMacroDecl] = astConstVisitSkip,
        [astException] = astConstVisitSkip,
    }, .fallback = astConstVisitFallbackVisitAll);
    // clang-format on

    astConstVisit(&visitor, node->metadata.node);
    writeFormatState(&context.types, backend->output);
    writeFormatState(&context.state, backend->output);
    freeFormatState(&context.state);
    freeFormatState(&context.types);
    return node;
}

#define appendCode(f, s) fwrite((s), 1, sizeof(s) - 1, f)

static void generatedCodePrologue(FILE *f)
{
    appendCode(
        f,
        "#if defined(__clang__)\n"
        "#pragma clang diagnostic push\n"
        "#pragma clang diagnostic ignored \"-Wvisibility\"\n"
        "#pragma clang diagnostic ignored \"-Wmain-return-type\"\n"
        "#pragma clang diagnostic ignored \"-Wincompatible-pointer-types\"\n"
        "#pragma clang diagnostic ignored "
        "\"-Wincompatible-library-redeclaration\"\n"
        "#elif defined(__GNUC__)\n"
        "#pragma GCC diagnostic push\n"
        "#pragma GCC diagnostic ignored \"-Wbuiltin-declaration-mismatch\"\n"
        "#pragma GCC diagnostic ignored \"-Wincompatible-pointer-types\"\n"
        "#else\n"
        "#error \"Unsupported compiler\"\n"
        "#endif\n"
        "\n"
        "#if __has_attribute(__builtin_unreachable)\n"
        "#define unreachable(...)\n"
        "    do {\n"
        "        assert(!\"Unreachable code reached\");\n"
        "        __builtin_unreachable();\n"
        "    } while (0)\n"
        "#else\n"
        "#define unreachable(...) abort();\n"
        "#endif\n"
        "\n"
        "#define __CXY_LOOP_CLEANUP(FLAGS, LABEL) \\\n"
        "   if ((FLAGS) == 1) goto LABEL; \\\n"
        "   if ((FLAGS) == 2) { (FLAGS) = 0; break; }\n"
        "\n"
        "#define __CXY_BLOCK_CLEANUP(FLAGS, LABEL) \\\n"
        "   if ((FLAGS) == 1) goto LABEL; \\\n"
        "\n"
        "#define __CXY_DROP_WITH_FLAGS(FLAGS, ...) if (FLAGS) __VA_ARGS__\n");
    appendCode(f, "\n");
}

static void generateCodeEpilogue(FILE *f)
{
    appendCode(f,
               "#if defined(__GNUC__)\n"
               "#pragma GCC diagnostic pop\n"
               "#elif defined(__clang__)\n"
               "#pragma clang diagnostic pop\n"
               "#else\n"
               "#error \"Unsupported compiler\"\n"
               "#endif");
    appendCode(f, "\n");
}

void *initCompilerBackend(CompilerDriver *driver, int argc, char **argv)
{
    csAssert0(driver->backend == NULL);
    Options *options = &driver->options;
    cstring filename = makeStringConcat(driver->strings,
                                        options->buildDir ?: "./",
                                        options->output ?: "app",
                                        ".c");
    struct stat st;
    if (stat(filename, &st) == 0) {
        // remove file
        remove(filename);
    }

    FILE *f = fopen(filename, "w+");
    if (f == NULL) {
        logError(driver->L,
                 NULL,
                 "creating output file '{s}' failed: '{s}'",
                 (FormatArg[]){{.s = filename}, {.s = strerror(errno)}});
        return NULL;
    }
    CBackend *backend = allocFromMemPool(driver->pool, sizeof(CBackend));
    driver->backend = backend;
    backend->output = f;
    backend->filename = filename;
    backend->testMode = options->cmd == cmdTest;

    appendCode(f, "#define nullptr ((void *)0)\n");
    appendCode(f, "#define true  1\n");
    appendCode(f, "#define false  0\n");
    appendCode(f, "\n");
    appendCode(f, "typedef int bool;\n");
    appendCode(f, "typedef unsigned int wchar_t;\n");
    appendCode(f, "typedef signed char int8_t;\n");
    appendCode(f, "typedef unsigned char uint8_t;\n");
    appendCode(f, "typedef signed short int16_t;\n");
    appendCode(f, "typedef unsigned short uint16_t;\n");
    appendCode(f, "typedef signed int int32_t;\n");
    appendCode(f, "typedef unsigned int uint32_t;\n");
    appendCode(f, "typedef signed long long int64_t;\n");
    appendCode(f, "typedef unsigned long long uint64_t;\n");
    appendCode(f, "\n");
    generatedCodePrologue(f);
    return backend;
}

void deinitCompilerBackend(CompilerDriver *driver)
{
    if (driver->backend == NULL)
        return;
    CBackend *backend = (CBackend *)driver->backend;
    driver->backend = NULL;
    if (backend->output) {
        generateCodeEpilogue(backend->output);
        fclose(backend->output);
    }
}

bool compilerBackendMakeExecutable(CompilerDriver *driver)
{
    if (driver->backend == NULL)
        return false;
    CBackend *backend = (CBackend *)driver->backend;
    // compile the source file
    Options *opts = &driver->options;
    FormatState cmd = newFormatState("\t", true);
    appendString(&cmd, "clang");
    if (opts->debug)
        appendString(&cmd, " -g");
    if (opts->optimizationLevel != O0)
        format(&cmd, " -O{c}", (FormatArg[]){{.c = opts->optimizationLevel}});

    for (int i = 0; i < opts->cDefines.size; i++) {
        format(&cmd,
               " {s}",
               (FormatArg[]){
                   {.s = dynArrayAt(const char **, &opts->cDefines, i)}});
    }

    for (int i = 0; i < opts->cflags.size; i++) {
        format(
            &cmd,
            " {s}",
            (FormatArg[]){{.s = dynArrayAt(const char **, &opts->cflags, i)}});
    }

    for (int i = 0; i < opts->librarySearchPaths.size; i++) {
        format(
            &cmd,
            " -L{s}",
            (FormatArg[]){{.s = dynArrayAt(
                               const char **, &opts->librarySearchPaths, i)}});
    }

    for (int i = 0; i < opts->libraries.size; i++) {
        format(&cmd,
               " -l{s}",
               (FormatArg[]){
                   {.s = dynArrayAt(const char **, &opts->libraries, i)}});
    }

    HashtableIt it = newHashTableIt(&driver->linkLibraries, sizeof(cstring));
    while (hashTableItHasNext(&it)) {
        cstring *lib = hashTableItNext(&it);
        format(&cmd, " -l{s}", (FormatArg[]){{.s = *lib}});
    }

    for (int i = 0; i < opts->frameworkSearchPaths.size; i++) {
        format(&cmd,
               " -I{s}",
               (FormatArg[]){{.s = dynArrayAt(const char **,
                                              &opts->frameworkSearchPaths,
                                              i)}});
    }

    for (int i = 0; i < opts->importSearchPaths.size; i++) {
        format(
            &cmd,
            " -I{s}",
            (FormatArg[]){
                {.s = dynArrayAt(const char **, &opts->importSearchPaths, i)}});
    }

    it = newHashTableIt(&driver->nativeSources, sizeof(cstring));
    while (hashTableItHasNext(&it)) {
        cstring *src = hashTableItNext(&it);
        format(&cmd, " {s}", (FormatArg[]){{.s = *src}});
    }

    format(&cmd, " {s}", (FormatArg[]){{.s = backend->filename}});
    format(&cmd,
           " -o {s}/{s}",
           (FormatArg[]){{.s = opts->buildDir ?: "."},
                         {.s = opts->output ?: "app"}});

    cstring command = formatStateToString(&cmd);
    freeFormatState(&cmd);

    if (backend->output) {
        generateCodeEpilogue(backend->output);
        fprintf(backend->output, "\n/* %s */\n", command);
        fclose(backend->output);
        backend->output = NULL;
    }

    int rc = system(command);
    if (rc != 0) {
        logError(driver->L,
                 builtinLoc(),
                 "compile command failed: {s}",
                 (FormatArg[]){{.s = command}});
        free((void *)command);
        return false;
    }

    free((void *)command);
    return true;
}

bool compilerBackendExecuteTestCase(CompilerDriver *driver)
{
    FormatState cmd = newFormatState("\t", true);
    Options *opts = &driver->options;
    format(&cmd,
           "{s}/{s}",
           (FormatArg[]){{.s = opts->buildDir ?: "."},
                         {.s = opts->output ?: "app"}});

    char *command = formatStateToString(&cmd);
    freeFormatState(&cmd);
    int rc = system(command);
    free(command);
    return rc == 0;
}

AstNode *backendDumpIR(CompilerDriver *driver, AstNode *node)
{
    CBackend *backend = (CBackend *)driver->backend;
    csAssert0(backend);
    if (backend->output) {
        generateCodeEpilogue(backend->output);
        fclose(backend->output);
        backend->output = NULL;
    }

    Options *opts = &driver->options;
    if (opts->output == NULL) {
        FILE *f = fopen("./app", "r");
        if (f == NULL) {
            logError(driver->L,
                     NULL,
                     "opening './app' failed: {s}",
                     (FormatArg[]){{.s = strerror(errno)}});
            return NULL;
        }
        int c;
        while ((c = fgetc(f)) != EOF)
            fputc(c, stdout);
        fclose(f);
    }
    return node;
}
