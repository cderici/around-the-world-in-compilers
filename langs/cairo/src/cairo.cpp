// Cairo driver: reads Athens source, compiles to PTX via LLVM NVPTX backend.
// Functions named kernel_* become CUDA kernel entries.

#include "athens_grammar_pack.h"
#include "athens_lex_rules.h"
#include "cuda_driver_launcher.h"
#include "nvptx_codegen.h"

#include "../../../shared/frontend/lex/include/char_stream.h"
#include "../../../shared/frontend/lex/include/lexer.h"
#include "../../../shared/frontend/parse/include/diagnostics.h"
#include "../../../shared/frontend/parse/include/parse_context.h"
#include "../../../shared/frontend/parse/include/parser_engine.h"
#include "../../../shared/frontend/parse/include/parser_registry.h"
#include "../../../shared/frontend/parse/include/token_stream.h"

#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

// ---------------------------------------------------------------------------
// Diagnostics
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

struct KernelInfo {
  std::string name;
  unsigned argCount{0};
};

static KernelInfo FindFirstKernel(
    const std::vector<std::unique_ptr<FunctionAST>> &functions) {
  for (const auto &function : functions) {
    const auto &proto = function->getProto();
    if (proto.getName().starts_with("kernel_"))
      return {proto.getName(), static_cast<unsigned>(proto.getArgs().size())};
  }
  return {};
}

static std::vector<std::unique_ptr<FunctionAST>>
ParseFile(const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    std::cerr << "could not open " << path << '\n';
    return {};
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

  std::vector<std::unique_ptr<FunctionAST>> functions;

  while (!tokens.is(frontend::lex::TokenKind::Eof)) {
    if (tokens.match(frontend::lex::TokenKind::Semicolon))
      continue;

    if (tokens.is(frontend::lex::TokenKind::KwFuncDef) ||
        tokens.is(frontend::lex::TokenKind::KwExtern)) {
      athens::AthensParseBuilder::Item item = parser.parseItem();
      if (diag.hadError())
        return {};

      if (item.function) {
        functions.push_back(std::move(item.function));
      } else if (item.prototype) {
        // for Cairo, we only care about functions with bodies
        // (extern prototypes are ignored)
        (void)item.prototype;
      } else {
        std::cerr << "unexpected top-level item\n";
        return {};
      }
    } else {
      std::cerr << "unexpected top-level token\n";
      return {};
    }

    (void)tokens.match(frontend::lex::TokenKind::Semicolon);
  }

  return functions;
}

// ---------------------------------------------------------------------------
// Help text
// ---------------------------------------------------------------------------

const char *HelpText = R"(Usage: cairo [options] <file>

Options:
  -h, --help      Show this help message and exit
  --emit-llvm     Print LLVM IR instead of PTX
  --run           Launch first kernel_* function on GPU (requires CUDA=1 build)
  --run-size N    Number of generated double elements for --run (default: 16)

Arguments:
  file            Athens source file (.ath) to compile to PTX.

Examples:
  cairo foo.ath               Compile foo.ath to PTX and print it
  cairo --emit-llvm foo.ath   Print LLVM IR for the NVPTX target
  cairo --run foo.ath         Compile and launch the first kernel_* on GPU
)";

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
  bool printHelp = false;
  bool emitLLVM = false;
  bool runOnGPU = false;
  std::size_t runSize = 16;
  const char *inputFile = nullptr;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-h") == 0 ||
        std::strcmp(argv[i], "--help") == 0)
      printHelp = true;
    else if (std::strcmp(argv[i], "--emit-llvm") == 0)
      emitLLVM = true;
    else if (std::strcmp(argv[i], "--run") == 0)
      runOnGPU = true;
    else if (std::strcmp(argv[i], "--run-size") == 0) {
      if (i + 1 >= argc) {
        std::cerr << "error: --run-size requires a value\n";
        return 1;
      }
      char *end = nullptr;
      unsigned long value = std::strtoul(argv[++i], &end, 10);
      if (!end || *end != '\0' || value == 0) {
        std::cerr << "error: --run-size must be a positive integer\n";
        return 1;
      }
      runSize = value;
    }
    else if (std::string_view(argv[i]).starts_with("-")) {
      std::cerr << "unknown option: " << argv[i] << '\n';
      return 1;
    } else {
      inputFile = argv[i];
    }
  }

  if (printHelp) {
    std::cout << HelpText;
    return 0;
  }

  if (!inputFile) {
    std::cerr << "error: no input file\n";
    std::cerr << HelpText;
    return 1;
  }

  InstallStandardBinaryOperators();

  auto functions = ParseFile(inputFile);
  if (functions.empty()) {
    std::cerr << "error: no functions parsed\n";
    return 1;
  }

  KernelInfo kernel = FindFirstKernel(functions);
  if (runOnGPU && kernel.name.empty()) {
    std::cerr << "error: --run requires at least one function named kernel_*\n";
    return 1;
  }

  NVPTXCodegen codegen(std::move(functions));
  if (!codegen.generate()) {
    std::cerr << "code generation failed\n";
    return 1;
  }

  if (emitLLVM) {
    std::cout << codegen.emitLLVMIR() << '\n';
    return 0;
  }

  std::string ptx = codegen.emitPTX();
  if (ptx.empty()) {
    std::cerr << "PTX emission failed\n";
    return 1;
  }

  if (runOnGPU) {
    CUDALauncher launcher;
    if (!launcher.loadPTX(ptx))
      return 1;
    if (!launcher.runKernel(kernel.name, kernel.argCount, runSize))
      return 1;
    return 0;
  }

  std::cout << ptx;

  return 0;
}
