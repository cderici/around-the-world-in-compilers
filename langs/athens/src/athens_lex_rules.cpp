#include "athens_lex_rules.h"

#include <array>
#include <cctype>
#include <optional>
#include <string_view>

namespace athens {

std::optional<frontend::lex::TokenKind>
AthensLexRules::keyword(std::string_view identifier) const {
  using frontend::lex::TokenKind;

  if (identifier == "def")
    return TokenKind::KwFuncDef;
  if (identifier == "extern")
    return TokenKind::KwExtern;
  if (identifier == "if")
    return TokenKind::KwIf;
  if (identifier == "then")
    return TokenKind::KwThen;
  if (identifier == "else")
    return TokenKind::KwElse;
  if (identifier == "for")
    return TokenKind::KwFor;
  if (identifier == "in")
    return TokenKind::KwIn;
  if (identifier == "binary")
    return TokenKind::KwBinaryOp;
  if (identifier == "unary")
    return TokenKind::KwUnaryOp;
  if (identifier == "var")
    return TokenKind::KwVar;

  return std::nullopt;
}

std::optional<frontend::lex::TokenKind>
AthensLexRules::punctuator(std::string_view text) const {
  using frontend::lex::TokenKind;

  if (text == "(")
    return TokenKind::LParen;
  if (text == ")")
    return TokenKind::RParen;
  if (text == ",")
    return TokenKind::Comma;
  if (text == ";")
    return TokenKind::Semicolon;
  if (text == "+")
    return TokenKind::Plus;
  if (text == "-")
    return TokenKind::Minus;
  if (text == "*")
    return TokenKind::Star;
  if (text == "/")
    return TokenKind::Slash;
  if (text == "=")
    return TokenKind::Equal;
  if (text == "<")
    return TokenKind::Less;
  if (text == "<=")
    return TokenKind::LessEqual;
  if (text == ">")
    return TokenKind::Greater;
  if (text == ">=")
    return TokenKind::GreaterEqual;
  if (text == "!")
    return TokenKind::LogicNot;

  return std::nullopt;
}

std::span<const frontend::lex::CommentDelimiter>
AthensLexRules::comments() const {
  using CD = frontend::lex::CommentDelimiter;
  static constexpr std::array<CD, 1> comment_delims = {
      CD{CD::Kind::Line, "#", ""},
  };
  return std::span<const CD>(comment_delims);
}

// can this character be the first char of an identifier?
bool AthensLexRules::isIdentStart(char c) const {
  return std::isalpha(static_cast<unsigned char>(c));
}

// can this character appear after the first char in an identifier?
bool AthensLexRules::isIdentContinue(char c) const {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // namespace athens
