#include "llvm/ADT/STLExtras.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

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
static int GetTok() {
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
            return GetTok();
        }
    }

    if(LastChar == EOF) {
        return tok_eof;
    }

    int ThisChar = LastChar;
    LastChar = getchar(); // prepare for the next token
    return ThisChar;
}

//===----------------------------------------------------------------------===//
// AST (Parse Tree)
//===----------------------------------------------------------------------===//

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
                : Callee(Callee), Args(std::move(Args)) {}
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
    FunctionAST(const std::unique_ptr<PrototypeAST>& Proto, const std::unique_ptr<ExprAST>& Body)
                : Proto(std::move(Proto)), Body(std::move(Body)) {}
};

} // end of the namespace

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

static int CurTok; // current token that the parser is looking at
static int GetNextToken() { return CurTok = GetTok(); } 

/// the precedence of each binary operator
/// - the compiler uses BinopPrecedence to record the precendence of operators 
/// - in parsing the the binop, the precedence, instead of the pre-set grammar, 
/// - is used to determine the parse order
static std::map<char, int> BinopPrecedence;
static int GetTokPrecedence() {
    if(!isascii(CurTok)) { return -1; }

    // make sure binop is declared
    int TokPrec = BinopPrecedence[CurTok];
    if(TokPrec <= 0) { return -1; }
    return TokPrec;
}

/// LogError* - These are little helper functions for error handling
std::unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
    LogError(Str);
    return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

/// numberexpr ::= number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    GetNextToken(); // advance the lexer to the next token
    return std::move(Result);
}

/// parenexpr ::= '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
    GetNextToken(); // eat (
    auto V = ParseExpression();
    if(!V) { return nullptr; }
    if(CurTok != ')') { return LogError("expected ')'"); }

    GetNextToken(); // eat )
    return V;
}

/// identifierexpr
///     ::= identifier
///     ::= identifier '(' expression* ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
    std::string IdName = IdentifierStr;
    GetNextToken(); // eat identifier

    // Variable
    if(CurTok != '(') { return std::make_unique<VariableExprAST>(IdName); }

    // Function Call
    GetNextToken(); // eat ( and read the next token
    std::vector<std::unique_ptr<ExprAST>> Args;
    if(CurTok != ')') {
        while(true) {
            if(auto Arg = ParseExpression()) { Args.push_back(std::move(Arg)); }
            else { return nullptr; }

            if(CurTok == ')') { break; }

            if(CurTok != ',') {
                return LogError("Expected ')' or ',' in argument list");
            }
            GetNextToken();
        }
    }

    GetNextToken(); // eat ')'
    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/// primary
///     ::= identifierexpr
///     ::= numberexpr
///     ::= parenexpr
static std::unique_ptr<ExprAST> ParsePrimary() {
    switch(CurTok) {
        default: return LogError("Unknown token when expecting an expression");
        case tok_identifier: return ParseIdentifierExpr();
        case tok_number: return ParseNumberExpr();
        case '(': return ParseParenExpr();
    }
}

/// binoprhs
///     ::= ('+' primary)*
static std::unique_ptr<ExprAST> 
ParseBinOpRHS(int ExprPrec,
              std::unique_ptr<ExprAST> LHS) {
    // If this is a binop, get its precedence
    // The binop should be bound to what side is determined by the precedence
    while(true) {
        int TokPrec = GetTokPrecedence();

        // If this is a binop that binds at least as tightly as the current binop,
        // consume it, otherwise we are done
        if(TokPrec < ExprPrec) { return LHS; }

        // This is a binop
        int BinOp = CurTok;
        GetTokPrecedence(); // eat binop

        // Parse the primary expression after the binop
        auto RHS = ParsePrimary();
        if(!RHS) { return nullptr; }

        // If BinOp binds less tightly with RHS than the operator after RHS,
        // let the pending operator take RHS as its LHS
        int NextPrec = GetTokPrecedence();
        if(TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec+1, std::move(RHS));
            if(!RHS) { return nullptr; }
        }

        // Merge LHS?RHS
        LHS = std::make_unique<BinaryExprAST>(BinOp, 
                                              std::move(LHS), std::move(RHS));
    }
}

/// expression
///     ::= primary binoprhs
static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary();
    if(!LHS) { return nullptr; }

    return ParseBinOpRHS(0, std::move(LHS));
}

/// prototype
///     ::= id '(' id* ')'
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    if(CurTok != tok_identifier) { 
        return LogErrorP("Expected function name in prototype"); 
    }

    std::string FnName = IdentifierStr;
    GetNextToken();

    if(CurTok != '(') {
        return LogErrorP("Expected '(' in prototype"); 
    }

    // Read the list of argument names
    std::vector<std::string> ArgNames;
    while(GetNextToken() == tok_identifier) {
        ArgNames.push_back(IdentifierStr);
    }
    if(CurTok != ')') {
        return LogErrorP("Expected ')' in prototype");
    }

    // success
    GetNextToken(); // eat ')'
    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

/// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
    GetNextToken(); // eat "def"
    auto Proto = ParsePrototype();
    if(!Proto) { return nullptr; }

    if(auto E = ParseDefinition()) {
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

/// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
    GetNextToken(); // eat "extern"
    return ParsePrototype();
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if(auto E = ParseExpression()) {
        // Make an anonymous proto.
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
        // auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std:;vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

//===----------------------------------------------------------------------===//
// Top-Level parsing
//===----------------------------------------------------------------------===//

static void HandleDefinition() {
  if (ParseDefinition()) {
    fprintf(stderr, "Parsed a function definition.\n");
  } else {
    // Skip token for error recovery.
    GetNextToken();
  }
}

static void HandleExtern() {
  if (ParseExtern()) {
    fprintf(stderr, "Parsed an extern\n");
  } else {
    // Skip token for error recovery.
    GetNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (ParseTopLevelExpr()) {
    fprintf(stderr, "Parsed a top-level expr\n");
  } else {
    // Skip token for error recovery.
    GetNextToken();
  }
}

/// top ::= definition | external | expression | ';'
static void MainLoop() {
  while (true) {
    fprintf(stderr, "ready> ");
    switch (CurTok) {
    case tok_eof:
      return;
    case ';': // ignore top-level semicolons.
      GetNextToken();
      break;
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main() {
  return 0;
}