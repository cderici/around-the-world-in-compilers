#pragma once

#include "parser_engine.h"

namespace frontend::parse {

template <typename BuilderT> struct DefaultGrammarPack {
  static void registerExpressionHandlers(ParserRegistry<BuilderT> &registry) {
    registry.setPrefix(frontend::lex::TokenKind::Identifier,
                       prefixIdentifierExpr);
    registry.setPrefix(frontend::lex::TokenKind::Integer, prefixIntegerExpr);
    registry.setPrefix(frontend::lex::TokenKind::Float, prefixFloatExpr);
    registry.setPrefix(frontend::lex::TokenKind::LParen, prefixParenExpr);

    registry.setInfix(frontend::lex::TokenKind::Equal, 5, infixBinaryExpr);
    registry.setInfix(frontend::lex::TokenKind::Less, 10, infixBinaryExpr);
    registry.setInfix(frontend::lex::TokenKind::LessEqual, 10, infixBinaryExpr);
    registry.setInfix(frontend::lex::TokenKind::Greater, 10, infixBinaryExpr);
    registry.setInfix(frontend::lex::TokenKind::GreaterEqual, 10,
                      infixBinaryExpr);
    registry.setInfix(frontend::lex::TokenKind::Plus, 20, infixBinaryExpr);
    registry.setInfix(frontend::lex::TokenKind::Minus, 20, infixBinaryExpr);
    registry.setInfix(frontend::lex::TokenKind::Star, 40, infixBinaryExpr);
    registry.setInfix(frontend::lex::TokenKind::Slash, 40, infixBinaryExpr);
  }

private:
  static typename BuilderT::Expr
  prefixIdentifierExpr(ParseContext<BuilderT> &ctx, ParserEngine<BuilderT> &,
                       frontend::lex::Token tok) {
    return ctx.builder.makeIdentifier(tok);
  }

  static typename BuilderT::Expr prefixIntegerExpr(ParseContext<BuilderT> &ctx,
                                                    ParserEngine<BuilderT> &,
                                                    frontend::lex::Token tok) {
    if (auto val = std::get_if<long long>(&tok.literal))
      return ctx.builder.makeInteger(tok, *val);

    ctx.diag.error(tok.source_loc, "integer token missing numeric payload");
    return ctx.builder.makeErrorExpr(tok.source_loc);
  }

  static typename BuilderT::Expr prefixFloatExpr(ParseContext<BuilderT> &ctx,
                                                 ParserEngine<BuilderT> &,
                                                 frontend::lex::Token tok) {
    if (auto val = std::get_if<double>(&tok.literal))
      return ctx.builder.makeFloat(tok, *val);

    ctx.diag.error(tok.source_loc, "float token missing numeric payload");
    return ctx.builder.makeErrorExpr(tok.source_loc);
  }

  static typename BuilderT::Expr prefixParenExpr(ParseContext<BuilderT> &ctx,
                                                 ParserEngine<BuilderT> &engine,
                                                 frontend::lex::Token tok) {
    (void)tok;

    typename BuilderT::Expr inner = engine.parseExpression(0);
    if (!ctx.tokenStream.expect(
            frontend::lex::TokenKind::RParen, ctx.diag,
            "expected ')' to close parenthesized expression")) {
      return ctx.builder.makeErrorExpr(tok.source_loc);
    }
    return inner;
  }

  static typename BuilderT::Expr infixBinaryExpr(ParseContext<BuilderT> &ctx,
                                                 ParserEngine<BuilderT> &,
                                                 typename BuilderT::Expr lhs,
                                                 frontend::lex::Token opTok,
                                                 typename BuilderT::Expr rhs) {
    return ctx.builder.makeBinary(opTok, std::move(lhs), std::move(rhs));
  }
};

} // namespace frontend::parse
