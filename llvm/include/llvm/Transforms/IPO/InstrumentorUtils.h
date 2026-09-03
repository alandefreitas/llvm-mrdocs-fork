//===-- Transforms/IPO/InstrumentorUtils.h --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// General utilities for the Instrumentor pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_INSTRUMENTOR_UTILS_H
#define LLVM_TRANSFORMS_IPO_INSTRUMENTOR_UTILS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <bitset>
#include <tuple>

namespace llvm {
namespace instrumentor {

struct InstrumentationConfig;
struct InstrumentationOpportunity;

/// An IR builder augmented with extra information for the instrumentor pass.
///
/// The underlying IR builder features an insertion callback to keep track of the new instructions.
struct InstrumentorIRBuilderTy {
  /// Construct an IR builder for the module \p M.
  ///
  /// \param M Module whose IR will be inspected and modified.
  InstrumentorIRBuilderTy(Module &M)
      : M(M), Ctx(M.getContext()),
        IRB(Ctx, ConstantFolder(),
            // Save the inserted instructions in a structure.
            IRBuilderCallbackInserter(
                [&](Instruction *I) { NewInsts[I] = Epoch; })) {}

  /// Destroy the IR builder and remove all erasable instructions cached during
  /// the process of instrumenting.
  ~InstrumentorIRBuilderTy() {
    for (auto *I : ErasableInstructions) {
      if (!I->getType()->isVoidTy())
        I->replaceAllUsesWith(PoisonValue::get(I->getType()));
      I->eraseFromParent();
    }

    // Delete the alloca lists that may have been allocated.
    for (auto &KV : AllocaMap) {
      if (KV.second)
        delete KV.second;
    }
  }

  /// Get a temporary alloca to communicate (large) values with the runtime.
  ///
  /// \param Fn Function in which the alloca is allocated or reused.
  /// \param Ty Allocated type of the temporary.
  /// \param MatchType If true, only reuse an alloca with exactly type \p Ty.
  /// \return A temporary alloca of type \p Ty, newly created or reused.
  AllocaInst *getAlloca(Function *Fn, Type *Ty, bool MatchType = false) {
    const DataLayout &DL = Fn->getDataLayout();
    auto *&AllocaList = AllocaMap[{Fn, DL.getTypeAllocSize(Ty)}];
    if (!AllocaList)
      AllocaList = new AllocaListTy;
    AllocaInst *AI = nullptr;
    for (auto *&ListAI : *AllocaList) {
      if (MatchType && ListAI->getAllocatedType() != Ty)
        continue;
      AI = ListAI;
      ListAI = *AllocaList->rbegin();
      break;
    }
    if (AI)
      AllocaList->pop_back();
    else
      AI = new AllocaInst(Ty, DL.getAllocaAddrSpace(), "",
                          Fn->getEntryBlock().begin());
    UsedAllocas[AI] = AllocaList;
    return AI;
  }

  /// Return the temporary allocas.
  void returnAllocas() {
    for (auto [AI, List] : UsedAllocas)
      List->push_back(AI);
    UsedAllocas.clear();
  }

  /// Save instruction \p I to be erased later. The instructions are erased when
  /// the IR builder is destroyed.
  ///
  /// \param I Instruction to erase when this builder is destroyed.
  void eraseLater(Instruction *I) { ErasableInstructions.insert(I); }

  /// Commonly used values for IR inspection and creation.
  ///{
  Module &M;

  /// LLVM context of the module.
  LLVMContext &Ctx;

  /// Data layout of the module.
  const DataLayout &DL = M.getDataLayout();

  /// Void type for the module context.
  Type *VoidTy = Type::getVoidTy(Ctx);
  /// Opaque pointer type in address space 0.
  PointerType *PtrTy = PointerType::get(Ctx, 0);
  /// 8-bit integer type.
  IntegerType *Int8Ty = Type::getInt8Ty(Ctx);
  /// 32-bit integer type.
  IntegerType *Int32Ty = Type::getInt32Ty(Ctx);
  /// 64-bit integer type.
  IntegerType *Int64Ty = Type::getInt64Ty(Ctx);
  ///}

  /// List of reusable temporary allocas of a given size.
  using AllocaListTy = SmallVector<AllocaInst *>;

  /// Map that holds a list of currently available allocas for a function and
  /// alloca size.
  DenseMap<std::pair<Function *, unsigned>, AllocaListTy *> AllocaMap;

  /// Map that holds the currently used allocas and the list where they belong.
  /// Once an alloca has to be returned, it is returned directly to its list.
  MapVector<AllocaInst *, AllocaListTy *> UsedAllocas;

  /// Instructions that should be erased later.
  SmallPtrSet<Instruction *, 32> ErasableInstructions;

  /// The underlying IR builder with insertion callback.
  IRBuilder<ConstantFolder, IRBuilderCallbackInserter> IRB;

  /// Current instrumentation epoch counter.
  ///
  /// Each instrumentation, e.g., of an instruction, is happening in a dedicated
  /// epoch. The epoch allows to determine if instrumentation instructions were
  /// already around, due to prior instrumentations, or have been introduced to
  /// support the current instrumentation, e.g., compute information about the
  /// current instruction.
  unsigned Epoch = 0;

  /// A mapping from instrumentation instructions to the epoch they have been
  /// created.
  DenseMap<Instruction *, unsigned> NewInsts;
};

/// Caches for instrumentation call argument values.
///
/// The value of an argument may not need to be recomputed between the pre and
/// post instrumentation calls.
struct InstrumentationCaches {
  /// A cache for direct and indirect arguments. The cache is indexed by the
  /// epoch, the instrumentation opportunity name and the argument name. The
  /// result is a value.
  DenseMap<std::tuple<unsigned, StringRef, StringRef>, Value *> DirectArgCache;
  /// Cache for indirect argument values indexed by epoch, opportunity, and name.
  DenseMap<std::tuple<unsigned, StringRef, StringRef>, Value *>
      IndirectArgCache;
};

/// Boolean option bitset with a compile-time number of bits to.
///
/// store as many options as the enumeration type \p EnumTy defines. The enumeration type is expected to have an ascending and consecutive values, starting at zero, and the last value being artificial and named as NumConfig (i.e., the number of values in the enumeration).
template <typename EnumTy> struct BaseConfigTy {
  /// The bistset with as many bits as the enumeration's values.
  std::bitset<static_cast<int>(EnumTy::NumConfig)> Options;

  /// Construct the option bitset with all bits set to \p Enable. If not
  /// provided, all options are enabled.
  ///
  /// \param Enable Initial value for every option bit.
  BaseConfigTy(bool Enable = true) {
    if (Enable)
      Options.set();
  }

  /// Check if the option \p Opt is enabled.
  ///
  /// \param Opt Configuration option to query.
  /// \return True if \p Opt is enabled.
  bool has(EnumTy Opt) const { return Options.test(static_cast<int>(Opt)); }

  /// Set the boolean value of option \p Opt to \p Value.
  ///
  /// \param Opt Configuration option to update.
  /// \param Value New enablement state for \p Opt.
  void set(EnumTy Opt, bool Value = true) {
    Options.set(static_cast<int>(Opt), Value);
  }
};

/// Evaluate a filter expression for an instrumentation opportunity.
///
/// Returns true if the filter passes (or is empty), false otherwise. Dynamic
/// values (non-constants) are assumed to pass.
///
  /// \param V Filter expression value to evaluate.
  /// \param Changed Set to true if IR was modified during evaluation.
  /// \param IO Instrumentation opportunity providing filter context.
  /// \param IConf Instrumentation configuration.
  /// \param IIRB Instrumentor IR builder used if evaluation needs IR.
  /// \return True if the filter passes (or is empty), false otherwise.
LLVM_ABI
bool evaluateFilter(Value &V, bool &Changed, InstrumentationOpportunity &IO,
                    InstrumentationConfig &IConf,
                    InstrumentorIRBuilderTy &IIRB);

} // namespace instrumentor
} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_INSTRUMENTOR_UTILS_H
