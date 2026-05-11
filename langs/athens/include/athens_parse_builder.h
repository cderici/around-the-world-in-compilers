// AthensParseBuilder adapts the reusable shared parser to Athens' existing AST
// classes. The shared parser only knows about a small builder contract; this
// file is the bridge that turns generic parser events into Athens ExprAST nodes.

#pragma once

#include "parser.h"
#include "../../../shared/frontend/lex/include/source_loc.h"
#include "../../../shared/frontend/lex/include/token.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace athens {

struct AthensParsedItem {
  std::unique_ptr<FunctionAST> function;
  std::unique_ptr<PrototypeAST> prototype;
};

class AthensParseBuilder {
public:
  using Expr = std::unique_ptr<ExprAST>;
  using Stmt = std::unique_ptr<ExprAST>;
  using Item = AthensParsedItem;

  Expr makeIdentifier(frontend::lex::Token tok) {
    return std::make_unique<VariableExprAST>(std::string(tok.lexeme));
  }

  Expr makeInteger(frontend::lex::Token, long long value) {
    return std::make_unique<NumberExprAST>(static_cast<double>(value));
  }

  Expr makeFloat(frontend::lex::Token, double value) {
    return std::make_unique<NumberExprAST>(value);
  }

  Expr makeCall(frontend::lex::Token callee,
                std::vector<Expr> arguments) {
    return std::make_unique<CallExprAST>(std::string(callee.lexeme),
                                         std::move(arguments));
  }

  Expr makeUnary(frontend::lex::Token opTok, Expr operand) {
    if (!operand || opTok.lexeme.size() != 1)
      return nullptr;

    return std::make_unique<UnaryExprAST>(opTok.lexeme.front(),
                                          std::move(operand));
  }

  Expr makeBinary(frontend::lex::Token opTok, Expr lhs, Expr rhs) {
    if (!lhs || !rhs || opTok.lexeme.size() != 1)
      return nullptr;

    return std::make_unique<BinaryExprAST>(opTok.lexeme.front(), std::move(lhs),
                                           std::move(rhs));
  }

  Expr makeIf(Expr cond, Expr thenExpr, Expr elseExpr) {
    if (!cond || !thenExpr || !elseExpr)
      return nullptr;

    return std::make_unique<IfExprAST>(std::move(cond), std::move(thenExpr),
                                       std::move(elseExpr));
  }

  Expr makeFor(frontend::lex::Token varName, Expr start, Expr end, Expr step,
               Expr body) {
    if (!start || !end || !body)
      return nullptr;

    return std::make_unique<ForExprAST>(std::string(varName.lexeme),
                                        std::move(start), std::move(end),
                                        std::move(step), std::move(body));
  }

  Expr makeVar(std::vector<std::pair<std::string, Expr>> bindings, Expr body) {
    if (!body)
      return nullptr;

    return std::make_unique<VarExprAST>(std::move(bindings), std::move(body));
  }

  std::unique_ptr<PrototypeAST>
  makePrototype(std::string name, std::vector<std::string> args,
                bool isOperator = false, unsigned precedence = 0) {
    return std::make_unique<PrototypeAST>(name, std::move(args), isOperator,
                                          precedence);
  }

  Item makeFunction(std::unique_ptr<PrototypeAST> prototype, Expr body) {
    Item item;
    if (prototype && body)
      item.function =
          std::make_unique<FunctionAST>(std::move(prototype), std::move(body));
    return item;
  }

  Item makeExtern(std::unique_ptr<PrototypeAST> prototype) {
    Item item;
    item.prototype = std::move(prototype);
    return item;
  }

  Expr makeErrorExpr(frontend::lex::SourceLoc) { return nullptr; }
};

} // namespace athens
