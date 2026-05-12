#pragma once

#include "dublin_parse_builder.h"
#include "../../../shared/frontend/parse/include/default_grammar_pack.h"
#include "../../../shared/frontend/parse/include/parser_engine.h"
#include "../../../shared/frontend/parse/include/parser_registry.h"

#include <string>
#include <utility>
#include <vector>

namespace dublin {

using DublinContext = frontend::parse::ParseContext<DublinParseBuilder>;
using DublinEngine = frontend::parse::ParserEngine<DublinParseBuilder>;
using DublinRegistry = frontend::parse::ParserRegistry<DublinParseBuilder>;

inline bool parseTypeToken(frontend::lex::Token tok, Type &type) {
  if (tok.kind != frontend::lex::TokenKind::Identifier)
    return false;

  if (tok.lexeme == "i1") {
    type = {TypeKind::BV, 1};
    return true;
  }
  if (tok.lexeme == "i8") {
    type = {TypeKind::BV, 8};
    return true;
  }
  if (tok.lexeme == "i16") {
    type = {TypeKind::BV, 16};
    return true;
  }
  if (tok.lexeme == "i32") {
    type = {TypeKind::BV, 32};
    return true;
  }
  if (tok.lexeme == "i64") {
    type = {TypeKind::BV, 64};
    return true;
  }

  return false;
}

inline DublinParseBuilder::Expr
parseIdentifierOrCall(DublinContext &ctx, DublinEngine &engine,
                      frontend::lex::Token tok) {
  if (!ctx.tokenStream.match(frontend::lex::TokenKind::LParen))
    return ctx.builder.makeIdentifier(tok);

  std::vector<DublinParseBuilder::Expr> args;
  if (!ctx.tokenStream.is(frontend::lex::TokenKind::RParen)) {
    while (true) {
      args.push_back(engine.parseExpression());
      if (ctx.tokenStream.is(frontend::lex::TokenKind::RParen))
        break;
      if (!ctx.tokenStream.expect(frontend::lex::TokenKind::Comma, ctx.diag,
                                  "expected ',' in argument list"))
        return ctx.builder.makeErrorExpr(tok.source_loc);
    }
  }

  if (!ctx.tokenStream.expect(frontend::lex::TokenKind::RParen, ctx.diag,
                              "expected ')' to close argument list"))
    return ctx.builder.makeErrorExpr(tok.source_loc);

  return ctx.builder.makeCall(tok, std::move(args));
}

inline DublinParseBuilder::Expr parseBool(DublinContext &ctx, DublinEngine &,
                                          frontend::lex::Token tok) {
  return ctx.builder.makeBool(tok.kind == frontend::lex::TokenKind::KwTrue);
}

inline DublinParseBuilder::Expr parseUnary(DublinContext &ctx,
                                           DublinEngine &engine,
                                           frontend::lex::Token tok) {
  return ctx.builder.makeUnary(tok, engine.parseExpression(100));
}

inline DublinParseBuilder::Stmt parseAssert(DublinContext &ctx,
                                            DublinEngine &engine) {
  frontend::lex::Token assertTok = ctx.tokenStream.consume();
  auto condition = engine.parseExpression();
  (void)ctx.tokenStream.expect(frontend::lex::TokenKind::Semicolon, ctx.diag,
                               "expected ';' after assertion");
  return ctx.builder.makeAssert(assertTok, std::move(condition));
}

inline DublinParseBuilder::Item parseVerify(DublinContext &ctx,
                                            DublinEngine &engine) {
  frontend::lex::Token verifyTok = ctx.tokenStream.consume();
  (void)verifyTok;

  if (!ctx.tokenStream.is(frontend::lex::TokenKind::Identifier)) {
    ctx.diag.error(ctx.tokenStream.current().source_loc,
                   "expected name after 'verify'");
    return {};
  }
  std::string name(ctx.tokenStream.consume().lexeme);

  if (!ctx.tokenStream.expect(frontend::lex::TokenKind::LParen, ctx.diag,
                              "expected '(' after verify name"))
    return {};

  std::vector<Argument> args;
  if (!ctx.tokenStream.is(frontend::lex::TokenKind::RParen)) {
    while (true) {
      if (!ctx.tokenStream.is(frontend::lex::TokenKind::Identifier)) {
        ctx.diag.error(ctx.tokenStream.current().source_loc,
                       "expected argument name");
        return {};
      }
      std::string argName(ctx.tokenStream.consume().lexeme);

      Type argType;
      frontend::lex::Token typeTok = ctx.tokenStream.consume();
      if (!parseTypeToken(typeTok, argType)) {
        ctx.diag.error(typeTok.source_loc,
                       "expected type i1, i8, i16, i32, or i64");
        return {};
      }
      args.push_back({std::move(argName), argType});

      if (ctx.tokenStream.is(frontend::lex::TokenKind::RParen))
        break;
      if (!ctx.tokenStream.expect(frontend::lex::TokenKind::Comma, ctx.diag,
                                  "expected ',' between arguments"))
        return {};
    }
  }

  if (!ctx.tokenStream.expect(frontend::lex::TokenKind::RParen, ctx.diag,
                              "expected ')' after argument list"))
    return {};

  std::vector<Assertion> assertions;
  while (!ctx.tokenStream.is(frontend::lex::TokenKind::Eof)) {
    assertions.push_back(engine.parseStatement());
  }

  return ctx.builder.makeVerify(std::move(name), std::move(args),
                                std::move(assertions));
}

inline DublinParseBuilder::Expr infixBinary(DublinContext &ctx,
                                            DublinEngine &,
                                            DublinParseBuilder::Expr lhs,
                                            frontend::lex::Token opTok,
                                            DublinParseBuilder::Expr rhs) {
  return ctx.builder.makeBinary(opTok, std::move(lhs), std::move(rhs));
}

struct DublinGrammarPack {
  static void registerHandlers(DublinRegistry &registry) {
    frontend::parse::DefaultGrammarPack<DublinParseBuilder>::registerExpressionHandlers(
        registry);

    registry.setPrefix(frontend::lex::TokenKind::Identifier,
                       parseIdentifierOrCall);
    registry.setPrefix(frontend::lex::TokenKind::KwTrue, parseBool);
    registry.setPrefix(frontend::lex::TokenKind::KwFalse, parseBool);
    registry.setPrefix(frontend::lex::TokenKind::LogicNot, parseUnary);
    registry.setPrefix(frontend::lex::TokenKind::Minus, parseUnary);

    registry.setInfix(frontend::lex::TokenKind::EqualEqual, 10, infixBinary);
    registry.setInfix(frontend::lex::TokenKind::BangEqual, 10, infixBinary);
    registry.setInfix(frontend::lex::TokenKind::Less, 10, infixBinary);
    registry.setInfix(frontend::lex::TokenKind::LessEqual, 10, infixBinary);
    registry.setInfix(frontend::lex::TokenKind::Greater, 10, infixBinary);
    registry.setInfix(frontend::lex::TokenKind::GreaterEqual, 10, infixBinary);
    registry.setInfix(frontend::lex::TokenKind::Plus, 20, infixBinary);
    registry.setInfix(frontend::lex::TokenKind::Minus, 20, infixBinary);
    registry.setInfix(frontend::lex::TokenKind::Star, 40, infixBinary);

    registry.setStmt(frontend::lex::TokenKind::KwAssert, parseAssert);
    registry.setItem(frontend::lex::TokenKind::KwVerify, parseVerify);
  }
};

} // namespace dublin
