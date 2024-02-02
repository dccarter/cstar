// Credits https://github.com/madmann91/fu/blob/master/src/fu/lang/ast.h

#pragma once

#include "core/format.h"
#include "core/log.h"
#include "core/mempool.h"
#include "operator.h"
#include "primitives.h"
#include "token.h"

struct StrPool;

// clang-format off

#define CXY_LANG_AST_TAGS(f) \
    f(Error)                 \
    f(Nop)                  \
    f(Ref)                  \
    f(Deleted)              \
    f(ComptimeOnly)         \
    f(ClosureCapture)       \
    f(ExternDecl)           \
    f(Program)              \
    f(Metadata)             \
    f(CCode)                \
    f(Define)               \
    f(Attr)                 \
    f(PathElem)             \
    f(Path)                 \
    f(UnionValue)           \
    f(GenericParam)         \
    f(GenericDecl)          \
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
    f(Declarations)         \
    f(FuncParam)            \
    f(FuncDecl)             \
    f(MacroDecl)            \
    f(VarDecl)              \
    f(TypeDecl)             \
    f(ForwardDecl)           \
    f(UnionDecl)            \
    f(EnumOption)           \
    f(EnumDecl)             \
    f(Field)                \
    f(StructDecl)           \
    f(ClassDecl)            \
    f(InterfaceDecl)        \
    f(ModuleDecl)           \
    f(ImportDecl)           \
    f(Expressions)          \
    f(GroupExpr)            \
    f(UnaryExpr)            \
    f(AddressOf)            \
    f(BinaryExpr)           \
    f(AssignExpr)           \
    f(TernaryExpr)          \
    f(StmtExpr)             \
    f(StringExpr)           \
    f(TypedExpr)            \
    f(CastExpr)             \
    f(CallExpr)             \
    f(MacroCallExpr)        \
    f(ClosureExpr)          \
    f(ArrayExpr)            \
    f(IndexExpr)            \
    f(TupleExpr)            \
    f(FieldExpr)            \
    f(StructExpr)           \
    f(MemberExpr)           \
    f(RangeExpr)            \
    f(NewExpr)              \
    f(SpreadExpr)           \
    f(Statements)           \
    f(ExprStmt)             \
    f(BreakStmt)            \
    f(ContinueStmt)         \
    f(DeferStmt)            \
    f(ReturnStmt)           \
    f(BlockStmt)            \
    f(IfStmt)               \
    f(ForStmt)              \
    f(WhileStmt)            \
    f(SwitchStmt)           \
    f(MatchStmt)            \
    f(CaseStmt)

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
    MemPool *pool;
    HashTable mapping;
} AstNodeCloneConfig;

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
    void *codegen;

typedef struct FunctionSignature {
    struct AstNode *params;
    struct AstNode *ret;
    struct AstNode *typeParams;
} Signature;

typedef struct SortedNodes {
    u64 count;
    int (*compare)(const void *lhs, const void *rhs);
    struct AstNode *nodes[0];
} SortedNodes;

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
        } program;

        struct {
            AstNode *what;
        } cCode;

        struct {
            AstNode *names;
            AstNode *type;
            AstNode *container;
        } define;

        struct {
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
        } moduleDecl;

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
            struct AstNode *constraints;
            u16 inferIndex;
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
            Signature *signature;
            struct AstNode *opaqueParams;
            struct AstNode *this_;
            union {
                struct AstNode *body;
                struct AstNode *definition;
            };
            struct AstNode *coroEntry;
        } funcDecl;

        struct {
            const char *name;
            struct AstNode *params;
            struct AstNode *ret;
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
            struct AstNode *type;
            struct AstNode *value;
        } structField;

        struct {
            cstring name;
            struct AstNode *implements;
            struct AstNode *base;
            struct AstNode *members;
            struct AstNode *typeParams;
            const struct Type *thisType;
            struct AstNode *closureForward;
        } structDecl;

        struct {
            cstring name;
            struct AstNode *implements;
            struct AstNode *base;
            struct AstNode *members;
            struct AstNode *typeParams;
            const struct Type *thisType;
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
            struct AstNode *expr;
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
            bool returned;
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
    };
};

#define CXY_AST_NODE_BODY_SIZE (sizeof(AstNode) - sizeof(((AstNode *)0)->_head))

AstNode *nodeMakeVoid(MemPool *pool,
                      const FileLoc *loc,
                      u64 flags,
                      AstNode *next,
                      const Type *type);

AstNode *nodeMakeIntegerLit(MemPool *pool,
                            const FileLoc *loc,
                            i64 value,
                            AstNode *next,
                            const Type *type);
AstNode *nodeMakeUnsignedIntLit(MemPool *pool,
                                const FileLoc *loc,
                                u64 value,
                                AstNode *next,
                                const Type *type);
AstNode *nodeMakeCharLit(MemPool *pool,
                         const FileLoc *loc,
                         i32 value,
                         AstNode *next,
                         const Type *type);

AstNode *nodeMakeBoolLit(MemPool *pool,
                         const FileLoc *loc,
                         bool value,
                         AstNode *next,
                         const Type *type);

AstNode *nodeMakeFloatLit(MemPool *pool,
                          const FileLoc *loc,
                          f64 value,
                          AstNode *next,
                          const Type *type);

AstNode *nodeMakeNullLit(MemPool *pool,
                         const FileLoc *loc,
                         AstNode *next,
                         const Type *type);

AstNode *nodeMakeStringLit(MemPool *pool,
                           const FileLoc *loc,
                           cstring value,
                           AstNode *next,
                           const Type *type);

AstNode *nodeMakeIdentifier(MemPool *pool,
                            const FileLoc *loc,
                            cstring name,
                            u32 super,
                            AstNode *next,
                            const Type *type);

AstNode *nodeMakeResolvedIdentifier(MemPool *pool,
                                    const FileLoc *loc,
                                    cstring name,
                                    u32 super,
                                    AstNode *resolvesTo,
                                    AstNode *next,
                                    const Type *type);

AstNode *nodeMakePointer(MemPool *pool,
                         const FileLoc *loc,
                         u64 flags,
                         AstNode *pointed,
                         AstNode *next,
                         const Type *type);

AstNode *nodeMakeVoidPointer(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             AstNode *next);

AstNode *nodeMakePath(MemPool *pool,
                      const FileLoc *loc,
                      cstring name,
                      u64 flags,
                      const Type *type);

AstNode *nodeMakePathWithElems(MemPool *pool,
                               const FileLoc *loc,
                               u64 flags,
                               AstNode *elements,
                               AstNode *next);

AstNode *nodeMakeResolvedPath(MemPool *pool,
                              const FileLoc *loc,
                              cstring name,
                              u64 flags,
                              AstNode *resolvesTo,
                              AstNode *next,
                              const Type *type);

AstNode *nodeMakeResolvedPathWithArgs(MemPool *pool,
                                      const FileLoc *loc,
                                      cstring name,
                                      u64 flags,
                                      AstNode *resolvesTo,
                                      AstNode *genericArgs,
                                      const Type *type);

AstNode *nodeMakeResolvedPathElem(MemPool *pool,
                                  const FileLoc *loc,
                                  cstring name,
                                  u64 flags,
                                  AstNode *resolvesTo,
                                  AstNode *next,
                                  const Type *type);

AstNode *nodeMakeResolvedPathElemWithArgs(MemPool *pool,
                                          const FileLoc *loc,
                                          cstring name,
                                          u64 flags,
                                          AstNode *resolvesTo,
                                          AstNode *next,
                                          AstNode *genericArgs,
                                          const Type *type);

AstNode *nodeMakeFieldExpr(MemPool *pool,
                           const FileLoc *loc,
                           cstring name,
                           u64 flags,
                           AstNode *value,
                           AstNode *next);

AstNode *nodeMakeField(MemPool *pool,
                       const FileLoc *loc,
                       cstring name,
                       u64 flags,
                       AstNode *type,
                       AstNode *def,
                       AstNode *next);

AstNode *nodeMakeGroupExpr(MemPool *pool, //
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *expr,
                           AstNode *next);

AstNode *nodeMakeUnionValue(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *value,
                            u32 idx,
                            AstNode *next,
                            const Type *type);

AstNode *nodeMakeCastExpr(MemPool *pool,
                          const FileLoc *loc,
                          u64 flags,
                          AstNode *expr,
                          AstNode *target,
                          AstNode *next,
                          const Type *type);

AstNode *nodeMakeAddrOffExpr(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             AstNode *expr,
                             AstNode *next,
                             const Type *type);

AstNode *nodeMakeTypedExpr(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *expr,
                           AstNode *target,
                           AstNode *next,
                           const Type *type);

AstNode *nodeMakeTupleExpr(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *members,
                           AstNode *next,
                           const Type *type);

AstNode *nodeMakeTupleType(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *members,
                           AstNode *next,
                           const Type *type);

attr(always_inline) static AstNode *nodeMakePathElem(MemPool *pool,
                                                     const FileLoc *loc,
                                                     cstring name,
                                                     u64 flags,
                                                     AstNode *next,
                                                     const Type *type)
{
    return nodeMakeResolvedPathElem(pool, loc, name, flags, NULL, next, type);
}

AstNode *nodeMakeCallExpr(MemPool *pool,
                          const FileLoc *loc,
                          AstNode *callee,
                          AstNode *args,
                          u64 flags,
                          AstNode *next,
                          const Type *type);

AstNode *nodeMakeSpreadExpr(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *expr,
                            AstNode *next,
                            const Type *type);

AstNode *nodeMakeMemberExpr(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *target,
                            AstNode *member,
                            AstNode *next,
                            const Type *type);

AstNode *nodeGenerateIdent(MemPool *pool,
                           struct StrPool *strPool,
                           const FileLoc *loc,
                           const Type *type);

AstNode *nodeMakeExprStmt(MemPool *pool,
                          const FileLoc *loc,
                          u64 flags,
                          AstNode *expr,
                          AstNode *next,
                          const Type *type);

AstNode *nodeMakeStmtExpr(MemPool *pool,
                          const FileLoc *loc,
                          u64 flags,
                          AstNode *stmt,
                          AstNode *next,
                          const Type *type);

AstNode *nodeMakeUnaryExpr(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           bool isPrefix,
                           Operator op,
                           AstNode *operand,
                           AstNode *next,
                           const Type *type);

AstNode *nodeMakeBlockStmt(MemPool *pool,
                           const FileLoc *loc,
                           AstNode *stmts,
                           AstNode *next,
                           const Type *type);

AstNode *nodeMakeWhileStmt(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *condition,
                           AstNode *body,
                           AstNode *next,
                           const Type *type);

AstNode *nodeMakeFucDecl(MemPool *pool,
                         const FileLoc *loc,
                         cstring name,
                         AstNode *params,
                         AstNode *returnType,
                         AstNode *body,
                         u64 flags,
                         AstNode *next,
                         const Type *type);

AstNode *nodeMakeFuncParam(MemPool *pool,
                           const FileLoc *loc,
                           cstring name,
                           AstNode *paramType,
                           AstNode *defaultValue,
                           u64 flags,
                           AstNode *next);

AstNode *nodeMakeOperatorFunc(MemPool *pool,
                              const FileLoc *loc,
                              Operator op,
                              AstNode *params,
                              AstNode *returnType,
                              AstNode *body,
                              u64 flags,
                              AstNode *next,
                              const Type *type);

AstNode *nodeMakeNewExpr(MemPool *pool,
                         const FileLoc *loc,
                         u64 flags,
                         AstNode *target,
                         AstNode *init,
                         AstNode *next,
                         const Type *type);

AstNode *nodeMakeStructExpr_(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             AstNode *left,
                             AstNode *fields,
                             AstNode *next,
                             const Type *type);

AstNode *nodeMakeStructExpr(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *fields,
                            AstNode *next,
                            const Type *type);

AstNode *nodeMakeVarDecl(MemPool *pool,
                         const FileLoc *loc,
                         u64 flags,
                         cstring name,
                         AstNode *varType,
                         AstNode *init,
                         AstNode *next,
                         const Type *type);

AstNode *nodeMakeArrayType(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *elementType,
                           u64 len,
                           AstNode *next,
                           const Type *type);

AstNode *nodeMakeBinaryExpr(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *lhs,
                            Operator op,
                            AstNode *rhs,
                            AstNode *next,
                            const Type *type);

AstNode *nodeMakeAssignExpr(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *lhs,
                            Operator op,
                            AstNode *rhs,
                            AstNode *next,
                            const Type *type);

AstNode *nodeMakeIndexExpr(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *target,
                           AstNode *index,
                           AstNode *next,
                           const Type *type);

AstNode *nodeMakeTypeReference_(MemPool *pool,
                                const FileLoc *loc,
                                u64 flags,
                                const Type *type,
                                AstNode *next);

static inline AstNode *makeTypeReferenceNode(MemPool *pool,
                                             const FileLoc *loc,
                                             u64 flags,
                                             const Type *type)
{
    return nodeMakeTypeReference_(pool, loc, flags, type, NULL);
}

Signature *nodeMakeSignature(MemPool *pool, const Signature *from);
AstNode *nodeMakeClosureCapture(MemPool *pool, AstNode *captured);
AstNode *nodeMakePathFromIdent(MemPool *pool, const AstNode *ident);
AstNode *nodeMakeNop(MemPool *pool, const FileLoc *loc);
void nodeClearBody(AstNode *node);
AstNode *nodeNew(MemPool *pool, const FileLoc *loc, const AstNode *node);

void nodeCloneConfigInit(AstNodeCloneConfig *config);
void nodeCloneConfigDeinit(AstNodeCloneConfig *config);
AstNode *nodeCopy(const AstNode *node, MemPool *pool);
AstNode *nodeDuplicate(const AstNode *node, MemPool *pool);
AstNode *nodeClone(AstNodeCloneConfig *config, const AstNode *node);
AstNode *nodeDeepClone(const AstNode *node, MemPool *pool);
AstNode *nodeGenericDeclClone(const AstNode *node, MemPool *pool);
AstNode *nodeReplace(AstNode *node, const AstNode *with);
void nodeReplaceInList(const AstNode *node, AstNode **list, AstNode *with);
AstNode *replaceAstNodeWith(AstNode *node, const AstNode *with);
static inline AstNode *nodeShallowClone(const AstNode *node, MemPool *pool)
{
    AstNodeCloneConfig config = {.pool = pool, .createMapping = false};
    return nodeClone(&config, node);
}

static inline bool nodeIs_(const AstNode *node, AstTag tag)
{
    return node && node->tag == tag;
}
#define nodeIs(NODE, TAG) nodeIs_((NODE), ast##TAG)

cstring nodeGetString(const AstNode *node);
cstring nodeGetMemberName(const AstNode *node);

bool nodeIsTuple(const AstNode *node);
bool nodeIsAssignableExpr(const AstNode *node);
bool nodeIsLiteralExpr(const AstNode *node);
bool nodeIsEnumLiteral(const AstNode *node);
bool nodeIsIntegerLit(const AstNode *node);
bool nodeIsTypeExpr(const AstNode *node);
bool nodeIsBuiltinTypeExpr(const AstNode *node);
bool nodeIsLValue(const AstNode *node);

static inline bool nodeIsStructural(const AstNode *node)
{
    return nodeIs(node, StructDecl) || nodeIs(node, ClassDecl);
}

static bool nodeIsStructDecl(AstNode *node)
{
    return nodeIs(node, StructDecl) ||
           nodeIs(node, GenericDecl) &&
               nodeIsStructDecl(node->genericDecl.decl);
}

u64 nodeListCount(const AstNode *node);
AstNode *nodeListGetLast(AstNode *node);
AstNode *nodeListInsert(AstNodeList *list, AstNode *node);
AstNode *nodeListGetAtIndex(AstNode *node, u64 index);
const AstNode *nodeListGetLastConst(const AstNode *node);
const AstNode *nodeListGetAtIndexConst(const AstNode *node, u64 index);
void nodeListUnlink(AstNode **head, AstNode *prev, AstNode *node);
void nodeListInsertAfter(AstNode *before, AstNode *after);

AstNode *nodeFindMemberByName(AstNode *node, cstring name);
AstNode *nodeFindOptionByName(AstNode *node, cstring name);
const AstNode *nodeFindAttr(const AstNode *node, cstring name);
const AstNode *nodeFindAttrArgument(const AstNode *attr, cstring name);

AstNode *nodeGetParentScope(AstNode *node, AstTag tag);
const AstNode *nodeGetParentScopeConst(const AstNode *node, AstTag tag);
AstNode *nodeResolveParentScope(AstNode *node);

cstring getDeclKeyword(AstTag tag);
cstring nodeGetDeclName(const AstNode *node);
AstNode *nodeGetDefinition(AstNode *node);
AstNode *nodeGetGenericDeclParams(AstNode *node);
void nodeSetGenericDeclParams(AstNode *node, AstNode *params);
void nodeSetDeclName(AstNode *node, cstring name);
void nodeSetDefinition(AstNode *node, AstNode *definition);
bool nodeAddMapping(HashTable *mapping, const AstNode *from, AstNode *to);
AstNode *nodeFindByName(AstNode *node, cstring name);
AstNode *nodeResolvePath(const AstNode *path);
AstNode *nodeGetResolved(const AstNode *path);
AstNode *nodeBaseAtLevel(AstNode *node, u64 level);
AstNode *nodeFindBase(AstNode *node, cstring name);
int nodeIsInheritanceChain(const AstNode *node, const AstNode *parent);

attr(always_inline) static i64 nodeIntegerLitValue(const AstNode *node)
{
    csAssert0(nodeIs(node, IntegerLit));
    return node->intLiteral.isNegative ? node->intLiteral.value
                                       : (i64)node->intLiteral.uValue;
}

attr(always_inline) static u64 nodeUnsignedIntegerLitValue(const AstNode *node)
{
    csAssert0(nodeIs(node, IntegerLit));
    return node->intLiteral.isNegative ? (u64)node->intLiteral.value
                                       : node->intLiteral.uValue;
}

int nodeCompareByName(const void *lhs, const void *rhs);
const AstNode *nodeGetOptionalDecl();
AstNode *nodeFindInSortedNodes(SortedNodes *sorted, cstring name);
SortedNodes *nodeMakeSorted(MemPool *pool,
                            AstNode *nodes,
                            int (*compare)(const void *, const void *));

static inline AstNode *nodeGetUnderlyingDecl(AstNode *decl)
{
    return nodeIs(decl, GenericDecl) ? decl->genericDecl.decl : decl;
}
