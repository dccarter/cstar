//
// Created by Carter Mbotho on 2024-04-26.
//

#include "import.hpp"
#include "context.hpp"

#include "core/alloc.h"
#include "core/strpool.h"
#include "driver/c.h"
#include "lang/frontend/defines.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"

#undef make
#undef Pair

#pragma warning(push, 0)
#include <clang/AST/Decl.h>
#include <clang/AST/DeclGroup.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Parse/ParseAST.h>
#include <clang/Sema/Sema.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/Path.h>
#include <llvm/TargetParser/Host.h>
#pragma warning(pop)

#include <filesystem>

namespace fs = std::filesystem;

namespace cxy {

static const Type *getIntegerTypeFromBitWidth(TypeTable *types,
                                              int bitWidth,
                                              bool isSigned)
{
    switch (bitWidth) {
    case 8:
        return getPrimitiveType(types, isSigned ? prtI8 : prtU8);
    case 16:
        return getPrimitiveType(types, isSigned ? prtI16 : prtU16);
    case 32:
        return getPrimitiveType(types, isSigned ? prtI32 : prtU32);
    case 64:
        return getPrimitiveType(types, isSigned ? prtI64 : prtU64);
    default:
        unreachable("invalid integer bit width");
    }
}

static const Type *toCxy(IncludeContext &ctx, const clang::BuiltinType &type)
{
    switch (type.getKind()) {
    case clang::BuiltinType::Void:
        return makeVoidType(ctx.types);
    case clang::BuiltinType::Bool:
        return getPrimitiveType(ctx.types, prtBool);
    case clang::BuiltinType::Char_S:
        return getPrimitiveType(ctx.types, prtCChar);
    case clang::BuiltinType::Char_U:
        return getPrimitiveType(ctx.types, prtChar);
    case clang::BuiltinType::SChar:
        return getIntegerTypeFromBitWidth(
            ctx.types, ctx.target.getCharWidth(), true);
    case clang::BuiltinType::UChar:
        return getIntegerTypeFromBitWidth(
            ctx.types, ctx.target.getCharWidth(), false);
    case clang::BuiltinType::Short:
        return getIntegerTypeFromBitWidth(
            ctx.types, ctx.target.getShortWidth(), true);
    case clang::BuiltinType::UShort:
        return getIntegerTypeFromBitWidth(
            ctx.types, ctx.target.getShortWidth(), false);
    case clang::BuiltinType::Int:
        return getIntegerTypeFromBitWidth(
            ctx.types, ctx.target.getIntWidth(), true);
    case clang::BuiltinType::UInt:
        return getIntegerTypeFromBitWidth(
            ctx.types, ctx.target.getIntWidth(), false);
    case clang::BuiltinType::Long:
        return getIntegerTypeFromBitWidth(
            ctx.types, ctx.target.getLongWidth(), true);
    case clang::BuiltinType::ULong:
        return getIntegerTypeFromBitWidth(
            ctx.types, ctx.target.getLongWidth(), false);
    case clang::BuiltinType::LongLong:
        return getIntegerTypeFromBitWidth(
            ctx.types, ctx.target.getLongLongWidth(), true);
    case clang::BuiltinType::ULongLong:
        return getIntegerTypeFromBitWidth(
            ctx.types, ctx.target.getLongLongWidth(), false);
    case clang::BuiltinType::Float16:
    case clang::BuiltinType::Float:
        return getPrimitiveType(ctx.types, prtF32);
    case clang::BuiltinType::Double:
    case clang::BuiltinType::LongDouble:
        return getPrimitiveType(ctx.types, prtF64);
    default:
        unreachable("unsupported C type ");
    }
}

static cstring getCDeclName(IncludeContext &ctx, const clang::TagDecl &decl)
{
    auto name = decl.getName();
    if (!name.empty()) {
        return makeStringSized(ctx.strings, name.data(), name.size());
    }
    else if (auto *typedefNameDecl = decl.getTypedefNameForAnonDecl()) {
        name = typedefNameDecl->getName();
        return makeStringSized(ctx.strings, name.data(), name.size());
    }
    return "";
}

static cstring getCDeclName(IncludeContext &ctx, const clang::NamedDecl &decl)
{
    auto name = decl.getName();
    if (!name.empty()) {
        return makeStringSized(ctx.strings, name.data(), name.size());
    }
    return "";
}

static const Type *toCxy(IncludeContext &ctx, clang::QualType qualtype);

static const Type *withFlags(IncludeContext &ctx, const Type *type, u64 flags)
{
    if ((type->flags & flags) != flags)
        return makeWrappedType(ctx.types, type, flags);
    return type;
}

static FilePos toCxy(IncludeContext &ctx, const clang::SourceLocation &loc)
{
    auto presumedLoc = ctx.SM.getPresumedLoc(loc);
    if (!loc.isValid()) {
        return {};
    }

    return {.row = presumedLoc.getLine(),
            .col = presumedLoc.getColumn(),
            .byteOffset = ctx.SM.getFileOffset(ctx.SM.getFileLoc(loc))};
}

static FileLoc toCxy(IncludeContext &ctx, const clang::Decl &decl)
{
    if (decl.getLocation().isValid()) {
        auto start = toCxy(ctx, decl.getSourceRange().getBegin());
        auto end = toCxy(ctx, decl.getSourceRange().getEnd());
        auto filename = ctx.SM.getFilename(decl.getLocation());
        ctx.loc = FileLoc{.fileName = makeStringSized(
                              ctx.strings, filename.data(), filename.size()),
                          .begin = start,
                          .end = end};
    };
    return ctx.loc;
}

static FileLoc toCxy(IncludeContext &ctx, const clang::SourceRange &range)
{
    if (range.isValid()) {
        auto start = toCxy(ctx, range.getBegin());
        auto end = toCxy(ctx, range.getEnd());
        auto filename = ctx.SM.getFilename(range.getBegin());
        ctx.loc = FileLoc{.fileName = makeStringSized(
                              ctx.strings, filename.data(), filename.size()),
                          .begin = start,
                          .end = end};
    }
    return ctx.loc;
}

static FileLoc toCxy(IncludeContext &ctx, const clang::Token &token)
{
    if (token.getLocation().isValid()) {
        auto start = toCxy(ctx, token.getLocation());
        auto end = toCxy(ctx, token.getEndLoc());
        auto filename = ctx.SM.getFilename(token.getLocation());
        ctx.loc = FileLoc{.fileName = makeStringSized(
                              ctx.strings, filename.data(), filename.size()),
                          .begin = start,
                          .end = end};
    }
    return ctx.loc;
}

static AstNode *toCxy(IncludeContext &ctx, const clang::RecordDecl &decl);

static const Type *toCxy(IncludeContext &ctx, const clang::RecordDecl *record)
{
    if (!ctx.recordsStack.empty() && record == ctx.recordsStack.top().decl) {
        // the type needs to be this
        return ctx.recordsStack.top().type;
    }
    auto flags = flgExtern | flgPublic;
    auto name = getCDeclName(ctx, *record);
    auto type =
        (record->isUnion() ? findUntaggedUnionType(ctx.types, name, flags)
                           : findStructType(ctx.types, name, flags))
            ?: findAliasType(ctx.types, name, flags);
    if (type != nullptr)
        return type;

    auto kind = record->getLexicalParent()->getDeclKind();
    bool isInnerStruct = kind == clang::Decl::Record;
    bool isTypeDef = kind == clang::Decl::TranslationUnit &&
                     !record->fields().empty() && ctx.recordsStack.empty();
    if (isInnerStruct || isTypeDef) {
        // inner struct
        auto decl = toCxy(ctx, *record);
        ctx.addDeclaration(decl);
        return decl->type;
    }

    auto loc = toCxy(ctx, llvm::cast<clang::Decl>(*record));
    auto node = AstNode{.tag = astTypeDecl,
                        .flags = flgExtern | flgPublic,
                        .typeDecl = {.name = name}};
    auto decl = makeAstNode(ctx.pool, &loc, &node);
    decl->type = makeOpaqueType(ctx.types, name, decl);
    return decl->type;
}

static const Type *toCxy(IncludeContext &ctx, const clang::EnumDecl *enum_)
{
    auto name = getCDeclName(ctx, *enum_);
    if (auto type = findEnumType(ctx.types, name, flgExtern | flgPublic))
        return type;

    return getPrimitiveType(ctx.types, prtI32);
}

static const Type *toCxy(IncludeContext &ctx,
                         const clang::FunctionProtoType &proto)
{
    std::vector<const Type *> params;
    params.reserve(proto.getNumParams());
    for (int i = 0; i < proto.getNumParams(); i++) {
        params.push_back(toCxy(ctx, proto.getParamType(i)));
    }
    Type func = {.tag = typFunc,
                 .flags = (proto.isVariadic() ? flgVariadic : flgNone) |
                          flgExtern | flgPublic,
                 .func = {
                     .paramsCount = (u16)params.size(),
                     .retType = toCxy(ctx, proto.getReturnType()),
                     .params = params.data(),
                 }};

    auto type = makeFuncType(ctx.types, &func);
    return type;
}

static const Type *toCxy(IncludeContext &ctx,
                         const clang::FunctionNoProtoType &proto)
{
    Type func = {.tag = typFunc,
                 .flags = flgExtern | flgPublic,
                 .func = {
                     .retType = toCxy(ctx, proto.getReturnType()),
                 }};

    return makeFuncType(ctx.types, &func);
}

static const Type *toCxy(IncludeContext &ctx, const clang::QualType qualtype)
{
    u64 flags = qualtype.isConstQualified() ? flgConst : flgNone;
    auto &type = *qualtype.getTypePtr();
    switch (type.getTypeClass()) {
    case clang::Type::Pointer: {
        auto pointee = llvm::cast<clang::PointerType>(type).getPointeeType();
        if (pointee->isFunctionType()) {
            return toCxy(ctx, pointee);
        }
        return makePointerType(ctx.types, toCxy(ctx, pointee), flags);
    }
    case clang::Type::Builtin:
        return withFlags(
            ctx, toCxy(ctx, llvm::cast<clang::BuiltinType>(type)), flags);
    case clang::Type::Typedef: {
        auto desugared = llvm::cast<clang::TypedefType>(type).desugar();
        if (flags & flgConst)
            desugared.addConst();
        return toCxy(ctx, desugared);
    }
    case clang::Type::Elaborated:
        return withFlags(
            ctx,
            toCxy(ctx, llvm::cast<clang::ElaboratedType>(type).getNamedType()),
            flags);
    case clang::Type::Record: {
        auto record = llvm::cast<clang::RecordType>(type).getDecl();
        return withFlags(ctx, toCxy(ctx, record), flags);
    }
    case clang::Type::Paren:
        return toCxy(ctx, llvm::cast<clang::ParenType>(type).getInnerType());
    case clang::Type::FunctionNoProto:
        return toCxy(ctx, llvm::cast<clang::FunctionNoProtoType>(type));
    case clang::Type::FunctionProto:
        return toCxy(ctx, llvm::cast<clang::FunctionProtoType>(type));
    case clang::Type::ConstantArray: {
        auto &constArray = llvm::cast<clang::ConstantArrayType>(type);
        if (!constArray.getSize().isIntN(64)) {
            logError(ctx.L, builtinLoc(), "array is too large", nullptr);
        }
        return makeArrayType(ctx.types,
                             toCxy(ctx, constArray.getElementType()),
                             constArray.getSize().getLimitedValue());
    }
    case clang::Type::IncompleteArray:
        return makePointerType(
            ctx.types,
            toCxy(
                ctx,
                llvm::cast<clang::IncompleteArrayType>(type).getElementType()),
            flags);
    case clang::Type::Attributed:
        return toCxy(
            ctx, llvm::cast<clang::AttributedType>(type).getEquivalentType());
    case clang::Type::Decayed:
        return toCxy(ctx,
                     llvm::cast<clang::DecayedType>(type).getDecayedType());
    case clang::Type::Enum: {
        auto &enumType = llvm::cast<clang::EnumType>(type);
        if (enumType.getDecl()->getName().empty()) {
            return withFlags(
                ctx, toCxy(ctx, enumType.getDecl()->getIntegerType()), flags);
        }
        else {
            return withFlags(ctx, toCxy(ctx, enumType.getDecl()), flags);
        }
    }
    default:
        logWarning(ctx.L,
                   builtinLoc(),
                   "unhandled C type class '{s}' (importing type '{s}')",
                   (FormatArg[]){{.s = type.getTypeClassName()},
                                 {.s = qualtype.getAsString().data()}});
        return getPrimitiveType(ctx.types, prtI32);
    }
}

static AstNode *toCxy(IncludeContext &ctx, const clang::FieldDecl &decl)
{
    cstring name;
    if (decl.getName().empty())
        name = makeAnonymousVariable(ctx.strings, "__ext_f");
    else
        name = getCDeclName(ctx, decl);

    auto loc = toCxy(ctx, llvm::cast<clang::Decl>(decl));
    auto type = toCxy(ctx, decl.getType());
    auto node = makeStructField(ctx.pool,
                                &loc,
                                name,
                                flgPublic,
                                makeTypeReferenceNode(ctx.pool, type, &loc),
                                nullptr,
                                nullptr);
    if (decl.isBitField())
        node->structField.bits = decl.getBitWidthValue(ctx.Ci.getASTContext());

    node->type = type;
    return node;
}

static AstNode *toCxy(IncludeContext &ctx, const clang::RecordDecl &decl)
{
    auto loc = toCxy(ctx, llvm::cast<clang::Decl>(decl));
    AstNodeList fields = {};
    auto flags = flgExtern | flgPublic;
    auto name = decl.getName().empty()
                    ? makeAnonymousVariable(ctx.strings, "__ext_s")
                    : getCDeclName(ctx, decl);

    ctx.recordsStack.push({&decl, makeThisType(ctx.types, name, flags)});
    auto node =
        makeStructDecl(ctx.pool, &loc, flags, name, nullptr, nullptr, nullptr);
    AstNodeList attrs = {};
    for (auto &attr : decl.getAttrs()) {
        if (attr->getKind() == clang::attr::Packed)
            insertAstNode(&attrs,
                          makeAttribute(ctx.pool, &loc, S_packed, NULL, NULL));
    }
    node->attrs = attrs.first;

    std::vector<NamedTypeMember> members;
    u64 index = 0;
    for (auto *field : decl.fields()) {
        if (auto fieldDecl = toCxy(ctx, *field)) {
            fieldDecl->structField.index = index++;
            insertAstNode(&fields, fieldDecl);
            members.push_back(
                NamedTypeMember{.name = fieldDecl->structField.name,
                                .type = fieldDecl->type,
                                .decl = fieldDecl});
            fieldDecl->parentScope = node;
        }
        else {
            loc = toCxy(ctx, field->getSourceRange());
            logWarningWithId(ctx.L,
                             wrnCUnsupportedField,
                             &loc,
                             "unsupported field detected in C struct",
                             nullptr);
        }
    }

    if (decl.isUnion()) {
        node->type = makeReplaceUntaggedUnionType(
            ctx.types, node, members.data(), members.size());
    }
    else {
        node->type = makeReplaceStructType(ctx.types,
                                           node->structDecl.name,
                                           members.data(),
                                           members.size(),
                                           node,
                                           flags);
    }

    const_cast<Type *>(ctx.recordsStack.top().type)->_this.that = node->type;
    ctx.recordsStack.pop();

    return node;
}

static AstNode *toCxy(IncludeContext &ctx, const clang::TypedefDecl &decl)
{
    auto loc = toCxy(ctx, decl.getSourceRange());
    cstring name = getCDeclName(ctx, decl);
    auto type = toCxy(ctx, decl.getUnderlyingType());
    return makeTypeDeclAstNode(
        ctx.pool,
        &loc,
        flgExtern | flgPublic,
        name,
        makeTypeReferenceNode(ctx.pool, type, &loc),
        NULL,
        makeAliasType(ctx.types, type, name, flgExtern | flgPublic));
}

static AstNode *toCxy(IncludeContext &ctx, const clang::VarDecl &decl)
{
    auto loc = toCxy(ctx, llvm::cast<clang::Decl>(decl));
    auto type = toCxy(ctx, decl.getType());
    return makeVarDecl(ctx.pool,
                       &loc,
                       flgPublic | flgExtern | flgTopLevelDecl,
                       getCDeclName(ctx, decl),
                       makeTypeReferenceNode(ctx.pool, type, &loc),
                       nullptr,
                       nullptr,
                       type);
}

AstNode *toCxy(IncludeContext &ctx, const clang::FunctionDecl &decl)
{
    auto loc = toCxy(ctx, llvm::cast<clang::Decl>(decl));
    std::vector<const Type *> paramTypes{};
    AstNodeList params = {};
    u64 flags = decl.isVariadic() ? flgVariadic : flgNone;
    cstring name = getCDeclName(ctx, decl);

    for (int i = 0; i < decl.getNumParams(); i++) {
        auto &param = *decl.getParamDecl(i);
        auto paramLoc = toCxy(ctx, llvm::cast<clang::Decl>(param));
        paramTypes.push_back(toCxy(ctx, param.getType()));
        insertAstNode(&params,
                      makeFunctionParam(ctx.pool,
                                        &paramLoc,
                                        getCDeclName(ctx, param),
                                        makeTypeReferenceNode(
                                            ctx.pool, paramTypes[i], &paramLoc),
                                        nullptr,
                                        flgNone,
                                        nullptr));
    }

    if (decl.isVariadic()) {
        insertAstNode(&params,
                      makeFunctionParam(
                          ctx.pool,
                          builtinLoc(),
                          makeString(ctx.strings, "_"),
                          makeTypeReferenceNode(
                              ctx.pool, makeAutoType(ctx.types), builtinLoc()),
                          nullptr,
                          flags,
                          nullptr));
    }

    Type func = {.tag = typFunc,
                 .name = name,
                 .flags = flags | flgExtern | flgPublic,
                 .func = {
                     .paramsCount = (u16)paramTypes.size(),
                     .retType = toCxy(ctx, decl.getReturnType()),
                     .params = paramTypes.data(),
                 }};

    auto retLoc = toCxy(ctx, decl.getReturnTypeSourceRange());
    auto node = makeFunctionDecl(
        ctx.pool,
        &loc,
        func.name,
        params.first,
        makeTypeReferenceNode(
            ctx.pool, toCxy(ctx, decl.getReturnType()), &retLoc),
        nullptr,
        flgExtern | flgPublic | flags,
        nullptr,
        nullptr);

    func.func.decl = node;
    node->type = makeFuncType(ctx.types, &func);

    return node;
}

void unnamedEnumToCxy(IncludeContext &ctx,
                      const Type *type,
                      const clang::EnumDecl &decl)
{
    for (clang::EnumConstantDecl *option : decl.enumerators()) {
        auto memberName = makeStringSized(
            ctx.strings, option->getName().data(), option->getName().size());
        auto value = option->getInitVal();
        auto optionLoc = toCxy(ctx, option->getSourceRange());
        auto expr = makeIntegerLiteral(
            ctx.pool, &optionLoc, value.getExtValue(), nullptr, type);
        auto macro = makeMacroDeclAstNode(ctx.pool,
                                          &optionLoc,
                                          flgPublic | flgExtern,
                                          memberName,
                                          NULL,
                                          expr,
                                          NULL);
        preprocessorOverrideDefinedMacro(
            ctx.preprocessor, memberName, macro, NULL);
    }
}

AstNode *toCxy(IncludeContext &ctx, const clang::EnumDecl &decl)
{
    auto name = getCDeclName(ctx, decl);
    auto type = toCxy(ctx,
                      name[0] ? decl.getIntegerType()
                              : clang::QualType(decl.getTypeForDecl(), 0));
    if (name[0] == '\0') {
        unnamedEnumToCxy(ctx, type, decl);
        return nullptr;
    }

    auto loc = toCxy(ctx, decl.getSourceRange());
    AstNodeList options = {};
    std::vector<EnumOptionDecl> members;
    for (clang::EnumConstantDecl *option : decl.enumerators()) {
        auto memberName = makeStringSized(
            ctx.strings, option->getName().data(), option->getName().size());
        auto value = option->getInitVal();
        auto optionLoc = toCxy(ctx, option->getSourceRange());
        auto expr = makeIntegerLiteral(
            ctx.pool, &optionLoc, value.getExtValue(), nullptr, type);
        insertAstNode(&options,
                      makeEnumOptionAst(ctx.pool,
                                        &optionLoc,
                                        flgNone,
                                        memberName,
                                        expr,
                                        nullptr,
                                        nullptr));
        members.emplace_back((EnumOptionDecl){
            .name = memberName,
            .value = value.getExtValue(),
            .decl = options.last,
        });
    }

    auto node = makeEnumAst(ctx.pool,
                            &loc,
                            flgNative | flgPublic,
                            name,
                            makeTypeReferenceNode(ctx.pool, type, builtinLoc()),
                            options.first,
                            nullptr,
                            nullptr);

    Type enum_ = {.tag = typEnum,
                  .name = name,
                  .flags = flgExtern,
                  .tEnum = {.base = type,
                            .options = members.data(),
                            .optionsCount = members.size(),
                            .decl = node}};
    node->type = makeEnum(ctx.types, &enum_);
    for (AstNode *it = options.first; it; it = it->next)
        it->type = node->type;
    return node;
}

struct CToCxyConverter : clang::ASTConsumer {
    explicit CToCxyConverter(IncludeContext &ctx) : ctx(ctx) {}

    bool HandleTopLevelDecl(clang::DeclGroupRef declGroup) final override
    {
        for (clang::Decl *decl : declGroup) {
            auto loc = ctx.SM.getPresumedLoc(decl->getLocation());
            if (ctx.importer.find(loc.getFilename()))
                continue;

            switch (decl->getKind()) {
            case clang::Decl::Function:
                ctx.addDeclaration(
                    toCxy(ctx, *llvm::cast<clang::FunctionDecl>(decl)));
                break;
            case clang::Decl::Record:
                ctx.addDeclaration(
                    toCxy(ctx, llvm::cast<clang::RecordDecl>(*decl)));
                break;
            case clang::Decl::Enum: {
                ctx.addDeclaration(
                    toCxy(ctx, llvm::cast<clang::EnumDecl>(*decl)));
                break;
            }
            case clang::Decl::Var:
                ctx.addDeclaration(
                    toCxy(ctx, llvm::cast<clang::VarDecl>(*decl)));
                break;
            case clang::Decl::Typedef:
                ctx.addDeclaration(
                    toCxy(ctx, llvm::cast<clang::TypedefDecl>(*decl)));
                break;
            default:
                break;
            }
        }
        return true; // continue parsing
    }

private:
    IncludeContext &ctx;
};

struct MacroImporter : clang::PPCallbacks {
    MacroImporter(IncludeContext &ctx,
                  clang::CompilerInstance &compilerInstance)
        : ctx(ctx), compilerInstance(compilerInstance)
    {
    }

    void MacroDefined(const clang::Token &name,
                      const clang::MacroDirective *macro) final override
    {
        if (macro->getMacroInfo()->getNumTokens() != 1)
            return;
        auto loc = ctx.SM.getPresumedLoc(name.getLocation());
        if (ctx.importer.find(loc.getFilename()))
            return;

        auto &token = macro->getMacroInfo()->getReplacementToken(0);

        switch (token.getKind()) {
        case clang::tok::numeric_constant:
            importMacroConstant(name.getIdentifierInfo()->getName(),
                                token,
                                toCxy(ctx, name.getLocation()));
            break;
        case clang::tok::string_literal:
            importMacroStringLit(name.getIdentifierInfo()->getName(),
                                 token,
                                 toCxy(ctx, name.getLocation()));
            break;
        case clang::tok::identifier:
            importMacroCall(name.getIdentifierInfo()->getName(),
                            token,
                            toCxy(ctx, name.getLocation()));
            break;
        default:
            break;
        }
    }

private:
    void importMacroCall(llvm::StringRef name,
                         const clang::Token &token,
                         FilePos start)
    {
        AstNode *value{nullptr};
        auto identifier = token.getIdentifierInfo()->getName();
        auto variable =
            makeStringSized(ctx.strings, identifier.data(), identifier.size());
        if (preprocessorHasMacro(ctx.preprocessor, variable, &value)) {
            auto loc = toCxy(ctx, token);
            loc.begin = start;
            auto macro = makeMacroDeclAstNode(
                ctx.pool,
                &loc,
                flgPublic | flgExtern,
                makeStringSized(ctx.strings, name.data(), name.size()),
                NULL,
                deepCloneAstNode(ctx.pool, value->macroDecl.body),
                NULL);
            defineMacro(macro);
        }
    }

    void importMacroConstant(llvm::StringRef name,
                             const clang::Token &token,
                             FilePos start)
    {
        auto result = compilerInstance.getSema().ActOnNumericConstant(token);
        if (!result.isUsable())
            return;
        clang::Expr *parsed = result.get();
        auto valueLoc = toCxy(ctx, token);
        auto loc = valueLoc;
        loc.begin = start;
        if (auto *intLiteral = llvm::dyn_cast<clang::IntegerLiteral>(parsed)) {
            auto type = toCxy(ctx, parsed->getType());
            llvm::APSInt value(intLiteral->getValue(),
                               parsed->getType()->isUnsignedIntegerType());
            auto macro = makeMacroDeclAstNode(
                ctx.pool,
                &loc,
                flgPublic | flgExtern,
                makeStringSized(ctx.strings, name.data(), name.size()),
                NULL,
                makeIntegerLiteral(
                    ctx.pool, &valueLoc, value.getSExtValue(), NULL, type),
                NULL);
            defineMacro(macro);
        }
        else if (auto *floatLiteral =
                     llvm::dyn_cast<clang::FloatingLiteral>(parsed)) {
            auto value = floatLiteral->getValue();
            auto type = toCxy(ctx, parsed->getType());
            auto macro = makeMacroDeclAstNode(
                ctx.pool,
                &loc,
                flgPublic | flgExtern,
                makeStringSized(ctx.strings, name.data(), name.size()),
                NULL,
                makeFloatLiteral(
                    ctx.pool, &valueLoc, value.convertToDouble(), NULL, type),
                NULL);
            defineMacro(macro);
        }
    }

    void importMacroStringLit(llvm::StringRef name,
                              const clang::Token &token,
                              FilePos start)
    {
        auto result = compilerInstance.getSema().ActOnStringLiteral(token);
        if (!result.isUsable())
            return;
        clang::Expr *parsed = result.get();
        auto valueLoc = toCxy(ctx, token);
        auto loc = valueLoc;
        loc.begin = start;
        if (auto *stringLit = llvm::dyn_cast<clang::StringLiteral>(parsed)) {
            auto type = makeStringType(ctx.types);
            auto value = makeStringSized(ctx.strings,
                                         stringLit->getString().data(),
                                         stringLit->getString().size());
            auto macro = makeMacroDeclAstNode(
                ctx.pool,
                &loc,
                flgPublic | flgExtern,
                makeStringSized(ctx.strings, name.data(), name.size()),
                NULL,
                makeStringLiteral(ctx.pool, &valueLoc, value, NULL, type),
                NULL);
            defineMacro(macro);
        }
    }

    void defineMacro(AstNode *node)
    {
        AstNode *previous = NULL;
        if (preprocessorOverrideDefinedMacro(
                ctx.preprocessor, node->macroDecl.name, node, &previous)) //
        {
            return;
        }

        if (isWarningEnabled(ctx.L, CMacroRedefine)) {
            logWarning(
                ctx.L,
                &node->loc,
                "overriding macro with name '{s}' which was already defined",
                (FormatArg[]){{.s = node->macroDecl.name}});
            if (previous && previous->loc.fileName != NULL) {
                logNote(ctx.L,
                        &previous->loc,
                        "macro previously declared here",
                        NULL);
            }
        }
    }

private:
    IncludeContext &ctx;
    clang::CompilerInstance &compilerInstance;
};

} // namespace cxy

template <typename T, typename R>
static auto map(DynArray &arr, std::function<R(T &)> func) -> std::vector<R>
{
    std::vector<R> mapped;
    for (int i = 0; i < arr.size; i++) {
        mapped.push_back(func(dynArrayAt(T *, &arr, i)));
    }
    return mapped;
}

template <typename T>
static auto for_each(DynArray &arr, std::function<void(T &)> func)
{
    for (int i = 0; i < arr.size; i++) {
        func(dynArrayAt(T *, &arr, i));
    }
}

static std::string removeSDKVersion(clang::StringRef path)
{
    static std::string sSDKVersion{};
    if (sSDKVersion.empty()) {
        FormatState state = newFormatState(nullptr, true);
        exec("xcrun --show-sdk-version", &state);
        auto version = formatStateToString(&state);
        version[strlen(version) - 1] = '\0';
        freeFormatState(&state);
        sSDKVersion = std::string(version);
        free(version);
    }
    auto str = path.str();
    auto index = path.find(sSDKVersion);
    if (index == std::string::npos)
        return str;
    str.erase(index, sSDKVersion.size());
    return str;
}

AstNode *importCHeader(CompilerDriver *driver,
                       const AstNode *node,
                       cstring name)
{
    auto importer = (cxy::CImporter *)driver->cImporter;
    cstring importerPath = node->loc.fileName;
    cstring headerName = node->stringLiteral.value;
    auto &options = driver->options;
    clang::CompilerInstance ci;
    ci.createDiagnostics();
    auto args =
        map<cstring, cstring>(options.cflags, [](auto cflag) { return cflag; });
    clang::CompilerInvocation::CreateFromArgs(
        ci.getInvocation(), args, ci.getDiagnostics());

    std::shared_ptr<clang::TargetOptions> pto =
        std::make_shared<clang::TargetOptions>();
    pto->Triple = llvm::sys::getDefaultTargetTriple();
    auto targetInfo =
        clang::TargetInfo::CreateTargetInfo(ci.getDiagnostics(), pto);
    ci.setTarget(targetInfo);

    ci.createFileManager();
    ci.createSourceManager(ci.getFileManager());
    ci.getHeaderSearchOpts().AddPath(llvm::sys::path::parent_path(importerPath),
                                     clang::frontend::Quoted,
                                     false,
                                     true);

    cxy::IncludeContext context(driver, ci);

    for_each<cstring>(options.importSearchPaths, [&ci](auto path) {
        ci.getHeaderSearchOpts().AddPath(
            path, clang::frontend::System, false, true);
        ci.getHeaderSearchOpts().AddPath(
            path, clang::frontend::System, false, false);
    });

    for_each<cstring>(options.frameworkSearchPaths, [&ci](auto path) {
        ci.getHeaderSearchOpts().AddPath(
            path, clang::frontend::System, true, true);
        ci.getHeaderSearchOpts().AddPath(
            path, clang::frontend::System, true, false);
    });

    for_each<cstring>(options.cDefines, [&ci](auto define) {
        ci.getPreprocessorOpts().addMacroDef(&define[2]);
    });
    // prevent a warning that comes when importing C headers
    ci.getPreprocessorOpts().addMacroDef("__GNUC__=4");

    ci.createPreprocessor(clang::TU_Complete);
    auto &pp = ci.getPreprocessor();
    pp.getBuiltinInfo().initializeBuiltins(pp.getIdentifierTable(),
                                           pp.getLangOpts());

    clang::HeaderSearch &headerSearch =
        ci.getPreprocessor().getHeaderSearchInfo();
    auto fileEntry = headerSearch.LookupFile(headerName,
                                             {},
                                             false,
                                             nullptr,
                                             nullptr,
                                             {},
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr);
    if (!fileEntry) {
        std::string searchDirs;
        for (auto searchDir = headerSearch.search_dir_begin(),
                  end = headerSearch.search_dir_end();
             searchDir != end;
             ++searchDir) {
            searchDirs += searchDir->getName();
            searchDirs += ' ';
        }
        logError(driver->L,
                 &node->loc,
                 "couldn't find C header file '{s}' (search dirs: {s})",
                 (FormatArg[]){{.s = headerName}, {.s = searchDirs.data()}});
        return NULL;
    }

    auto requestPath = fileEntry->getFileEntry().tryGetRealPathName();
#ifdef __APPLE__
    auto requestPathString = removeSDKVersion(requestPath.str());
    requestPath = clang::StringRef(requestPathString);
#endif
    if (auto module = importer->find(requestPath))
        return module;

    ci.setASTConsumer(std::make_unique<cxy::CToCxyConverter>(context));
    ci.createASTContext();
    ci.createSema(clang::TU_Complete, nullptr);
    pp.addPPCallbacks(std::make_unique<cxy::MacroImporter>(context, ci));

    auto fileID = ci.getSourceManager().createFileID(
        *fileEntry, clang::SourceLocation(), clang::SrcMgr::C_System);
    ci.getSourceManager().setMainFileID(fileID);
    ci.getDiagnosticClient().BeginSourceFile(ci.getLangOpts(),
                                             &ci.getPreprocessor());
    clang::ParseAST(
        ci.getPreprocessor(), &ci.getASTConsumer(), ci.getASTContext());
    ci.getDiagnosticClient().EndSourceFile();
    ci.getDiagnosticClient().finish();

    if (ci.getDiagnosticClient().getNumErrors() > 0) {
        return NULL;
    }

    auto program = context.buildModules(requestPath);
    csAssert(program != nullptr, "Something broken with c importer");
    return program;
}

bool isCHeaderFile(cstring filePath)
{
    return fs::path(filePath).extension() == ".h";
}

void initCImporter(struct CompilerDriver *driver)
{
    csAssert0(driver->cImporter == nullptr);
    driver->cImporter = new cxy::CImporter();
}

void deinitCImporter(struct CompilerDriver *driver)
{
    auto importer = (cxy::CImporter *)driver->cImporter;
    if (importer)
        delete importer;
}