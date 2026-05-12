// Berlin MLIRGen: walks Athens AST and produces MLIR ops
// (affine.for, func.func, etc.). Reuses the full Athens frontend.

#pragma once

#include "ast.h"
#include <mlir/Dialect/Affine/IR/AffineOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/MemRef/IR/MemRef.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/Location.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/Value.h>
#include <string>
#include <unordered_map>

namespace berlin {

class MLIRGen {
public:
  explicit MLIRGen(mlir::MLIRContext &ctx);

  void genFunction(const PrototypeAST &proto, ExprAST &body);
  void genPrototype(const PrototypeAST &proto);

  void setModule(mlir::ModuleOp module) { module_ = module; }
  mlir::ModuleOp module() const { return module_; }

private:
  mlir::Value gen(ExprAST &expr);
  mlir::Value genNumber(NumberExprAST &expr);
  mlir::Value genVariable(VariableExprAST &expr);
  mlir::Value genBinary(BinaryExprAST &expr);
  mlir::Value genUnary(UnaryExprAST &expr);
  mlir::Value genIf(IfExprAST &expr);
  mlir::Value genFor(ForExprAST &expr);
  mlir::Value genCall(CallExprAST &expr);
  mlir::Value genVar(VarExprAST &expr);

  mlir::Value defaultF64Zero();

  MLIRGen &operator=(const MLIRGen &) = delete;
  MLIRGen(const MLIRGen &) = delete;

  mlir::MLIRContext &ctx_;
  mlir::OpBuilder builder_;
  mlir::ModuleOp module_;
  std::unordered_map<std::string, mlir::Value> namedValues_;
};

} // namespace berlin
