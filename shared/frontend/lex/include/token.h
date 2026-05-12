#pragma once

#include "source_loc.h"
#include <string_view>
#include <variant>

namespace frontend::lex {

enum class TokenKind {
  Invalid,
  InvalidNumber,
  Eof,

  Identifier,
  Integer,
  Float,

  LParen,
  RParen,
  Comma,
  Semicolon,
  Plus,
  Minus,
  Star,
  Slash,
  Equal,
  EqualEqual,
  BangEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  LogicNot,
  Operator,

  KwFuncDef,
  KwExtern,
  KwIf,
  KwThen,
  KwElse,
  KwFor,
  KwIn,
  KwBinaryOp,
  KwUnaryOp,
  KwVar,
  KwVerify,
  KwAssert,
  KwTrue,
  KwFalse,

};

using LiteralValue = std::variant<std::monostate, long long, double>;

struct Token {
  TokenKind kind{TokenKind::Invalid};
  std::string_view lexeme;
  SourceLoc source_loc{};
  LiteralValue literal{};
};

} // namespace frontend::lex
