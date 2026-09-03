//===- Verifier.h - LLVM IR Verifier ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the function verifier interface, that can be used for
// validation checking of input to the system, and for checking that
// transformations haven't done something bad.
//
// Note that this does not provide full 'java style' security and verifications,
// instead it just tries to ensure that code is well formed.
//
// To see what specifically is checked, look at the top of Verifier.cpp
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VERIFIER_H
#define LLVM_IR_VERIFIER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include <utility>

namespace llvm {

class APInt;
class Function;
class FunctionPass;
class Instruction;
class MDNode;
class Module;
class raw_ostream;

/// Shared diagnostic and module state used by the IR verifier.
struct VerifierSupport;

/// Verify that the TBAA Metadatas are valid.
class TBAAVerifier {
  VerifierSupport *Diagnostic = nullptr;

  /// Helper to diagnose a failure
  template <typename... Tys> void CheckFailed(Tys &&... Args);

  /// Cache of TBAA base nodes that have already been visited.  This cachce maps
  /// a node that has been visited to a pair (IsInvalid, BitWidth) where
  ///
  ///  \c IsInvalid is true iff the node is invalid.
  ///  \c BitWidth, if non-zero, is the bitwidth of the integer used to denoting
  ///    the offset of the access.  If zero, only a zero offset is allowed.
  ///
  /// \c BitWidth has no meaning if \c IsInvalid is true.
  using TBAABaseNodeSummary = std::pair<bool, unsigned>;
  DenseMap<const MDNode *, TBAABaseNodeSummary> TBAABaseNodes;

  /// Maps an alleged scalar TBAA node to a boolean that is true if the said
  /// TBAA node is a valid scalar TBAA node or false otherwise.
  DenseMap<const MDNode *, bool> TBAAScalarNodes;

  /// \name Helper functions used by \c visitTBAAMetadata.
  /// @{
  MDNode *getFieldNodeFromTBAABaseNode(const Instruction *I,
                                       const MDNode *BaseNode, APInt &Offset,
                                       bool IsNewFormat);
  TBAAVerifier::TBAABaseNodeSummary verifyTBAABaseNode(const Instruction *I,
                                                       const MDNode *BaseNode,
                                                       bool IsNewFormat);
  TBAABaseNodeSummary verifyTBAABaseNodeImpl(const Instruction *I,
                                             const MDNode *BaseNode,
                                             bool IsNewFormat);

  bool isValidScalarTBAANode(const MDNode *MD);
  /// @}

public:
  /// Construct a TBAA verifier that reports through \p Diagnostic.
  /// @param Diagnostic Optional verifier support used to emit diagnostics.
  TBAAVerifier(VerifierSupport *Diagnostic = nullptr)
      : Diagnostic(Diagnostic) {}
  /// Visit an instruction, or a TBAA node itself as part of a metadata, and
  /// return true if it is valid, return false if an invalid TBAA is attached.
  /// @param I Instruction whose TBAA metadata is checked, or the context
  ///        instruction when validating a TBAA node.
  /// @param MD TBAA metadata node to validate, or null if none is attached.
  /// @return True if the TBAA metadata is valid; false otherwise.
  LLVM_ABI bool visitTBAAMetadata(const Instruction *I, const MDNode *MD);
};

/// Check a function for errors, useful for use when debugging a
/// pass.
///
/// If there are no errors, the function returns false. If an error is found,
/// a message describing the error is written to OS (if non-null) and true is
/// returned.
/// @param F Function whose IR is verified.
/// @param OS Optional stream that receives a description of any error found.
/// @return True if the function is broken; false otherwise.
LLVM_ABI bool verifyFunction(const Function &F, raw_ostream *OS = nullptr);

/// Check a module for errors.
///
/// If there are no errors, the function returns false. If an error is
/// found, a message describing the error is written to OS (if
/// non-null) and true is returned.
///
/// \return true if the module is broken. If BrokenDebugInfo is
/// supplied, DebugInfo verification failures won't be considered as
/// error and instead *BrokenDebugInfo will be set to true. Debug
/// info errors can be "recovered" from by stripping the debug info.
/// @param M Module whose IR is verified.
/// @param OS Optional stream that receives a description of any error found.
/// @param BrokenDebugInfo Optional flag set to true when only debug info is
///        broken and IR is otherwise valid.
LLVM_ABI bool verifyModule(const Module &M, raw_ostream *OS = nullptr,
                           bool *BrokenDebugInfo = nullptr);

/// Create a verifier pass.
///
/// Check a module or function for validity. This is essentially a pass wrapped
/// around the above verifyFunction and verifyModule routines and
/// functionality. When the pass detects a verification error it is always
/// printed to stderr, and by default they are fatal. You can override that by
/// passing \c false to \p FatalErrors.
///
/// Note that this creates a pass suitable for the legacy pass manager. It has
/// nothing to do with \c VerifierPass.
/// @param FatalErrors If true, verification failures abort compilation.
/// @return A new verifier FunctionPass for the legacy pass manager.
LLVM_ABI FunctionPass *createVerifierPass(bool FatalErrors = true);

/// Check a module for errors, and report separate error states for IR
/// and debug info errors.
class VerifierAnalysis : public AnalysisInfoMixin<VerifierAnalysis> {
  friend AnalysisInfoMixin<VerifierAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Result of verifying a module or function.
  struct Result {
    /// True when the IR itself failed verification.
    bool IRBroken;
    /// True when debug info failed verification.
    bool DebugInfoBroken;
  };

  /// Run the verifier over module \p M.
  /// @param M Module to verify.
  /// @param AM Module analysis manager (unused).
  /// @return Whether IR and debug info verification failed.
  LLVM_ABI Result run(Module &M, ModuleAnalysisManager &AM);
  /// Run the verifier over function \p F.
  /// @param F Function to verify.
  /// @param AM Function analysis manager (unused).
  /// @return Whether IR and debug info verification failed.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &AM);
};

/// New pass manager pass that verifies IR and optionally aborts on failure.
class VerifierPass : public RequiredPassInfoMixin<VerifierPass> {
  bool FatalErrors;

public:
  /// Construct a verifier pass.
  /// @param FatalErrors If true, verification failures abort compilation.
  explicit VerifierPass(bool FatalErrors = true) : FatalErrors(FatalErrors) {}

  /// Verify module \p M and return all analyses preserved.
  /// @param M Module to verify.
  /// @param AM Module analysis manager providing VerifierAnalysis.
  /// @return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  /// Verify function \p F and return all analyses preserved.
  /// @param F Function to verify.
  /// @param AM Function analysis manager providing VerifierAnalysis.
  /// @return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_IR_VERIFIER_H
