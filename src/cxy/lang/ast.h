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
    f(Types)                \
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
    HashTable table;
    u64 index;
} ClosureCapture;

#define CXY_AST_NODE_HEAD                                                      \
    AstTag tag;                                                                \
    FileLoc loc;                                                               \
    u64 flags;                                                                 \
    const Type *type;                                                          \
    struct AstNode *parentScope;                                               \
    struct AstNode *next;                                                      \
    struct AstNode *link;                                                      \
    struct AstNode *attrs;

typedef enum { iptModule, iptPath } ImportKind;
typedef enum { cInclude, cDefine } CCodeKind;

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
            cstring alias;
            cstring name;
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
            const char *name;
            struct AstNode *args;
        } attr;

        struct {
            u64 len;
            struct AstNode *args;
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
        } genericParam;

        struct {
            Env *env;
            struct AstNode *params;
            struct AstNode *decl;
        } genericDecl;

        struct {
            const char *name;
            const char *alt;
            union {
                const char *alt2;
                struct Scope *scope;
            };
            struct AstNode *args;
            struct AstNode *resolvesTo;
            u64 index;
        } pathElement;

        struct {
            struct AstNode *elements;
            bool isType;
        } path;

        struct {
            Operator operatorOverload;
            u32 index;
            const char *name;
            struct AstNode *params;
            struct AstNode *ret;
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
            struct AstNode *names;
            struct AstNode *type;
            struct AstNode *init;
        } varDecl;

        struct {
            const char *name;
            struct AstNode *aliased;
        } typeDecl;

        struct {
            const char *name;
            struct AstNode *members;
        } unionDecl;

        struct {
            const char *name;
            struct AstNode *value;
            u64 index;
        } enumOption;

        struct {
            u64 len;
            const char *name;
            struct AstNode *base;
            struct AstNode *options;

            Env *env;
        } enumDecl;

        struct {
            const char *name;
            u64 index;
            struct AstNode *type;
            struct AstNode *value;
        } structField;

        struct {
            const char *name;
            struct AstNode *base;
            struct AstNode *members;
            const Type *generatedFrom;
        } structDecl;

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
            ClosureCapture capture;
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
        } exprStmt, groupExpr;

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

AstNode *makePathFromIdent(MemPool *pool, const AstNode *ident);

AstNode *makeGenIdent(MemPool *pool,
                      struct StrPool *strPool,
                      const FileLoc *loc);

AstNode *copyAstNode(MemPool *pool, const AstNode *node);

AstNode *duplicateAstNode(MemPool *pool, const AstNode *node);

AstNode *cloneAstNode(MemPool *pool, const AstNode *node);

AstNode *replaceAstNode(AstNode *node, const AstNode *with);

#define nodeIs(NODE, TAG) ((NODE) && ((NODE)->tag == ast##TAG))
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

const char *getDeclName(const AstNode *node);

const AstNode *findAttribute(const AstNode *node, cstring name);

const AstNode *findAttributeArgument(const AstNode *attr, cstring name);

cstring getAstNodeName(const AstNode *node);
