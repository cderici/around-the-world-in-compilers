#include "dublin_lex_rules.h"

#include <array>
#include <cctype>

namespace dublin {

std::optional<frontend::lex::TokenKind>
DublinLexRules::keyword(std::string_view identifier) const {
  using frontend::lex::TokenKind;

  if (identifier == "verify")
    return TokenKind::KwVerify;
  if (identifier == "assert")
    return TokenKind::KwAssert;
  if (identifier == "true")
    return TokenKind::KwTrue;
  if (identifier == "false")
    return TokenKind::KwFalse;
  return std::nullopt;
}

std::optional<frontend::lex::TokenKind>
DublinLexRules::punctuator(std::string_view text) const {
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
  if (text == "==")
    return TokenKind::EqualEqual;
  if (text == "!=")
    return TokenKind::BangEqual;
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
DublinLexRules::comments() const {
  using CD = frontend::lex::CommentDelimiter;
  static constexpr std::array<CD, 2> comment_delims = {
      CD{CD::Kind::Line, "#", ""},
      CD{CD::Kind::Line, "//", ""},
  };
  return std::span<const CD>(comment_delims);
}

bool DublinLexRules::isIdentStart(char c) const {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool DublinLexRules::isIdentContinue(char c) const {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // namespace dublin
