// lexer_smoke is an isolated executable for validating the shared lexer against
// Athens language rules outside the compiler driver.

#include "../../../shared/frontend/lex/include/char_stream.h"
#include "../../../shared/frontend/lex/include/lexer.h"
#include "../../../shared/frontend/lex/include/token.h"
#include "../include/athens_lex_rules.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

const char *tokenKindName(frontend::lex::TokenKind kind) {
  using frontend::lex::TokenKind;

  switch (kind) {
  case TokenKind::Invalid:
    return "Invalid";
  case TokenKind::InvalidNumber:
    return "InvalidNumber";
  case TokenKind::Eof:
    return "Eof";
  case TokenKind::Identifier:
    return "Identifier";
  case TokenKind::Integer:
    return "Integer";
  case TokenKind::Float:
    return "Float";
  case TokenKind::LParen:
    return "LParen";
  case TokenKind::RParen:
    return "RParen";
  case TokenKind::Comma:
    return "Comma";
  case TokenKind::Semicolon:
    return "Semicolon";
  case TokenKind::Plus:
    return "Plus";
  case TokenKind::Minus:
    return "Minus";
  case TokenKind::Star:
    return "Star";
  case TokenKind::Slash:
    return "Slash";
  case TokenKind::Equal:
    return "Equal";
  case TokenKind::Less:
    return "Less";
  case TokenKind::LessEqual:
    return "LessEqual";
  case TokenKind::Greater:
    return "Greater";
  case TokenKind::GreaterEqual:
    return "GreaterEqual";
  case TokenKind::LogicNot:
    return "LogicNot";
  case TokenKind::Operator:
    return "Operator";
  case TokenKind::KwFuncDef:
    return "KwFuncDef";
  case TokenKind::KwExtern:
    return "KwExtern";
  case TokenKind::KwIf:
    return "KwIf";
  case TokenKind::KwThen:
    return "KwThen";
  case TokenKind::KwElse:
    return "KwElse";
  case TokenKind::KwFor:
    return "KwFor";
  case TokenKind::KwIn:
    return "KwIn";
  case TokenKind::KwBinaryOp:
    return "KwBinaryOp";
  case TokenKind::KwUnaryOp:
    return "KwUnaryOp";
  case TokenKind::KwVar:
    return "KwVar";
  }

  return "<unknown>";
}

void dumpTokens(std::istream &input, std::ostream &output) {
  frontend::lex::CharStream chars(input);
  athens::AthensLexRules rules;
  frontend::lex::Lexer lexer(chars, rules);

  while (true) {
    const frontend::lex::Token token = lexer.next();
    output << token.source_loc.start_line << ':'
           << token.source_loc.start_column << ' ' << tokenKindName(token.kind)
           << " `" << token.lexeme << "`\n";

    if (token.kind == frontend::lex::TokenKind::Eof)
      break;
  }
}

int runSelfTest() {
  constexpr const char *source = "def add(x y) x + y;\n";
  constexpr const char *expected = "1:1 KwFuncDef `def`\n"
                                   "1:5 Identifier `add`\n"
                                   "1:8 LParen `(`\n"
                                   "1:9 Identifier `x`\n"
                                   "1:11 Identifier `y`\n"
                                   "1:12 RParen `)`\n"
                                   "1:14 Identifier `x`\n"
                                   "1:16 Plus `+`\n"
                                   "1:18 Identifier `y`\n"
                                   "1:19 Semicolon `;`\n"
                                   "2:1 Eof ``\n";

  std::istringstream input(source);
  std::ostringstream actual;
  dumpTokens(input, actual);

  const std::string actualText = actual.str();
  std::cout << actualText;

  if (actualText != expected) {
    std::cerr << "lexer smoke self-test failed\n";
    std::cerr << "expected:\n" << expected;
    return 1;
  }

  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--self-test")
    return runSelfTest();

  std::ifstream file;
  std::istream *input = &std::cin;

  if (argc > 1) {
    file.open(argv[1]);
    if (!file.is_open()) {
      std::cerr << "could not open " << argv[1] << '\n';
      return 1;
    }
    input = &file;
  }

  dumpTokens(*input, std::cout);

  return 0;
}
