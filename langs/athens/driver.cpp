#include "KaleidoscopeJIT.h"
#include "athens_grammar_pack.h"
#include "athens_lex_rules.h"
#include "codegen.h"
#include "../../shared/frontend/lex/include/char_stream.h"
#include "../../shared/frontend/lex/include/lexer.h"
#include "../../shared/frontend/parse/include/diagnostics.h"
#include "../../shared/frontend/parse/include/parse_context.h"
#include "../../shared/frontend/parse/include/parser_engine.h"
#include "../../shared/frontend/parse/include/parser_registry.h"
#include "../../shared/frontend/parse/include/token_stream.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//
extern std::unique_ptr<FunctionPassManager> TheFPM;
extern std::unique_ptr<LoopAnalysisManager> TheLAM;
extern std::unique_ptr<FunctionAnalysisManager> TheFAM;
std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
std::unique_ptr<ModuleAnalysisManager> TheMAM;
std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
std::unique_ptr<StandardInstrumentations> TheSI;
static ExitOnError ExitOnErr;

std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;

void InitializeModuleAndManagers(void) {
  // Open a new context and module
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("Athens Top Module", *TheContext);
  TheModule->setDataLayout(TheJIT->getDataLayout());

  // Create a new builder for the module
  Builder = std::make_unique<IRBuilder<>>(*TheContext);

  // Create new pass and analysis managers
  TheFPM = std::make_unique<llvm::FunctionPassManager>();
  TheLAM = std::make_unique<llvm::LoopAnalysisManager>();
  TheFAM = std::make_unique<llvm::FunctionAnalysisManager>();
  TheCGAM = std::make_unique<llvm::CGSCCAnalysisManager>();
  TheMAM = std::make_unique<llvm::ModuleAnalysisManager>();
  ThePIC = std::make_unique<llvm::PassInstrumentationCallbacks>();
  TheSI =
      std::make_unique<llvm::StandardInstrumentations>(*TheContext,
                                                       /*DebugLogging*/ true);
  TheSI->registerCallbacks(*ThePIC, TheMAM.get());

  // Add transform passes.

  // Promote allocas to registers
  TheFPM->addPass(llvm::PromotePass());

  // Do simple "peephole" optimizations and big-twiddling opts.
  TheFPM->addPass(llvm::InstCombinePass());

  // Reassociate expressions.
  TheFPM->addPass(llvm::ReassociatePass());

  // Eliminate common subexpressions
  TheFPM->addPass(llvm::GVNPass());

  // Simplify the control flow graph (delete unreachable blocks, etc).
  TheFPM->addPass(llvm::SimplifyCFGPass());

  // Register analysis passes used in these transform passes.
  llvm::PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}

static void printIfVerbose(bool verbose, const char *str) {
  if (verbose)
    std::cerr << str;
}

enum class Mode { Run, EmitLLVMIR };

static std::string RuntimePath(const char *argv0) {
  const std::filesystem::path executablePath(argv0);
  const std::filesystem::path executableDir = executablePath.has_parent_path()
                                                 ? executablePath.parent_path()
                                                 : std::filesystem::path(".");

  const std::filesystem::path candidates[] = {
      executableDir / "lib" / "runtime.ath",
      executableDir / "langs" / "athens" / "lib" / "runtime.ath",
      std::filesystem::path("langs") / "athens" / "lib" / "runtime.ath",
  };

  for (const auto &candidate : candidates) {
    if (std::filesystem::exists(candidate))
      return candidate.string();
  }

  return candidates[0].string();
}

static void InstallStandardBinaryOperators() {
  BinopPrecedence.clear();
  BinopPrecedence['='] = 2;
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40; // highest.
}

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

static bool HandleDefinition(std::unique_ptr<FunctionAST> fnAST, Mode mode,
                             bool verbose) {
  if (!fnAST)
    return false;

  if (auto *fnIR = fnAST->codegen()) {
    if (mode == Mode::EmitLLVMIR)
      fnIR->print(outs());

    if (verbose) {
      fprintf(stderr, "Read function definition:\n");
      fnIR->print(errs());
      fprintf(stderr, "\n");
    }

    ExitOnErr(TheJIT->addModule(
        orc::ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
    InitializeModuleAndManagers();
    return true;
  }

  return false;
}

static bool HandleExtern(std::unique_ptr<PrototypeAST> protoAST, Mode mode,
                         bool verbose) {
  if (!protoAST)
    return false;

  const std::string name = protoAST->getName();
  if (auto *fnIR = protoAST->codegen()) {
    if (mode == Mode::EmitLLVMIR)
      fnIR->print(outs());

    if (verbose) {
      fprintf(stderr, "Read extern:\n");
      fnIR->print(errs());
      fprintf(stderr, "\n");
    }

    FunctionProtos[name] = std::move(protoAST);
    return true;
  }

  return false;
}

static bool HandleTopLevelExpression(std::unique_ptr<ExprAST> expr) {
  if (!expr)
    return false;

  auto proto =
      std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
  auto fnAST = std::make_unique<FunctionAST>(std::move(proto), std::move(expr));

  if (!fnAST->codegen())
    return false;

  auto rt = TheJIT->getMainJITDylib().createResourceTracker();
  auto tsm = llvm::orc::ThreadSafeModule(std::move(TheModule),
                                         std::move(TheContext));
  ExitOnErr(TheJIT->addModule(std::move(tsm), rt));
  InitializeModuleAndManagers();

  auto exprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));
  double (*fp)() = exprSymbol.toPtr<double (*)()>();
  fprintf(stderr, "%f\n", fp());

  ExitOnErr(rt->remove());
  return true;
}

static bool ParseStream(std::istream &in, Mode mode, bool verbose) {
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
        if (!HandleDefinition(std::move(item.function), mode, verbose))
          return false;
      } else if (item.prototype) {
        if (!HandleExtern(std::move(item.prototype), mode, verbose))
          return false;
      } else {
        return false;
      }
    } else {
      auto expr = parser.parseExpression();
      if (diag.hadError())
        return false;
      if (!HandleTopLevelExpression(std::move(expr)))
        return false;
    }

    (void)tokens.match(frontend::lex::TokenKind::Semicolon);
  }

  return true;
}

static bool LoadFile(const std::string &path, Mode mode, bool verbose) {
  std::ifstream in(path);
  if (!in.is_open()) {
    std::cerr << "could not open " << path << '\n';
    return false;
  }

  if (!ParseStream(in, mode, verbose))
    return false;

  std::string msg = "\n" + path + " loaded.\n\n";
  printIfVerbose(verbose, msg.c_str());
  return true;
}

static void LoadRepl(Mode mode, bool verbose) {
  std::cerr << "Welcome to Athens!\n> ";

  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream input(line);
    (void)ParseStream(input, mode, verbose);
    std::cerr << "> ";
  }

  std::cerr << '\n';
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

const char *HelpText = R"(Usage: athens [options] [file]

Options:
  --llvmir        Emit LLVM IR instead of executing the program
                  All output except LLVM IR are put in stderr
  -h, --help      Show this help message and exit
  -v, --verbose   Print internal stuff

Arguments:
  file            Athens source file (.ath).
                  If omitted, the REPL starts.

Examples:
  athens foo.ath          Compile and run foo.ath
  athens --llvmir foo.ath Emit LLVM IR for foo.ath on stdout
  athens                  Start the REPL
)";

int main(int argc, char **argv) {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  TheJIT = ExitOnErr(llvm::orc::KaleidoscopeJIT::Create());

  // Make the module, which holds all the code.
  // InitializeModule();
  InitializeModuleAndManagers();

  bool printHelp = false;
  bool verbose = false;

  Mode mode = Mode::Run;
  const char *InputFile = nullptr;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--llvmir") == 0)
      mode = Mode::EmitLLVMIR;
    else if ((std::strcmp(argv[i], "-h") == 0) ||
             (std::strcmp(argv[i], "--help") == 0))
      printHelp = true;
    else if ((std::strcmp(argv[i], "-v") == 0) ||
             (std::strcmp(argv[i], "--verbose") == 0))
      verbose = true;
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

  // Install standard binary operators.
  // 1 is lowest precedence.
  InstallStandardBinaryOperators();

  // Load the runtime support library (written in Athens)
  if (!LoadFile(RuntimePath(argv[0]), Mode::Run, verbose))
    return 1;

  if (InputFile) {
    if (!LoadFile(InputFile, mode, verbose))
      return 1;
  } else {
    LoadRepl(mode, verbose);
  }

  // Print out all of the generated code.
  if (verbose)
    TheModule->print(errs(), nullptr);

  return 0;
}
