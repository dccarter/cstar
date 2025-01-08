//
// Created by Carter Mbotho on 2024-11-01.
//

#include <core/log.h>
#include <core/mempool.h>

#include "lang/frontend/flag.h"
#include "lang/frontend/operator.h"

#pragma once

// clang-format off
#define CXY_MIR_TAGS(f)             \
    f(NullConst)                    \
    f(BoolConst)                    \
    f(CharConst)                    \
    f(UnsignedConst)                \
    f(SignedConst)                  \
    f(FloatConst)                   \
    f(StringConst)                  \
    f(ArrayConst)                   \
    f(StructConst)                  \
    f(AllocaOp)                     \
    f(LoadOp)                       \
    f(AssignOp)                     \
    f(CastOp)                       \
    f(MoveOp)                       \
    f(CopyOp)                       \
    f(MallocOp)                     \
    f(ReferenceOfOp)                \
    f(PointerOfOp)                  \
    f(DropOp)                       \
    f(SizeofOp)                     \
    f(ZeromemOp)                    \
    f(UnaryOp)                      \
    f(BinaryOp)                     \
    f(ArrayInitOp)                  \
    f(StructInitOp)                 \
    f(IndexOp)                      \
    f(MemberOp)                     \
    f(CallOp)                       \
    f(JumpOp)                       \
    f(IfOp)                         \
    f(CaseOp)                       \
    f(SwitchOp)                     \
    f(ReturnOp)                     \
    f(PanicOp)                      \
    f(GlobalVariable)               \
    f(TypeInfo)                     \
    f(AsmOperand)                   \
    f(InlineAssembly)               \
    f(BasicBlock)                   \
    f(FunctionParam)                \
    f(Function)                     \
    f(Module)

// clang-format on

typedef enum MirTag {
#define f(NAME, ...) mir##NAME,
    CXY_MIR_TAGS(f)
#undef f
} MirTag;

struct Type;

typedef struct MirNode MirNode;

typedef struct MirNodeList {
    MirNode *first;
    MirNode *last;
} MirNodeList;

struct MirNode {
    struct MirNode *next;
    struct MirNode *parent;
    MirTag tag;
    const struct Type *type;
    FileLoc loc;
    Flags flags;

    union {
        struct {
        } NullConst;

        struct {
            bool value;
        } BoolConst;

        struct {
            u32 value;
        } CharConst;

        union {
            i64 value;
        } SignedConst;
        union {
            u64 value;
        } UnsignedConst;

        struct {
            f64 value;
        } FloatConst;

        struct {
            u64 len;
            cstring value;
        } StringConst;

        struct {
            u64 len;
            struct MirNode *elems;
        } ArrayConst, StructConst;

        struct {
            cstring name;
        } AllocaOp;

        struct {
            cstring name;
            struct MirNode *var;
        } LoadOp;

        struct {
            struct MirNode *lValue;
            struct MirNode *rValue;
        } AssignOp;

        struct {
            Operator op;
            struct MirNode *operand;
        } UnaryOp;

        struct {
            struct MirNode *lhs;
            Operator op;
            struct MirNode *rhs;
        } BinaryOp;

        struct {
            struct MirNode *expr;
            struct MirNode *type;
        } CastOp;

        struct {
            struct MirNode *operand;
        } ReferenceOfOp, PointerOfOp;

        struct {
            struct MirNode *operand;
        } ZeromemOp, SizeofOp, MoveOp, CopyOp, DropOp, MallocOp;

        struct {
            u64 len;
            struct MirNode *elems;
        } ArrayInitOp, StructInitOp;

        struct {
            struct MirNode *target;
            struct MirNode *index;
        } IndexOp;

        struct {
            struct MirNode *target;
            u64 index;
        } MemberOp;

        struct {
            struct MirNode *func;
            struct MirNode *args;
            struct MirNode *panic;
        } CallOp;

        struct {
            struct MirNode *bb;
        } JumpOp;

        struct {
            struct MirNode *cond;
            struct MirNode *ifTrue;
            struct MirNode *ifFalse;
        } IfOp;

        struct {
            struct MirNode *matches;
            struct MirNode *body;
        } CaseOp;

        struct {
            struct MirNode *cond;
            struct MirNode *cases;
            struct MirNode *def;
        } SwitchOp;

        struct {
        } ReturnOp;

        struct {
        } PanicOp;

        struct {
            const struct Type *type;
        } TypeInfo;

        struct {
            cstring constraint;
            struct MirNode *operand;
        } AsmOperand;

        struct {
            cstring text;
            struct MirNode *outputs;
            struct MirNode *inputs;
            struct MirNode *clobbers;
        } InlineAssembly;

        struct {
            cstring name;
            cstring ns;
            struct MirNode *init;
        } GlobalVariable;

        struct {
            cstring name;
            MirNodeList nodes;
        } BasicBlock;

        struct {
            cstring name;
            struct MirNode *type;
        } FunctionParam;

        struct {
            cstring name;
            cstring ns;
            cstring container;
            struct MirNode *params;
            struct MirNode *body;
        } Function;

        struct {
            cstring name;
            cstring path;
            struct MirNode *decls;
        } Module;
    };
};

typedef struct MirContext MirContext;

static inline bool mirIs_(const MirNode *node, MirTag tag)
{
    return node && node->tag == tag;
}
#define mirIs(NODE, TAG) mirIs_((NODE), mir##TAG)

static inline MirNode *withMirFlags(MirNode *node, u64 flags)
{
    node->flags |= flags;
    return node;
}

u64 mirCount(const MirNode *nodes);

MirNode *makeMirNode(MirContext *ctx, MirNode *tmpl);
MirNode *cloneMirNode(MirContext *ctx, const MirNode *node);
MirNode *makeMirNullConst(MirContext *ctx,
                          const FileLoc *loc,
                          const struct Type *type,
                          MirNode *next);
MirNode *makeMirBoolConst(MirContext *ctx,
                          const FileLoc *loc,
                          bool value,
                          MirNode *next);

MirNode *makeMirCharConst(MirContext *ctx,
                          const FileLoc *loc,
                          char value,
                          MirNode *next);

MirNode *makeMirUnsignedConst(MirContext *ctx,
                              const FileLoc *loc,
                              u64 value,
                              const struct Type *type,
                              MirNode *next);
MirNode *makeMirSignedConst(MirContext *ctx,
                            const FileLoc *loc,
                            i64 value,
                            const struct Type *type,
                            MirNode *next);
MirNode *makeMirFloatConst(MirContext *ctx,
                           const FileLoc *loc,
                           f64 value,
                           const struct Type *type,
                           MirNode *next);
MirNode *makeMirStringConst(MirContext *ctx,
                            const FileLoc *loc,
                            cstring value,
                            MirNode *next);
MirNode *makeMirArrayConst(MirContext *ctx,
                           const FileLoc *loc,
                           MirNode *elems,
                           u64 len,
                           const struct Type *type,
                           MirNode *next);
MirNode *makeMirStructConst(MirContext *ctx,
                            const FileLoc *loc,
                            MirNode *elems,
                            u64 len,
                            const struct Type *type,
                            MirNode *next);

MirNode *makeMirAllocaOp(MirContext *ctx,
                         const FileLoc *loc,
                         cstring name,
                         const struct Type *type,
                         MirNode *next);
MirNode *makeMirLoadOp(MirContext *ctx,
                       const FileLoc *loc,
                       cstring name,
                       MirNode *var,
                       MirNode *next);
MirNode *makeMirAssignOp(MirContext *ctx,
                         const FileLoc *loc,
                         MirNode *lValue,
                         MirNode *rValue,
                         MirNode *next);
MirNode *makeMirUnaryOp(MirContext *ctx,
                        const FileLoc *loc,
                        Operator op,
                        MirNode *operand,
                        MirNode *next);
MirNode *makeMirBinaryOp(MirContext *ctx,
                         const FileLoc *loc,
                         MirNode *lhs,
                         Operator op,
                         MirNode *rhs,
                         const struct Type *type,
                         MirNode *next);
MirNode *makeMirCastOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *expr,
                       MirNode *type,
                       MirNode *next);
MirNode *makeMirDropOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *operand,
                       MirNode *next);

MirNode *makeMirMallocOp(MirContext *ctx,
                         const FileLoc *loc,
                         MirNode *operand,
                         const Type *type,
                         MirNode *next);

MirNode *makeMirMoveOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *operand,
                       MirNode *next);
MirNode *makeMirCopyOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *operand,
                       MirNode *next);
MirNode *makeMirReferenceOfOp(MirContext *ctx,
                              const FileLoc *loc,
                              MirNode *operand,
                              const struct Type *type,
                              MirNode *next);
MirNode *makeMirPointerOfOp(MirContext *ctx,
                            const FileLoc *loc,
                            MirNode *operand,
                            const struct Type *type,
                            MirNode *next);
MirNode *makeMirSizeofOp(MirContext *ctx,
                         const FileLoc *loc,
                         const struct Type *type,
                         MirNode *next);
MirNode *makeMirZeromemOp(MirContext *ctx,
                          const FileLoc *loc,
                          MirNode *operand,
                          MirNode *next);

MirNode *makeMirArrayInitOp(MirContext *ctx,
                            const FileLoc *loc,
                            MirNode *elems,
                            u64 len,
                            const struct Type *type,
                            MirNode *next);
MirNode *makeMirStructInitOp(MirContext *ctx,
                             const FileLoc *loc,
                             MirNode *elems,
                             u64 len,
                             const struct Type *type,
                             MirNode *next);
MirNode *makeMirIndexOp(MirContext *ctx,
                        const FileLoc *loc,
                        MirNode *target,
                        MirNode *index,
                        MirNode *next);
MirNode *makeMirMemberOp(MirContext *ctx,
                         const FileLoc *loc,
                         MirNode *target,
                         u64 index,
                         MirNode *next);
MirNode *makeMirMemberOpEx(MirContext *ctx,
                           const FileLoc *loc,
                           MirNode *target,
                           u64 index,
                           const Type *type,
                           MirNode *next);
MirNode *makeMirUnionMemberOp(MirContext *ctx,
                              const FileLoc *loc,
                              MirNode *target,
                              u64 index,
                              MirNode *next);
MirNode *makeMirCallOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *func,
                       MirNode *args,
                       MirNode *panic,
                       MirNode *next);
MirNode *makeMirJumpOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *bb,
                       MirNode *next);
MirNode *makeMirIfOp(MirContext *ctx,
                     const FileLoc *loc,
                     MirNode *cond,
                     MirNode *ifTrue,
                     MirNode *ifFalse,
                     MirNode *next);
MirNode *makeMirCaseOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *matches,
                       MirNode *body,
                       MirNode *next);
MirNode *makeMirSwitchOp(MirContext *ctx,
                         const FileLoc *loc,
                         MirNode *cond,
                         MirNode *cases,
                         MirNode *next);

MirNode *makeMirReturnOp(MirContext *ctx,
                         const FileLoc *loc,
                         const struct Type *type);

MirNode *makeMirPanicOp(MirContext *ctx, const FileLoc *loc);

MirNode *makeMirTypeInfo(MirContext *ctx,
                         const FileLoc *loc,
                         const struct Type *type,
                         MirNode *next);

MirNode *makeMirAsmOperand(MirContext *ctx,
                           const FileLoc *loc,
                           cstring constraint,
                           struct MirNode *operand,
                           MirNode *next);

MirNode *makeMirInlineAssembly(MirContext *ctx,
                               const FileLoc *loc,
                               cstring text,
                               MirNode *outputs,
                               MirNode *inputs,
                               MirNode *clobbers,
                               const struct Type *type,
                               MirNode *next);

MirNode *makeMirGlobalVariable(MirContext *ctx,
                               const FileLoc *loc,
                               cstring name,
                               MirNode *init,
                               const struct Type *type,
                               MirNode *next);

MirNode *makeMirBasicBlock(MirContext *ctx,
                           const FileLoc *loc,
                           cstring name,
                           MirNode *next);

MirNode *makeMirFunctionParam(MirContext *ctx,
                              const FileLoc *loc,
                              cstring name,
                              MirNode *type,
                              MirNode *next);

MirNode *makeMirFunction(MirContext *ctx,
                         const FileLoc *loc,
                         cstring name,
                         MirNode *params,
                         MirNode *body,
                         const struct Type *type,
                         MirNode *next);

MirNode *makeMirModule(MirContext *ctx,
                       const FileLoc *loc,
                       cstring name,
                       cstring path,
                       MirNode *decls);

void mirNodeListInit(MirNodeList *list);
MirNode *mirNodeGetLastNode(MirNode *node);
MirNode *mirNodeListInsert(MirNodeList *list, MirNode *node);
