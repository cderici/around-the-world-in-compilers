// parser_smoke is an isolated executable for validating that Athens can create
// the new shared parser pipeline without touching the existing compiler driver.
// It uses AthensLexRules plus a tiny local builder so this step proves wiring,
// not final Athens AST construction; the real AST builder comes next.

#include "../include/athens_lex_rules.h"
#include "../../../shared/frontend/lex/include/char_stream.h"
#include "../../../shared/frontend/lex/include/lexer.h"
#include "../../../shared/frontend/parse/include/default_grammar_pack.h"
#include "../../../shared/frontend/parse/include/diagnostics.h"
#include "../../../shared/frontend/parse/include/parse_context.h"
#include "../../../shared/frontend/parse/include/parser_engine.h"
#include "../../../shared/frontend/parse/include/parser_registry.h"
#include "../../../shared/frontend/parse/include/token_stream.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

struct SmokeExpr {
  std::string text;
};

struct SmokeBuilder {
  using Expr = SmokeExpr;
  using Stmt = SmokeExpr;
  using Item = SmokeExpr;

  Expr makeIdentifier(frontend::lex::Token tok) {
    return Expr{"id:" + std::string(tok.lexeme)};
  }

  Expr makeInteger(frontend::lex::Token, long long value) {
    return Expr{"int:" + std::to_string(value)};
  }

  Expr makeBinary(frontend::lex::Token opTok, Expr lhs, Expr rhs) {
    return Expr{"(" + std::string(opTok.lexeme) + " " + std::move(lhs.text) +
                " " + std::move(rhs.text) + ")"};
  }

  Expr makeErrorExpr(frontend::lex::SourceLoc) { return Expr{"<error>"}; }
};

class SmokeDiagnostics final : public frontend::parse::IDiagnostics {
public:
  void error(const frontend::lex::SourceLoc &loc,
             std::string_view message) override {
    hadError_ = true;
    output_ << loc.start_line << ':' << loc.start_column << ": " << message
            << '\n';
  }

  bool hadError() const { return hadError_; }
  std::string text() const { return output_.str(); }

private:
  bool hadError_{false};
  std::ostringstream output_;
};

SmokeExpr parseExpression(std::istream &input, SmokeDiagnostics &diag) {
  frontend::lex::CharStream chars(input);
  athens::AthensLexRules rules;
  frontend::lex::Lexer lexer(chars, rules);
  frontend::parse::TokenStream tokens(lexer);

  SmokeBuilder builder;
  frontend::parse::ParserRegistry<SmokeBuilder> registry;
  frontend::parse::DefaultGrammarPack<SmokeBuilder>::registerExpressionHandlers(
      registry);

  frontend::parse::ParseContext<SmokeBuilder> ctx{tokens, builder, diag};
  frontend::parse::ParserEngine<SmokeBuilder> parser(ctx, registry);

  SmokeExpr expr = parser.parseExpression();
  if (!tokens.is(frontend::lex::TokenKind::Eof))
    diag.error(tokens.current().source_loc, "expected EOF after expression");

  return expr;
}

int runSelfTest() {
  constexpr const char *source = "x + 2 * y\n";
  constexpr const char *expected = "(+ id:x (* int:2 id:y))";

  std::istringstream input(source);
  SmokeDiagnostics diag;
  const SmokeExpr expr = parseExpression(input, diag);

  std::cout << expr.text << '\n';

  if (diag.hadError()) {
    std::cerr << diag.text();
    return 1;
  }

  if (expr.text != expected) {
    std::cerr << "parser smoke self-test failed\n";
    std::cerr << "expected: " << expected << '\n';
    std::cerr << "actual:   " << expr.text << '\n';
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

  SmokeDiagnostics diag;
  const SmokeExpr expr = parseExpression(*input, diag);
  std::cout << expr.text << '\n';

  if (diag.hadError()) {
    std::cerr << diag.text();
    return 1;
  }

  return 0;
}
