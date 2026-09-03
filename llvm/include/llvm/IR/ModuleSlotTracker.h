//===-- llvm/IR/ModuleSlotTracker.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MODULESLOTTRACKER_H
#define LLVM_IR_MODULESLOTTRACKER_H

#include "llvm/Support/Compiler.h"
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

class Module;
class Function;
/// Slot numbering table used when printing LLVM IR as assembly.
class SlotTracker;
class Value;
class MDNode;

/// Abstract interface of slot tracker storage.
class LLVM_ABI AbstractSlotTrackerStorage {
public:
  /// Virtual destructor.
  virtual ~AbstractSlotTrackerStorage();

  /// Return the next metadata slot number that will be assigned.
  ///
  /// \return The next metadata slot number.
  virtual unsigned getNextMetadataSlot() = 0;

  /// Create a metadata slot for \p N if one does not already exist.
  ///
  /// \param N The metadata node to assign a slot.
  virtual void createMetadataSlot(const MDNode *N) = 0;
  /// Return the slot number of metadata node \p N, or -1 if none.
  ///
  /// \param N The metadata node whose slot to look up.
  /// \return The metadata slot number, or -1 if none.
  virtual int getMetadataSlot(const MDNode *N) = 0;
};

/// Manage lifetime of a slot tracker for printing IR.
///
/// Wrapper around the \a SlotTracker used internally by \a AsmWriter.  This
/// class allows callers to share the cost of incorporating the metadata in a
/// module or a function.
///
/// If the IR changes from underneath \a ModuleSlotTracker, strings like
/// "<badref>" will be printed, or, worse, the wrong slots entirely.
class LLVM_ABI ModuleSlotTracker {
  /// Storage for a slot tracker.
  std::unique_ptr<SlotTracker> MachineStorage;
  bool ShouldCreateStorage = false;
  bool ShouldInitializeAllMetadata = false;

  const Module *M = nullptr;
  const Function *F = nullptr;
  SlotTracker *Machine = nullptr;

  std::function<void(AbstractSlotTrackerStorage *, const Module *, bool)>
      ProcessModuleHookFn;
  std::function<void(AbstractSlotTrackerStorage *, const Function *, bool)>
      ProcessFunctionHookFn;

public:
  /// Wrap a preinitialized SlotTracker.
  ///
  /// \param Machine Existing slot tracker to reuse.
  /// \param M       Module whose values and metadata are numbered, or null.
  /// \param F       Optional function already incorporated into \p Machine.
  ModuleSlotTracker(SlotTracker &Machine, const Module *M,
                    const Function *F = nullptr);

  /// Construct a slot tracker from a module.
  ///
  /// If \a M is \c nullptr, uses a null slot tracker.  Otherwise, initializes
  /// a slot tracker, and initializes all metadata slots.  \c
  /// ShouldInitializeAllMetadata defaults to true because this is expected to
  /// be shared between multiple callers, and otherwise MDNode references will
  /// not match up.
  ///
  /// \param M Module to track, or null for an empty tracker.
  /// \param ShouldInitializeAllMetadata If true, number all metadata in the
  ///        module up front so references stay consistent across callers.
  explicit ModuleSlotTracker(const Module *M,
                             bool ShouldInitializeAllMetadata = true);

  /// Destructor to clean up storage.
  virtual ~ModuleSlotTracker();

  /// Lazily creates a slot tracker.
  ///
  /// \return The underlying slot tracker, creating storage if needed.
  SlotTracker *getMachine();

  /// Return the module whose slots are being tracked.
  ///
  /// \return The module, or null if none is set.
  const Module *getModule() const { return M; }
  /// Return the function currently incorporated into the slot tracker.
  ///
  /// \return The current function, or null if none is incorporated.
  const Function *getCurrentFunction() const { return F; }

  /// Incorporate the given function.
  ///
  /// Purge the currently incorporated function and incorporate \c F.  If \c F
  /// is currently incorporated, this is a no-op.
  ///
  /// \param F Function whose local values should be numbered.
  void incorporateFunction(const Function &F);

  /// Return the slot number of the specified local value.
  ///
  /// A function that defines this value should be incorporated prior to calling
  /// this method.
  ///
  /// \param V Local value whose slot to look up.
  /// \return The local slot number, or -1 if the value is not in the
  ///         function's SlotTracker.
  int getLocalSlot(const Value *V);

  /// Set a hook invoked when processing a module into the slot tracker.
  ///
  /// \param Fn Callback receiving the storage, module, and whether all
  ///        metadata should be initialized.
  void setProcessHook(
      std::function<void(AbstractSlotTrackerStorage *, const Module *, bool)>
          Fn);
  /// Set a hook invoked when processing a function into the slot tracker.
  ///
  /// \param Fn Callback receiving the storage, function, and whether all
  ///        metadata should be initialized.
  void setProcessHook(
      std::function<void(AbstractSlotTrackerStorage *, const Function *, bool)>
          Fn);

  /// List of metadata nodes paired with their assigned slot numbers.
  using MachineMDNodeListType =
      std::vector<std::pair<unsigned, const MDNode *>>;

  /// Collect metadata nodes whose slots fall in [\p LB, \p UB).
  ///
  /// \param L  Output list of (slot, MDNode) pairs in the range.
  /// \param LB Inclusive lower bound of the slot range.
  /// \param UB Exclusive upper bound of the slot range.
  void collectMDNodes(MachineMDNodeListType &L, unsigned LB, unsigned UB) const;
};

} // end namespace llvm

#endif
