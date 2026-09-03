//===- MemoryOpRemark.h - Memory operation remark analysis -*- C++ ------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provide more information about instructions that copy, move, or initialize
// memory, including those with a "auto-init" !annotation metadata.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_MEMORYOPREMARK_H
#define LLVM_TRANSFORMS_UTILS_MEMORYOPREMARK_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include <optional>

namespace llvm {

class CallInst;
class DataLayout;
class DiagnosticInfoIROptimization;
class Instruction;
class IntrinsicInst;
class Value;
class OptimizationRemarkEmitter;
class StoreInst;

// FIXME: Once we get to more remarks like this one, we need to re-evaluate how
// much of this logic should actually go into the remark emitter.
/// Emit optimization remarks for instructions that copy, move, or initialize
/// memory.
struct LLVM_ABI MemoryOpRemark {
  /// Emitter used to report memory-operation remarks.
  OptimizationRemarkEmitter &ORE;
  /// Pass name attached to emitted remarks.
  StringRef RemarkPass;
  /// Data layout used to compute store and operand sizes.
  const DataLayout &DL;
  /// Target library info used to recognize known memory libcalls.
  const TargetLibraryInfo &TLI;

  /// Construct a remark helper bound to the given emitter and analyses.
  /// @param ORE Emitter that receives generated remarks.
  /// @param RemarkPass Pass name recorded on each remark.
  /// @param DL Data layout used for size calculations.
  /// @param TLI Library info used to identify known memory calls.
  MemoryOpRemark(OptimizationRemarkEmitter &ORE, StringRef RemarkPass,
                 const DataLayout &DL, const TargetLibraryInfo &TLI)
      : ORE(ORE), RemarkPass(RemarkPass), DL(DL), TLI(TLI) {}

  /// Destroy the remark helper.
  virtual ~MemoryOpRemark();

  /// Return true if \p I is a memory operation this helper can remark on.
  /// @param I Instruction to test.
  /// @param TLI Library info used to recognize known memory libcalls.
  /// @return True when \p I is a supported store, intrinsic, or libcall.
  static bool canHandle(const Instruction *I, const TargetLibraryInfo &TLI);

  /// Emit a remark describing the memory operation performed by \p I.
  /// @param I Instruction previously accepted by canHandle.
  void visit(const Instruction *I);

protected:
  /// Return a short explanation string for a memory operation of kind \p Type.
  /// @param Type Human-readable kind label (e.g. "Store" or "Initialization").
  /// @return Explanation text included in the remark message.
  virtual std::string explainSource(StringRef Type) const;

  /// Classification of the memory operation being remarked on.
  enum RemarkKind {
    RK_Store,         ///< Plain store instruction.
    RK_Unknown,       ///< Unrecognized or generic memory operation.
    RK_IntrinsicCall, ///< Known memory intrinsic (memcpy, memset, etc.).
    RK_Call           ///< Known library call (memcpy, bzero, etc.).
  };
  /// Return the stable remark identifier for kind \p RK.
  /// @param RK Kind of memory operation being remarked on.
  /// @return Remark name string for diagnostics and serialized remarks.
  virtual StringRef remarkName(RemarkKind RK) const;

  /// Return the diagnostic category used when constructing remarks.
  /// @return Diagnostic kind for emitted memory-operation remarks.
  virtual DiagnosticKind diagnosticKind() const { return DK_OptimizationRemarkAnalysis; }

private:
  template<typename ...Ts>
  std::unique_ptr<DiagnosticInfoIROptimization> makeRemark(Ts... Args);

  /// Emit a remark using information from the store's destination, size, etc.
  void visitStore(const StoreInst &SI);
  /// Emit a generic auto-init remark.
  void visitUnknown(const Instruction &I);
  /// Emit a remark using information from known intrinsic calls.
  void visitIntrinsicCall(const IntrinsicInst &II);
  /// Emit a remark using information from known function calls.
  void visitCall(const CallInst &CI);

  /// Add callee information to a remark: whether it's known, the function name,
  /// etc.
  template <typename FTy>
  void visitCallee(FTy F, bool KnownLibCall, DiagnosticInfoIROptimization &R);
  /// Add operand information to a remark based on knowledge we have for known
  /// libcalls.
  void visitKnownLibCall(const CallInst &CI, LibFunc LF,
                         DiagnosticInfoIROptimization &R);
  /// Add the memory operation size to a remark.
  void visitSizeOperand(Value *V, DiagnosticInfoIROptimization &R);

  struct VariableInfo {
    std::optional<StringRef> Name;
    std::optional<uint64_t> Size;
    bool isEmpty() const { return !Name && !Size; }
  };
  /// Gather more information about \p V as a variable. This can be debug info,
  /// information from the alloca, etc. Since \p V can represent more than a
  /// single variable, they will all be added to the remark.
  void visitPtr(Value *V, bool IsSrc, DiagnosticInfoIROptimization &R);
  void visitVariable(const Value *V, SmallVectorImpl<VariableInfo> &Result);
};

/// Special case for -ftrivial-auto-var-init remarks.
struct LLVM_ABI AutoInitRemark : public MemoryOpRemark {
  /// Construct an auto-init remark helper bound to the given emitter and
  /// analyses.
  /// @param ORE Emitter that receives generated remarks.
  /// @param RemarkPass Pass name recorded on each remark.
  /// @param DL Data layout used for size calculations.
  /// @param TLI Library info used to identify known memory calls.
  AutoInitRemark(OptimizationRemarkEmitter &ORE, StringRef RemarkPass,
                 const DataLayout &DL, const TargetLibraryInfo &TLI)
      : MemoryOpRemark(ORE, RemarkPass, DL, TLI) {}

  /// Return true if \p I is annotated as trivial auto-init.
  /// @param I Instruction to test for auto-init annotation metadata.
  /// @return True when \p I carries an "auto-init" annotation.
  static bool canHandle(const Instruction *I);

protected:
  /// Return an explanation noting the operation came from
  /// -ftrivial-auto-var-init.
  /// @param Type Human-readable kind label (e.g. "Store" or "Initialization").
  /// @return Explanation text included in the remark message.
  std::string explainSource(StringRef Type) const override;
  /// Return the auto-init-specific remark identifier for kind \p RK.
  /// @param RK Kind of memory operation being remarked on.
  /// @return Remark name string for auto-init diagnostics.
  StringRef remarkName(RemarkKind RK) const override;
  /// Return the missed-optimization diagnostic category for auto-init remarks.
  /// @return DK_OptimizationRemarkMissed for auto-init diagnostics.
  DiagnosticKind diagnosticKind() const override {
    return DK_OptimizationRemarkMissed;
  }
};

} // namespace llvm

#endif
