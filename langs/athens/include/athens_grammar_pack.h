// AthensGrammarPack registers Athens-specific parser handlers on top of the
// reusable parser engine. This keeps language syntax decisions (calls, if/for,
// var/in, prototypes, and user operators) out of the shared parser core.

#pragma once

#include "../../../shared/frontend/parse/include/default_grammar_pack.h"
#include "../../../shared/frontend/parse/include/parser_engine.h"
#include "../../../shared/frontend/parse/include/parser_registry.h"
#include "athens_parse_builder.h"
#include "parser.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace athens {

using AthensContext = frontend::parse::ParseContext<AthensParseBuilder>;
using AthensEngine = frontend::parse::ParserEngine<AthensParseBuilder>;
using AthensRegistry = frontend::parse::ParserRegistry<AthensParseBuilder>;

inline bool isAthensOperatorToken(frontend::lex::TokenKind kind) {
  // Athens treats built-in punctuation and fallback Operator tokens uniformly
  // for unary/binary operator parsing. Delimiters like ')' and ',' are
  // excluded.
  using frontend::lex::TokenKind;
  switch (kind) {
  case TokenKind::Plus:
  case TokenKind::Minus:
  case TokenKind::Star:
  case TokenKind::Slash:
  case TokenKind::Equal:
  case TokenKind::Less:
  case TokenKind::LessEqual:
  case TokenKind::Greater:
  case TokenKind::GreaterEqual:
  case TokenKind::LogicNot:
  case TokenKind::Operator:
    return true;
  default:
    return false;
  }
}

inline int athensOperatorPrecedence(const frontend::lex::Token &tok) {
  // Generic Operator tokens get their binding power from Athens' runtime table.
  // Returning -1 tells the Pratt loop "this token is not an active binary op".
  if (tok.lexeme.size() != 1)
    return -1;

  const auto it = BinopPrecedence.find(tok.lexeme.front());
  if (it == BinopPrecedence.end() || it->second <= 0)
    return -1;

  return it->second;
}

inline frontend::lex::Token consumeOperator(AthensContext &ctx,
                                            std::string_view message) {
  // Prototype parsing needs to consume the operator itself after 'unary' or
  // 'binary'. Keep this validation in one place because Athens only supports
  // one-character user operators today.
  const frontend::lex::Token tok = ctx.tokenStream.current();
  if (!isAthensOperatorToken(tok.kind) || tok.lexeme.size() != 1) {
    ctx.diag.error(tok.source_loc, message);
    return {};
  }

  return ctx.tokenStream.consume();
}

inline AthensParseBuilder::Expr
parseIdentifierOrCall(AthensContext &ctx, AthensEngine &engine,
                      frontend::lex::Token tok) {
  // identifier
  // identifier '(' args? ')'
  // The identifier token is already consumed by ParserEngine before this runs.
  if (!ctx.tokenStream.match(frontend::lex::TokenKind::LParen))
    return ctx.builder.makeIdentifier(tok);

  std::vector<AthensParseBuilder::Expr> args;
  if (!ctx.tokenStream.is(frontend::lex::TokenKind::RParen)) {
    while (true) {
      // Arguments are full expressions, so nested calls/operators/etc. work.
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

inline AthensParseBuilder::Expr
parseUnary(AthensContext &ctx, AthensEngine &engine, frontend::lex::Token tok) {
  // Give unary operators a high minimum precedence so '-x * y' parses as
  // '(-x) * y', matching the existing Athens parser behavior.
  return ctx.builder.makeUnary(tok, engine.parseExpression(100));
}

inline AthensParseBuilder::Expr
parseIf(AthensContext &ctx, AthensEngine &engine, frontend::lex::Token tok) {
  // if expression then expression else expression
  // Each arm is parsed as a full expression, which allows nested ifs and calls.
  (void)tok;
  auto cond = engine.parseExpression();
  if (!ctx.tokenStream.expect(frontend::lex::TokenKind::KwThen, ctx.diag,
                              "expected 'then' in if expression"))
    return ctx.builder.makeErrorExpr(tok.source_loc);

  auto thenExpr = engine.parseExpression();
  if (!ctx.tokenStream.expect(frontend::lex::TokenKind::KwElse, ctx.diag,
                              "expected 'else' in if expression"))
    return ctx.builder.makeErrorExpr(tok.source_loc);

  auto elseExpr = engine.parseExpression();
  return ctx.builder.makeIf(std::move(cond), std::move(thenExpr),
                            std::move(elseExpr));
}

inline AthensParseBuilder::Expr
parseFor(AthensContext &ctx, AthensEngine &engine, frontend::lex::Token tok) {
  // for identifier = start, end (, step)? in body
  // ParserEngine has already consumed 'for', so the next token must name the
  // loop variable. We keep the token so the builder can preserve the name.
  if (!ctx.tokenStream.is(frontend::lex::TokenKind::Identifier)) {
    ctx.diag.error(ctx.tokenStream.current().source_loc,
                   "expected loop variable after 'for'");
    return ctx.builder.makeErrorExpr(tok.source_loc);
  }

  frontend::lex::Token varName = ctx.tokenStream.consume();
  if (!ctx.tokenStream.expect(frontend::lex::TokenKind::Equal, ctx.diag,
                              "expected '=' after loop variable"))
    return ctx.builder.makeErrorExpr(tok.source_loc);

  // Parse the start expression up to the comma. The Pratt loop naturally stops
  // at ',' because comma has no infix handler in Athens.
  auto start = engine.parseExpression();
  if (!ctx.tokenStream.expect(frontend::lex::TokenKind::Comma, ctx.diag,
                              "expected ',' after loop start"))
    return ctx.builder.makeErrorExpr(tok.source_loc);

  // Parse the loop continuation condition. Athens follows Kaleidoscope here:
  // this is an expression whose non-zero value means "keep looping".
  auto end = engine.parseExpression();
  AthensParseBuilder::Expr step;

  // The step is optional; codegen supplies a default of 1.0 when this remains
  // null, matching the old parser/codegen path.
  if (ctx.tokenStream.match(frontend::lex::TokenKind::Comma))
    step = engine.parseExpression();

  if (!ctx.tokenStream.expect(frontend::lex::TokenKind::KwIn, ctx.diag,
                              "expected 'in' after for range"))
    return ctx.builder.makeErrorExpr(tok.source_loc);

  // Body is another expression, often a parenthesized sequence using ':' in
  // current Athens programs.
  auto body = engine.parseExpression();
  return ctx.builder.makeFor(varName, std::move(start), std::move(end),
                             std::move(step), std::move(body));
}

inline AthensParseBuilder::Expr
parseVar(AthensContext &ctx, AthensEngine &engine, frontend::lex::Token tok) {
  // var name (= init)? (, name (= init)?)* in body
  // Initializers are optional. A null initializer means codegen will initialize
  // that binding to Athens' default 0.0.
  std::vector<std::pair<std::string, AthensParseBuilder::Expr>> bindings;

  while (true) {
    if (!ctx.tokenStream.is(frontend::lex::TokenKind::Identifier)) {
      ctx.diag.error(ctx.tokenStream.current().source_loc,
                     "expected variable name after 'var'");
      return ctx.builder.makeErrorExpr(tok.source_loc);
    }

    frontend::lex::Token nameTok = ctx.tokenStream.consume();
    AthensParseBuilder::Expr init;

    // Parse initializer only when '=' is present; otherwise leave init null.
    if (ctx.tokenStream.match(frontend::lex::TokenKind::Equal))
      init = engine.parseExpression();

    bindings.emplace_back(std::string(nameTok.lexeme), std::move(init));

    if (!ctx.tokenStream.match(frontend::lex::TokenKind::Comma))
      break;
  }

  if (!ctx.tokenStream.expect(frontend::lex::TokenKind::KwIn, ctx.diag,
                              "expected 'in' after var bindings"))
    return ctx.builder.makeErrorExpr(tok.source_loc);

  return ctx.builder.makeVar(std::move(bindings), engine.parseExpression());
}

inline std::unique_ptr<PrototypeAST> parsePrototype(AthensContext &ctx) {
  // prototype ::= name(args)
  //             | unary operator(arg)
  //             | binary operator precedence?(lhs rhs)
  std::string name;
  unsigned kind = 0;
  unsigned precedence = 30;

  if (ctx.tokenStream.is(frontend::lex::TokenKind::Identifier)) {
    // Ordinary function prototype, e.g. 'foo(x y)'.
    const frontend::lex::Token nameTok = ctx.tokenStream.consume();
    name = std::string(nameTok.lexeme);
  } else if (ctx.tokenStream.match(frontend::lex::TokenKind::KwUnaryOp)) {
    // User-defined unary operator prototype, e.g. 'unary!(v)'.
    const frontend::lex::Token opTok =
        consumeOperator(ctx, "expected unary operator in prototype");
    if (opTok.lexeme.empty())
      return nullptr;
    name = "unary" + std::string(opTok.lexeme);
    kind = 1;
  } else if (ctx.tokenStream.match(frontend::lex::TokenKind::KwBinaryOp)) {
    // User-defined binary operator prototype, e.g. 'binary: 1 (x y)'.
    const frontend::lex::Token opTok =
        consumeOperator(ctx, "expected binary operator in prototype");
    if (opTok.lexeme.empty())
      return nullptr;
    name = "binary" + std::string(opTok.lexeme);
    kind = 2;

    if (ctx.tokenStream.is(frontend::lex::TokenKind::Integer)) {
      // Athens allows an optional numeric precedence immediately after the
      // operator name. If omitted, it uses the existing default of 30.
      const frontend::lex::Token precTok = ctx.tokenStream.consume();
      const auto *value = std::get_if<long long>(&precTok.literal);
      if (!value || *value < 1 || *value > 100) {
        ctx.diag.error(precTok.source_loc,
                       "binary precedence must be in [1, 100]");
        return nullptr;
      }
      precedence = static_cast<unsigned>(*value);
    }

    // Make this operator available while parsing the function body and later
    // top-level forms. This preserves Athens' user-defined operator behavior.
    BinopPrecedence[opTok.lexeme.front()] = static_cast<int>(precedence);
  } else {
    ctx.diag.error(ctx.tokenStream.current().source_loc,
                   "expected function name in prototype");
    return nullptr;
  }

  if (!ctx.tokenStream.expect(frontend::lex::TokenKind::LParen, ctx.diag,
                              "expected '(' in prototype"))
    return nullptr;

  std::vector<std::string> args;
  // Athens prototypes separate argument names by whitespace, not commas.
  while (ctx.tokenStream.is(frontend::lex::TokenKind::Identifier)) {
    args.push_back(std::string(ctx.tokenStream.consume().lexeme));
  }

  if (!ctx.tokenStream.expect(frontend::lex::TokenKind::RParen, ctx.diag,
                              "expected ')' in prototype"))
    return nullptr;

  if (kind != 0 && args.size() != kind) {
    // Operator prototypes have fixed arity: unary takes one operand, binary
    // two.
    ctx.diag.error(ctx.tokenStream.current().source_loc,
                   "invalid number of operator operands");
    return nullptr;
  }

  return ctx.builder.makePrototype(std::move(name), std::move(args), kind != 0,
                                   precedence);
}

inline AthensParseBuilder::Item parseDefinition(AthensContext &ctx,
                                                AthensEngine &engine) {
  // def prototype body-expression
  // ParserEngine dispatches here based on KwFuncDef but leaves 'def' current.
  (void)ctx.tokenStream.consume();
  auto prototype = parsePrototype(ctx);
  auto body = engine.parseExpression();
  return ctx.builder.makeFunction(std::move(prototype), std::move(body));
}

inline AthensParseBuilder::Item parseExtern(AthensContext &ctx,
                                            AthensEngine &engine) {
  // extern prototype
  // There is no body expression for extern declarations.
  (void)engine;
  (void)ctx.tokenStream.consume();
  return ctx.builder.makeExtern(parsePrototype(ctx));
}

inline void registerAthensGrammar(AthensRegistry &registry) {
  // Start from shared expression primitives: literals, parentheses, and common
  // binary tokens. Athens-specific registrations below override/extend these.
  frontend::parse::DefaultGrammarPack<
      AthensParseBuilder>::registerExpressionHandlers(registry);

  registry.setPrefix(frontend::lex::TokenKind::Identifier,
                     parseIdentifierOrCall);
  registry.setPrefix(frontend::lex::TokenKind::KwIf, parseIf);
  registry.setPrefix(frontend::lex::TokenKind::KwFor, parseFor);
  registry.setPrefix(frontend::lex::TokenKind::KwVar, parseVar);

  registry.setItem(frontend::lex::TokenKind::KwFuncDef, parseDefinition);
  registry.setItem(frontend::lex::TokenKind::KwExtern, parseExtern);

  auto registerOperator = [&](frontend::lex::TokenKind kind) {
    // Every Athens operator token can appear as a unary prefix. As an infix op,
    // its precedence comes from BinopPrecedence, including user-defined ops.
    registry.setPrefix(kind, parseUnary);
    registry.setInfixDynamic(
        kind, athensOperatorPrecedence,
        [](AthensContext &ctx, AthensEngine &, AthensParseBuilder::Expr lhs,
           frontend::lex::Token opTok, AthensParseBuilder::Expr rhs) {
          return ctx.builder.makeBinary(opTok, std::move(lhs), std::move(rhs));
        });
  };

  registerOperator(frontend::lex::TokenKind::Plus);
  registerOperator(frontend::lex::TokenKind::Minus);
  registerOperator(frontend::lex::TokenKind::Star);
  registerOperator(frontend::lex::TokenKind::Slash);
  registerOperator(frontend::lex::TokenKind::Equal);
  registerOperator(frontend::lex::TokenKind::Less);
  registerOperator(frontend::lex::TokenKind::LessEqual);
  registerOperator(frontend::lex::TokenKind::Greater);
  registerOperator(frontend::lex::TokenKind::GreaterEqual);
  registerOperator(frontend::lex::TokenKind::LogicNot);
  registerOperator(frontend::lex::TokenKind::Operator);
}

} // namespace athens
