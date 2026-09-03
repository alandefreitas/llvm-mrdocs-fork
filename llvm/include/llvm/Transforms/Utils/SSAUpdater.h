//===- SSAUpdater.h - Unstructured SSA Update Tool --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SSAUpdater class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SSAUPDATER_H
#define LLVM_TRANSFORMS_UTILS_SSAUPDATER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace llvm {

class BasicBlock;
class Instruction;
class LoadInst;
class PHINode;
class DbgVariableRecord;
template <typename T> class SmallVectorImpl;
template <typename T> class SSAUpdaterTraits;
class Type;
class Use;
class Value;

/// Helper class for SSA formation on a set of values defined in
/// multiple blocks.
///
/// This is used when code duplication or another unstructured
/// transformation wants to rewrite a set of uses of one value with uses of a
/// set of values.
class SSAUpdater {
  friend class SSAUpdaterTraits<SSAUpdater>;

private:
  /// This keeps track of which value to use on a per-block basis. When we
  /// insert PHI nodes, we keep track of them here.
  void *AV = nullptr;

  /// ProtoType holds the type of the values being rewritten.
  Type *ProtoType = nullptr;

  /// PHI nodes are given a name based on ProtoName.
  std::string ProtoName;

  /// If this is non-null, the SSAUpdater adds all PHI nodes that it creates to
  /// the vector.
  SmallVectorImpl<PHINode *> *InsertedPHIs;

public:
  /// If InsertedPHIs is specified, it will be filled
  /// in with all PHI Nodes created by rewriting.
  ///
  /// \param InsertedPHIs Optional list that receives newly inserted PHI nodes.
  LLVM_ABI explicit SSAUpdater(
      SmallVectorImpl<PHINode *> *InsertedPHIs = nullptr);
  /// Deleted copy constructor; SSAUpdater is not copyable.
  ///
  /// \param Other Unused; copy construction is not allowed.
  SSAUpdater(const SSAUpdater &Other) = delete;
  /// Deleted copy assignment; SSAUpdater cannot be copy-assigned.
  ///
  /// \param Other Unused; copy assignment is not allowed.
  SSAUpdater &operator=(const SSAUpdater &Other) = delete;
  /// Destroy the SSA updater.
  LLVM_ABI ~SSAUpdater();

  /// Reset this object to get ready for a new set of SSA updates with
  /// type 'Ty'.
  ///
  /// PHI nodes get a name based on 'Name'.
  ///
  /// \param Ty Type of the values being rewritten.
  /// \param Name Basename used when naming newly inserted PHI nodes.
  LLVM_ABI void Initialize(Type *Ty, StringRef Name);

  /// Indicate that a rewritten value is available in the specified block
  /// with the specified value.
  ///
  /// \param BB Block in which the rewritten value is available.
  /// \param V Value available in \p BB.
  LLVM_ABI void AddAvailableValue(BasicBlock *BB, Value *V);

  /// Return true if the SSAUpdater already has a value for the specified
  /// block.
  ///
  /// \param BB Block to query for an available value.
  /// \return True if a value is already available for \p BB.
  LLVM_ABI bool HasValueForBlock(BasicBlock *BB) const;

  /// Return the value for the specified block if the SSAUpdater has one,
  /// otherwise return nullptr.
  ///
  /// \param BB Block to look up an available value for.
  /// \return The available value for \p BB, or nullptr if none.
  LLVM_ABI Value *FindValueForBlock(BasicBlock *BB) const;

  /// Construct SSA form, materializing a value that is live at the end
  /// of the specified block.
  ///
  /// \param BB Block at whose end the live value is materialized.
  /// \return The value live at the end of \p BB.
  LLVM_ABI Value *GetValueAtEndOfBlock(BasicBlock *BB);

  /// Construct SSA form, materializing a value that is live in the
  /// middle of the specified block.
  ///
  /// \c GetValueInMiddleOfBlock is the same as \c GetValueAtEndOfBlock except
  /// in one important case: if there is a definition of the rewritten value
  /// after the 'use' in BB.  Consider code like this:
  ///
  /// \code
  ///      X1 = ...
  ///   SomeBB:
  ///      use(X)
  ///      X2 = ...
  ///      br Cond, SomeBB, OutBB
  /// \endcode
  ///
  /// In this case, there are two values (X1 and X2) added to the AvailableVals
  /// set by the client of the rewriter, and those values are both live out of
  /// their respective blocks.  However, the use of X happens in the *middle* of
  /// a block.  Because of this, we need to insert a new PHI node in SomeBB to
  /// merge the appropriate values, and this value isn't live out of the block.
  ///
  /// \param BB Block in whose middle the live value is materialized.
  /// \return The value live in the middle of \p BB.
  LLVM_ABI Value *GetValueInMiddleOfBlock(BasicBlock *BB);

  /// Rewrite a use of the symbolic value.
  ///
  /// This handles PHI nodes, which use their value in the corresponding
  /// predecessor. Note that this will not work if the use is supposed to be
  /// rewritten to a value defined in the same block as the use, but above it.
  /// Any 'AddAvailableValue's added for the use's block will be considered to
  /// be below it.
  ///
  /// \param U Use of the symbolic value to rewrite.
  LLVM_ABI void RewriteUse(Use &U);

  /// Rewrite debug value intrinsics to conform to a new SSA form.
  ///
  /// This will scout out all the debug value intrinsics associated with
  /// the instruction. Anything outside of its block will have its
  /// value set to the new SSA value if available, and undef if not.
  ///
  /// \param I Instruction whose associated debug values are updated.
  LLVM_ABI void UpdateDebugValues(Instruction *I);
  /// Rewrite the given debug value records to conform to a new SSA form.
  ///
  /// \param I Instruction that defines the new SSA value used for updates.
  /// \param DbgValues Debug value records to update.
  LLVM_ABI void
  UpdateDebugValues(Instruction *I,
                    SmallVectorImpl<DbgVariableRecord *> &DbgValues);

  /// Rewrite a use like \c RewriteUse but handling in-block definitions.
  ///
  /// This version of the method can rewrite uses in the same block as
  /// a definition, because it assumes that all uses of a value are below any
  /// inserted values.
  ///
  /// \param U Use of the symbolic value to rewrite.
  LLVM_ABI void RewriteUseAfterInsertions(Use &U);

private:
  Value *GetValueAtEndOfBlockInternal(BasicBlock *BB);
  void UpdateDebugValue(Instruction *I, DbgVariableRecord *DbgValue);
};

/// Helper class for promoting a collection of loads and stores into SSA
/// Form using the SSAUpdater.
///
/// This handles complexities that SSAUpdater doesn't, such as multiple loads
/// and stores in one block.
///
/// Clients of this class are expected to subclass this and implement the
/// virtual methods.
class LoadAndStorePromoter {
protected:
  /// SSA updater used to form SSA values for promoted loads and stores.
  SSAUpdater &SSA;

public:
  /// Construct a promoter for the given loads and stores.
  ///
  /// \param Insts Loads and stores that will be promoted.
  /// \param S SSA updater used to rewrite values.
  /// \param Name Basename used when naming newly inserted PHI nodes.
  LLVM_ABI LoadAndStorePromoter(ArrayRef<const Instruction *> Insts,
                                SSAUpdater &S, StringRef Name = StringRef());
  /// Destroy the load/store promoter.
  virtual ~LoadAndStorePromoter() = default;

  /// This does the promotion.
  ///
  /// Insts is a list of loads and stores to promote, and Name is the basename
  /// for the PHIs to insert. After this is complete, the loads and stores are
  /// removed from the code.
  ///
  /// \param Insts Loads and stores to promote into SSA form.
  LLVM_ABI void run(const SmallVectorImpl<Instruction *> &Insts);

  /// This hook is invoked after all the stores are found and inserted as
  /// available values.
  virtual void doExtraRewritesBeforeFinalDeletion() {}

  /// Clients can choose to implement this to get notified right before
  /// a load is RAUW'd another value.
  ///
  /// \param LI Load being replaced.
  /// \param V Value that replaces \p LI.
  virtual void replaceLoadWithValue(LoadInst *LI, Value *V) const {}

  /// Called before each instruction is deleted.
  ///
  /// \param I Instruction about to be deleted.
  virtual void instructionDeleted(Instruction *I) const {}

  /// Called to update debug info associated with the instruction.
  ///
  /// \param I Instruction whose debug info should be updated.
  virtual void updateDebugInfo(Instruction *I) const {}

  /// Return false if a sub-class wants to keep one of the loads/stores
  /// after the SSA construction.
  ///
  /// \param I Load or store under consideration for deletion.
  /// \return True if \p I should be deleted after SSA construction.
  virtual bool shouldDelete(Instruction *I) const { return true; }

  /// Return the value to use at the alloca's position in the code.
  ///
  /// This will only be used if an Alloca is included in Insts,
  /// otherwise the value of a uninitialized load will be assumed to be poison.
  ///
  /// \param AI Alloca whose position selects the value to use.
  /// \return The value to use at \p AI's position, or nullptr by default.
  virtual Value *getValueToUseForAlloca(Instruction *AI) const {
    return nullptr;
  }
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SSAUPDATER_H
