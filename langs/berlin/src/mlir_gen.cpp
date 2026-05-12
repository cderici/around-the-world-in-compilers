#include "berlin_mlir_gen.h"
#include "error.h"

#include <mlir/Dialect/Affine/IR/AffineOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/MemRef/IR/MemRef.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/Verifier.h>
#include <mlir/Support/LogicalResult.h>

#include <llvm/ADT/STLExtras.h>

#include <cstdlib>
#include <iostream>

namespace berlin {

MLIRGen::MLIRGen(mlir::MLIRContext &ctx)
    : ctx_(ctx), builder_(&ctx) {}

// ---------------------------------------------------------------------------
// Top-level: functions and prototypes
// ---------------------------------------------------------------------------

void MLIRGen::genFunction(const PrototypeAST &proto, ExprAST &body) {
  mlir::Location loc = mlir::UnknownLoc::get(&ctx_);

  auto f64Ty = builder_.getF64Type();
  std::vector<mlir::Type> paramTypes(proto.getArgs().size(), f64Ty);
  auto fnType = builder_.getFunctionType(paramTypes, f64Ty);

  auto funcOp = builder_.create<mlir::func::FuncOp>(loc, proto.getName(),
                                                     fnType);
  funcOp.setPrivate();
  module_.push_back(funcOp);

  mlir::Block *entryBlock = funcOp.addEntryBlock();
  builder_.setInsertionPointToStart(entryBlock);

  namedValues_.clear();
  for (auto [idx, arg] : llvm::enumerate(entryBlock->getArguments())) {
    auto alloca = builder_.create<mlir::memref::AllocaOp>(
        loc, mlir::MemRefType::get({}, f64Ty));
    builder_.create<mlir::memref::StoreOp>(loc, arg, alloca);
    namedValues_[proto.getArgs()[idx]] = alloca;
  }

  mlir::Value bodyVal = gen(body);
  if (!bodyVal) {
    funcOp.erase();
    return;
  }

  builder_.create<mlir::func::ReturnOp>(loc, bodyVal);
}

void MLIRGen::genPrototype(const PrototypeAST &proto) {
  mlir::Location loc = mlir::UnknownLoc::get(&ctx_);

  auto f64Ty = builder_.getF64Type();
  std::vector<mlir::Type> paramTypes(proto.getArgs().size(), f64Ty);
  auto fnType = builder_.getFunctionType(paramTypes, f64Ty);

  auto funcOp = builder_.create<mlir::func::FuncOp>(loc, proto.getName(),
                                                     fnType);
  funcOp.setPrivate();
  module_.push_back(funcOp);
}

// ---------------------------------------------------------------------------
// Expression dispatch
// ---------------------------------------------------------------------------

mlir::Value MLIRGen::gen(ExprAST &expr) {
  if (auto *n = dynamic_cast<NumberExprAST *>(&expr))
    return genNumber(*n);
  if (auto *v = dynamic_cast<VariableExprAST *>(&expr))
    return genVariable(*v);
  if (auto *b = dynamic_cast<BinaryExprAST *>(&expr))
    return genBinary(*b);
  if (auto *u = dynamic_cast<UnaryExprAST *>(&expr))
    return genUnary(*u);
  if (auto *i = dynamic_cast<IfExprAST *>(&expr))
    return genIf(*i);
  if (auto *f = dynamic_cast<ForExprAST *>(&expr))
    return genFor(*f);
  if (auto *c = dynamic_cast<CallExprAST *>(&expr))
    return genCall(*c);
  if (auto *v = dynamic_cast<VarExprAST *>(&expr))
    return genVar(*v);
  return nullptr;
}

mlir::Value MLIRGen::defaultF64Zero() {
  auto loc = mlir::UnknownLoc::get(&ctx_);
  auto f64Ty = builder_.getF64Type();
  return builder_.create<mlir::arith::ConstantOp>(
      loc, f64Ty, builder_.getFloatAttr(f64Ty, 0.0));
}

mlir::Value MLIRGen::genNumber(NumberExprAST &expr) {
  auto loc = mlir::UnknownLoc::get(&ctx_);
  auto f64Ty = builder_.getF64Type();
  return builder_.create<mlir::arith::ConstantOp>(
      loc, f64Ty, builder_.getFloatAttr(f64Ty, expr.getVal()));
}

mlir::Value MLIRGen::genVariable(VariableExprAST &expr) {
  auto loc = mlir::UnknownLoc::get(&ctx_);
  auto it = namedValues_.find(expr.getName());
  if (it == namedValues_.end()) {
    std::cerr << "Unknown variable: " << expr.getName() << '\n';
    return nullptr;
  }
  return builder_.create<mlir::memref::LoadOp>(loc, it->second);
}

mlir::Value MLIRGen::genBinary(BinaryExprAST &expr) {
  auto loc = mlir::UnknownLoc::get(&ctx_);

  if (expr.getOp() == '=') {
    auto *lhsVar = dynamic_cast<VariableExprAST *>(&expr.getLHS());
    if (!lhsVar) {
      std::cerr << "LHS of = must be a variable\n";
      return nullptr;
    }

    mlir::Value rhs = gen(expr.getRHS());
    if (!rhs)
      return nullptr;

    auto it = namedValues_.find(lhsVar->getName());
    if (it == namedValues_.end()) {
      std::cerr << "Unknown variable in assignment: " << lhsVar->getName()
                << '\n';
      return nullptr;
    }

    builder_.create<mlir::memref::StoreOp>(loc, rhs, it->second);
    return rhs;
  }

  mlir::Value lhs = gen(expr.getLHS());
  mlir::Value rhs = gen(expr.getRHS());
  if (!lhs || !rhs)
    return nullptr;

  switch (expr.getOp()) {
  case '+':
    return builder_.create<mlir::arith::AddFOp>(loc, lhs, rhs);
  case '-':
    return builder_.create<mlir::arith::SubFOp>(loc, lhs, rhs);
  case '*':
    return builder_.create<mlir::arith::MulFOp>(loc, lhs, rhs);
  case '<': {
    auto cmp = builder_.create<mlir::arith::CmpFOp>(
        loc, mlir::arith::CmpFPredicate::ULT, lhs, rhs);
    auto i64Ty = builder_.getIntegerType(64);
    auto ext = builder_.create<mlir::arith::ExtUIOp>(loc, i64Ty, cmp.getResult());
    return builder_.create<mlir::arith::SIToFPOp>(loc, builder_.getF64Type(), ext);
  }
  default:
    std::cerr << "Unknown binary operator: " << expr.getOp() << '\n';
    return nullptr;
  }
}

mlir::Value MLIRGen::genUnary(UnaryExprAST &expr) {
  auto loc = mlir::UnknownLoc::get(&ctx_);

  if (expr.getOp() == '-') {
    mlir::Value operand = gen(expr.getOperand());
    if (!operand)
      return nullptr;
    auto zero = defaultF64Zero();
    return builder_.create<mlir::arith::SubFOp>(loc, zero, operand);
  }

  std::string fnName = "unary" + std::string(1, expr.getOp());
  mlir::Value operand = gen(expr.getOperand());
  if (!operand)
    return nullptr;

  return builder_
      .create<mlir::func::CallOp>(loc, fnName, builder_.getF64Type(), operand)
      .getResult(0);
}

mlir::Value MLIRGen::genIf(IfExprAST &expr) {
  auto loc = mlir::UnknownLoc::get(&ctx_);

  mlir::Value cond = gen(expr.getCond());
  if (!cond)
    return nullptr;

  auto zero = defaultF64Zero();
  auto i1 = builder_.create<mlir::arith::CmpFOp>(
      loc, mlir::arith::CmpFPredicate::UNE, cond, zero);
  auto f64Ty = builder_.getF64Type();

  auto ifOp = builder_.create<mlir::scf::IfOp>(loc, f64Ty, i1,
                                                /*withElse=*/true);

  builder_.setInsertionPointToStart(ifOp.thenBlock());
  mlir::Value thenVal = gen(expr.getThen());
  if (!thenVal)
    return nullptr;
  builder_.create<mlir::scf::YieldOp>(loc, thenVal);

  builder_.setInsertionPointToStart(ifOp.elseBlock());
  mlir::Value elseVal = gen(expr.getElse());
  if (!elseVal)
    return nullptr;
  builder_.create<mlir::scf::YieldOp>(loc, elseVal);

  builder_.setInsertionPointAfter(ifOp);
  return ifOp.getResult(0);
}

mlir::Value MLIRGen::genFor(ForExprAST &expr) {
  auto loc = mlir::UnknownLoc::get(&ctx_);
  auto f64Ty = builder_.getF64Type();
  auto idxTy = builder_.getIndexType();

  mlir::Value startF64 = gen(expr.getStart());
  mlir::Value endF64 = gen(expr.getEnd());
  if (!startF64 || !endF64)
    return nullptr;

  mlir::Value stepF64;
  if (expr.getStep()) {
    stepF64 = gen(*expr.getStep());
  } else {
    stepF64 = builder_.create<mlir::arith::ConstantOp>(
        loc, f64Ty, builder_.getFloatAttr(f64Ty, 1.0));
  }
  if (!stepF64)
    return nullptr;

  auto i64Ty = builder_.getIntegerType(64);

  auto startI64 =
      builder_.create<mlir::arith::FPToSIOp>(loc, i64Ty, startF64);
  auto endI64 = builder_.create<mlir::arith::FPToSIOp>(loc, i64Ty, endF64);
  auto stepI64 = builder_.create<mlir::arith::FPToSIOp>(loc, i64Ty, stepF64);

  auto lb = builder_.create<mlir::arith::IndexCastOp>(loc, idxTy, startI64);
  auto ub = builder_.create<mlir::arith::IndexCastOp>(loc, idxTy, endI64);
  auto step = builder_.create<mlir::arith::IndexCastOp>(loc, idxTy, stepI64);

  auto forOp = builder_.create<mlir::scf::ForOp>(loc, lb, ub, step);

  mlir::Block *bodyBlock = forOp.getBody();
  builder_.setInsertionPointToStart(bodyBlock);

  // scf.for: 1st block arg is the induction variable, rest are iter args.
  mlir::Value iv = bodyBlock->getArgument(0);

  auto ivI64 = builder_.create<mlir::arith::IndexCastOp>(loc, i64Ty, iv);
  auto ivF64 = builder_.create<mlir::arith::SIToFPOp>(loc, f64Ty, ivI64);

  auto varAlloca = builder_.create<mlir::memref::AllocaOp>(
      loc, mlir::MemRefType::get({}, f64Ty));
  builder_.create<mlir::memref::StoreOp>(loc, ivF64, varAlloca);

  auto shadowed = namedValues_.find(expr.getVarName());
  bool hadShadow = shadowed != namedValues_.end();
  mlir::Value saved;
  if (hadShadow)
    saved = shadowed->second;
  namedValues_[expr.getVarName()] = varAlloca;

  mlir::Value bodyVal = gen(expr.getBodyExpr());
  if (!bodyVal) {
    if (hadShadow)
      namedValues_[expr.getVarName()] = saved;
    else
      namedValues_.erase(expr.getVarName());
    forOp->erase();
    return nullptr;
  }

  if (hadShadow)
    namedValues_[expr.getVarName()] = saved;
  else
    namedValues_.erase(expr.getVarName());

  builder_.setInsertionPointAfter(forOp);
  return defaultF64Zero();
}

mlir::Value MLIRGen::genCall(CallExprAST &expr) {
  auto loc = mlir::UnknownLoc::get(&ctx_);

  std::vector<mlir::Value> args;
  for (auto &arg : expr.getArgs()) {
    mlir::Value argVal = gen(*arg);
    if (!argVal)
      return nullptr;
    args.push_back(argVal);
  }

  auto callOp = builder_.create<mlir::func::CallOp>(loc, expr.getCallee(),
                                                     builder_.getF64Type(),
                                                     args);
  return callOp.getResult(0);
}

mlir::Value MLIRGen::genVar(VarExprAST &expr) {
  auto loc = mlir::UnknownLoc::get(&ctx_);
  auto f64Ty = builder_.getF64Type();

  std::vector<std::pair<std::string, mlir::Value>> oldBindings;

  for (auto &binding : expr.getVarNames()) {
    mlir::Value initVal;
    if (binding.second) {
      initVal = gen(*binding.second);
      if (!initVal)
        return nullptr;
    } else {
      initVal = defaultF64Zero();
    }

    auto alloca = builder_.create<mlir::memref::AllocaOp>(
        loc, mlir::MemRefType::get({}, f64Ty));
    builder_.create<mlir::memref::StoreOp>(loc, initVal, alloca);

    auto it = namedValues_.find(binding.first);
    if (it != namedValues_.end())
      oldBindings.emplace_back(binding.first, it->second);
    else
      oldBindings.emplace_back(binding.first, mlir::Value());

    namedValues_[binding.first] = alloca;
  }

  mlir::Value bodyVal = gen(expr.getBodyExpr());

  for (auto &[name, oldVal] : oldBindings) {
    if (oldVal)
      namedValues_[name] = oldVal;
    else
      namedValues_.erase(name);
  }

  return bodyVal;
}

} // namespace berlin
