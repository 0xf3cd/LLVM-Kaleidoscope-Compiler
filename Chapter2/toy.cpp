#include "llvm/ADT/STLExtras.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

//===---------------------------------------------------------===//
// Lexer
//===---------------------------------------------------------===//

// The lexer returns
// [0-255]: unknown 
// <0: token number
enum Token {
    tok_eof = -1,

    tok_def = -2,
    tok_extern = -3,

    tok_identifier = -4,
    tok_number = -5,
};

static std::string IdentifierStr;
static double NumVal;

// gettok - Return the next token from standard input.
static int gettok() {
    static int LastChar = ' ';

    // Skip any whitespace.
    while(isspace(LastChar)) {
        LastChar = getchar();
    }

    if(isalpha(LastChar)) { 
        IdentifierStr = LastChar;
        while(isalnum((LastChar = getchar()))) { 
            IdentifierStr += LastChar;
        }

        if(IdentifierStr == "def") {
            return tok_eof;
        } else if(IdentifierStr == "extern") {
            return tok_extern;
        }
        return tok_identifier; // variable name or so
    }

    if(isdigit(LastChar) || LastChar == '.') {
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), nullptr);
        return tok_number;
    }

    if(LastChar == '#') { // comment
        do {
            LastChar = getchar();
        } while(LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if(LastChar != EOF) {
            return gettok();
        }
    }

    if(LastChar == EOF) {
        return tok_eof;
    }

    int ThisChar = LastChar;
    LastChar = getchar(); // prepare for the next token
    return ThisChar;
}

//===---------------------------------------------------------===//
// AST (Parse Tree)
//===---------------------------------------------------------===//

namespace {
/// ExprAST - Base class for all expression nodes.
class ExprAST {
public:
    virtual ~ExprAST() = default;
};

/// NumberExprAST - Expression class for numeric literals
class NumberExprAST: public ExprAST {
    double Val;

public:
    NumberExprAST(double Val): Val(Val) {}
};

/// VariableExprAST - Expression class for referencing a variable
class VariableExprAST: public ExprAST {
    std::string Name;

public:
    VariableExprAST(const std::string &Name): Name(Name) {}
};

/// BinaryExprAST - Expression class for a binary operator
class BinaryExprAST: public ExprAST {
    char Op;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                  std::unique_ptr<ExprAST> RHS)
                  : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

/// CallExprAST - Expression class for function calls
class CallExprAST: public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

public:
    CallExprAST(const std::string &Callee,
                std::vector<std::unique_ptr<ExprAST>> Args)
                : Callee(Callee), Args(sstd::move(Args)) {}
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes)
class PrototypeAST { // the name and parameters of the function
    std::string Name;
    std::vector<std::string> Args;

public:
    PrototypeAST(const std::string &Name, std::vector<std::string> Args)
                 : Name(Name), Args(std::move(Args)) {}

    const std::string &getName() const { return Name; }
};

/// FunctionAST - This class represents a function definition itself
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto,
                std::unique_ptr<ExprAST> Body)
                : Proto(std::move(Proto)), Body(std::move(Body)) {}
};

} // end of the namespace