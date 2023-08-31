// Credits https://github.com/madmann91/fu/blob/master/src/fu/lang/ast.h

#pragma once

#include "core/format.h"
#include "core/log.h"
#include "core/mempool.h"
#include "lang/operator.h"
#include "lang/token.h"

struct StrPool;

// clang-format off

#define CXY_LANG_AST_TAGS(f) \
    f(Error)                \
    f(Nop)                  \
    f(ComptimeOnly)         \
    f(Program)              \
    f(Metadata)             \
    f(CCode)                \
    f(Define)               \
    f(Attr)                 \
    f(PathElem)             \
    f(Path)                 \
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
    f(UnionDecl)            \
    f(EnumOption)           \
    f(EnumDecl)             \
    f(StructField)          \
    f(StructDecl)           \
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
    f(CaseStmt)

// clang-format on

typedef enum {

#define f(name) ast##name,
    CXY_LANG_AST_TAGS(f)
#undef f

        astCOUNT
} AstTag;

struct Scope;

typedef struct AstNode AstNode;

typedef struct AstNodeList {
    AstNode *first;
    AstNode *last;
} AstNodeList;

typedef struct CaptureSet {
    HashTable *table;
    u64 index;
} ClosureCapture;

typedef struct {
    bool createMapping;
    MemPool *pool;
    HashTable mapping;
} CloneAstConfig;

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
    struct AstNode *attrs;

typedef enum { iptModule, iptPath } ImportKind;
typedef enum { cInclude, cDefine } CCodeKind;

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
        } _body;

        struct {
            cstring name;
        } _namedNode;

        struct {
            struct AstNode *module;
            struct AstNode *top;
            struct AstNode *decls;
        } program;

        struct {
            CCodeKind kind;
            AstNode *what;
        } cCode;

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
            cstring value;
            cstring alias;
            AstNode *resolvesTo;
        } ident;

        struct {
            bool value;
        } boolLiteral;

        struct {
            i64 value;
            bool hasMinus;
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
        } arrayExpr;

        struct {
            struct AstNode *target;
            struct AstNode *member;
        } memberExpr;

        struct {
            struct AstNode *start;
            struct AstNode *end;
            struct AstNode *step;
        } rangeExpr;

        struct {
            struct AstNode *type;
            struct AstNode *init;
        } newExpr;

        struct {
            struct AstNode *to;
            struct AstNode *expr;
        } castExpr;

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
            u32 index;
            bool isKeyword;
        } pathElement;

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
            struct AstNode *body;
        } funcDecl;

        struct {
            const char *name;
            struct AstNode *params;
            struct AstNode *ret;
            struct AstNode *body;
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
        } varDecl;

        struct {
            cstring name;
            struct AstNode *aliased;
            struct AstNode *typeParams;
        } typeDecl;

        struct {
            cstring name;
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
            struct AstNode *base;
            struct AstNode *implements;
            struct AstNode *members;
            struct AstNode *typeParams;
            const struct Type *thisType;
            SortedNodes *sortedMembers;
        } structDecl;

        struct {
            cstring name;
            struct AstNode *members;
            struct AstNode *typeParams;
            SortedNodes *sortedMembers;
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
        } ternaryExpr, ifStmt;

        struct {
            struct AstNode *stmt;
        } stmtExpr;

        struct {
            struct AstNode *parts;
        } stringExpr;

        struct {
            struct AstNode *expr;
            struct AstNode *type;
        } typedExpr;

        struct {
            struct AstNode *callee;
            struct AstNode *args;
            u32 overload;
        } callExpr, macroCallExpr;

        struct {
            union {
                ClosureCapture captureSet;
                struct {
                    const AstNode **capture;
                    u64 captureCount;
                };
            };
            struct AstNode *params;
            struct AstNode *ret;
            struct AstNode *body;
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
        } blockStmt;

        struct {
            struct AstNode *var;
            struct AstNode *range;
            struct AstNode *body;
        } forStmt;

        struct {
            struct AstNode *cond;
            struct AstNode *body;
        } whileStmt;

        struct {
            u64 index;
            struct AstNode *cond;
            struct AstNode *cases;
        } switchStmt;

        struct {
            struct AstNode *match;
            struct AstNode *body;
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
    };
};

#define CXY_AST_NODE_BODY_SIZE (sizeof(AstNode) - sizeof(((AstNode *)0)->_head))

void clearAstBody(AstNode *node);
AstNode *makeAstNode(MemPool *pool, const FileLoc *loc, const AstNode *node);

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
                          const Type *type);

AstNode *makeResolvedPathElement(MemPool *pool,
                                 const FileLoc *loc,
                                 cstring name,
                                 u64 flags,
                                 AstNode *resolvesTo,
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

AstNode *makePathFromIdent(MemPool *pool, const AstNode *ident);

AstNode *makeGenIdent(MemPool *pool,
                      struct StrPool *strPool,
                      const FileLoc *loc,
                      const Type *type);

AstNode *makeExprStmt(MemPool *pool,
                      const FileLoc *loc,
                      AstNode *expr,
                      u64 flags,
                      AstNode *next,
                      const Type *type);

AstNode *makeStmtExpr(MemPool *pool,
                      const FileLoc *loc,
                      AstNode *stmt,
                      u64 flags,
                      AstNode *next,
                      const Type *type);

AstNode *makeBlockStmt(MemPool *pool,
                       const FileLoc *loc,
                       AstNode *stmts,
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

AstNode *makeVarDecl(MemPool *pool,
                     const FileLoc *loc,
                     u64 flags,
                     cstring name,
                     AstNode *init,
                     AstNode *next,
                     const Type *type);

AstNode *copyAstNode(MemPool *pool, const AstNode *node);

AstNode *duplicateAstNode(MemPool *pool, const AstNode *node);

void initCloneAstNodeMapping(CloneAstConfig *config);
void deinitCloneAstNodeConfig(CloneAstConfig *config);

AstNode *cloneAstNode(CloneAstConfig *config, const AstNode *node);

static inline AstNode *shallowCloneAstNode(MemPool *pool, const AstNode *node)
{
    return cloneAstNode(&(CloneAstConfig){.pool = pool, .createMapping = false},
                        node);
}

AstNode *cloneGenericDeclaration(MemPool *pool, const AstNode *node);

AstNode *replaceAstNode(AstNode *node, const AstNode *with);

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

bool isEnumLiteral(const AstNode *node);

bool isIntegralLiteral(const AstNode *node);

bool isTypeExpr(const AstNode *node);

bool isBuiltinTypeExpr(const AstNode *node);

bool comptimeCompareTypes(const AstNode *lhs, const AstNode *rhs);

u64 countAstNodes(const AstNode *node);

AstNode *getLastAstNode(AstNode *node);

AstNode *getNodeAtIndex(AstNode *node, u64 index);

AstNode *findStructMemberByName(AstNode *node, cstring name);

AstNode *findEnumOptionByName(AstNode *node, cstring name);

AstNode *getParentScopeWithTag(AstNode *node, AstTag tag);

const AstNode *getLastAstNodeConst(const AstNode *node);

const AstNode *getConstNodeAtIndex(const AstNode *node, u64 index);

const AstNode *getParentScopeWithTagConst(const AstNode *node, AstTag tag);

void insertAstNodeAfter(AstNode *before, AstNode *after);

void insertAstNode(AstNodeList *list, AstNode *node);

void unlinkAstNode(AstNode **head, AstNode *prev, AstNode *node);

const char *getDeclKeyword(AstTag tag);

const char *getDeclarationName(const AstNode *node);
void setDeclarationName(AstNode *node, cstring name);
AstNode *getGenericDeclarationParams(AstNode *node);
void setGenericDeclarationParams(AstNode *node, AstNode *params);

const AstNode *findAttribute(const AstNode *node, cstring name);

FileLoc *getDeclarationLoc(FileLoc *dst, const AstNode *node);

const AstNode *findAttributeArgument(const AstNode *attr, cstring name);

bool mapAstNode(HashTable *mapping, const AstNode *from, AstNode *to);

cstring getAstNodeName(const AstNode *node);

FunctionSignature *makeFunctionSignature(MemPool *pool,
                                         const FunctionSignature *from);

AstNode *getParentScope(AstNode *node);

AstNode *makeTypeReferenceNode(MemPool *pool,
                               const Type *type,
                               const FileLoc *loc);

AstNode *findInAstNode(AstNode *node, cstring name);
AstNode *resolvePath(const AstNode *path);
AstNode *getResolvedPath(const AstNode *path);

int isInInheritanceChain(const AstNode *node, const AstNode *parent);
AstNode *getBaseClassAtLevel(AstNode *node, u64 level);
AstNode *getBaseClassByName(AstNode *node, cstring name);

attr(always_inline) static i64 integerLiteralValue(const AstNode *node)
{
    csAssert0(nodeIs(node, IntegerLit));
    return node->intLiteral.hasMinus ? -node->intLiteral.value
                                     : node->intLiteral.value;
}

int compareNamedAstNodes(const void *lhs, const void *rhs);
SortedNodes *makeSortedNodes(MemPool *pool,
                             AstNode *nodes,
                             int (*compare)(const void *, const void *));

AstNode *findInSortedNodes(SortedNodes *sorted, cstring name);

static inline AstNode *underlyingDeclaration(AstNode *decl)
{
    return nodeIs(decl, GenericDecl) ? decl->genericDecl.decl : decl;
}

attr(always_inline) static bool isStructDeclaration(AstNode *node)
{
    return nodeIs(node, StructDecl) ||
           nodeIs(node, GenericDecl) &&
               isStructDeclaration(node->genericDecl.decl);
}