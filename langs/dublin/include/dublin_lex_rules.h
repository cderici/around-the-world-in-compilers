#pragma once

#include "../../../shared/frontend/lex/include/lex_language_rules.h"

#include <optional>
#include <span>
#include <string_view>

namespace dublin {

class DublinLexRules final : public frontend::lex::ILexLanguageRules {
public:
  std::optional<frontend::lex::TokenKind>
  keyword(std::string_view identifier) const override;
  std::optional<frontend::lex::TokenKind>
  punctuator(std::string_view text) const override;
  std::span<const frontend::lex::CommentDelimiter> comments() const override;

  bool isIdentStart(char c) const override;
  bool isIdentContinue(char c) const override;

  bool emitNewlineToken() const override { return false; }
  bool emitIndentDedent() const override { return false; }
  bool allowRationalLiteral() const override { return false; }
};

} // namespace dublin
