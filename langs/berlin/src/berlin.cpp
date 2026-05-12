// Berlin driver: uses Athens frontend (shared lex/parse) + MLIR back-end.

#include "athens_grammar_pack.h"
#include "athens_lex_rules.h"
#include "berlin_mlir_gen.h"

#include "../../../shared/frontend/lex/include/char_stream.h"
#include "../../../shared/frontend/lex/include/lexer.h"
#include "../../../shared/frontend/parse/include/diagnostics.h"
#include "../../../shared/frontend/parse/include/parse_context.h"
#include "../../../shared/frontend/parse/include/parser_engine.h"
#include "../../../shared/frontend/parse/include/parser_registry.h"
#include "../../../shared/frontend/parse/include/token_stream.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

#include <mlir/Dialect/Affine/IR/AffineOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/MemRef/IR/MemRef.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/AsmState.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Diagnostics.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/Verifier.h>
#include <mlir/Support/LogicalResult.h>

#include <llvm/Support/raw_ostream.h>

#include <map>
#include <memory>

// ---------------------------------------------------------------------------
// Diagnostics (shared parser errors -> stderr)
// ---------------------------------------------------------------------------

class StderrDiagnostics final : public frontend::parse::IDiagnostics {
public:
  void error(const frontend::lex::SourceLoc &loc,
             std::string_view message) override {
    hadError_ = true;
    std::cerr << loc.start_line << ':' << loc.start_column << ": " << message
              << '\n';
  }

  bool hadError() const { return hadError_; }

private:
  bool hadError_{false};
};

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

static void InstallStandardBinaryOperators() {
  BinopPrecedence.clear();
  BinopPrecedence['='] = 2;
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;
}

static bool ParseFile(const std::string &path,
                      berlin::MLIRGen &mlirGen) {
  std::ifstream in(path);
  if (!in.is_open()) {
    std::cerr << "could not open " << path << '\n';
    return false;
  }

  frontend::lex::CharStream chars(in);
  athens::AthensLexRules rules;
  frontend::lex::Lexer lexer(chars, rules);
  frontend::parse::TokenStream tokens(lexer);

  athens::AthensParseBuilder builder;
  athens::AthensRegistry registry;
  athens::registerAthensGrammar(registry);
  StderrDiagnostics diag;
  frontend::parse::ParseContext<athens::AthensParseBuilder> ctx{tokens, builder,
                                                                 diag};
  athens::AthensEngine parser(ctx, registry);

  while (!tokens.is(frontend::lex::TokenKind::Eof)) {
    if (tokens.match(frontend::lex::TokenKind::Semicolon))
      continue;

    if (tokens.is(frontend::lex::TokenKind::KwFuncDef) ||
        tokens.is(frontend::lex::TokenKind::KwExtern)) {
      athens::AthensParseBuilder::Item item = parser.parseItem();
      if (diag.hadError())
        return false;

      if (item.function) {
        mlirGen.genFunction(item.function->getProto(),
                            item.function->getBody());
      } else if (item.prototype) {
        mlirGen.genPrototype(*item.prototype);
      } else {
        std::cerr << "unexpected top-level item\n";
        return false;
      }
    } else {
      std::cerr << "unexpected top-level token\n";
      return false;
    }

    (void)tokens.match(frontend::lex::TokenKind::Semicolon);
  }

  return true;
}

// ---------------------------------------------------------------------------
// MLIR infrastructure
// ---------------------------------------------------------------------------

static std::unique_ptr<mlir::MLIRContext> CreateMLIRContext() {
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::affine::AffineDialect,
                  mlir::func::FuncDialect, mlir::scf::SCFDialect,
                  mlir::memref::MemRefDialect>();
  auto context = std::make_unique<mlir::MLIRContext>(registry);
  context->getOrLoadDialect<mlir::arith::ArithDialect>();
  context->getOrLoadDialect<mlir::affine::AffineDialect>();
  context->getOrLoadDialect<mlir::func::FuncDialect>();
  context->getOrLoadDialect<mlir::scf::SCFDialect>();
  context->getOrLoadDialect<mlir::memref::MemRefDialect>();
  return context;
}

// ---------------------------------------------------------------------------
// Help text
// ---------------------------------------------------------------------------

const char *HelpText = R"(Usage: berlin [options] <file>

Options:
  -h, --help      Show this help message and exit

Arguments:
  file            Athens source file (.ath) to compile to MLIR.

Examples:
  berlin foo.ath  Compile foo.ath to MLIR and print the result on stdout.
)";

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
  bool printHelp = false;
  const char *InputFile = nullptr;

  for (int i = 1; i < argc; ++i) {
    if ((std::strcmp(argv[i], "-h") == 0) ||
        (std::strcmp(argv[i], "--help") == 0))
      printHelp = true;
    else if (std::string_view(argv[i]).starts_with("-")) {
      std::cerr << "unknown option: " << argv[i] << '\n';
      return 1;
    } else {
      InputFile = argv[i];
    }
  }

  if (printHelp) {
    std::cout << HelpText;
    return 0;
  }

  if (!InputFile) {
    std::cerr << "error: no input file\n";
    std::cerr << HelpText;
    return 1;
  }

  InstallStandardBinaryOperators();

  auto context = CreateMLIRContext();
  berlin::MLIRGen mlirGen(*context);

  // Create an empty module op.
  mlir::OpBuilder builder(context.get());
  auto moduleOp = builder.create<mlir::ModuleOp>(
      mlir::UnknownLoc::get(context.get()));
  mlirGen.setModule(moduleOp);

  if (!ParseFile(InputFile, mlirGen))
    return 1;

  // Verify and print.
  if (mlir::failed(mlir::verify(moduleOp))) {
    std::cerr << "MLIR verification failed\n";
    moduleOp->print(llvm::errs());
    return 1;
  }

  mlir::OpPrintingFlags flags;
  moduleOp.print(llvm::outs(), flags);
  llvm::outs() << '\n';

  return 0;
}
