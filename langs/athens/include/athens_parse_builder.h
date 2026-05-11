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

namespace athens {

class AthensParseBuilder {
public:
  using Expr = std::unique_ptr<ExprAST>;
  using Stmt = std::unique_ptr<ExprAST>;
  using Item = std::unique_ptr<FunctionAST>;

  Expr makeIdentifier(frontend::lex::Token tok) {
    return std::make_unique<VariableExprAST>(std::string(tok.lexeme));
  }

  Expr makeInteger(frontend::lex::Token, long long value) {
    return std::make_unique<NumberExprAST>(static_cast<double>(value));
  }

  Expr makeBinary(frontend::lex::Token opTok, Expr lhs, Expr rhs) {
    if (!lhs || !rhs || opTok.lexeme.size() != 1)
      return nullptr;

    return std::make_unique<BinaryExprAST>(opTok.lexeme.front(), std::move(lhs),
                                           std::move(rhs));
  }

  Expr makeErrorExpr(frontend::lex::SourceLoc) { return nullptr; }
};

} // namespace athens
