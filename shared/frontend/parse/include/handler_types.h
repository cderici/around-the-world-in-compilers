#pragma once

#include "parse_context.h"

#include <functional>

namespace frontend::parse {

template <typename BuilderT> class ParserEngine;

// For things that start and expression (identifier, int, (, etc)
template <typename BuilderT>
using PrefixExprHandler = std::function<typename BuilderT::Expr(
    ParseContext<BuilderT> &, ParserEngine<BuilderT> &, frontend::lex::Token)>;

// For ops btw expressions (+, *, <, etc)
template <typename BuilderT>
using InfixExprHandler = std::function<typename BuilderT::Expr(
    ParseContext<BuilderT> &, ParserEngine<BuilderT> &, typename BuilderT::Expr,
    frontend::lex::Token, typename BuilderT::Expr)>;

// Statements (if, for, var, etc.)
template <typename BuilderT>
using StmtHandler = std::function<typename BuilderT::Stmt(
    ParseContext<BuilderT> &, ParserEngine<BuilderT> &)>;

// For top-level forms (def, extern, module items, etc.)
template <typename BuilderT>
using ItemHandler = std::function<typename BuilderT::Item(
    ParseContext<BuilderT> &, ParserEngine<BuilderT> &)>;

// Adds precedence tp infix handlers
template <typename BuilderT> struct InfixEntry {
  int precedence{0};
  std::function<int(const frontend::lex::Token &)> precedenceFor;
  InfixExprHandler<BuilderT> handler;

  int getPrecedence(const frontend::lex::Token &tok) const {
    return precedenceFor ? precedenceFor(tok) : precedence;
  }
};

} // namespace frontend::parse
