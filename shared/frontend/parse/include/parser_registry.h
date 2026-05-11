#pragma once

#include "handler_types.h"

#include <unordered_map>

namespace frontend::parse {

template <typename BuilderT> class ParserRegistry {
public:
  explicit ParserRegistry(const ParserRegistry *fallback = nullptr)
      : fallback_(fallback) {}

  // Register how to parse expressions that start with kind
  void setPrefix(frontend::lex::TokenKind kind,
                 PrefixExprHandler<BuilderT> handler) {
    prefix_[kind] = std::move(handler);
  }

  void setInfix(frontend::lex::TokenKind kind, int precedence,
                 InfixExprHandler<BuilderT> handler) {
    infix_[kind] = InfixEntry<BuilderT>{precedence, {}, std::move(handler)};
  }

  void setInfixDynamic(
      frontend::lex::TokenKind kind,
      std::function<int(const frontend::lex::Token &)> precedenceFor,
      InfixExprHandler<BuilderT> handler) {
    infix_[kind] =
        InfixEntry<BuilderT>{0, std::move(precedenceFor), std::move(handler)};
  }

  void setStmt(frontend::lex::TokenKind kind, StmtHandler<BuilderT> handler) {
    stmt_[kind] = std::move(handler);
  }

  void setItem(frontend::lex::TokenKind kind, ItemHandler<BuilderT> handler) {
    item_[kind] = std::move(handler);
  }

  const PrefixExprHandler<BuilderT> *
  findPrefixHandler(frontend::lex::TokenKind kind) const {
    if (auto it = prefix_.find(kind); it != prefix_.end())
      return &it->second;

    return fallback_ ? fallback_->findPrefixHandler(kind) : nullptr;
  }

  const InfixEntry<BuilderT> *
  findInfixHandler(frontend::lex::TokenKind kind) const {
    if (auto it = infix_.find(kind); it != infix_.end())
      return &it->second;

    return fallback_ ? fallback_->findInfixHandler(kind) : nullptr;
  }

  const StmtHandler<BuilderT> *
  findStmtHandler(frontend::lex::TokenKind kind) const {
    if (auto it = stmt_.find(kind); it != stmt_.end())
      return &it->second;

    return fallback_ ? fallback_->findStmtHandler(kind) : nullptr;
  }

  const ItemHandler<BuilderT> *
  findItemHandler(frontend::lex::TokenKind kind) const {
    if (auto it = item_.find(kind); it != item_.end())
      return &it->second;

    return fallback_ ? fallback_->findItemHandler(kind) : nullptr;
  }

private:
  const ParserRegistry *fallback_;

  std::unordered_map<frontend::lex::TokenKind, PrefixExprHandler<BuilderT>>
      prefix_;
  std::unordered_map<frontend::lex::TokenKind, InfixEntry<BuilderT>> infix_;
  std::unordered_map<frontend::lex::TokenKind, StmtHandler<BuilderT>> stmt_;
  std::unordered_map<frontend::lex::TokenKind, ItemHandler<BuilderT>> item_;
};

} // namespace frontend::parse
