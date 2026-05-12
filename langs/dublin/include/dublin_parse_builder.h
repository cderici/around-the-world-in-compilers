#pragma once

#include "dublin_ast.h"
#include "../../../shared/frontend/lex/include/token.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dublin {

struct DublinItem {
  std::unique_ptr<VerifyBlock> verify;
};

class DublinParseBuilder {
public:
  using Expr = ExprPtr;
  using Stmt = Assertion;
  using Item = DublinItem;

  Expr makeIdentifier(frontend::lex::Token tok) {
    return std::make_unique<VariableExpr>(std::string(tok.lexeme));
  }

  Expr makeInteger(frontend::lex::Token, long long value) {
    return std::make_unique<IntegerExpr>(value);
  }

  Expr makeFloat(frontend::lex::Token tok, double) {
    return makeErrorExpr(tok.source_loc);
  }

  Expr makeBool(bool value) { return std::make_unique<BoolExpr>(value); }

  Expr makeCall(frontend::lex::Token callee, std::vector<Expr> arguments) {
    return std::make_unique<CallExpr>(std::string(callee.lexeme),
                                      std::move(arguments));
  }

  Expr makeUnary(frontend::lex::Token opTok, Expr operand) {
    return std::make_unique<UnaryExpr>(std::string(opTok.lexeme),
                                       std::move(operand));
  }

  Expr makeBinary(frontend::lex::Token opTok, Expr lhs, Expr rhs) {
    return std::make_unique<BinaryExpr>(std::string(opTok.lexeme),
                                        std::move(lhs), std::move(rhs));
  }

  Assertion makeAssert(frontend::lex::Token tok, Expr condition) {
    return Assertion{std::move(condition), tok.source_loc};
  }

  Item makeVerify(std::string name, std::vector<Argument> args,
                  std::vector<Assertion> assertions) {
    Item item;
    item.verify = std::make_unique<VerifyBlock>(
        VerifyBlock{std::move(name), std::move(args), std::move(assertions)});
    return item;
  }

  Expr makeErrorExpr(frontend::lex::SourceLoc) { return nullptr; }
};

} // namespace dublin
