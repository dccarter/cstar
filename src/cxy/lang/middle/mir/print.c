//
// Created by Carter Mbotho on 2024-11-06.
//
#include "context.h"
#include "driver/driver.h"
#include "node.h"

typedef struct {
    FormatState state;
} PrintContext;

static void printMirNode(PrintContext *ctx, const MirNode *node);

static void printManyMirNodes(PrintContext *ctx,
                              const MirNode *nodes,
                              cstring start,
                              cstring sep,
                              cstring end)
{
    if (start)
        format(&ctx->state, start, NULL);
    for (const MirNode *it = nodes; it; it = it->next) {
        printMirNode(ctx, it);
        if (sep && it->next)
            format(&ctx->state, sep, NULL);
    }
    if (end)
        format(&ctx->state, end, NULL);
}

static void printMirNode(PrintContext *ctx, const MirNode *node)
{
    switch (node->tag) {
    case mirNullConst:
        printKeyword(&ctx->state, "null");
        return;
    case mirBoolConst:
        printKeyword(&ctx->state, node->BoolConst.value ? "true" : "false");
        return;
    case mirCharConst:
        format(&ctx->state,
               "{$}'{cE}'{$}",
               (FormatArg[]){{.style = stringStyle},
                             {.c = node->CharConst.value},
                             {.style = resetStyle}});
        return;
    case mirUnsignedConst:
        format(&ctx->state,
               "{$}{u64}:{s}{$}",
               (FormatArg[]){{.style = literalStyle},
                             {.u64 = node->UnsignedConst.value},
                             {.s = node->type->name},
                             {.style = resetStyle}});
        return;
    case mirSignedConst:
        format(&ctx->state,
               "{$}{i64}:{s}{$}",
               (FormatArg[]){{.style = literalStyle},
                             {.i64 = node->SignedConst.value},
                             {.s = node->type->name},
                             {.style = resetStyle}});
        return;
    case mirFloatConst:
        format(&ctx->state,
               "{$}{f64}:{s}{$}",
               (FormatArg[]){{.style = literalStyle},
                             {.f64 = node->FloatConst.value},
                             {.s = node->type->name},
                             {.style = resetStyle}});
        return;
    case mirStringConst:
        format(&ctx->state,
               "{$}\"{sE}\"{$}",
               (FormatArg[]){{.style = stringStyle},
                             {.s = node->StringConst.value},
                             {.style = resetStyle}});
        return;
    case mirArrayConst:
    case mirStructConst:
        format(&ctx->state,
               "({$}const{$} ",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        // fall-through
    case mirArrayInitOp:
    case mirStructInitOp:
        format(&ctx->state, mirIs(node, ArrayConst) ? "[" : "{{", NULL);
        for (const MirNode *it = node->ArrayConst.elems; it; it = it->next) {
            printMirNode(ctx, it);
            if (it->next)
                format(&ctx->state, ", ", NULL);
        }
        format(&ctx->state, mirIs(node, ArrayConst) ? "]" : "}", NULL);
        if (mirIs(node, ArrayInitOp) || mirIs(node, StructInitOp))
            return;
        break;
    case mirAllocaOp:
        format(&ctx->state,
               "({$}alloca{$} {s}",
               (FormatArg[]){{.style = keywordStyle},
                             {.style = resetStyle},
                             {.s = node->AllocaOp.name}});
        break;
    case mirLoadOp:
        format(&ctx->state, "{s}", (FormatArg[]){{.s = node->LoadOp.name}});
        return;
    case mirAssignOp:
        appendString(&ctx->state, "(`=` ");
        printMirNode(ctx, node->AssignOp.lValue);
        appendString(&ctx->state, " ");
        printMirNode(ctx, node->AssignOp.rValue);
        break;
    case mirCastOp:
        format(&ctx->state,
               "({$}cast{$} ",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        printMirNode(ctx, node->CastOp.expr);
        appendString(&ctx->state, " ");
        printMirNode(ctx, node->CastOp.type);
        break;
    case mirMallocOp:
        format(&ctx->state,
               "({$}malloc{$} ({$}sizeof{$} ",
               (FormatArg[]){{.style = keywordStyle},
                             {.style = resetStyle},
                             {.style = keywordStyle},
                             {.style = resetStyle}});
        printType(&ctx->state, node->type);
        format(&ctx->state, ") ", NULL);
        printMirNode(ctx, node->MallocOp.operand);
        break;
    case mirMoveOp:
        appendString(&ctx->state, "(&& ");
        printMirNode(ctx, node->MoveOp.operand);
        break;
    case mirCopyOp:
        format(&ctx->state,
               "({$}copy{$} ",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        printMirNode(ctx, node->CopyOp.operand);
        break;
    case mirReferenceOfOp:
        appendString(&ctx->state, "(`&` ");
        printMirNode(ctx, node->ReferenceOfOp.operand);
        break;
    case mirPointerOfOp:
        format(&ctx->state,
               "({$}ptrof{$} ",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        printMirNode(ctx, node->PointerOfOp.operand);
        break;
    case mirDropOp:
        format(&ctx->state,
               "({$}drop{$} ",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        printMirNode(ctx, node->DropOp.operand);
        break;
    case mirSizeofOp:
        format(&ctx->state,
               "({$}sizeof{$} ",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        printMirNode(ctx, node->SizeofOp.operand);
        break;
    case mirZeromemOp:
        format(&ctx->state,
               "({$}zeromem{$} ",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        printMirNode(ctx, node->ZeromemOp.operand);
        break;
    case mirUnaryOp:
        format(&ctx->state,
               "(`{s}` ",
               (FormatArg[]){{.s = getUnaryOpString(node->UnaryOp.op)}});
        printMirNode(ctx, node->UnaryOp.operand);
        break;
    case mirBinaryOp:
        format(&ctx->state,
               "(`{s}` ",
               (FormatArg[]){{.s = getBinaryOpString(node->BinaryOp.op)}});
        printMirNode(ctx, node->BinaryOp.lhs);
        appendString(&ctx->state, " ");
        printMirNode(ctx, node->BinaryOp.rhs);
        break;
    case mirIndexOp:
        appendString(&ctx->state, "(`[]` ");
        printMirNode(ctx, node->IndexOp.target);
        appendString(&ctx->state, " ");
        printMirNode(ctx, node->IndexOp.index);
        break;
    case mirMemberOp:
        appendString(&ctx->state, "(`.` ");
        printMirNode(ctx, node->MemberOp.target);
        format(&ctx->state,
               " {u64}",
               (FormatArg[]){{.u64 = node->MemberOp.index}});
        break;
    case mirCallOp:
        //(call Hello_hello greeter "Bonjour" @panic)
        format(&ctx->state,
               "({$}call{$} ",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        printMirNode(ctx, node->CallOp.func);
        for (const MirNode *it = node->CallOp.args; it; it = it->next) {
            format(&ctx->state, " ", NULL);
            printMirNode(ctx, it);
            if (it->next)
                format(&ctx->state, " ", NULL);
        }
        break;
    case mirJumpOp:
        // (jmp @label)
        format(&ctx->state,
               "({$}jmp{$} @{s}",
               (FormatArg[]){{.style = keywordStyle},
                             {.style = resetStyle},
                             {.s = node->JumpOp.bb->BasicBlock.name}});
        break;
    case mirIfOp:
        // (if cond @then @otherwise)
        format(&ctx->state,
               "({$}if{$} ",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        printMirNode(ctx, node->IfOp.cond);
        format(&ctx->state,
               " @{s} @{s}",
               (FormatArg[]){{.s = node->IfOp.ifTrue->BasicBlock.name},
                             {.s = node->IfOp.ifFalse->BasicBlock.name}});
        break;
    case mirSwitchOp:
        // (switch cond [1 @case1] [2 @case2] @def)
        format(&ctx->state,
               "({$}switch{$}",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        printMirNode(ctx, node->SwitchOp.cond);
        for (const MirNode *it = node->SwitchOp.cases; it; it = it->next) {
            if (mirIs(it->CaseOp.matches, UnsignedConst))
                format(&ctx->state,
                       " [{u64} @{s}]",
                       (FormatArg[]){
                           {.u64 = it->CaseOp.matches->UnsignedConst.value},
                           {.s = it->CaseOp.body->BasicBlock.name}});
            else
                format(&ctx->state,
                       " [{i64} @{s}]",
                       (FormatArg[]){
                           {.i64 = it->CaseOp.matches->SignedConst.value},
                           {.s = it->CaseOp.body->BasicBlock.name}});
        }
        if (node->SwitchOp.def)
            format(&ctx->state,
                   " @{s}",
                   (FormatArg[]){{.s = node->SwitchOp.def->BasicBlock.name}});
        break;

    case mirReturnOp:
        // (return expr)
        format(&ctx->state,
               "({$}return{$}",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        break;
    case mirPanicOp:
        // (panic expr)
        format(&ctx->state,
               "({$}panic{$}",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        break;
    case mirGlobalVariable:
        format(&ctx->state,
               "({$}var{$} {s}",
               (FormatArg[]){{.style = keywordStyle},
                             {.style = resetStyle},
                             {.s = node->GlobalVariable.name}});
        if (node->GlobalVariable.init) {
            printUtf8(&ctx->state, ' ', false);
            printMirNode(ctx, node->GlobalVariable.init);
        }
        break;
    case mirAsmOperand:
        format(&ctx->state,
               "({$}\"{s}\"{$} ",
               (FormatArg[]){{.style = stringStyle},
                             {.s = node->AsmOperand.constraint},
                             {.style = resetStyle}});
        printMirNode(ctx, node->AsmOperand.operand);
        format(&ctx->state, ")", NULL);
        break;
    case mirInlineAssembly:
        format(&ctx->state,
               "({$}asm{$} ",
               (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
        printAssemblyString(&ctx->state, node->InlineAssembly.text);
        format(&ctx->state, "{>}\n", NULL);
        printManyMirNodes(ctx, node->InlineAssembly.outputs, "(", " ", ")");
        format(&ctx->state, "\n", NULL);
        printManyMirNodes(ctx, node->InlineAssembly.inputs, "(", " ", ")");
        format(&ctx->state, "\n", NULL);
        printManyMirNodes(ctx, node->InlineAssembly.clobbers, "(", " ", ")");
        format(&ctx->state, "{<}\n", NULL);
        break;
    case mirBasicBlock:
        format(&ctx->state,
               "({$}:{s}{$}",
               (FormatArg[]){{.style = {STYLE_BOLD, COLOR_NORMAL}},
                             {.s = node->BasicBlock.name},
                             {.style = resetStyle}});
        if (node->BasicBlock.nodes.first) {
            format(&ctx->state, "{>}\n", NULL);
            printManyMirNodes(
                ctx, node->BasicBlock.nodes.first, NULL, "\n", NULL);
            format(&ctx->state, "{<}\n", NULL);
        }
        break;
    case mirFunctionParam:
        format(
            &ctx->state, "({s}: ", (FormatArg[]){{.s = node->Function.name}});
        printMirNode(ctx, node->FunctionParam.type);
        break;
    case mirFunction:
        format(&ctx->state,
               "({$}func{$}{>}\n",
               (FormatArg[]){{.style = {STYLE_BOLD, COLOR_NORMAL}},
                             {.style = resetStyle}});
        format(&ctx->state, "{s}\n", (FormatArg[]){{.s = node->Function.name}});
        printManyMirNodes(ctx, node->Function.params, "(", " ", ")");
        if (!(node->flags & mifExtern)) {
            format(&ctx->state, "\n", NULL);
            for (const MirNode *it = node->Function.body; it; it = it->next) {
                printMirNode(ctx, it);
                if (it->next)
                    format(&ctx->state, "\n", NULL);
            }
        }
        format(&ctx->state, "{<}\n", NULL);
        break;
    case mirModule:
        printManyMirNodes(ctx, node->Module.decls, NULL, "\n", NULL);
        break;
    case mirTypeInfo:
        format(&ctx->state,
               "({$}typeinfo{$} ",
               (FormatArg[]){{.style = {STYLE_BOLD, COLOR_NORMAL}},
                             {.style = resetStyle}});
        printType(&ctx->state, node->TypeInfo.type);
        break;
    default:
        break;
    }

    format(&ctx->state, ")", NULL);
}

AstNode *dumpMir(CompilerDriver *driver, AstNode *node)
{
    Options *opts = &driver->options;
    FILE *output = stdout;
    PrintContext context = {};
    csAssert0(node->mir);
    if (opts->output) {
        context.state = newFormatState("  ", true);
        output = fopen(opts->output, "w");
        csAssert0(output);
    }
    else {
        context.state = newFormatState("  ", false);
    }

    printMirNode(&context, node->mir);
    writeFormatState(&context.state, output);

    if (opts->output) {
        fclose(output);
    }
    freeFormatState(&context.state);
    return node;
}