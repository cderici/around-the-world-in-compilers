#include "../include/lexer.h"
#include "../include/lex_language_rules.h"
#include "../include/source_loc.h"
#include "../include/token.h"
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace frontend::lex {

Lexer::Lexer(CharStream &cs, const ILexLanguageRules &langLexConfig)
    : cs_(cs), langLexConfig_(langLexConfig) {}

// invariant: next always consumes at least one char unless EOF
//
// Trivia flow:
// - Start of next, we clear the trivia
// - Then consumeTrivia fills trivia_
// - next() returns the non-trivia token
// - leaadingTrivia refers to that token's leading trivia.
Token Lexer::next() {
  // clear trivia from previous token
  trivia_.clear();

  consumeTrivia();

  if (cs_.eof())
    return Token{TokenKind::Eof, std::string_view{},
                 SourceLoc{
                     .start_offset = cs_.position(),
                     .end_offset = cs_.position(),
                     .start_line = cs_.line(),
                     .start_column = cs_.column(),
                     .end_line = cs_.line(),
                     .end_column = cs_.column(),
                 },
                 LiteralValue{}};

  char c = cs_.peek();

  // can it be a keyword?
  if (langLexConfig_.isIdentStart(c)) {
    return lexIdentifierOrKeyword();
    // maybe a number?
  } else if (std::isdigit(static_cast<unsigned char>(c)) ||
             (c == '.' &&
              std::isdigit(static_cast<unsigned char>(cs_.peek2())))) {
    return lexNumber();
  } else {
    // then it must be punctuation or invalid
    return lexPunctOrInvalid();
  }
}

const std::vector<TriviaPiece> &Lexer::leadingTrivia() const { return trivia_; }

// consumeTrivia lexes Trivia
// Trivia are: positions, spaces, comments, everything we *could* ignore,
// but we don't so I can maybe experiment with some tooling later on
void Lexer::consumeTrivia() {
  bool consumed_any = true;
  // Keep going as long as we don't hit an eof and we consumed something in the
  // previous run (if we didn't consume anything in the previous run, then we're
  // done)
  while (!cs_.eof() && consumed_any) {
    consumed_any = false;

    char c = cs_.peek();

    if (c == '\n') {
      const std::size_t start_position = cs_.position();
      const std::size_t start_line = cs_.line();
      const std::size_t start_column = cs_.column();

      cs_.consumeOne();

      const std::size_t end_position = cs_.position();
      trivia_.push_back(TriviaPiece{
          TriviaKind::Newline,
          cs_.view(start_position, end_position),
          SourceLoc{
              .start_offset = start_position,
              .end_offset = end_position,
              .start_line = start_line,
              .start_column = start_column,
              .end_line = cs_.line(),
              .end_column = cs_.column(),
          },
      });
      consumed_any = true;
      continue;
    }

    if (std::isspace(static_cast<unsigned char>(c))) {
      const std::size_t start_position = cs_.position();
      const std::size_t start_line = cs_.line();
      const std::size_t start_column = cs_.column();

      while (!cs_.eof()) {
        const char wc = cs_.peek();
        if (wc == '\n' || !std::isspace(static_cast<unsigned char>(wc)))
          break;
        cs_.consumeOne();
      }

      const std::size_t end_position = cs_.position();
      trivia_.push_back(TriviaPiece{
          TriviaKind::Whitespace,
          cs_.view(start_position, end_position),
          SourceLoc{
              .start_offset = start_position,
              .end_offset = end_position,
              .start_line = start_line,
              .start_column = start_column,
              .end_line = cs_.line(),
              .end_column = cs_.column(),
          },
      });
      consumed_any = true;
      continue;
    }

    if (consumeCommentMaybe()) {
      consumed_any = true;
      continue;
    }
  }
}

// consumeCommentMaybe checks whether a comment starts at the current cursor.
// If yes, it consumes full comment, records TriviaPiece, return true
bool Lexer::consumeCommentMaybe() {
  std::span<const frontend::lex::CommentDelimiter> commentDelims =
      langLexConfig_.comments();

  for (auto &delim : commentDelims) {
    // For each delimiter, we'll check if the current cursor is "open"
    auto delimOpen = delim.open;
    // guarding aainst bad config from upstream
    if (delimOpen.empty())
      continue;

    const std::size_t delimSize = delimOpen.size();

    // cursor position
    const std::size_t start_position = cs_.position();
    const std::size_t start_line = cs_.line();
    const std::size_t start_column = cs_.column();

    if (cs_.size() - start_position < delimSize)
      continue;

    std::string_view candidate =
        cs_.view(start_position, start_position + delimSize);
    if (candidate != delimOpen)
      continue;

    // consume opening delimiter
    cs_.advance(delimSize);

    if (delim.kind == frontend::lex::CommentDelimiter::Kind::Line) {
      while (!cs_.eof() && cs_.peek() != '\n')
        cs_.consumeOne();
    } else {
      const std::string_view close = delim.close;

      if (close.empty()) {
        while (!cs_.eof())
          cs_.consumeOne();
      } else {
        while (!cs_.eof()) {
          const std::size_t cur = cs_.position();
          if (cs_.size() - cur >= close.size() &&
              cs_.view(cur, cur + close.size()) == close) {
            cs_.advance(close.size());
            break;
          }
          cs_.consumeOne();
        }
      }
    }

    const std::size_t end_position = cs_.position();
    trivia_.push_back(TriviaPiece{
        TriviaKind::Comment,
        cs_.view(start_position, end_position),
        SourceLoc{
            .start_offset = start_position,
            .end_offset = end_position,
            .start_line = start_line,
            .start_column = start_column,
            .end_line = cs_.line(),
            .end_column = cs_.column(),
        },
    });
    return true;
  }

  return false;
}

// next's invariant must be kept for all lexing functions
// --> always consume at least one char unless EOF

// lexIdentifierOrKeyword lexes alphanumeric char streams, something that can be
// a keyword or just an Identifier, depending on the ILexLanguageRules
Token Lexer::lexIdentifierOrKeyword() {

  std::size_t startPos = cs_.position();
  std::size_t startLine = cs_.line();
  std::size_t startCol = cs_.column();

  while (!cs_.eof() && langLexConfig_.isIdentContinue(cs_.peek())) {
    cs_.consumeOne();
  }

  std::size_t endPos = cs_.position();
  std::size_t endLine = cs_.line();
  std::size_t endCol = cs_.column();

  std::string_view lexeme = cs_.view(startPos, endPos);

  std::optional<frontend::lex::TokenKind> itsAKeyword =
      langLexConfig_.keyword(lexeme);

  if (itsAKeyword) {
    return Token{itsAKeyword.value(), lexeme,
                 SourceLoc{
                     .start_offset = startPos,
                     .end_offset = endPos,
                     .start_line = startLine,
                     .start_column = startCol,
                     .end_line = endLine,
                     .end_column = endCol,
                 },
                 LiteralValue{}};
  }

  return Token{frontend::lex::TokenKind::Identifier, lexeme,
               SourceLoc{
                   .start_offset = startPos,
                   .end_offset = endPos,
                   .start_line = startLine,
                   .start_column = startCol,
                   .end_line = endLine,
                   .end_column = endCol,
               },
               LiteralValue{}};
}

// lexNumber lexes an integer or floating-point numeric stream.
// Returns TokenKind::InvalidNumber if something is malformed.
Token Lexer::lexNumber() {
  const std::size_t startPos = cs_.position();
  const std::size_t startLine = cs_.line();
  const std::size_t startCol = cs_.column();

  bool sawDot = false;
  bool invalid = false;
  while (!cs_.eof()) {
    const char c = cs_.peek();
    if (std::isdigit(static_cast<unsigned char>(c))) {
      cs_.consumeOne();
      continue;
    }

    if (c == '.') {
      if (sawDot)
        invalid = true;
      sawDot = true;
      cs_.consumeOne();
      continue;
    }

    break;
  }

  if (invalid) {
    while (!cs_.eof() && (std::isdigit(static_cast<unsigned char>(cs_.peek())) ||
                          cs_.peek() == '.')) {
      cs_.consumeOne();
    }
  } else {
    // The loop above already consumed the complete numeric spelling.
  }

  const std::size_t endPos = cs_.position();
  const std::size_t endLine = cs_.line();
  const std::size_t endCol = cs_.column();

  const std::string_view lexeme = cs_.view(startPos, endPos);

  if (invalid) {
    return Token{TokenKind::InvalidNumber, lexeme,
                 SourceLoc{
                     .start_offset = startPos,
                     .end_offset = endPos,
                     .start_line = startLine,
                     .start_column = startCol,
                     .end_line = endLine,
                     .end_column = endCol,
                 },
                 LiteralValue{}};
  }

  if (sawDot) {
    const std::string text(lexeme);
    char *end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size()) {
      return Token{TokenKind::InvalidNumber, lexeme,
                   SourceLoc{
                       .start_offset = startPos,
                       .end_offset = endPos,
                       .start_line = startLine,
                       .start_column = startCol,
                       .end_line = endLine,
                       .end_column = endCol,
                   },
                   LiteralValue{}};
    }

    return Token{TokenKind::Float, lexeme,
                 SourceLoc{
                     .start_offset = startPos,
                     .end_offset = endPos,
                     .start_line = startLine,
                     .start_column = startCol,
                     .end_line = endLine,
                     .end_column = endCol,
                 },
                 LiteralValue{parsed}};
  }

  while (!cs_.eof() && std::isdigit(static_cast<unsigned char>(cs_.peek()))) {
    cs_.consumeOne();
  }

  long long parsed = 0;
  const auto parseRes =
      std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), parsed);
  // no error code && we parsed the whole thing
  // We don't throw exception for InvalidNumber here because it's not a runtime
  // failure, it's a use problem and we should handle that in downstream
  // (parser) depending on how language wants to communicate it to the user.
  if (parseRes.ec != std::errc{} ||
      parseRes.ptr != lexeme.data() + lexeme.size()) {
    return Token{TokenKind::InvalidNumber, lexeme,
                 SourceLoc{
                     .start_offset = startPos,
                     .end_offset = endPos,
                     .start_line = startLine,
                     .start_column = startCol,
                     .end_line = endLine,
                     .end_column = endCol,
                 },
                 LiteralValue{}};
  }

  LiteralValue literal{parsed};

  return Token{TokenKind::Integer, lexeme,
               SourceLoc{
                   .start_offset = startPos,
                   .end_offset = endPos,
                   .start_line = startLine,
                   .start_column = startCol,
                   .end_line = endLine,
                   .end_column = endCol,
               },
                literal};
}

// lexPunctOrInvalid lexes punctuation
// returns std::optional<TokenKing> (so it can be invalid)j
Token Lexer::lexPunctOrInvalid() {
  const std::size_t startPos = cs_.position();
  const std::size_t startLine = cs_.line();
  const std::size_t startCol = cs_.column();

  // Check two-char punctuation first (e.g. <=) -- longest match lexing.
  if (cs_.size() - startPos >= 2) {
    const std::string_view two = cs_.view(startPos, startPos + 2);
    if (auto k = langLexConfig_.punctuator(two)) {
      cs_.advance(2);
      return Token{*k, two,
                   SourceLoc{
                       .start_offset = startPos,
                       .end_offset = cs_.position(),
                       .start_line = startLine,
                       .start_column = startCol,
                       .end_line = cs_.line(),
                       .end_column = cs_.column(),
                   },
                   LiteralValue{}};
    }
  }

  // Now check 1-char punctuation (e.g. <)
  const std::string_view one = cs_.view(startPos, startPos + 1);
  if (auto k = langLexConfig_.punctuator(one)) {
    cs_.consumeOne();
    return Token{*k, one,
                 SourceLoc{
                     .start_offset = startPos,
                     .end_offset = cs_.position(),
                     .start_line = startLine,
                     .start_column = startCol,
                     .end_line = cs_.line(),
                     .end_column = cs_.column(),
                 },
                 LiteralValue{}};
  }

  if (!std::isalnum(static_cast<unsigned char>(cs_.peek())) &&
      !std::isspace(static_cast<unsigned char>(cs_.peek()))) {
    cs_.consumeOne();
    return Token{TokenKind::Operator, one,
                 SourceLoc{
                     .start_offset = startPos,
                     .end_offset = cs_.position(),
                     .start_line = startLine,
                     .start_column = startCol,
                     .end_line = cs_.line(),
                     .end_column = cs_.column(),
                 },
                 LiteralValue{}};
  }

  cs_.consumeOne();
  return Token{TokenKind::Invalid, one,
               SourceLoc{
                   .start_offset = startPos,
                   .end_offset = cs_.position(),
                   .start_line = startLine,
                   .start_column = startCol,
                   .end_line = cs_.line(),
                   .end_column = cs_.column(),
               },
               LiteralValue{}};
}

} // namespace frontend::lex
