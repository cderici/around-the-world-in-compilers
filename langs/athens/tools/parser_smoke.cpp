// parser_smoke is an isolated executable for validating that Athens can create
// the new shared parser pipeline without touching the existing compiler driver.
// It uses AthensLexRules plus AthensParseBuilder so this step proves the shared
// parser can produce Athens' existing AST nodes and run the basic expression
// subset through Athens' existing codegen.

#include "../include/athens_lex_rules.h"
#include "../include/athens_parse_builder.h"
#include "../include/codegen.h"
#include "../../../shared/frontend/lex/include/char_stream.h"
#include "../../../shared/frontend/lex/include/lexer.h"
#include "../../../shared/frontend/parse/include/default_grammar_pack.h"
#include "../../../shared/frontend/parse/include/diagnostics.h"
#include "../../../shared/frontend/parse/include/parse_context.h"
#include "../../../shared/frontend/parse/include/parser_engine.h"
#include "../../../shared/frontend/parse/include/parser_registry.h"
#include "../../../shared/frontend/parse/include/token_stream.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace {

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

void initializeSmokeCodegen() {
  NamedValues.clear();
  Builder.reset();
  TheModule.reset();
  TheContext.reset();

  TheContext = std::make_unique<llvm::LLVMContext>();
  TheModule = std::make_unique<llvm::Module>("Athens Parser Smoke", *TheContext);
  Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);
}

athens::AthensParseBuilder::Expr parseExpression(std::istream &input,
                                                 SmokeDiagnostics &diag) {
  frontend::lex::CharStream chars(input);
  athens::AthensLexRules rules;
  frontend::lex::Lexer lexer(chars, rules);
  frontend::parse::TokenStream tokens(lexer);

  athens::AthensParseBuilder builder;
  frontend::parse::ParserRegistry<athens::AthensParseBuilder> registry;
  frontend::parse::DefaultGrammarPack<
      athens::AthensParseBuilder>::registerExpressionHandlers(registry);

  frontend::parse::ParseContext<athens::AthensParseBuilder> ctx{tokens, builder,
                                                                diag};
  frontend::parse::ParserEngine<athens::AthensParseBuilder> parser(ctx,
                                                                   registry);

  athens::AthensParseBuilder::Expr expr = parser.parseExpression();
  if (!tokens.is(frontend::lex::TokenKind::Eof))
    diag.error(tokens.current().source_loc, "expected EOF after expression");

  return expr;
}

bool expectConstant(std::string_view source, double expected) {
  std::istringstream input{std::string(source)};
  SmokeDiagnostics diag;
  athens::AthensParseBuilder::Expr expr = parseExpression(input, diag);

  if (diag.hadError()) {
    std::cerr << diag.text();
    return false;
  }

  if (!expr) {
    std::cerr << "parser smoke self-test failed\n";
    std::cerr << "expected a non-null Athens ExprAST for: " << source << '\n';
    return false;
  }

  initializeSmokeCodegen();
  llvm::Value *value = expr->codegen();
  auto *constant = llvm::dyn_cast_or_null<llvm::ConstantFP>(value);
  if (!constant) {
    std::cerr << "parser smoke self-test failed\n";
    std::cerr << "expected constant codegen for: " << source << '\n';
    return false;
  }

  const double actual = constant->getValueAPF().convertToDouble();
  if (std::fabs(actual - expected) > 0.000001) {
    std::cerr << "parser smoke self-test failed\n";
    std::cerr << "source:   " << source << '\n';
    std::cerr << "expected: " << expected << '\n';
    std::cerr << "actual:   " << actual << '\n';
    return false;
  }

  std::cout << source << " => " << actual << '\n';
  return true;
}

int runSelfTest() {
  if (!expectConstant("1 + 2 * 3", 7.0))
    return 1;
  if (!expectConstant("(1 + 2) * 3", 9.0))
    return 1;
  if (!expectConstant("4 < 5", 1.0))
    return 1;

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
  athens::AthensParseBuilder::Expr expr = parseExpression(*input, diag);

  if (diag.hadError()) {
    std::cerr << diag.text();
    return 1;
  }

  if (!expr) {
    std::cout << "<parse failed>\n";
    return 1;
  }

  initializeSmokeCodegen();
  llvm::Value *value = expr->codegen();
  if (auto *constant = llvm::dyn_cast_or_null<llvm::ConstantFP>(value)) {
    std::cout << constant->getValueAPF().convertToDouble() << '\n';
    return 0;
  }

  std::cout << "parsed Athens AST expression\n";

  return 0;
}
