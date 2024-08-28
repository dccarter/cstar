// Credits https://github.com/madmann91/fu/blob/master/src/fu/lang/ast.h

#pragma once

#include "core/format.h"
#include "core/log.h"
#include "core/mempool.h"
#include "operator.h"
#include "token.h"

#ifdef __cplusplus
extern "C" {
#endif

struct StrPool;

// clang-format off

#define CXY_LANG_AST_EXP_TAGS(f)            \
    f(GroupExpr)                            \
    f(UnaryExpr)                            \
    f(PointerOf)                            \
    f(ReferenceOf)                          \
    f(BinaryExpr)                           \
    f(AssignExpr)                           \
    f(TernaryExpr)                          \
    f(StmtExpr)                             \
    f(StringExpr)                           \
    f(TypedExpr)                            \
    f(CastExpr)                             \
    f(CallExpr)                             \
    f(MacroCallExpr)                        \
    f(ClosureExpr)                          \
    f(ArrayExpr)                            \
    f(IndexExpr)                            \
    f(TupleExpr)                            \
    f(FieldExpr)                            \
    f(StructExpr)                           \
    f(MemberExpr)                           \
    f(RangeExpr)                            \
    f(NewExpr)                              \
    f(SpreadExpr)                           \
    f(BackendCall)                          \
    f(UnionValueExpr)

#define CXY_LANG_AST_STMT_TAGS(f)           \
    f(ExprStmt)                             \
    f(BreakStmt)                            \
    f(ContinueStmt)                         \
    f(DeferStmt)                            \
    f(ReturnStmt)                           \
    f(BlockStmt)                            \
    f(IfStmt)                               \
    f(ForStmt)                              \
    f(WhileStmt)                            \
    f(SwitchStmt)                           \
    f(MatchStmt)                            \
    f(CaseStmt)

#define CXY_LANG_AST_DECL_TAGS(f)           \
    f(FuncDecl)                             \
    f(MacroDecl)                            \
    f(VarDecl)                              \
    f(TypeDecl)                             \
    f(ForwardDecl)                          \
    f(UnionDecl)                            \
    f(StructDecl)                           \
    f(ClassDecl)                            \
    f(InterfaceDecl)                        \
    f(ModuleDecl)                           \
    f(ImportDecl)                           \
    f(EnumDecl)                             \
    f(FieldDecl)                            \
    f(ExternDecl)                           \
    f(GenericDecl)                          \
    f(FuncParamDecl)                        \
    f(EnumOptionDecl)                       \

#define CXY_LANG_IR_TAGS(f)               \
    f(Branch)                             \
    f(BranchIf)                           \
    f(BasicBlock)                         \
    f(Phi)                                \
    f(SwitchIr)                           \
    f(Gep)

#define CXY_LANG_AST_TAGS(f) \
    f(Error)                 \
    f(Noop)                  \
    f(Ref)                  \
    f(Deleted)              \
    f(ComptimeOnly)         \
    f(List)                 \
    f(ClosureCapture)       \
    f(Program)              \
    f(Metadata)             \
    f(CCode)                \
    f(Define)               \
    f(Attr)                 \
    f(Path)                 \
    f(PathElem)             \
    f(Substitution)         \
    f(GenericParam)         \
    f(Identifier)           \
    f(ImportEntity)         \
    f(DestructorRef)        \
    f(TypeRef)              \
    f(VoidType)             \
    f(AutoType)             \
    f(StringType)           \
    f(TupleType)            \
    f(ArrayType)            \
    f(PointerType)          \
    f(ReferenceType)        \
    f(FuncType)             \
    f(PrimitiveType)        \
    f(OptionalType)         \
    f(Literals)             \
    f(NullLit)              \
    f(BoolLit)              \
    f(CharLit)              \
    f(IntegerLit)           \
    f(FloatLit)             \
    f(StringLit)            \
    f(Asm)                  \
    f(AsmOperand)           \
    f(NodeArray)            \
    CXY_LANG_AST_EXP_TAGS(f)    \
    CXY_LANG_AST_STMT_TAGS(f)   \
    CXY_LANG_AST_DECL_TAGS(f)   \
    CXY_LANG_IR_TAGS(f)

// clang-format on

typedef enum {

#define f(name) ast##name,
    CXY_LANG_AST_TAGS(f)
#undef f

        astCOUNT
} AstTag;

struct Scope;
struct AstVisitor;

typedef struct AstNode AstNode;

typedef AstNode *(*EvaluateMacro)(struct AstVisitor *,
                                  const AstNode *,
                                  AstNode *);

typedef struct AstNodeList {
    AstNode *first;
    AstNode *last;
} AstNodeList;

typedef struct CaptureSet {
    HashTable *table;
    u64 index;
} ClosureCapture;

typedef struct Capture Capture;

typedef struct {
    bool createMapping;
    bool signatureOnly;
    DynArray *deferred;
    MemPool *pool;
    HashTable mapping;
    const AstNode *root;
} CloneAstConfig;

// clang-format off
#define BACKEND_FUNC_IDS(f)    \
    f(SizeOf)                  \
    f(Alloca)                  \
    f(Zeromem)

// clang-format on

typedef enum {
#define f(NN) bfi##NN,
    BACKEND_FUNC_IDS(f)
#undef f
} BackendFuncId;

struct IrValue;

#define CXY_AST_NODE_HEAD                                                      \
    AstTag tag;                                                                \
    FileLoc loc;                                                               \
    u64 flags;                                                                 \
    const Type *type;                                                          \
    struct AstNode *parentScope;                                               \
    struct AstNode *next;                                                      \
    struct {                                                                   \
        struct AstNode *first;                                                 \
        struct AstNode *link;                                                  \
    } list;                                                                    \
    struct AstNode *attrs;                                                     \
    void *codegen;                                                             \
    struct IrValue *ir;

typedef enum { iptModule, iptPath } ImportKind;
typedef enum { cInclude, cDefine, cSources } CCodeKind;

typedef struct FunctionSignature {
    struct AstNode *params;
    struct AstNode *ret;
    struct AstNode *typeParams;
} FunctionSignature;

typedef struct SortedNodes {
    u64 count;

    int (*compare)(const void *lhs, const void *rhs);

    struct AstNode *nodes[0];
} SortedNodes;

typedef struct {
    AstNode *match;
    AstNode *bb;
} SwitchIrCase;

struct AstNode {
    union {
        struct {
            CXY_AST_NODE_HEAD
        };
        struct {
            CXY_AST_NODE_HEAD
        } _head;
    };

    union {
        struct {
            u8 _[1];
        } _body;

        struct {
            cstring name;
        } _namedNode;

        struct {
            AstNode *captured;
        } capture;

        struct {
            struct AstNode *module;
            struct AstNode *top;
            struct AstNode *decls;
            cstring path;
        } program;

        struct {
            CCodeKind kind;
            AstNode *what;
        } cCode;

        struct {
            AstNodeList nodes;
        } nodesList;

        struct {
            AstNode *names;
            AstNode *type;
            AstNode *container;
            Env *env;
        } define;

        struct {
            ImportKind kind;
            struct AstNode *module;
            struct AstNode *exports;
            struct AstNode *alias;
            struct AstNode *entities;
        } import;

        struct {
            cstring name;
            cstring alias;
            AstNode *target;
        } importEntity;

        struct {
            cstring name;
            Env *env;
        } moduleDecl;

        struct {
            const Type *target;
        } destructorRef;

        struct {
            cstring constraint;
            AstNode *operand;
        } asmOperand;

        struct {
            cstring text;
            AstNode *outputs;
            AstNode *inputs;
            AstNode *clobbers;
            AstNode *flags;
        } inlineAssembly;

        struct {
            cstring value;
            cstring alias;
            AstNode *resolvesTo;
            u16 super;
        } ident;

        struct {
            bool value;
        } boolLiteral;

        struct {
            union {
                i64 value;
                u64 uValue;
            };
            bool isNegative;
        } intLiteral;

        struct {
            f64 value;
        } floatLiteral;

        struct {
            u32 value;
        } charLiteral;

        struct {
            const char *value;
        } stringLiteral;

        struct {
            cstring name;
            struct AstNode *args;
            u32 count;
            bool kvpArgs;
        } attr;

        struct {
            u64 len;
            struct AstNode *elements;
        } tupleType, tupleExpr;

        struct {
            struct AstNode *elementType;
            struct AstNode *dim;
        } arrayType;

        struct {
            struct AstNode *type;
        } optionalType;

        struct {
            struct AstNode *params;
            struct AstNode *ret;
        } funcType;

        struct {
            PrtId id;
        } primitiveType;

        struct {
            struct AstNode *pointed;
        } pointerType;

        struct {
            struct AstNode *referred;
        } referenceType;

        struct {
            u64 len;
            struct AstNode *elements;
            bool isLiteral;
        } arrayExpr;

        struct {
            struct AstNode *target;
            struct AstNode *member;
        } memberExpr;

        struct {
            struct AstNode *start;
            struct AstNode *end;
            struct AstNode *step;
            bool down;
        } rangeExpr;

        struct {
            struct AstNode *type;
            struct AstNode *init;
        } newExpr;

        struct {
            struct AstNode *target;
            struct AstNode *index;
        } indexExpr;

        struct {
            const char *name;
            struct AstNode *defaultValue;
            struct AstNode *constraints;
            u16 inferIndex;
            bool innerType;
        } genericParam;

        struct {
            cstring name;
            u16 paramsCount;
            i16 inferrable;
            struct AstNode *params;
            struct AstNode *decl;
        } genericDecl;

        struct {
            const char *name;
            const char *alt;
            struct AstNode *args;
            union {
                struct AstNode *enclosure;
                struct AstNode *resolvesTo;
            };
            u16 index;
            u16 super;
            bool isKeyword;
        } pathElement;

        struct {
            AstNode *func;
        } externDecl;

        struct {
            struct AstNode *elements;
            bool isType;
            u16 inheritanceDepth;
        } path;

        struct {
            const char *name;
            Operator operatorOverload;
            u16 index;
            u16 requiredParamsCount;
            u16 paramsCount;
            FunctionSignature *signature;
            struct AstNode *opaqueParams;
            struct AstNode *this_;
            union {
                struct AstNode *body;
                struct AstNode *definition;
            };
        } funcDecl;

        struct {
            const char *name;
            struct AstNode *params;
            union {
                struct AstNode *body;
                struct AstNode *definition;
            };
        } macroDecl;

        struct {
            const char *name;
            struct AstNode *type;
            struct AstNode *def;
            u32 index;
        } funcParam;

        struct {
            cstring name;
            struct AstNode *names;
            struct AstNode *type;
            struct AstNode *init;
            void *codegen;
        } varDecl;

        struct {
            cstring name;
            struct AstNode *typeParams;
            union {
                struct AstNode *aliased;
                struct AstNode *definition;
            };
        } typeDecl;

        struct {
            struct AstNode *members;
            struct AstNode *typeParams;
            SortedNodes *sortedMembers;
        } unionDecl;

        struct {
            cstring name;
            struct AstNode *value;
            u64 index;
        } enumOption;

        struct {
            cstring name;
            u64 len;
            struct AstNode *base;
            struct AstNode *options;
            struct AstNode *getName;
            SortedNodes *sortedOptions;
        } enumDecl;

        struct {
            cstring name;
            u64 index;
            u32 bits;
            struct AstNode *type;
            struct AstNode *value;
        } structField;

        struct {
            cstring name;
            struct AstNode *members;
            struct AstNode *typeParams;
            const Type *thisType;
            struct AstNode *closureForward;
        } structDecl;

        struct {
            cstring name;
            struct AstNode *members;
            struct AstNode *typeParams;
            const Type *thisType;
            struct AstNode *implements;
            struct AstNode *base;
        } classDecl;

        struct {
            cstring name;
            struct AstNode *members;
            struct AstNode *typeParams;
        } interfaceDecl;

        struct {
            Operator op;
            struct AstNode *lhs;
            struct AstNode *rhs;
        } binaryExpr, assignExpr;

        struct {
            Operator op;
            bool isPrefix;
            struct AstNode *operand;
        } unaryExpr;

        struct {
            AstNode *jmpTo;
            struct AstNode *cond;
            struct AstNode *body;
            struct AstNode *otherwise;
            bool isTernary;
        } ternaryExpr, ifStmt;

        struct {
            struct AstNode *stmt;
        } stmtExpr;

        struct {
            struct AstNode *parts;
        } stringExpr;

        struct {
            u32 idx;
            struct AstNode *expr;
            struct AstNode *type;
        } typedExpr;

        struct {
            u32 idx;
            struct AstNode *expr;
            struct AstNode *to;
        } castExpr;

        struct {
            struct AstNode *callee;
            struct AstNode *args;
            EvaluateMacro evaluator;
            u32 overload;
        } callExpr, macroCallExpr;

        struct {
            BackendFuncId func;
            struct AstNode *args;
        } backendCallExpr;

        struct {
            union {
                ClosureCapture captureSet;
                struct {
                    Capture *capture;
                    u64 captureCount;
                };
            };
            struct AstNode *params;
            struct AstNode *ret;
            struct AstNode *body;
            struct AstNode *construct;
        } closureExpr;

        struct {
            const char *name;
            u64 index;
            struct AstNode *value;
            const struct AstNode *structField;
            const Type *sliceType;
        } fieldExpr;

        struct {
            struct AstNode *left;
            struct AstNode *fields;
        } structExpr;

        struct {
            struct AstNode *expr;
        } exprStmt, groupExpr, spreadExpr;

        struct {
            struct AstNode *stmt;
            struct AstNode *block;
        } deferStmt;

        struct {
            struct AstNode *loop;
        } breakExpr, continueExpr;

        struct {
            struct AstNode *func;
            struct AstNode *expr;
        } returnStmt;

        struct {
            struct AstNodeList epilogue;
            struct AstNode *stmts;
            struct AstNode *last;
            DynArray dctorBlocks;
            bool returned;
            bool sealed;
        } blockStmt;

        struct {
            struct AstNode *var;
            struct AstNode *range;
            struct AstNode *body;
        } forStmt;

        struct {
            struct AstNode *cond;
            struct AstNode *body;
            struct AstNode *update;
        } whileStmt;

        struct {
            u64 index;
            struct AstNode *cond;
            struct AstNode *cases;
        } switchStmt;

        struct {
            u64 index;
            struct AstNode *expr;
            struct AstNode *cases;
        } matchStmt;

        struct {
            struct AstNode *match;
            struct AstNode *body;
            struct AstNode *variable;
            u32 idx;
        } caseStmt;

        struct {
            AstNode *original;
            cstring message;
        } error;

        struct {
            AstNode *node;
            u16 stages;
            cstring filePath;
            union {
                FormatState *state;
            };
        } metadata;

        struct {
            AstNode *target;
        } reference;

        struct {
            AstNode *value;
            u32 idx;
        } unionValue;

        struct {
            AstNode **nodes;
            u64 nodesCount;
        } nodeArray;

        struct {
            AstNode *target;
        } branch;

        struct {
            AstNode *cond;
            AstNode *trueBB;
            AstNode *falseBB;
        } branchIf;

        struct {
            u32 index;
            AstNodeList stmts;
        } basicBlock;

        struct {
            AstNode **incoming;
            u64 incomingCount;
        } phi;

        struct {
            AstNode *cond;
            AstNode *defaultBB;
            SwitchIrCase *cases;
            u64 casesCount;
        } switchIr;

        struct {
            AstNode *value;
            i64 index;
        } gep;
    };
};

#define CXY_AST_NODE_BODY_SIZE (sizeof(AstNode) - sizeof(((AstNode *)0)->_head))

void clearAstBody(AstNode *node);

AstNode *makeAstNode(MemPool *pool, const FileLoc *loc, const AstNode *node);

AstNode *makeVoidAstNode(MemPool *pool,
                         const FileLoc *loc,
                         u64 flags,
                         AstNode *next,
                         const Type *type);

AstNode *makeIntegerLiteral(MemPool *pool,
                            const FileLoc *loc,
                            i64 value,
                            AstNode *next,
                            const Type *type);

AstNode *makeUnsignedIntegerLiteral(MemPool *pool,
                                    const FileLoc *loc,
                                    u64 value,
                                    AstNode *next,
                                    const Type *type);

AstNode *makeCharLiteral(MemPool *pool,
                         const FileLoc *loc,
                         i32 value,
                         AstNode *next,
                         const Type *type);

AstNode *makeBoolLiteral(MemPool *pool,
                         const FileLoc *loc,
                         bool value,
                         AstNode *next,
                         const Type *type);

AstNode *makeFloatLiteral(MemPool *pool,
                          const FileLoc *loc,
                          f64 value,
                          AstNode *next,
                          const Type *type);

AstNode *makeNullLiteral(MemPool *pool,
                         const FileLoc *loc,
                         AstNode *next,
                         const Type *type);

AstNode *makeStringLiteral(MemPool *pool,
                           const FileLoc *loc,
                           cstring value,
                           AstNode *next,
                           const Type *type);

AstNode *makeIdentifier(MemPool *pool,
                        const FileLoc *loc,
                        cstring name,
                        u32 super,
                        AstNode *next,
                        const Type *type);

AstNode *makeResolvedIdentifier(MemPool *pool,
                                const FileLoc *loc,
                                cstring name,
                                u32 super,
                                AstNode *resolvesTo,
                                AstNode *next,
                                const Type *type);

AstNode *makePointerAstNode(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *pointed,
                            AstNode *next,
                            const Type *type);

AstNode *makeVoidPointerAstNode(MemPool *pool,
                                const FileLoc *loc,
                                u64 flags,
                                AstNode *next);

AstNode *makePath(MemPool *pool,
                  const FileLoc *loc,
                  cstring name,
                  u64 flags,
                  const Type *type);

AstNode *makePathWithElements(MemPool *pool,
                              const FileLoc *loc,
                              u64 flags,
                              AstNode *elements,
                              AstNode *next);

AstNode *makeResolvedPath(MemPool *pool,
                          const FileLoc *loc,
                          cstring name,
                          u64 flags,
                          AstNode *resolvesTo,
                          AstNode *next,
                          const Type *type);

AstNode *makeResolvedPathWithArgs(MemPool *pool,
                                  const FileLoc *loc,
                                  cstring name,
                                  u64 flags,
                                  AstNode *resolvesTo,
                                  AstNode *genericArgs,
                                  const Type *type);

AstNode *makeResolvedPathElement(MemPool *pool,
                                 const FileLoc *loc,
                                 cstring name,
                                 u64 flags,
                                 AstNode *resolvesTo,
                                 AstNode *next,
                                 const Type *type);

AstNode *makeResolvedPathElementWithArgs(MemPool *pool,
                                         const FileLoc *loc,
                                         cstring name,
                                         u64 flags,
                                         AstNode *resolvesTo,
                                         AstNode *next,
                                         AstNode *genericArgs,
                                         const Type *type);

AstNode *makeFieldExpr(MemPool *pool,
                       const FileLoc *loc,
                       cstring name,
                       u64 flags,
                       AstNode *value,
                       const AstNode *structField,
                       AstNode *next);

AstNode *makeStructField(MemPool *pool,
                         const FileLoc *loc,
                         cstring name,
                         u64 flags,
                         AstNode *type,
                         AstNode *def,
                         AstNode *next);

AstNode *makeStructDecl(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        cstring name,
                        AstNode *members,
                        AstNode *next,
                        const Type *type);

AstNode *makeClassDecl(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       cstring name,
                       AstNode *members,
                       AstNode *base,
                       AstNode *interfaces,
                       AstNode *next,
                       const Type *type);

AstNode *makeGroupExpr(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       AstNode *exprs,
                       AstNode *next);

AstNode *makeUnionValueExpr(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *value,
                            u32 idx,
                            AstNode *next,
                            const Type *type);

AstNode *makeCastExpr(MemPool *pool,
                      const FileLoc *loc,
                      u64 flags,
                      AstNode *expr,
                      AstNode *target,
                      AstNode *next,
                      const Type *type);

AstNode *makePointerOfExpr(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *expr,
                           AstNode *next,
                           const Type *type);

AstNode *makeReferenceOfExpr(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             AstNode *expr,
                             AstNode *next,
                             const Type *type);

AstNode *makeTypedExpr(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       AstNode *expr,
                       AstNode *target,
                       AstNode *next,
                       const Type *type);

AstNode *makeTupleExpr(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       AstNode *members,
                       AstNode *next,
                       const Type *type);

AstNode *makeArrayExpr(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       AstNode *members,
                       AstNode *next,
                       const Type *type);

AstNode *makeTupleTypeAst(MemPool *pool,
                          const FileLoc *loc,
                          u64 flags,
                          AstNode *members,
                          AstNode *next,
                          const Type *type);

attr(always_inline) static AstNode *makePathElement(MemPool *pool,
                                                    const FileLoc *loc,
                                                    cstring name,
                                                    u64 flags,
                                                    AstNode *next,
                                                    const Type *type)
{
    return makeResolvedPathElement(pool, loc, name, flags, NULL, next, type);
}

AstNode *makeCallExpr(MemPool *pool,
                      const FileLoc *loc,
                      AstNode *callee,
                      AstNode *args,
                      u64 flags,
                      AstNode *next,
                      const Type *type);

AstNode *makeSpreadExpr(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *expr,
                        AstNode *next,
                        const Type *type);

AstNode *makeMemberExpr(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *target,
                        AstNode *member,
                        AstNode *next,
                        const Type *type);

AstNode *makePathFromIdent(MemPool *pool, const AstNode *ident);

AstNode *makeGenIdent(MemPool *pool,
                      struct StrPool *strPool,
                      const FileLoc *loc,
                      const Type *type);

AstNode *makeExprStmt(MemPool *pool,
                      const FileLoc *loc,
                      u64 flags,
                      AstNode *expr,
                      AstNode *next,
                      const Type *type);

AstNode *makeDeferStmt(
    MemPool *pool, const FileLoc *loc, u64 flags, AstNode *stmt, AstNode *next);

AstNode *makeStmtExpr(MemPool *pool,
                      const FileLoc *loc,
                      u64 flags,
                      AstNode *stmt,
                      AstNode *next,
                      const Type *type);

AstNode *makeUnaryExpr(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       bool isPrefix,
                       Operator op,
                       AstNode *operand,
                       AstNode *next,
                       const Type *type);

AstNode *makeBlockStmt(MemPool *pool,
                       const FileLoc *loc,
                       AstNode *stmts,
                       AstNode *next,
                       const Type *type);

AstNode *makeWhileStmt(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       AstNode *condition,
                       AstNode *body,
                       AstNode *next,
                       AstNode *update);

AstNode *makeFunctionDecl(MemPool *pool,
                          const FileLoc *loc,
                          cstring name,
                          AstNode *params,
                          AstNode *returnType,
                          AstNode *body,
                          u64 flags,
                          AstNode *next,
                          const Type *type);

AstNode *makeFunctionType(MemPool *pool,
                          const FileLoc *loc,
                          AstNode *params,
                          AstNode *returnType,
                          u64 flags,
                          AstNode *next,
                          const Type *type);

AstNode *makeFunctionParam(MemPool *pool,
                           const FileLoc *loc,
                           cstring name,
                           AstNode *paramType,
                           AstNode *defaultValue,
                           u64 flags,
                           AstNode *next);

AstNode *makeOperatorOverload(MemPool *pool,
                              const FileLoc *loc,
                              Operator op,
                              AstNode *params,
                              AstNode *returnType,
                              AstNode *body,
                              u64 flags,
                              AstNode *next,
                              const Type *type);

AstNode *makeNewExpr(MemPool *pool,
                     const FileLoc *loc,
                     u64 flags,
                     AstNode *target,
                     AstNode *init,
                     AstNode *next,
                     const Type *type);

AstNode *makeStructExpr(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *left,
                        AstNode *fields,
                        AstNode *next,
                        const Type *type);

AstNode *makeStructExprFromType(MemPool *pool,
                                const FileLoc *loc,
                                u64 flags,
                                AstNode *fields,
                                AstNode *next,
                                const Type *type);

AstNode *makeVarDecl(MemPool *pool,
                     const FileLoc *loc,
                     u64 flags,
                     cstring name,
                     AstNode *varType,
                     AstNode *init,
                     AstNode *next,
                     const Type *type);

AstNode *makeArrayTypeAstNode(MemPool *pool,
                              const FileLoc *loc,
                              u64 flags,
                              AstNode *elementType,
                              u64 len,
                              AstNode *next,
                              const Type *type);

AstNode *makeBinaryExpr(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *lhs,
                        Operator op,
                        AstNode *rhs,
                        AstNode *next,
                        const Type *type);

AstNode *makeAssignExpr(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *lhs,
                        Operator op,
                        AstNode *rhs,
                        AstNode *next,
                        const Type *type);

AstNode *makeIndexExpr(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       AstNode *target,
                       AstNode *index,
                       AstNode *next,
                       const Type *type);

AstNode *makeAttribute(MemPool *pool,
                       const FileLoc *loc,
                       cstring name,
                       AstNode *args,
                       AstNode *next);

AstNode *makeBackendCallExpr(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             BackendFuncId func,
                             AstNode *args,
                             const Type *type);

AstNode *makeEnumOptionAst(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           cstring name,
                           AstNode *value,
                           AstNode *next,
                           const Type *type);

AstNode *makeEnumAst(MemPool *pool,
                     const FileLoc *loc,
                     u64 flags,
                     cstring name,
                     AstNode *base,
                     AstNode *options,
                     AstNode *next,
                     const Type *type);

AstNode *makeProgramAstNode(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *module,
                            AstNode *top,
                            AstNode *decls,
                            const Type *type);

AstNode *makeModuleAstNode(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           cstring name,
                           Env *env,
                           const Type *type);

AstNode *makeTypeDeclAstNode(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             cstring name,
                             AstNode *aliased,
                             AstNode *next,
                             const Type *type);

AstNode *makeMacroDeclAstNode(MemPool *pool,
                              const FileLoc *loc,
                              u64 flags,
                              cstring name,
                              AstNode *params,
                              AstNode *body,
                              AstNode *next);

AstNode *makeMacroCallAstNode(MemPool *pool,
                              const FileLoc *loc,
                              u64 flags,
                              AstNode *callee,
                              AstNode *args,
                              AstNode *next);

AstNode *makeBasicBlockAstNode(
    MemPool *pool, const FileLoc *loc, u64 flags, u32 index, AstNode *func);

AstNode *makeReturnAstNode(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *expr,
                           AstNode *next,
                           const Type *type);

AstNode *makeBranchAstNode(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *target,
                           AstNode *next);

AstNode *makeBranchIfAstNode(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             AstNode *cond,
                             AstNode *trueBB,
                             AstNode *falseBB,
                             AstNode *next);

AstNode *makePhiAstNode(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode **incoming,
                        u64 incomingCount,
                        AstNode *next);

AstNode *makeSwitchIrAstNode(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             AstNode *cond,
                             AstNode *defaultBB,
                             SwitchIrCase *cases,
                             u64 casesCount,
                             AstNode *next);

AstNode *makeGepAstNode(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *value,
                        i64 index,
                        AstNode *next);

AstNode *makeNodeArray(MemPool *pool,
                       const FileLoc *loc,
                       AstNode **nodes,
                       u64 nodesCount);

AstNode *makeAstClosureCapture(MemPool *pool, AstNode *captured);

AstNode *makeAstNop(MemPool *pool, const FileLoc *loc);

AstNode *copyAstNode(MemPool *pool, const AstNode *node);

AstNode *duplicateAstNode(MemPool *pool, const AstNode *node);

void initCloneAstNodeMapping(CloneAstConfig *config);

void deinitCloneAstNodeConfig(CloneAstConfig *config);

AstNode *cloneAstNode(CloneAstConfig *config, const AstNode *node);

static inline AstNode *shallowCloneAstNode(MemPool *pool, const AstNode *node)
{
    CloneAstConfig config = {.createMapping = false, .pool = pool};
    return cloneAstNode(&config, node);
}

AstNode *deepCloneAstNode(MemPool *pool, const AstNode *node);

AstNode *deepCloneManyAstNode(MemPool *pool, const AstNode *node);

AstNode *cloneGenericDeclaration(MemPool *pool, const AstNode *node);

AstNode *replaceAstNode(AstNode *node, const AstNode *with);

void replaceAstNodeInList(AstNode **list, const AstNode *node, AstNode *with);

AstNode *replaceAstNodeWith(AstNode *node, const AstNode *with);

static inline bool nodeIs_(const AstNode *node, AstTag tag)
{
    return node && node->tag == tag;
}

#define nodeIs(NODE, TAG) nodeIs_((NODE), ast##TAG)

#define hasFlag(ITEM, FLG) ((ITEM) && ((ITEM)->flags & (flg##FLG)))
#define hasFlags(ITEM, FLGS) ((ITEM) && ((ITEM)->flags & (FLGS)))

bool isTuple(const AstNode *node);

bool isAssignableExpr(const AstNode *node);

bool isLiteralExpr(const AstNode *node);

bool isStaticExpr(const AstNode *node);

bool isEnumLiteral(const AstNode *node);

bool isIntegralLiteral(const AstNode *node);

bool isNumericLiteral(const AstNode *node);

bool isTypeExpr(const AstNode *node);

bool isBuiltinTypeExpr(const AstNode *node);

bool isMemberFunction(const AstNode *node);

bool isStaticMemberFunction(const AstNode *node);

f64 nodeGetNumericLiteral(const AstNode *node);
i64 getEnumLiteralValue(const AstNode *node);

void nodeSetNumericLiteral(AstNode *node,
                           AstNode *lhs,
                           AstNode *rhs,
                           f64 value);

bool comptimeCompareTypes(const AstNode *lhs, const AstNode *rhs);

static inline bool isClassOrStructAstNode(const AstNode *node)
{
    return nodeIs(node, StructDecl) || nodeIs(node, ClassDecl);
}

u64 countAstNodes(const AstNode *node);

u64 countProgramDecls(const AstNode *program);

AstNode *getLastAstNode(AstNode *node);

AstNode *getNodeAtIndex(AstNode *node, u64 index);

AstNode *findMemberByName(AstNode *node, cstring name);

AstNode *findEnumOptionByName(AstNode *node, cstring name);

AstNode *getParentScopeWithTag(AstNode *node, AstTag tag);

const AstNode *getLastAstNodeConst(const AstNode *node);

const AstNode *getConstNodeAtIndex(const AstNode *node, u64 index);

const AstNode *getParentScopeWithTagConst(const AstNode *node, AstTag tag);

void insertAstNodeAfter(AstNode *before, AstNode *after);

AstNode *insertAstNode(AstNodeList *list, AstNode *node);

void unlinkAstNode(AstNode **head, AstNode *prev, AstNode *node);

const char *getDeclKeyword(AstTag tag);

const char *getDeclarationName(const AstNode *node);

void setDeclarationName(AstNode *node, cstring name);

void setForwardDeclDefinition(AstNode *node, AstNode *definition);

AstNode *getForwardDeclDefinition(AstNode *node);

AstNode *getGenericDeclarationParams(AstNode *node);

void setGenericDeclarationParams(AstNode *node, AstNode *params);

const AstNode *findAttribute(const AstNode *node, cstring name);

const AstNode *findAttributeArgument(const AstNode *attr, cstring name);

const AstNode *getAttributeArgument(Log *L,
                                    const FileLoc *loc,
                                    const AstNode *attr,
                                    u32 index);

FileLoc *getDeclarationLoc(FileLoc *dst, const AstNode *node);

bool mapAstNode(HashTable *mapping, const AstNode *from, AstNode *to);

cstring getAstNodeName(const AstNode *node);

FunctionSignature *makeFunctionSignature(MemPool *pool,
                                         const FunctionSignature *from);

AstNode *getParentScope(AstNode *node);

AstNode *getMemberParentScope(AstNode *node);

AstNode *makeTypeReferenceNode(MemPool *pool,
                               const Type *type,
                               const FileLoc *loc);

AstNode *makeTypeReferenceNode2(MemPool *pool,
                                const Type *type,
                                const FileLoc *loc,
                                AstNode *next);

AstNode *findInAstNode(AstNode *node, cstring name);

AstNode *resolvePath(const AstNode *path);

AstNode *resolveAstNode(AstNode *node);

AstNode *getResolvedPath(const AstNode *path);

AstNode *getMemberFunctionThis(AstNode *node);

int isInInheritanceChain(const AstNode *node, const AstNode *parent);

AstNode *getBaseClassAtLevel(AstNode *node, u64 level);

AstNode *getBaseClassByName(AstNode *node, cstring name);

attr(always_inline) static i64 integerLiteralValue(const AstNode *node)
{
    csAssert0(nodeIs(node, IntegerLit));
    return node->intLiteral.isNegative ? node->intLiteral.value
                                       : (i64)node->intLiteral.uValue;
}

attr(always_inline) static u64 unsignedIntegerLiteralValue(const AstNode *node)
{
    csAssert0(nodeIs(node, IntegerLit));
    return node->intLiteral.isNegative ? (u64)node->intLiteral.value
                                       : node->intLiteral.uValue;
}

int compareNamedAstNodes(const void *lhs, const void *rhs);

SortedNodes *makeSortedNodes(MemPool *pool,
                             AstNode *nodes,
                             int (*compare)(const void *, const void *));

const AstNode *getOptionalDecl();

AstNode *findInSortedNodes(SortedNodes *sorted, cstring name);

static inline AstNode *underlyingDeclaration(AstNode *decl)
{
    return nodeIs(decl, GenericDecl) ? decl->genericDecl.decl : decl;
}

static bool isStructDeclaration(AstNode *node)
{
    return nodeIs(node, StructDecl) ||
           nodeIs(node, GenericDecl) &&
               isStructDeclaration(node->genericDecl.decl);
}

static bool isClassDeclaration(AstNode *node)
{
    return nodeIs(node, ClassDecl) ||
           nodeIs(node, GenericDecl) &&
               isClassDeclaration(node->genericDecl.decl);
}

bool nodeIsLeftValue(const AstNode *node);

bool nodeIsCallExpr(const AstNode *node);

bool nodeIsNoop(const AstNode *node);

bool nodeIsThisParam(const AstNode *node);

bool nodeIsThisArg(const AstNode *node);

CCodeKind getCCodeKind(TokenTag tag);

#ifdef __cplusplus
}
#endif