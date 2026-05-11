#include "KaleidoscopeJIT.h"
#include "athens_grammar_pack.h"
#include "athens_lex_rules.h"
#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "../../shared/frontend/lex/include/char_stream.h"
#include "../../shared/frontend/lex/include/lexer.h"
#include "../../shared/frontend/parse/include/diagnostics.h"
#include "../../shared/frontend/parse/include/parse_context.h"
#include "../../shared/frontend/parse/include/parser_engine.h"
#include "../../shared/frontend/parse/include/parser_registry.h"
#include "../../shared/frontend/parse/include/token_stream.h"
#include <fstream>
#include <iostream>

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

static void HandleDefinition(Mode mode, bool verbose) {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()) {
      if (mode == Mode::EmitLLVMIR) {
        FnIR->print(outs());
      }

      if (verbose) {
        fprintf(stderr, "Read function definition:\n");
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }
      ExitOnErr(TheJIT->addModule(
          orc::ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
      InitializeModuleAndManagers();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern(Mode mode, bool verbose) {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()) {

      if (mode == Mode::EmitLLVMIR) {
        FnIR->print(outs());
      }

      if (verbose) {
        fprintf(stderr, "Read extern:\n");
        FnIR->print(errs());
        fprintf(stderr, "\n");
      }
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression(Mode mode, bool verbose) {
  (void)mode;
  (void)verbose;

  // Evaluate a top-level expression into an anonymous function.
  if (auto FnAST = ParseTopLevelExpr()) {
    if (FnAST->codegen()) {
      // Create a ResourceTracker to track JITted memory allocated to our
      // anonymous expression -- that way we can free it after executing.
      auto RT = TheJIT->getMainJITDylib().createResourceTracker();

      auto TSM = llvm::orc::ThreadSafeModule(std::move(TheModule),
                                             std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
      InitializeModuleAndManagers();

      // Search the JIT for the __anon_expr symbol.
      auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));

      // Get the symbol's address and cast it to the right type (takes no
      // arguments, returns a double) so we can call it as a native function.

      double (*FP)() = ExprSymbol.toPtr<double (*)()>();
      fprintf(stderr, "%f\n", FP());

      // FnIR->print(errs());
      // fprintf(stderr, "\n");
      //
      // // Remove the anonymous expression.
      // FnIR->eraseFromParent();

      // Delete the anonymous expression module from the JIT.
      ExitOnErr(RT->remove());
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static bool HandleSharedDefinition(std::unique_ptr<FunctionAST> fnAST,
                                   Mode mode, bool verbose) {
  if (!fnAST)
    return false;

  if (auto *fnIR = fnAST->codegen()) {
    if (mode == Mode::EmitLLVMIR)
      fnIR->print(outs());

    if (verbose) {
      fprintf(stderr, "Read function definition with shared parser:\n");
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

static bool HandleSharedExtern(std::unique_ptr<PrototypeAST> protoAST,
                               Mode mode, bool verbose) {
  if (!protoAST)
    return false;

  const std::string name = protoAST->getName();
  if (auto *fnIR = protoAST->codegen()) {
    if (mode == Mode::EmitLLVMIR)
      fnIR->print(outs());

    if (verbose) {
      fprintf(stderr, "Read extern with shared parser:\n");
      fnIR->print(errs());
      fprintf(stderr, "\n");
    }

    FunctionProtos[name] = std::move(protoAST);
    return true;
  }

  return false;
}

static bool HandleSharedTopLevelExpression(std::unique_ptr<ExprAST> expr) {
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

static bool LoadFileSharedParser(const std::string &path, Mode mode,
                                 bool verbose) {
  std::ifstream in(path);
  if (!in.is_open()) {
    std::string msg = "could not open " + path + "\n";
    printIfVerbose(verbose, msg.c_str());
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
        if (!HandleSharedDefinition(std::move(item.function), mode, verbose))
          return false;
      } else if (item.prototype) {
        if (!HandleSharedExtern(std::move(item.prototype), mode, verbose))
          return false;
      } else {
        return false;
      }
    } else {
      auto expr = parser.parseExpression();
      if (diag.hadError())
        return false;
      if (!HandleSharedTopLevelExpression(std::move(expr)))
        return false;
    }

    (void)tokens.match(frontend::lex::TokenKind::Semicolon);
  }

  std::string msg = "\n" + path + " loaded with shared parser.\n\n";
  printIfVerbose(verbose, msg.c_str());
  return true;
}

static void lexerLoop(bool isRepl, Mode mode, bool verbose) {
  if (isRepl)
    fprintf(stderr, "Welcome to Athens!\n> ");

  getNextToken();

  while (CurTok != Token::eof) {

    if (isRepl) {
      fprintf(stderr, "> ");
    }

    if (CurTok == static_cast<Token>(';')) {
      // Ignore top-level semicolons.
      getNextToken();
      continue;
    }

    switch (CurTok) {
    case Token::def:
      HandleDefinition(mode, verbose);
      break;
    case Token::extern_:
      HandleExtern(mode, verbose);
      break;
    default:
      HandleTopLevelExpression(mode, verbose);
      break;
    }
  }
}

/// top ::= definition | external | expression | ';'
static void LoadRepl(Mode mode, bool verbose) {
  // Make sure the lexer is reading STDIN
  lexer::ResetLexerInputStreamToSTDIN();

  lexerLoop(true, mode, verbose);
}

static void LoadFile(const std::string &Path, Mode mode, bool verbose) {
  std::ifstream in(Path);
  if (!in.is_open()) {
    std::string msg = "could not open " + Path + "\n";
    printIfVerbose(verbose, msg.c_str());
    return;
  }
  lexer::SetLexerInputStream(in);

  lexerLoop(false, mode, verbose);

  std::string msg = "\n" + Path + " loaded.\n\n";
  printIfVerbose(verbose, msg.c_str());

  // "in" goes out of scope when LoadFile returns, and CurIn inside the lexer
  // becomes a dangling pointer
  lexer::ResetLexerInputStreamToSTDIN();
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

const char *HelpText = R"(Usage: athens [options] [file]

Options:
  --llvmir        Emit LLVM IR instead of executing the program
                  All output except LLVM IR are put in stderr
  --new-parser    Use the shared lexer/parser path for file input
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
  bool useNewParser = false;

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
    else if (std::strcmp(argv[i], "--new-parser") == 0)
      useNewParser = true;
    else
      InputFile = argv[i];
  }

  if (printHelp) {
    std::cout << HelpText;
    return 0;
  }

  // Install standard binary operators.
  // 1 is lowest precedence.
  InstallStandardBinaryOperators();

  if (useNewParser && !InputFile) {
    std::cerr << "--new-parser currently supports file input only; REPL still "
                 "uses the old parser.\n";
    return 1;
  }

  // Load the runtime support library (written in Athens)
  if (useNewParser) {
    if (!LoadFileSharedParser("langs/athens/lib/runtime.ath", Mode::Run,
                              verbose))
      return 1;
  } else {
    LoadFile("langs/athens/lib/runtime.ath", Mode::Run, verbose);
  }

  if (InputFile) {
    if (useNewParser) {
      if (!LoadFileSharedParser(InputFile, mode, verbose))
        return 1;
    } else {
      LoadFile(InputFile, mode, verbose);
    }
  } else {
    // Run the main "interpreter loop" now.
    LoadRepl(mode, verbose);
  }

  // Print out all of the generated code.
  if (verbose)
    TheModule->print(errs(), nullptr);

  return 0;
}
