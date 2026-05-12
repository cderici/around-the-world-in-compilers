#pragma once

#include "../../../shared/frontend/lex/include/source_loc.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dublin {

enum class TypeKind { Bool, BV };

struct Type {
  TypeKind kind{TypeKind::BV};
  unsigned width{32};
};

enum class ExprKind { Integer, Bool, Variable, Call, Unary, Binary };

struct Expr {
  explicit Expr(ExprKind kind, Type type) : kind(kind), type(type) {}
  virtual ~Expr() = default;

  ExprKind kind;
  Type type;
};

using ExprPtr = std::unique_ptr<Expr>;

struct IntegerExpr final : Expr {
  IntegerExpr(long long value) : Expr(ExprKind::Integer, {TypeKind::BV, 0}), value(value) {}
  long long value;
};

struct BoolExpr final : Expr {
  BoolExpr(bool value) : Expr(ExprKind::Bool, {TypeKind::Bool, 1}), value(value) {}
  bool value;
};

struct VariableExpr final : Expr {
  VariableExpr(std::string name) : Expr(ExprKind::Variable, {TypeKind::BV, 0}), name(std::move(name)) {}
  std::string name;
};

struct CallExpr final : Expr {
  CallExpr(std::string callee, std::vector<ExprPtr> args)
      : Expr(ExprKind::Call, {TypeKind::Bool, 1}), callee(std::move(callee)), args(std::move(args)) {}
  std::string callee;
  std::vector<ExprPtr> args;
};

struct UnaryExpr final : Expr {
  UnaryExpr(std::string op, ExprPtr operand)
      : Expr(ExprKind::Unary, {TypeKind::Bool, 1}), op(std::move(op)), operand(std::move(operand)) {}
  std::string op;
  ExprPtr operand;
};

struct BinaryExpr final : Expr {
  BinaryExpr(std::string op, ExprPtr lhs, ExprPtr rhs)
      : Expr(ExprKind::Binary, {TypeKind::BV, 0}), op(std::move(op)), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
  std::string op;
  ExprPtr lhs;
  ExprPtr rhs;
};

struct Argument {
  std::string name;
  Type type;
};

struct Assertion {
  ExprPtr condition;
  frontend::lex::SourceLoc loc;
};

struct VerifyBlock {
  std::string name;
  std::vector<Argument> args;
  std::vector<Assertion> assertions;
};

} // namespace dublin
