//
// Created by Carter on 2023-03-29.
//

#include "lang/ttable.h"

#include "lang/codegen.h"

#include "core/alloc.h"

#include <string.h>

#define CXY_EPILOGUE_SRC_FILE CXY_SOURCE_LANG_DIR "/ccodegen/epilogue.cxy.c"

static void programEpilogue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    size_t bytes = 0;
    format(ctx->state,
           "\n"
           "/* --------------------- Generated EPILOGUE --------------*/\n"
           "\n",
           NULL);

    generateManyAsts(visitor, "\n\n", node->program.decls);

    format(ctx->state,
           "\n"
           "/* --------------------- epilogue.cxy.c --------------*/\n"
           "\n",
           NULL);

    append(ctx->state, readFile(CXY_EPILOGUE_SRC_FILE, &bytes), bytes);
    format(ctx->state, "\n", NULL);
}

static void generatePathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->flags & flgCapture) {
        if (node->type->tag == typPrimitive || node->type->tag == typPointer)
            format(ctx->state,
                   "self->_{u64}",
                   (FormatArg[]){{.u64 = node->pathElement.index}});
        else
            format(ctx->state,
                   "(*self->_{u64})",
                   (FormatArg[]){{.u64 = node->pathElement.index}});
    }
    else if (node->flags & flgAddThis) {
        format(ctx->state,
               "this->{s}{s}",
               (FormatArg[]){{.s = (node->flags & flgAddSuper) ? "super." : ""},
                             {.s = node->pathElement.name}});
    }
    else
        format(ctx->state, "{s}", (FormatArg[]){{.s = node->pathElement.name}});
}

static void generatePath(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->type->tag == typEnum && node->path.elements->next &&
        (node->path.elements->next->flags & flgMember)) {
        writeEnumPrefix(ctx, node->type);
        generateManyAstsWithDelim(
            visitor, "_", "_", "", node->path.elements->next);
    }
    else if (node->type->tag == typFunc && node->type->func.decl->parentScope &&
             node->type->func.decl->parentScope->tag == astStructDecl) {
        const Type *scope = node->type->func.decl->parentScope->type;
        const AstNode *func = node->type->func.decl;
        writeTypename(ctx, scope);
        format(ctx->state, "__{s}", (FormatArg[]){{.s = func->funcDecl.name}});
    }
    else {
        const AstNode *elem = node->path.elements;
        for (; elem; elem = elem->next) {
            astConstVisit(visitor, elem);
            if (elem->next) {
                if (elem->type->tag == typPointer || elem->type->tag == typThis)
                    format(ctx->state, "->", NULL);
                else
                    format(ctx->state, ".", NULL);
            }
        }
    }
}

static void generateIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->ident.value}});
}

static void generateFuncParam(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, " {s}", (FormatArg[]){{.s = node->funcParam.name}});
}

static void generateClosureForward(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *params = node->funcDecl.params;

    generateTypeUsage(ctx, node->type->func.retType);
    format(ctx->state,
           " {s}_fwd(void *self",
           (FormatArg[]){{.s = node->funcDecl.name}});
    if (params->next)
        format(ctx->state, ", ", NULL);
    generateManyAstsWithDelim(visitor, "", ", ", ") {{{>}\n", params->next);

    if (node->type->func.retType->tag != typVoid) {
        format(ctx->state, "return ", NULL);
    }
    format(ctx->state, "{s}((", (FormatArg[]){{.s = node->funcDecl.name}});
    generateTypeUsage(ctx, node->funcDecl.params->type);
    format(ctx->state, ")self", NULL);

    for (const AstNode *param = params->next; param; param = param->next) {
        format(
            ctx->state, ", {s}", (FormatArg[]){{.s = param->funcParam.name}});
    }
    format(ctx->state, ");{<}\n}", (FormatArg[]){{.s = node->funcDecl.name}});
}

static void generateFunc(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    TypeTable *table = (ctx)->types;

    const AstNode *parent = node->parentScope;
    bool isMember = parent && parent->tag == astStructDecl;

    if (node->flags & flgClosure)
        format(ctx->state, "attr(always_inline)\n", NULL);

    if (!isMember && node->flags & flgMain) {
        if (isIntegerType(node->type->func.retType)) {
            format(ctx->state,
                   "#define cxy_MAIN_INVOKE(...) return "
                   "cxy_main(__VA_ARGS__)\n\n",
                   NULL);
        }
        else {
            format(ctx->state,
                   "#define cxy_MAIN_INVOKE(...) cxy_main(__VA_ARGS__); "
                   "return EXIT_SUCCESS\n\n",
                   NULL);
        }
    }

    if (node->flags & flgNative)
        format(ctx->state, "extern ", NULL);

    generateTypeUsage(ctx, node->type->func.retType);
    if (isMember) {
        format(ctx->state, " ", NULL);
        writeTypename(ctx, parent->type);
        format(ctx->state, "__{s}", (FormatArg[]){{.s = node->funcDecl.name}});
    }
    else if (node->flags & flgMain) {
        format(ctx->state, " cxy_main", NULL);
    }
    else {
        format(ctx->state, " {s}", (FormatArg[]){{.s = node->funcDecl.name}});
    }

    if (isMember) {
        format(ctx->state, "(", NULL);
        if (node->type->flags & flgConst)
            format(ctx->state, "const ", NULL);
        writeTypename(ctx, parent->type);
        format(ctx->state, " *this", NULL);
        if (node->funcDecl.params)
            format(ctx->state, ", ", NULL);

        generateManyAstsWithDelim(
            visitor, "", ", ", ")", node->funcDecl.params);
    }
    else {
        generateManyAstsWithDelim(
            visitor, "(", ", ", ")", node->funcDecl.params);
    }

    if (node->flags & flgNative) {
        format(ctx->state, ";", NULL);
    }
    else {
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
    }

    if (node->flags & flgClosure) {
        format(ctx->state, "\n", NULL);
        generateClosureForward(visitor, node);
    }
}

static void generateTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (!(node->flags & flgNative))
        generateTypeUsage(ctx, node->type);
}

static void generateVariable(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    if (node->flags & flgNative)
        format(ctx->state, "extern ", NULL);

    if ((node->flags & flgConst) && !(node->type->flags & flgConst))
        format(ctx->state, "const ", NULL);

    // if (node->varDecl.init == NULL || node->varDecl.)
    generateTypeUsage(ctx, node->type);

    format(ctx->state, " ", NULL);
    astConstVisit(visitor, node->varDecl.names);

    if (node->varDecl.init) {
        format(ctx->state, " = ", NULL);
        astConstVisit(visitor, node->varDecl.init);
    }
    format(ctx->state, ";", NULL);
}

static void generateAddressOf(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "&", NULL);
    astConstVisit(visitor, node->unaryExpr.operand);
}

static void generateStatementExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    astConstVisit(visitor, node->stmtExpr.stmt);
}

static void generateBinaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->binaryExpr.lhs);
    format(ctx->state,
           " {s} ",
           (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});
    astConstVisit(visitor, node->binaryExpr.rhs);
}

static void generateNewExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const char *name = makeAnonymousVariable((ctx)->strPool, "cxy_new_temp");
    const Type *type = node->type->pointer.pointed;

    format(ctx->state, "({{{>}\n", NULL);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, " {s} = cxy_alloc(sizeof(", (FormatArg[]){{.s = name}});
    generateTypeUsage(ctx, type);
    format(ctx->state, "));\n", NULL);
    if (node->newExpr.init) {
        if (type->tag == typArray) {
            format(ctx->state, " memcpy(*{s}, &(", (FormatArg[]){{.s = name}});
            generateTypeUsage(ctx, type);
            format(ctx->state, ")", NULL);
            astConstVisit(visitor, node->newExpr.init);
            format(ctx->state, ", sizeof(", NULL);
            generateTypeUsage(ctx, type);
            format(ctx->state, "))", NULL);
        }
        else {
            format(ctx->state, " *{s} = ", (FormatArg[]){{.s = name}});
            astConstVisit(visitor, node->newExpr.init);
        }
        format(ctx->state, ";\n", NULL);
    }
    format(ctx->state, " {s};{<}\n})", (FormatArg[]){{.s = name}});
}

static void generateUnaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->unaryExpr.isPrefix) {
        switch (node->unaryExpr.op) {
        case opDelete:
            format(ctx->state, "cxy_free((void *)", NULL);
            astConstVisit(visitor, node->unaryExpr.operand);
            format(ctx->state, ")", NULL);
            break;
        case opDeref:
            format(ctx->state, "(*", NULL);
            astConstVisit(visitor, node->unaryExpr.operand);
            format(ctx->state, ")", NULL);
            break;

        default:
            format(ctx->state,
                   "{s}",
                   (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
            astConstVisit(visitor, node->unaryExpr.operand);
        }
    }
    else {
        astConstVisit(visitor, node->unaryExpr.operand);
        format(ctx->state,
               "{s}",
               (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
    }
}

static void generateAssignExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->assignExpr.lhs);
    format(ctx->state,
           " {s} ",
           (FormatArg[]){{.s = getAssignOpString(node->assignExpr.op)}});
    astConstVisit(visitor, node->assignExpr.rhs);
}

static void generateTupleExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *arg = node->tupleExpr.args;

    format(ctx->state, "(", NULL);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, ")", NULL);

    format(ctx->state, "{{", NULL);
    for (u64 i = 0; arg; arg = arg->next, i++) {
        if (i != 0)
            format(ctx->state, ", ", NULL);
        format(ctx->state, "._{u64} = ", (FormatArg[]){{.u64 = i}});
        astConstVisit(visitor, arg);
    }

    format(ctx->state, "}", NULL);
}

static void generateStructExpr(ConstAstVisitor *visitor, const AstNode *node)
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

static void generateArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    generateManyAstsWithDelim(
        visitor, "{{", ", ", "}", node->arrayExpr.elements);
}

static void generateClosureCapture(CodegenContext *ctx,
                                   const Type *type,
                                   cstring name,
                                   u64 index)
{
    generateTypeUsage(ctx, stripPointer(type->func.params[0]));

    format(ctx->state,
           " {s}{u64} = {{",
           (FormatArg[]){{.s = name}, {.u64 = index}});
    for (u64 i = 0; i < type->func.capturedNamesCount; i++) {
        const Type *captureType = stripPointer(type->func.params[0]);
        if (i != 0)
            format(ctx->state, ", ", NULL);
        if (captureType->tuple.members[i]->flags & flgCapturePointer)
            format(
                ctx->state,
                "._{u64} = &{s}",
                (FormatArg[]){{.u64 = i}, {.s = type->func.captureNames[i]}});
        else
            format(
                ctx->state,
                "._{u64} = {s}",
                (FormatArg[]){{.u64 = i}, {.s = type->func.captureNames[i]}});
    }
    format(ctx->state, "}; ", NULL);
}

static void generateCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    const Type *type = node->callExpr.callee->type;
    const AstNode *parent =
        type->func.decl ? type->func.decl->parentScope : NULL;

    const char *name =
        (type->flags & (flgClosureStyle | flgClosure))
            ? makeAnonymousVariable(ctx->strPool, "_closure_capture")
            : "";

    if (type->flags & (flgClosureStyle | flgClosure))
        format(ctx->state, "({{{>}\n", NULL);
    if (type->flags & flgClosureStyle) {
        const AstNode *arg = node->callExpr.args;
        for (u64 i = 1; arg; arg = arg->next, i++) {
            if (arg->type->flags & flgClosure) {
                generateClosureCapture(ctx, arg->type, name, i);
            }
        }
    }
    else if (type->flags & flgClosure) {
        generateClosureCapture(ctx, type, name, 0);
    }

    astConstVisit(visitor, node->callExpr.callee);
    bool isMember = parent && parent->tag == astStructDecl;
    if (type->flags & flgClosure) {
        format(ctx->state, "(&{s}0", (FormatArg[]){{.s = name}});
        if (type->func.paramsCount > 1)
            format(ctx->state, ", ", NULL);
    }
    else if (isMember) {
        const AstNode *callee = node->callExpr.callee;
        bool needsThis =
            (callee->tag == astIdentifier) ||
            ((callee->tag != astMemberExpr) &&
             (callee->tag == astPath && callee->path.elements->next == NULL));

        if (needsThis) {
            format(ctx->state, "(this", NULL);
        }
        else {
            format(ctx->state, "(", NULL);
            const AstNode *target, *elem;
            if (callee->tag == astPath) {
                target = callee->path.elements, elem = callee->path.elements;
                while (true) {
                    if (target->next == NULL || target->next->next == NULL)
                        break;
                    target = target->next;
                }

                if (target->type->tag != typPointer)
                    format(ctx->state, "&", NULL);

                for (;; elem = elem->next) {
                    astConstVisit(visitor, elem);
                    if (elem == target)
                        break;
                    if (elem->type->tag == typPointer)
                        format(ctx->state, "->", NULL);
                    else
                        format(ctx->state, ".", NULL);
                }
            }
            else {
                target = callee->memberExpr.target;
                if (target->type->tag != typPointer)
                    format(ctx->state, "&", NULL);
                astConstVisit(visitor, target);
            }
        }
    }
    else {
        format(ctx->state, "(", NULL);
    }
    {
        const AstNode *arg = node->callExpr.args;
        for (u64 i = 0; arg; arg = arg->next, i++) {
            const Type *param = type->func.params[i];
            if (isMember || i != 0)
                format(ctx->state, ", ", NULL);

            if (arg->type->flags & flgClosure) {
                format(ctx->state, "(", NULL);
                generateTypeUsage(ctx, param);
                format(ctx->state,
                       "){{._0 = &{s}{u64}, ._1 = ",
                       (FormatArg[]){{.s = name}, {.u64 = i + 1}});
                astConstVisit(visitor, arg->type->func.decl);
                format(ctx->state, "_fwd}", NULL);
            }
            else {
                astConstVisit(visitor, arg);
            }
        }
    }

    if (type->flags & (flgClosureStyle | flgClosure))
        format(ctx->state, ");{<}\n})", NULL);
    else
        format(ctx->state, ")", NULL);
}

static void generateGroupExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    astConstVisit(visitor, node->groupExpr.expr);
    format(ctx->state, ")", NULL);
}

static void generateTypedExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, ")", NULL);
    astConstVisit(visitor, node->typedExpr.expr);
}

static void generateCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    generateTypeUsage(ctx, node->castExpr.to->type);
    format(ctx->state, ")", NULL);
    astConstVisit(visitor, node->castExpr.expr);
}

static void generateIndexExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const Type *target = node->indexExpr.target->type;
    const Type *stripped = target->pointer.pointed;
    if (target->tag == typPointer && stripped->tag == typArray) {
        format(ctx->state, "(*", NULL);
        astConstVisit(visitor, node->indexExpr.target);
        format(ctx->state, ")", NULL);
    }
    else {
        astConstVisit(visitor, node->indexExpr.target);
    }
    format(ctx->state, "[", NULL);
    astConstVisit(visitor, node->indexExpr.index);
    format(ctx->state, "]", NULL);
}

static void generateTernaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->ternaryExpr.cond);
    format(ctx->state, "? ", NULL);
    astConstVisit(visitor, node->ternaryExpr.body);
    format(ctx->state, ": ", NULL);
    astConstVisit(visitor, node->ternaryExpr.otherwise);
}

static void generateBlock(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *ret = NULL;
    const AstNode *epilogue = node->blockStmt.epilogue.first;

    format(ctx->state, "{{{>}\n", NULL);
    for (const AstNode *stmt = node->blockStmt.stmts; stmt; stmt = stmt->next) {
        if (epilogue && stmt->tag == astReturnStmt) {
            ret = stmt;
            continue;
        }
        astConstVisit(visitor, stmt);
        if (stmt->tag == astCallExpr)
            format(ctx->state, ";", NULL);
        if (epilogue || stmt->next)
            format(ctx->state, "\n", NULL);
    }

    for (; epilogue; epilogue = epilogue->next) {
        astConstVisit(visitor, epilogue);
        if ((epilogue->flags & flgDeferred) && epilogue->tag != astBlockStmt)
            format(ctx->state, ";", NULL);

        if (ret || epilogue->next)
            format(ctx->state, "\n", NULL);
    }

    if (ret)
        astConstVisit(visitor, ret);
    format(ctx->state, "{<}\n}", NULL);
}

static void generateExpressionStmt(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->exprStmt.expr);
    format(ctx->state, ";", NULL);
}

static void generateReturn(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "return", NULL);
    if (node->returnStmt.expr) {
        format(ctx->state, " ", NULL);
        astConstVisit(visitor, node->returnStmt.expr);
    }
    format(ctx->state, ";", NULL);
}

static void generateBreakContinue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->tag == astBreakStmt)
        format(ctx->state, "break;", NULL);
    else
        format(ctx->state, "continue;", NULL);
}

static void generateIfStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *cond = node->ifStmt.cond;
    if (cond->tag == astVarDecl) {
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state,
               "\nif ({s}) ",
               (FormatArg[]){{.s = cond->varDecl.names->ident.value}});
    }
    else {
        format(ctx->state, "if (", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state, ") ", NULL);
    }
    astConstVisit(visitor, node->ifStmt.body);
    if (node->ifStmt.otherwise) {
        format(ctx->state, " else ", NULL);
        astConstVisit(visitor, node->ifStmt.otherwise);
    }

    if (cond->tag == astVarDecl) {
        format(ctx->state, "{<}\n}", NULL);
    }
}

static void generateWhileStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *cond = node->whileStmt.cond;
    if (cond->tag == astVarDecl) {
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state,
               "\nwhile ({s}) ",
               (FormatArg[]){{.s = cond->varDecl.names->ident.value}});
    }
    else {
        format(ctx->state, "while (", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state, ") ", NULL);
    }
    astConstVisit(visitor, node->whileStmt.body);

    if (cond->tag == astVarDecl) {
        format(ctx->state, "{<}\n}", NULL);
    }
}

void codegenEpilogue(CodegenContext *context, const AstNode *prog)
{
    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(context,
    {
        [astProgram] = programEpilogue,
        [astPathElem] = generatePathElement,
        [astPath] = generatePath,
        [astIdentifier] = generateIdentifier,
        [astNullLit] = generateLiteral,
        [astBoolLit] = generateLiteral,
        [astCharLit] = generateLiteral,
        [astIntegerLit] = generateLiteral,
        [astFloatLit] = generateLiteral,
        [astStringLit] = generateLiteral,
        [astAddressOf] = generateAddressOf,
        [astStmtExpr] = generateStatementExpr,
        [astBinaryExpr] = generateBinaryExpr,
        [astUnaryExpr] = generateUnaryExpr,
        [astAssignExpr] = generateAssignExpr,
        [astTupleExpr] = generateTupleExpr,
        [astStructExpr] = generateStructExpr,
        [astArrayExpr] = generateArrayExpr,
        [astMemberExpr] = generateMemberExpr,
        [astCallExpr] = generateCallExpr,
        [astStringExpr] = generateStringExpr,
        [astGroupExpr] = generateGroupExpr,
        [astTypedExpr] = generateTypedExpr,
        [astCastExpr] = generateCastExpr,
        [astIndexExpr] = generateIndexExpr,
        [astTernaryExpr] = generateTernaryExpr,
        [astNewExpr] = generateNewExpr,
        [astBlockStmt] = generateBlock,
        [astExprStmt] = generateExpressionStmt,
        [astReturnStmt] = generateReturn,
        [astBreakStmt] = generateBreakContinue,
        [astContinueStmt] = generateBreakContinue,
        [astIfStmt] = generateIfStmt,
        [astWhileStmt] = generateWhileStmt,
        [astForStmt] = generateForStmt,
        [astFuncParam] = generateFuncParam,
        [astFuncDecl] = generateFunc,
        [astVarDecl] = generateVariable,
        [astTypeDecl] = generateTypeDecl
    },

    .fallback = generateFallback);

    astConstVisit(&visitor, prog);
}
