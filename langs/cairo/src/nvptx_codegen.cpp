// NVPTXCodegen: walks Athens AST, generates LLVM IR targeting nvptx64-nvidia-cuda,
// emits PTX text. Functions named kernel_* become CUDA kernel entries.

#include "nvptx_codegen.h"

#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

#include <cstdlib>
#include <iostream>

// ---------------------------------------------------------------------------
// Construction and setup
// ---------------------------------------------------------------------------

NVPTXCodegen::NVPTXCodegen(std::vector<std::unique_ptr<FunctionAST>> functions)
    : functions_(std::move(functions)) {
  module_ = std::make_unique<llvm::Module>("Cairo NVPTX Module", context_);
  builder_ = std::make_unique<llvm::IRBuilder<>>(context_);
  setupTarget();
}

void NVPTXCodegen::initTargets() {
  LLVMInitializeNVPTXTargetInfo();
  LLVMInitializeNVPTXTarget();
  LLVMInitializeNVPTXTargetMC();
  LLVMInitializeNVPTXAsmPrinter();
}

void NVPTXCodegen::setupTarget() {
  initTargets();

  module_->setTargetTriple("nvptx64-nvidia-cuda");

  std::string error;
  auto *target = llvm::TargetRegistry::lookupTarget("nvptx64-nvidia-cuda",
                                                     error);
  if (!target) {
    std::cerr << "NVPTX target not found: " << error << "\n";
    return;
  }

  auto options = llvm::TargetOptions();
  auto *targetMachine = target->createTargetMachine(
      "nvptx64-nvidia-cuda", "sm_50", "", options, llvm::Reloc::PIC_,
      std::nullopt, llvm::CodeGenOptLevel::Aggressive);

  module_->setDataLayout(targetMachine->createDataLayout());
}

llvm::Function *NVPTXCodegen::getOrDeclareIntrinsic(
    const std::string &name, llvm::Type *retTy,
    std::vector<llvm::Type *> paramTys) {

  auto it = builtins_.find(name);
  if (it != builtins_.end())
    return it->second;

  auto *fnType = llvm::FunctionType::get(retTy, paramTys, false);
  auto *func = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage,
                                       name, module_.get());
  builtins_[name] = func;
  return func;
}

// ---------------------------------------------------------------------------
// Kernel detection and metadata
// ---------------------------------------------------------------------------

bool NVPTXCodegen::isKernel(const std::string &name) const {
  return name.starts_with("kernel_");
}

void NVPTXCodegen::addKernelMetadata(llvm::Function *func) {
  auto *annotations =
      module_->getOrInsertNamedMetadata("nvvm.annotations");

  llvm::Metadata *ops[] = {
      llvm::ValueAsMetadata::get(func),
      llvm::MDString::get(context_, "kernel"),
      llvm::ValueAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1))};
  annotations->addOperand(llvm::MDNode::get(context_, ops));
}

// ---------------------------------------------------------------------------
// Top-level generation
// ---------------------------------------------------------------------------

bool NVPTXCodegen::generate() { return genTopLevel(); }

bool NVPTXCodegen::genTopLevel() {
  for (auto &func : functions_) {
    if (!genFunction(*func))
      return false;
  }
  return true;
}

llvm::Function *NVPTXCodegen::genFunction(FunctionAST &func) {
  auto &proto = func.getProto();
  auto name = proto.getName();

  if (isKernel(name)) {
    // kernel function: all arguments become double* (global memory pointers)
    auto ptrTy = llvm::PointerType::get(context_, 1); // addrspace(1) = global
    std::vector<llvm::Type *> paramTypes(proto.getArgs().size(), ptrTy);
    auto *voidTy = llvm::Type::getVoidTy(context_);
    auto *fnType = llvm::FunctionType::get(voidTy, paramTypes, false);

    auto *llvmFunc =
        llvm::Function::Create(fnType, llvm::Function::ExternalLinkage,
                                name, module_.get());

    // set up entry block and store params
    auto *entry = llvm::BasicBlock::Create(context_, "entry", llvmFunc);
    builder_->SetInsertPoint(entry);

    kernelParams_.clear();
    for (auto [idx, arg] : llvm::enumerate(llvmFunc->args())) {
      arg.setName(proto.getArgs()[idx]);
      kernelParams_.push_back({proto.getArgs()[idx], &arg});
    }

    // generate body expression (result discarded — kernel returns void)
    if (!genBody(func.getBody()))
      return nullptr;

    builder_->CreateRetVoid();

    // verify
    if (llvm::verifyFunction(*llvmFunc, &llvm::outs())) {
      std::cerr << "kernel function verification failed\n";
      llvmFunc->print(llvm::errs());
      llvmFunc->eraseFromParent();
      return nullptr;
    }

    addKernelMetadata(llvmFunc);
    return llvmFunc;
  }

  // device function: standard Athens signature double(double, ...)
  auto f64Ty = llvm::Type::getDoubleTy(context_);
  std::vector<llvm::Type *> paramTypes(proto.getArgs().size(), f64Ty);
  auto *fnType = llvm::FunctionType::get(f64Ty, paramTypes, false);

  auto *llvmFunc = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage,
                                           name, module_.get());

  auto *entry = llvm::BasicBlock::Create(context_, "entry", llvmFunc);
  builder_->SetInsertPoint(entry);

  namedValues_.clear();
  for (auto [idx, arg] : llvm::enumerate(llvmFunc->args())) {
    arg.setName(proto.getArgs()[idx]);
    auto *alloca = builder_->CreateAlloca(f64Ty, nullptr, arg.getName());
    builder_->CreateStore(&arg, alloca);
    namedValues_[std::string(arg.getName())] = alloca;
  }

  llvm::Value *bodyVal = gen(func.getBody());
  if (!bodyVal) {
    llvmFunc->eraseFromParent();
    return nullptr;
  }

  builder_->CreateRet(bodyVal);

  if (llvm::verifyFunction(*llvmFunc, &llvm::outs())) {
    std::cerr << "function verification failed\n";
    llvmFunc->print(llvm::errs());
    llvmFunc->eraseFromParent();
    return nullptr;
  }

  return llvmFunc;
}

llvm::Value *NVPTXCodegen::gen(ExprAST &expr) {
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

llvm::Value *NVPTXCodegen::genBody(ExprAST &body) { return gen(body); }

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

llvm::Value *NVPTXCodegen::genNumber(NumberExprAST &expr) {
  return llvm::ConstantFP::get(context_, llvm::APFloat(expr.getVal()));
}

llvm::Value *NVPTXCodegen::genVariable(VariableExprAST &expr) {
  auto it = namedValues_.find(expr.getName());
  if (it == namedValues_.end()) {
    std::cerr << "unknown variable: " << expr.getName() << "\n";
    return nullptr;
  }
  return builder_->CreateLoad(builder_->getDoubleTy(), it->second,
                               expr.getName());
}

llvm::Value *NVPTXCodegen::genBinary(BinaryExprAST &expr) {
  if (expr.getOp() == '=') {
    auto *lhsVar = dynamic_cast<VariableExprAST *>(&expr.getLHS());
    if (!lhsVar) {
      std::cerr << "LHS of = must be a variable\n";
      return nullptr;
    }
    llvm::Value *rhs = gen(expr.getRHS());
    if (!rhs)
      return nullptr;
    auto it = namedValues_.find(lhsVar->getName());
    if (it == namedValues_.end()) {
      std::cerr << "unknown variable in assignment\n";
      return nullptr;
    }
    builder_->CreateStore(rhs, it->second);
    return rhs;
  }

  llvm::Value *lhs = gen(expr.getLHS());
  llvm::Value *rhs = gen(expr.getRHS());
  if (!lhs || !rhs)
    return nullptr;

  switch (expr.getOp()) {
  case '+':
    return builder_->CreateFAdd(lhs, rhs, "addtmp");
  case '-':
    return builder_->CreateFSub(lhs, rhs, "subtmp");
  case '*':
    return builder_->CreateFMul(lhs, rhs, "multmp");
  case '<': {
    auto *cmp = builder_->CreateFCmpULT(lhs, rhs, "cmptmp");
    auto *ext = builder_->CreateUIToFP(cmp, builder_->getDoubleTy(), "booltmp");
    return ext;
  }
  default:
    std::cerr << "unknown binary operator\n";
    return nullptr;
  }
}

llvm::Value *NVPTXCodegen::genUnary(UnaryExprAST &expr) {
  if (expr.getOp() == '-') {
    llvm::Value *operand = gen(expr.getOperand());
    if (!operand)
      return nullptr;
    auto *zero = llvm::ConstantFP::get(context_, llvm::APFloat(0.0));
    return builder_->CreateFSub(zero, operand, "negtmp");
  }
  std::cerr << "unknown unary operator\n";
  return nullptr;
}

llvm::Value *NVPTXCodegen::genIf(IfExprAST &expr) {
  auto f64Ty = builder_->getDoubleTy();
  auto *zero = llvm::ConstantFP::get(context_, llvm::APFloat(0.0));

  llvm::Value *cond = gen(expr.getCond());
  if (!cond)
    return nullptr;

  auto *condCmp = builder_->CreateFCmpUNE(cond, zero, "ifcond");

  auto *func = builder_->GetInsertBlock()->getParent();
  auto *thenBB = llvm::BasicBlock::Create(context_, "then", func);
  auto *elseBB = llvm::BasicBlock::Create(context_, "else");
  auto *mergeBB = llvm::BasicBlock::Create(context_, "ifcont");

  builder_->CreateCondBr(condCmp, thenBB, elseBB);

  // then block
  builder_->SetInsertPoint(thenBB);
  llvm::Value *thenVal = gen(expr.getThen());
  if (!thenVal)
    return nullptr;
  builder_->CreateBr(mergeBB);
  thenBB = builder_->GetInsertBlock();

  // else block
  func->insert(func->end(), elseBB);
  builder_->SetInsertPoint(elseBB);
  llvm::Value *elseVal = gen(expr.getElse());
  if (!elseVal)
    return nullptr;
  builder_->CreateBr(mergeBB);
  elseBB = builder_->GetInsertBlock();

  // merge block
  func->insert(func->end(), mergeBB);
  builder_->SetInsertPoint(mergeBB);
  auto *phi = builder_->CreatePHI(f64Ty, 2, "iftmp");
  phi->addIncoming(thenVal, thenBB);
  phi->addIncoming(elseVal, elseBB);

  return phi;
}

llvm::Value *NVPTXCodegen::genFor(ForExprAST &expr) {
  auto f64Ty = builder_->getDoubleTy();
  auto i64Ty = builder_->getInt64Ty();
  auto *zeroFP = llvm::ConstantFP::get(context_, llvm::APFloat(0.0));

  llvm::Value *start = gen(expr.getStart());
  llvm::Value *end = gen(expr.getEnd());
  if (!start || !end)
    return nullptr;

  llvm::Value *step;
  if (expr.getStep()) {
    step = gen(*expr.getStep());
    if (!step)
      return nullptr;
  } else {
    step = zeroFP;
  }

  auto *func = builder_->GetInsertBlock()->getParent();

  // Allocate and initialize loop variable
  auto *varAlloca = builder_->CreateAlloca(f64Ty, nullptr, expr.getVarName());
  builder_->CreateStore(start, varAlloca);

  // Convert to i64 for loop logic
  auto *startI64 = builder_->CreateFPToSI(start, i64Ty);
  auto *endI64 = builder_->CreateFPToSI(end, i64Ty);
  auto *stepI64 = builder_->CreateFPToSI(step, i64Ty);

  // loop header block
  auto *preheaderBB = builder_->GetInsertBlock();
  auto *loopBB = llvm::BasicBlock::Create(context_, "loop", func);
  auto *afterBB = llvm::BasicBlock::Create(context_, "afterloop");

  builder_->CreateBr(loopBB);
  builder_->SetInsertPoint(loopBB);

  // phi for loop induction variable (i64)
  auto *iv = builder_->CreatePHI(i64Ty, 2, "iv");
  iv->addIncoming(startI64, preheaderBB);

  // store induction variable as double
  auto *ivFP = builder_->CreateSIToFP(iv, f64Ty);
  builder_->CreateStore(ivFP, varAlloca);

  namedValues_[expr.getVarName()] = varAlloca;

  // generate loop body
  llvm::Value *bodyVal = gen(expr.getBodyExpr());
  if (!bodyVal)
    return nullptr;

  // step and branch
  auto *nextVal = builder_->CreateAdd(iv, stepI64, "next");
  auto *endCmp = builder_->CreateICmpSLT(nextVal, endI64, "loopcond");
  builder_->CreateCondBr(endCmp, loopBB, afterBB);
  iv->addIncoming(nextVal, builder_->GetInsertBlock());

  // after loop block
  func->insert(func->end(), afterBB);
  builder_->SetInsertPoint(afterBB);

  namedValues_.erase(expr.getVarName());
  return zeroFP;
}

// ---------------------------------------------------------------------------
// Calls and built-ins
// ---------------------------------------------------------------------------

bool NVPTXCodegen::isBuiltinCall(const std::string &name) const {
  return name == "cairoThreadX" || name == "cairoBlockX" ||
         name == "cairoDimX" || name == "cairoLoad" ||
         name == "cairoStore";
}

llvm::Value *NVPTXCodegen::genBuiltinCall(CallExprAST &expr) {
  auto &name = expr.getCallee();

  if (name == "cairoThreadX") {
    auto *fn = getOrDeclareIntrinsic("llvm.nvvm.read.ptx.sreg.tid.x",
                                      builder_->getInt32Ty(), {});
    auto *val = builder_->CreateCall(fn);
    return builder_->CreateSIToFP(val, builder_->getDoubleTy(), "tid");
  }

  if (name == "cairoBlockX") {
    auto *fn = getOrDeclareIntrinsic("llvm.nvvm.read.ptx.sreg.ctaid.x",
                                      builder_->getInt32Ty(), {});
    auto *val = builder_->CreateCall(fn);
    return builder_->CreateSIToFP(val, builder_->getDoubleTy(), "bid");
  }

  if (name == "cairoDimX") {
    auto *fn = getOrDeclareIntrinsic("llvm.nvvm.read.ptx.sreg.ntid.x",
                                      builder_->getInt32Ty(), {});
    auto *val = builder_->CreateCall(fn);
    return builder_->CreateSIToFP(val, builder_->getDoubleTy(), "dim");
  }

  if (name == "cairoLoad") {
    // args: ptr_index (literal), elem_index (expression)
    if (expr.getArgs().size() != 2) {
      std::cerr << "cairoLoad expects 2 args\n";
      return nullptr;
    }

    auto *ptrIdxNum = dynamic_cast<NumberExprAST *>(expr.getArgs()[0].get());
    if (!ptrIdxNum) {
      std::cerr << "cairoLoad: first arg must be a number literal\n";
      return nullptr;
    }
    unsigned ptrIdx = static_cast<unsigned>(ptrIdxNum->getVal());

    llvm::Value *elemIdx = gen(*expr.getArgs()[1]);
    if (!elemIdx)
      return nullptr;
    auto *elemI64 = builder_->CreateFPToSI(elemIdx, builder_->getInt64Ty(),
                                            "elem");

    if (ptrIdx >= kernelParams_.size()) {
      std::cerr << "cairoLoad: ptr index " << ptrIdx << " out of range\n";
      return nullptr;
    }

    auto *ptr = builder_->CreateGEP(builder_->getDoubleTy(),
                                     kernelParams_[ptrIdx].ptr, elemI64, "gep");
    return builder_->CreateLoad(builder_->getDoubleTy(), ptr, "load");
  }

  if (name == "cairoStore") {
    // args: ptr_index (literal), elem_index (expression), value (expression)
    if (expr.getArgs().size() != 3) {
      std::cerr << "cairoStore expects 3 args\n";
      return nullptr;
    }

    auto *ptrIdxNum = dynamic_cast<NumberExprAST *>(expr.getArgs()[0].get());
    if (!ptrIdxNum) {
      std::cerr << "cairoStore: first arg must be a number literal\n";
      return nullptr;
    }
    unsigned ptrIdx = static_cast<unsigned>(ptrIdxNum->getVal());

    llvm::Value *elemIdx = gen(*expr.getArgs()[1]);
    if (!elemIdx)
      return nullptr;
    auto *elemI64 = builder_->CreateFPToSI(elemIdx, builder_->getInt64Ty(),
                                            "elem");

    llvm::Value *val = gen(*expr.getArgs()[2]);
    if (!val)
      return nullptr;

    if (ptrIdx >= kernelParams_.size()) {
      std::cerr << "cairoStore: ptr index " << ptrIdx << " out of range\n";
      return nullptr;
    }

    auto *ptr = builder_->CreateGEP(builder_->getDoubleTy(),
                                     kernelParams_[ptrIdx].ptr, elemI64, "gep");
    builder_->CreateStore(val, ptr);
    return val;
  }

  return nullptr;
}

llvm::Value *NVPTXCodegen::genCall(CallExprAST &expr) {
  if (isBuiltinCall(expr.getCallee()))
    return genBuiltinCall(expr);

  // regular function call
  auto *callee = module_->getFunction(expr.getCallee());
  if (!callee) {
    std::cerr << "unknown function: " << expr.getCallee() << "\n";
    return nullptr;
  }

  std::vector<llvm::Value *> args;
  for (auto &arg : expr.getArgs()) {
    llvm::Value *argVal = gen(*arg);
    if (!argVal)
      return nullptr;
    args.push_back(argVal);
  }

  return builder_->CreateCall(callee, args, "calltmp");
}

llvm::Value *NVPTXCodegen::genVar(VarExprAST &expr) {
  auto f64Ty = builder_->getDoubleTy();

  std::vector<std::pair<std::string, llvm::AllocaInst *>> oldBindings;

  for (auto &binding : expr.getVarNames()) {
    llvm::Value *initVal;
    if (binding.second) {
      initVal = gen(*binding.second);
      if (!initVal)
        return nullptr;
    } else {
      initVal = llvm::ConstantFP::get(context_, llvm::APFloat(0.0));
    }

    auto *alloca = builder_->CreateAlloca(f64Ty, nullptr, binding.first);
    builder_->CreateStore(initVal, alloca);

    auto it = namedValues_.find(binding.first);
    oldBindings.emplace_back(binding.first,
                              it != namedValues_.end() ? it->second : nullptr);

    namedValues_[binding.first] = alloca;
  }

  llvm::Value *body = gen(expr.getBodyExpr());

  for (auto &[name, oldVal] : oldBindings) {
    if (oldVal)
      namedValues_[name] = oldVal;
    else
      namedValues_.erase(name);
  }

  return body;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

std::string NVPTXCodegen::emitLLVMIR() {
  std::string str;
  llvm::raw_string_ostream stream(str);
  module_->print(stream, nullptr);
  return str;
}

std::string NVPTXCodegen::emitPTX() {
  std::string error;
  auto *target = llvm::TargetRegistry::lookupTarget("nvptx64-nvidia-cuda",
                                                     error);
  if (!target) {
    std::cerr << "NVPTX target not found: " << error << "\n";
    return {};
  }

  auto options = llvm::TargetOptions();
  auto *targetMachine = target->createTargetMachine(
      "nvptx64-nvidia-cuda", "sm_50", "", options, llvm::Reloc::PIC_,
      std::nullopt, llvm::CodeGenOptLevel::Aggressive);

  llvm::SmallVector<char, 0> buffer;
  llvm::raw_svector_ostream stream(buffer);

  llvm::legacy::PassManager passManager;
  targetMachine->addPassesToEmitFile(passManager, stream, nullptr,
                                      llvm::CodeGenFileType::AssemblyFile);
  passManager.run(*module_);

  return std::string(buffer.data(), buffer.size());
}
