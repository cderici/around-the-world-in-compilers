#include "dublin_grammar_pack.h"
#include "dublin_lex_rules.h"

#include "../../../shared/frontend/lex/include/char_stream.h"
#include "../../../shared/frontend/lex/include/lexer.h"
#include "../../../shared/frontend/parse/include/diagnostics.h"
#include "../../../shared/frontend/parse/include/parse_context.h"
#include "../../../shared/frontend/parse/include/token_stream.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

namespace {

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

struct SmtValue {
  std::string name;
  dublin::Type type;
};

struct EmitError {
  std::string message;
};

class SmtEmitter {
public:
  SmtEmitter(const dublin::VerifyBlock &block, std::size_t assertionIndex)
      : block_(block), assertionIndex_(assertionIndex) {}

  std::optional<std::string> emit(EmitError &error) {
    out_ << "module {\n";
    out_ << "  smt.solver () : () -> () {\n";
    out_ << "    smt.set_logic \"QF_BV\"\n";

    for (const auto &arg : block_.args) {
      if (symbols_.contains(arg.name)) {
        error.message = "duplicate argument: " + arg.name;
        return std::nullopt;
      }
      std::string value = nextName(arg.name);
      out_ << "    " << value << " = smt.declare_fun \"" << arg.name
           << "\" : " << typeName(arg.type) << "\n";
      symbols_[arg.name] = {value, arg.type};
    }

    for (std::size_t i = 0; i < assertionIndex_; ++i) {
      auto assumption = emitExpr(*block_.assertions[i].condition, error,
                                 std::nullopt);
      if (!assumption)
        return std::nullopt;
      if (assumption->type.kind != dublin::TypeKind::Bool) {
        error.message = "assertion is not a boolean expression";
        return std::nullopt;
      }
      out_ << "    smt.assert " << assumption->name << "\n";
    }

    auto assertion = emitExpr(*block_.assertions[assertionIndex_].condition,
                              error, std::nullopt);
    if (!assertion)
      return std::nullopt;
    if (assertion->type.kind != dublin::TypeKind::Bool) {
      error.message = "assertion is not a boolean expression";
      return std::nullopt;
    }

    std::string bad = nextName("bad");
    out_ << "    " << bad << " = smt.not " << assertion->name << "\n";
    out_ << "    smt.assert " << bad << "\n";
    out_ << "    smt.check sat {\n";
    out_ << "    } unknown {\n";
    out_ << "    } unsat {\n";
    out_ << "    }\n";
    out_ << "    smt.yield\n";
    out_ << "  }\n";
    out_ << "}\n";
    return out_.str();
  }

private:
  static bool sameType(dublin::Type lhs, dublin::Type rhs) {
    return lhs.kind == rhs.kind && lhs.width == rhs.width;
  }

  static bool isBV(dublin::Type type) { return type.kind == dublin::TypeKind::BV; }

  static std::string typeName(dublin::Type type) {
    if (type.kind == dublin::TypeKind::Bool)
      return "!smt.bool";
    return "!smt.bv<" + std::to_string(type.width) + ">";
  }

  std::string nextName(std::string_view hint) {
    std::string clean;
    for (char c : hint) {
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')
        clean.push_back(c);
    }
    if (clean.empty())
      clean = "v";
    return "%" + clean + std::to_string(nextId_++);
  }

  std::optional<SmtValue> emitExpr(const dublin::Expr &expr, EmitError &error,
                                   std::optional<dublin::Type> expected) {
    switch (expr.kind) {
    case dublin::ExprKind::Integer: {
      auto &integer = static_cast<const dublin::IntegerExpr &>(expr);
      dublin::Type type = expected.value_or(dublin::Type{dublin::TypeKind::BV, 32});
      if (!isBV(type)) {
        error.message = "integer literal used where boolean was expected";
        return std::nullopt;
      }
      std::string value = nextName("c");
      out_ << "    " << value << " = smt.bv.constant #smt.bv<"
           << integer.value << "> : " << typeName(type) << "\n";
      return SmtValue{value, type};
    }
    case dublin::ExprKind::Bool: {
      auto &boolean = static_cast<const dublin::BoolExpr &>(expr);
      std::string value = nextName("b");
      out_ << "    " << value << " = smt.constant "
           << (boolean.value ? "true" : "false") << "\n";
      return SmtValue{value, {dublin::TypeKind::Bool, 1}};
    }
    case dublin::ExprKind::Variable: {
      auto &variable = static_cast<const dublin::VariableExpr &>(expr);
      auto it = symbols_.find(variable.name);
      if (it == symbols_.end()) {
        error.message = "unknown variable: " + variable.name;
        return std::nullopt;
      }
      return it->second;
    }
    case dublin::ExprKind::Unary:
      return emitUnary(static_cast<const dublin::UnaryExpr &>(expr), error,
                       expected);
    case dublin::ExprKind::Binary:
      return emitBinary(static_cast<const dublin::BinaryExpr &>(expr), error,
                        expected);
    case dublin::ExprKind::Call:
      return emitCall(static_cast<const dublin::CallExpr &>(expr), error);
    }
    error.message = "unknown expression";
    return std::nullopt;
  }

  std::optional<SmtValue> emitUnary(const dublin::UnaryExpr &expr,
                                    EmitError &error,
                                    std::optional<dublin::Type> expected) {
    if (expr.op == "!") {
      auto operand = emitExpr(*expr.operand, error,
                              dublin::Type{dublin::TypeKind::Bool, 1});
      if (!operand)
        return std::nullopt;
      if (operand->type.kind != dublin::TypeKind::Bool) {
        error.message = "'!' expects a boolean operand";
        return std::nullopt;
      }
      std::string value = nextName("not");
      out_ << "    " << value << " = smt.not " << operand->name << "\n";
      return SmtValue{value, operand->type};
    }

    if (expr.op == "-") {
      auto operand = emitExpr(*expr.operand, error, expected);
      if (!operand)
        return std::nullopt;
      if (!isBV(operand->type)) {
        error.message = "unary '-' expects a bitvector operand";
        return std::nullopt;
      }
      std::string value = nextName("neg");
      out_ << "    " << value << " = smt.bv.neg " << operand->name << " : "
           << typeName(operand->type) << "\n";
      return SmtValue{value, operand->type};
    }

    error.message = "unsupported unary operator: " + expr.op;
    return std::nullopt;
  }

  std::optional<SmtValue> emitBinary(const dublin::BinaryExpr &expr,
                                     EmitError &error,
                                     std::optional<dublin::Type> expected) {
    if (expr.op == "+" || expr.op == "-" || expr.op == "*") {
      auto lhs = emitExpr(*expr.lhs, error, expected);
      if (!lhs)
        return std::nullopt;
      auto rhs = emitExpr(*expr.rhs, error, lhs->type);
      if (!rhs)
        return std::nullopt;
      if (!sameType(lhs->type, rhs->type) || !isBV(lhs->type)) {
        error.message = "arithmetic operands must have the same bitvector type";
        return std::nullopt;
      }

      const char *op = expr.op == "+" ? "add" : expr.op == "-" ? "sub" : "mul";
      std::string value = nextName(op);
      out_ << "    " << value << " = smt.bv." << op << " " << lhs->name
           << ", " << rhs->name << " : " << typeName(lhs->type) << "\n";
      return SmtValue{value, lhs->type};
    }

    if (expr.op == "==" || expr.op == "!=") {
      auto lhs = emitExpr(*expr.lhs, error, expected);
      if (!lhs)
        return std::nullopt;
      auto rhs = emitExpr(*expr.rhs, error, lhs->type);
      if (!rhs)
        return std::nullopt;
      if (!sameType(lhs->type, rhs->type)) {
        error.message = "equality operands must have the same type";
        return std::nullopt;
      }
      std::string eq = nextName("eq");
      out_ << "    " << eq << " = smt.eq " << lhs->name << ", " << rhs->name
           << " : " << typeName(lhs->type) << "\n";
      if (expr.op == "==")
        return SmtValue{eq, {dublin::TypeKind::Bool, 1}};
      std::string neq = nextName("neq");
      out_ << "    " << neq << " = smt.not " << eq << "\n";
      return SmtValue{neq, {dublin::TypeKind::Bool, 1}};
    }

    if (expr.op == "<" || expr.op == "<=" || expr.op == ">" ||
        expr.op == ">=") {
      auto lhs = emitExpr(*expr.lhs, error, expected);
      if (!lhs)
        return std::nullopt;
      auto rhs = emitExpr(*expr.rhs, error, lhs->type);
      if (!rhs)
        return std::nullopt;
      if (!sameType(lhs->type, rhs->type) || !isBV(lhs->type)) {
        error.message = "comparison operands must have the same bitvector type";
        return std::nullopt;
      }

      std::string pred = expr.op == "<" ? "slt" : expr.op == "<=" ? "sle" :
                         expr.op == ">" ? "sgt" : "sge";
      std::string value = nextName("cmp");
      out_ << "    " << value << " = smt.bv.cmp " << pred << " "
           << lhs->name << ", " << rhs->name << " : " << typeName(lhs->type)
           << "\n";
      return SmtValue{value, {dublin::TypeKind::Bool, 1}};
    }

    error.message = "unsupported binary operator: " + expr.op;
    return std::nullopt;
  }

  std::optional<SmtValue> emitCall(const dublin::CallExpr &expr,
                                   EmitError &error) {
    static const std::map<std::string, std::string> preds = {
        {"ult", "ult"}, {"ule", "ule"}, {"ugt", "ugt"}, {"uge", "uge"}};
    auto pred = preds.find(expr.callee);
    if (pred == preds.end()) {
      error.message = "unsupported call: " + expr.callee;
      return std::nullopt;
    }
    if (expr.args.size() != 2) {
      error.message = expr.callee + " expects two arguments";
      return std::nullopt;
    }

    auto lhs = emitExpr(*expr.args[0], error, std::nullopt);
    if (!lhs)
      return std::nullopt;
    auto rhs = emitExpr(*expr.args[1], error, lhs->type);
    if (!rhs)
      return std::nullopt;
    if (!sameType(lhs->type, rhs->type) || !isBV(lhs->type)) {
      error.message = expr.callee + " operands must have the same bitvector type";
      return std::nullopt;
    }

    std::string value = nextName("ucmp");
    out_ << "    " << value << " = smt.bv.cmp " << pred->second << " "
         << lhs->name << ", " << rhs->name << " : " << typeName(lhs->type)
         << "\n";
    return SmtValue{value, {dublin::TypeKind::Bool, 1}};
  }

  const dublin::VerifyBlock &block_;
  std::size_t assertionIndex_;
  std::map<std::string, SmtValue> symbols_;
  std::ostringstream out_;
  unsigned nextId_{0};
};

std::string shellQuote(const std::string &path) {
  std::string quoted = "'";
  for (char c : path) {
    if (c == '\'')
      quoted += "'\\''";
    else
      quoted += c;
  }
  quoted += "'";
  return quoted;
}

std::string makeTempPath(std::string_view suffix) {
  std::string templ = "/tmp/dublin_XXXXXX";
  std::vector<char> chars(templ.begin(), templ.end());
  chars.push_back('\0');
  int fd = mkstemp(chars.data());
  if (fd >= 0)
    close(fd);
  std::string path(chars.data());
  std::filesystem::rename(path, path + std::string(suffix));
  return path + std::string(suffix);
}

bool writeFile(const std::string &path, const std::string &contents) {
  std::ofstream out(path);
  out << contents;
  return static_cast<bool>(out);
}

std::optional<std::string> exportSmtlib(const std::string &smtMlir,
                                        EmitError &error) {
  std::string mlirPath = makeTempPath(".mlir");
  std::string smt2Path = makeTempPath(".smt2");
  if (!writeFile(mlirPath, smtMlir)) {
    error.message = "could not write temporary SMT MLIR file";
    return std::nullopt;
  }

  std::string command = "mlir-translate-21 --export-smtlib " +
                        shellQuote(mlirPath) + " > " + shellQuote(smt2Path);
  int status = std::system(command.c_str());
  std::filesystem::remove(mlirPath);
  if (status != 0) {
    error.message = "mlir-translate-21 --export-smtlib failed";
    std::filesystem::remove(smt2Path);
    return std::nullopt;
  }

  std::ifstream in(smt2Path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::filesystem::remove(smt2Path);
  return buffer.str();
}

std::optional<std::string> runZ3(const std::string &smt2, EmitError &error) {
  std::string smt2Path = makeTempPath(".smt2");
  if (!writeFile(smt2Path, smt2)) {
    error.message = "could not write temporary SMT-LIB file";
    return std::nullopt;
  }

  std::string command = "z3 " + shellQuote(smt2Path);
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) {
    error.message = "failed to run z3";
    std::filesystem::remove(smt2Path);
    return std::nullopt;
  }

  std::string output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe))
    output += buffer;
  int status = pclose(pipe);
  std::filesystem::remove(smt2Path);
  if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    error.message = "z3 failed";
    return std::nullopt;
  }
  return output;
}

std::unique_ptr<dublin::VerifyBlock> parseFile(const std::string &path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    std::cerr << "could not open " << path << '\n';
    return nullptr;
  }

  frontend::lex::CharStream chars(in);
  dublin::DublinLexRules rules;
  frontend::lex::Lexer lexer(chars, rules);
  frontend::parse::TokenStream tokens(lexer);

  dublin::DublinParseBuilder builder;
  dublin::DublinRegistry registry;
  dublin::DublinGrammarPack::registerHandlers(registry);
  StderrDiagnostics diag;
  frontend::parse::ParseContext<dublin::DublinParseBuilder> ctx{tokens, builder,
                                                                 diag};
  dublin::DublinEngine parser(ctx, registry);

  auto item = parser.parseItem();
  if (diag.hadError() || !item.verify)
    return nullptr;
  if (!tokens.is(frontend::lex::TokenKind::Eof)) {
    std::cerr << "unexpected tokens after verify block\n";
    return nullptr;
  }
  if (item.verify->assertions.empty()) {
    std::cerr << "verify block has no assertions\n";
    return nullptr;
  }
  return std::move(item.verify);
}

const char *HelpText = R"(Usage: dublin [options] <file.dub>

Options:
  -h, --help      Show this help message and exit
  --emit-smt      Print generated MLIR smt dialect for each assertion
  --emit-smt2     Print generated SMT-LIB v2 for each assertion
)";

} // namespace

int main(int argc, char **argv) {
  bool printHelp = false;
  bool emitSmt = false;
  bool emitSmt2 = false;
  const char *inputFile = nullptr;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0)
      printHelp = true;
    else if (std::strcmp(argv[i], "--emit-smt") == 0)
      emitSmt = true;
    else if (std::strcmp(argv[i], "--emit-smt2") == 0)
      emitSmt2 = true;
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
    std::cerr << "error: no input file\n" << HelpText;
    return 1;
  }

  auto block = parseFile(inputFile);
  if (!block)
    return 1;

  bool allVerified = true;
  for (std::size_t i = 0; i < block->assertions.size(); ++i) {
    EmitError error;
    SmtEmitter emitter(*block, i);
    auto smt = emitter.emit(error);
    if (!smt) {
      std::cerr << "error: " << error.message << '\n';
      return 1;
    }

    if (emitSmt) {
      std::cout << *smt;
      continue;
    }

    auto smt2 = exportSmtlib(*smt, error);
    if (!smt2) {
      std::cerr << "error: " << error.message << '\n';
      return 1;
    }

    if (emitSmt2) {
      std::cout << *smt2;
      continue;
    }

    auto z3 = runZ3(*smt2, error);
    if (!z3) {
      std::cerr << "error: " << error.message << '\n';
      return 1;
    }

    std::istringstream lines(*z3);
    std::string firstLine;
    std::getline(lines, firstLine);
    if (firstLine == "unsat") {
      continue;
    }
    if (firstLine == "sat") {
      std::cout << "counterexample: " << block->name << " assertion " << (i + 1)
                << " may fail\n";
      allVerified = false;
      continue;
    }
    if (firstLine == "unknown") {
      std::cout << "unknown: " << block->name << " assertion " << (i + 1)
                << " inconclusive\n";
      return 1;
    }
    std::cerr << "error: unexpected z3 output: " << firstLine << '\n';
    return 1;
  }

  if (emitSmt || emitSmt2)
    return 0;
  if (!allVerified)
    return 1;

  std::cout << "verified: " << block->name << "\n";
  return 0;
}
