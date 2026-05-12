// NVPTXCodegen: walk Athens AST, generate LLVM IR for the nvptx64-nvidia-cuda,
// and emit PTX assembly.
//
// Functions named kernel_* become CUDA kernels.

#pragma once

#include "ast.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class NVPTXCodegen {
public:
  NVPTXCodegen(std::vector<std::unique_ptr<FunctionAST>> functions);
  ~NVPTXCodegen() = default;

  bool generate();
  std::string emitPTX();
  std::string emitLLVMIR();

private:
  llvm::LLVMContext context_;
  std::unique_ptr<llvm::Module> module_;
  std::unique_ptr<llvm::IRBuilder<>> builder_;

  std::vector<std::unique_ptr<FunctionAST>> functions_;

  // symbol table: variable name -> alloca
  std::unordered_map<std::string, llvm::AllocaInst *> namedValues_;

  // for kernel functions: mapping from arg index to LLVM pointer parameter
  struct KernelParam {
    std::string name;
    llvm::Value *ptr;
  };
  std::vector<KernelParam> kernelParams_;

  // cached built-in function declarations
  std::unordered_map<std::string, llvm::Function *> builtins_;

  void setupTarget();
  void initTargets();
  llvm::Function *getOrDeclareIntrinsic(const std::string &name,
                                        llvm::Type *retTy,
                                        std::vector<llvm::Type *> paramTys);

  bool isKernel(const std::string &name) const;
  void addKernelMetadata(llvm::Function *func);

  // codegen dispatchers
  bool genTopLevel();
  llvm::Value *gen(ExprAST &expr);
  llvm::Function *genFunction(FunctionAST &func);
  llvm::Value *genBody(ExprAST &body);

  // expression codegen
  llvm::Value *genNumber(NumberExprAST &expr);
  llvm::Value *genVariable(VariableExprAST &expr);
  llvm::Value *genBinary(BinaryExprAST &expr);
  llvm::Value *genUnary(UnaryExprAST &expr);
  llvm::Value *genIf(IfExprAST &expr);
  llvm::Value *genFor(ForExprAST &expr);
  llvm::Value *genCall(CallExprAST &expr);
  llvm::Value *genVar(VarExprAST &expr);

  // built-in handling
  bool isBuiltinCall(const std::string &name) const;
  llvm::Value *genBuiltinCall(CallExprAST &expr);

  NVPTXCodegen(const NVPTXCodegen &) = delete;
  NVPTXCodegen &operator=(const NVPTXCodegen &) = delete;
};
