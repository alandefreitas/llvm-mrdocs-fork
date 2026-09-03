//===- Function.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_FUNCTION_H
#define LLVM_SANDBOXIR_FUNCTION_H

#include "llvm/IR/Function.h"
#include "llvm/SandboxIR/Constant.h"
#include "llvm/Support/Compiler.h"

namespace llvm::sandboxir {

/// SandboxIR wrapper around an LLVM Function.
class Function : public GlobalWithNodeAPI<Function, llvm::Function,
                                          GlobalObject, llvm::GlobalObject> {
  /// Helper for mapped_iterator.
  struct LLVMBBToBB {
    Context &Ctx;
    LLVMBBToBB(Context &Ctx) : Ctx(Ctx) {}
    BasicBlock &operator()(llvm::BasicBlock &LLVMBB) const {
      return *cast<BasicBlock>(Ctx.getValue(&LLVMBB));
    }
  };
  /// Use Context::createFunction() instead.
  Function(llvm::Function *F, sandboxir::Context &Ctx)
      : GlobalWithNodeAPI(ClassID::Function, F, Ctx) {}
  friend class Context; // For constructor.

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for Function.
  /// \Returns True if \p From is a Function.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::Function;
  }

  /// Return the enclosing module, or null if none.
  /// \Returns The parent Module, or null if none.
  Module *getParent() {
    return Ctx.getModule(cast<llvm::Function>(Val)->getParent());
  }

  /// Return the argument at the given index.
  /// \param Idx Zero-based argument index.
  /// \Returns The Argument at \p Idx.
  Argument *getArg(unsigned Idx) const {
    llvm::Argument *Arg = cast<llvm::Function>(Val)->getArg(Idx);
    return cast<Argument>(Ctx.getValue(Arg));
  }

  /// Return the number of arguments.
  /// \Returns The number of arguments.
  size_t arg_size() const { return cast<llvm::Function>(Val)->arg_size(); }
  /// Return true if this function has no arguments.
  /// \Returns True if this function has no arguments.
  bool arg_empty() const { return cast<llvm::Function>(Val)->arg_empty(); }

  /// Iterator over basic blocks in this function.
  using iterator = mapped_iterator<llvm::Function::iterator, LLVMBBToBB>;
  /// Return an iterator to the first basic block.
  /// \Returns An iterator to the first basic block.
  iterator begin() const {
    LLVMBBToBB BBGetter(Ctx);
    return iterator(cast<llvm::Function>(Val)->begin(), BBGetter);
  }
  /// Return an iterator to the past-the-end position.
  /// \Returns An iterator to the past-the-end position.
  iterator end() const {
    LLVMBBToBB BBGetter(Ctx);
    return iterator(cast<llvm::Function>(Val)->end(), BBGetter);
  }
  /// Return the function type of this function.
  /// \Returns The FunctionType of this function.
  LLVM_ABI FunctionType *getFunctionType() const;

  /// Returns the alignment of the given function.
  /// \Returns The alignment of this function, if set.
  MaybeAlign getAlign() const { return cast<llvm::Function>(Val)->getAlign(); }

  // TODO: Add missing: setAligment(Align)

  /// Sets the alignment attribute of the Function.
  /// This method will be deprecated as the alignment property should always be
  /// defined.
  /// \param Align Alignment to set, or none to clear it.
  LLVM_ABI void setAlignment(MaybeAlign Align);

#ifndef NDEBUG
  /// Verify that this wraps an LLVM Function.
  void verify() const final {
    assert(isa<llvm::Function>(Val) && "Expected Function!");
  }
  /// Dump the function name and argument list to \p OS.
  /// \param OS Output stream.
  LLVM_ABI void dumpNameAndArgs(raw_ostream &OS) const;
  /// Dump this function to \p OS.
  /// \param OS Output stream.
  LLVM_ABI void dumpOS(raw_ostream &OS) const final;
#endif
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_FUNCTION_H
