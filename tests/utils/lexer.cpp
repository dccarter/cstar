//
// Created by Carter Mbotho on 2024-05-01.
//

#include "lang/frontend/lexer.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

void printType_(FormatState *state, const Type *type, bool keyword) {}

int main(int argc, char *argv[])
{
    const char *fileName = "<stdin>";
    std::string source;
    if (argc > 1) {
        fileName = argv[1];
        std::ifstream is(argv[1]);
        if (is.fail()) {
            std::cerr << "error: opening source file '" << fileName
                      << "' failed\n";
            exit(EXIT_FAILURE);
        }
        std::istreambuf_iterator<char> begin(is), end;
        source = std::string(begin, end);
        is.close();
    }
    else {
        std::istreambuf_iterator<char> begin(std::cin), end;
        source = std::string(begin, end);
    }

    Log L = newLog(nullptr, nullptr);
    L.ignoreStyles = true;
    Lexer lexer = newLexer(fileName, source.data(), source.size(), &L);
    auto getRangeValue = [&source](const FileLoc &loc, bool trim = true) {
        auto s = loc.begin.byteOffset, e = loc.end.byteOffset;
        if (trim) {
            s++;
            e--;
        }
        return source.substr(s, e - s);
    };

    while (true) {
        auto token = advanceLexer(&lexer);
        if (token.tag == tokEoF)
            break;
        std::cout << token_tag_to_str(token.tag);
        switch (token.tag) {
        case tokIdent:
            std::cout << '(' << getRangeValue(token.fileLoc, false) << ')';
            break;
        case tokStringLiteral:
            std::cout << "(\"" << getRangeValue(token.fileLoc) << "\")";
            break;
        case tokIntLiteral:
            std::cout << '(' << token.iVal << ')';
            break;
        case tokFloatLiteral:
            std::cout << '(' << token.fVal << ')';
            break;
        case tokCharLiteral:
            std::cout << '(' << token.cVal << ')';
            break;
        default:
            break;
        }

        std::cout << " @(" << token.fileLoc.begin.row << ":"
                  << token.fileLoc.begin.col << ", " << token.fileLoc.end.row
                  << ":" << token.fileLoc.end.col << ")\n";
    }

    freeLexer(&lexer);
    freeLog(&L);
    return EXIT_SUCCESS;
}
