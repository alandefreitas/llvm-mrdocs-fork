//===- AutoUpgrade.h - AutoUpgrade Helpers ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  These functions are implemented by lib/IR/AutoUpgrade.cpp.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_AUTOUPGRADE_H
#define LLVM_IR_AUTOUPGRADE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <vector>

namespace llvm {
  class AttrBuilder;
  class CallBase;
  class Constant;
  class Function;
  class Instruction;
  class GlobalVariable;
  class MDNode;
  class Module;
  class StringRef;
  class Type;
  class Value;

  template <typename T> class OperandBundleDefT;
  /// Operand bundle definition that owns its inputs as \c Value pointers.
  using OperandBundleDef = OperandBundleDefT<Value *>;

  /// Check whether an intrinsic function needs upgrading.
  ///
  /// This is a more granular function that simply checks an intrinsic function
  /// for upgrading, and returns true if it requires upgrading. It may return
  /// null in NewFn if the all calls to the original intrinsic function
  /// should be transformed to non-function-call instructions.
  /// \param F Intrinsic function to inspect.
  /// \param NewFn Set to the replacement function, or null if calls should
  ///        become non-call instructions.
  /// \param CanUpgradeDebugIntrinsicsToRecords If true, allow rewriting debug
  ///        intrinsics into debug records.
  /// \return true if \p F requires upgrading.
  LLVM_ABI bool
  UpgradeIntrinsicFunction(Function *F, Function *&NewFn,
                           bool CanUpgradeDebugIntrinsicsToRecords = true);

  /// This is the complement to the above, replacing a specific call to an
  /// intrinsic function with a call to the specified new function.
  /// \param CB Call to the old intrinsic to rewrite.
  /// \param NewFn Replacement function, or null to rewrite the call as
  ///        non-call instructions.
  LLVM_ABI void UpgradeIntrinsicCall(CallBase *CB, Function *NewFn);

  /// Upgrade inline-asm comments for Objective-C retain/release markers.
  /// \param AsmStr Inline assembly string to rewrite in place.
  LLVM_ABI void UpgradeInlineAsmString(std::string *AsmStr);

  /// Upgrade an old intrinsic function and all of its call sites.
  ///
  /// This is an auto-upgrade hook for any old intrinsic function syntaxes
  /// which need to have both the function updated as well as all calls updated
  /// to the new function. This should only be run in a post-processing fashion
  /// so that it can update all calls to the old function.
  /// \param F Intrinsic function whose declaration and calls are upgraded.
  LLVM_ABI void UpgradeCallsToIntrinsic(Function *F);

  /// This checks for global variables which should be upgraded. If it requires
  /// upgrading, returns a pointer to the upgraded variable.
  /// \param GV Global variable to inspect.
  /// \return A pointer to the upgraded variable, or null if no upgrade is needed.
  LLVM_ABI GlobalVariable *UpgradeGlobalVariable(GlobalVariable *GV);

  /// This checks for module flags which should be upgraded. It returns true if
  /// module is modified.
  /// \param M Module whose flags metadata is upgraded.
  /// \return true if the module was modified.
  LLVM_ABI bool UpgradeModuleFlags(Module &M);

  /// Upgrade the cfi.functions metadata node by calculating and inserting
  /// the GUID for each function entry if it's missing.
  /// \param M Module whose \c cfi.functions metadata is upgraded.
  /// \return true if the metadata was modified.
  LLVM_ABI bool UpgradeCFIFunctionsMetadata(Module &M);

  /// Convert legacy nvvm.annotations metadata to appropriate function
  /// attributes.
  /// \param M Module whose \c nvvm.annotations metadata is converted.
  LLVM_ABI void UpgradeNVVMAnnotations(Module &M);

  /// Convert calls to ARC runtime functions to intrinsic calls and upgrade the
  /// old retain release marker to new module flag format.
  /// \param M Module whose ARC runtime calls and markers are upgraded.
  LLVM_ABI void UpgradeARCRuntime(Module &M);

  /// Upgrade outdated section attributes on globals in a module.
  /// \param M Module whose global section attributes are upgraded.
  LLVM_ABI void UpgradeSectionAttributes(Module &M);

  /// Correct any IR that is relying on old function attribute behavior.
  /// \param F Function whose attributes are upgraded.
  LLVM_ABI void UpgradeFunctionAttributes(Function &F);

  /// Upgrade a scalar TBAA tag to the struct-path aware TBAA format.
  ///
  /// If the given TBAA tag uses the scalar TBAA format, create a new node
  /// corresponding to the upgrade to the struct-path aware TBAA format.
  /// Otherwise return the \p TBAANode itself.
  /// \param TBAANode TBAA metadata node to inspect and possibly rewrite.
  /// \return The upgraded TBAA node, or \p TBAANode itself if already upgraded.
  LLVM_ABI MDNode *UpgradeTBAANode(MDNode &TBAANode);

  /// This is an auto-upgrade for bitcast between pointers with different
  /// address spaces: the instruction is replaced by a pair ptrtoint+inttoptr.
  /// \param Opc Opcode of the cast being considered.
  /// \param V Source value of the bitcast.
  /// \param DestTy Destination type of the bitcast.
  /// \param Temp Set to the ptrtoint instruction when an upgrade is performed.
  /// \return The inttoptr instruction when an upgrade is performed, or null.
  LLVM_ABI Instruction *UpgradeBitCastInst(unsigned Opc, Value *V, Type *DestTy,
                                           Instruction *&Temp);

  /// This is an auto-upgrade for bitcast constant expression between pointers
  /// with different address spaces: the instruction is replaced by a pair
  /// ptrtoint+inttoptr.
  /// \param Opc Opcode of the cast being considered.
  /// \param C Source constant of the bitcast.
  /// \param DestTy Destination type of the bitcast.
  /// \return The upgraded constant expression, or null if no upgrade is needed.
  LLVM_ABI Constant *UpgradeBitCastExpr(unsigned Opc, Constant *C,
                                        Type *DestTy);

  /// Check the debug info version number, if it is out-dated, drop the debug
  /// info. Return true if module is modified.
  /// \param M Module whose debug info version is checked.
  /// \return true if the module was modified.
  LLVM_ABI bool UpgradeDebugInfo(Module &M);

  /// Copies module attributes to the functions in the module.
  ///
  /// Currently only effects ARM, Thumb and AArch64 targets.
  /// Supported attributes:
  ///  - branch-target-enforcement
  ///  - branch-protection-pauth-lr
  ///  - guarded-control-stack
  ///  - sign-return-address
  ///  - sign-return-address-with-bkey
  /// \param M Module whose attributes are copied onto its functions.
  LLVM_ABI void copyModuleAttrToFunctions(Module &M);

  /// Single-operand tags replacing a removed two-operand form
  /// !{!"<Enable>", i1 X}: X = true selects Enable, X = false selects Disable.
  struct BooleanLoopTags {
    /// Tag selected when the old two-operand form had a true operand.
    StringLiteral Enable;
    /// Tag selected when the old two-operand form had a false operand.
    StringLiteral Disable;
  };

  /// Table mapping removed two-operand loop enable tags to replacement tags.
  inline constexpr BooleanLoopTags OldBooleanLoopTags[] = {
      {"llvm.loop.distribute.enable", "llvm.loop.distribute.disable"},
      {"llvm.loop.vectorize.enable", "llvm.loop.vectorize.disable"},
      {"llvm.loop.vectorize.predicate.enable",
       "llvm.loop.vectorize.predicate.disable"},
      {"llvm.loop.vectorize.scalable.enable",
       "llvm.loop.vectorize.scalable.disable"}};

  /// Return the replacement tags for the enable tag \p Name, or nullptr.
  /// \param Name Enable-tag string to look up.
  /// \return The replacement tags for \p Name, or nullptr if not found.
  inline const BooleanLoopTags *findBooleanLoopTags(StringRef Name) {
    for (const BooleanLoopTags &Tags : OldBooleanLoopTags)
      if (Tags.Enable == Name)
        return &Tags;
    return nullptr;
  }

  /// Check whether a string looks like an old loop attachment tag.
  /// \param Name Metadata tag string to test.
  /// \return true if \p Name looks like an old loop attachment tag.
  inline bool mayBeOldLoopAttachmentTag(StringRef Name) {
    // The enable tags are intentionally included: the current single-operand
    // form shares the tag with the removed two-operand form (!{!"...", i1 X}),
    // so we can only decide by inspecting the operands, which happens in
    // upgradeLoopArgument().
    return Name.starts_with("llvm.vectorizer.") || findBooleanLoopTags(Name);
  }

  /// Upgrade the loop attachment metadata node.
  /// \param N Loop attachment metadata node to upgrade.
  /// \return The upgraded loop attachment metadata node.
  LLVM_ABI MDNode *upgradeInstructionLoopAttachment(MDNode &N);

  /// Upgrade the datalayout string by adding a section for address space
  /// pointers.
  /// \param DL Data layout string to upgrade.
  /// \param Triple Target triple used to select layout upgrades.
  /// \return The upgraded data layout string.
  LLVM_ABI std::string UpgradeDataLayoutString(StringRef DL, StringRef Triple);

  /// Upgrade attributes that changed format or kind.
  /// \param B Attribute builder whose entries are rewritten in place.
  LLVM_ABI void UpgradeAttributes(AttrBuilder &B);

  /// Upgrade operand bundles (without knowing about their user instruction).
  /// \param OperandBundles Operand bundles to rewrite in place.
  LLVM_ABI void
  UpgradeOperandBundles(std::vector<OperandBundleDef> &OperandBundles);

} // End llvm namespace

#endif
