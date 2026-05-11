#pragma once

#include "parse_context.h"
#include "parser_registry.h"

#include <utility>

namespace frontend::parse {

template <typename BuilderT> class ParserEngine {
public:
  ParserEngine(ParseContext<BuilderT> &ctx,
               const ParserRegistry<BuilderT> &registry)
      : ctx_(ctx), registry_(registry) {}

  typename BuilderT::Expr parseExpression(int minPrecedence = 0) {
    const frontend::lex::Token firstTok = ctx_.tokenStream.consume();

    const auto *prefixHandler = registry_.findPrefixHandler(firstTok.kind);
    if (!prefixHandler) {
      ctx_.diag.error(firstTok.source_loc,
                      "unexpected token at expression start");
      return typename BuilderT::Expr{};
    }

    // Passing the whole ParserEngine into the handler because sometimes
    // handlers might need to recursively parse sub-expressions etc.
    typename BuilderT::Expr lhs = (*prefixHandler)(ctx_, *this, firstTok);

    while (true) {
      const auto &lookahead = ctx_.tokenStream.current();
      const auto *infixEntry = registry_.findInfixHandler(lookahead.kind);
      if (!infixEntry)
        break;

      const int precedence = infixEntry->getPrecedence(lookahead);
      if (precedence < minPrecedence)
        break;

      const frontend::lex::Token opTok = ctx_.tokenStream.consume();
      typename BuilderT::Expr rhs = parseExpression(precedence + 1);
      lhs = infixEntry->handler(ctx_, *this, std::move(lhs), opTok,
                                std::move(rhs));
    }

    return lhs;
  }

  typename BuilderT::Stmt parseStatement() {
    const auto &tok = ctx_.tokenStream.current();
    const auto *stmtHandler = registry_.findStmtHandler(tok.kind);
    if (!stmtHandler) {
      ctx_.diag.error(tok.source_loc, "unexpected token at statement start");
      (void)ctx_.tokenStream.consume();
      return typename BuilderT::Stmt{};
    }

    return (*stmtHandler)(ctx_, *this);
  }

  typename BuilderT::Item parseItem() {
    const auto &tok = ctx_.tokenStream.current();
    const auto *itemHandler = registry_.findItemHandler(tok.kind);
    if (!itemHandler) {
      ctx_.diag.error(tok.source_loc, "unexpected token at top-level start");
      (void)ctx_.tokenStream.consume();
      return typename BuilderT::Item{};
    }

    return (*itemHandler)(ctx_, *this);
  }

  TokenStream &tokens() { return ctx_.tokenStream; }
  const TokenStream &tokens() const { return ctx_.tokenStream; }

private:
  ParseContext<BuilderT> &ctx_;
  const ParserRegistry<BuilderT> &registry_;
};

} // namespace frontend::parse
