//===- Tracker.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is the component of SandboxIR that tracks all changes made to its
// state, such that we can revert the state when needed.
//
// Tracking changes
// ----------------
// The user needs to call `Tracker::save()` to enable tracking changes
// made to SandboxIR. From that point on, any change made to SandboxIR, will
// automatically create a change tracking object and register it with the
// tracker. IR-change objects are subclasses of `IRChangeBase` and get
// registered with the `Tracker::track()` function. The change objects
// are saved in the order they are registered with the tracker and are stored in
// the `Tracker::Changes` vector. All of this is done transparently to
// the user.
// Calling `Tracker::save()` a second time without having accepted/reverted the
// state, creates a second nested checkpoint.
//
// Reverting changes
// -----------------
// Calling `Tracker::revert()` will restore the state saved when the last
// `Tracker::save()` was called. Internally this goes through the
// change objects in `Tracker::Changes` in reverse order, calling their
// `IRChangeBase::revert()` function one by one.
// In the context of a nested checkpoint, this will revert the state
// until the last `Tracker::save()` checkpoint.
// You can revert all changes with `Tracker::revert(/*RevertAll=*/true)`.
//
// Accepting changes
// -----------------
// The user needs to either revert or accept changes before the tracker object
// is destroyed. This is enforced in the tracker's destructor.
// This is the job of `Tracker::accept()`. Internally this will go
// through the change objects in `Tracker::Changes` in order, calling
// `IRChangeBase::accept()`.
// In the context of a nested checkpoint, this will leave the state unchanged
// and will remove the last checkpoint for the stack.
// You can accept all changes with `Tracker::accept(/*AcceptAll=*/true)`.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_TRACKER_H
#define LLVM_SANDBOXIR_TRACKER_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StableHashing.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/SandboxIR/Use.h"
#include "llvm/SandboxIR/Value.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include <memory>

namespace llvm::sandboxir {

class BasicBlock;
class CallBrInst;
class LoadInst;
class StoreInst;
class Instruction;
class Tracker;
class AllocaInst;
class CatchSwitchInst;
class SwitchInst;
class ConstantInt;
class ShuffleVectorInst;
class CmpInst;
class GlobalVariable;

#ifndef NDEBUG

/// Debug checker that snapshots SandboxIR functions and compares them after revert.
///
/// Saves hashes and textual IR snapshots of functions in a SandboxIR Context,
/// and does hash comparison when `expectNoDiff` is called. If hashes differ, it
/// prints textual IR for both old and new versions to aid debugging.
///
/// This is used as an additional debug check when reverting changes to
/// SandboxIR, to verify the reverted state matches the initial state.
class IRSnapshotChecker {
  Context &Ctx;

  // A snapshot of textual IR for a function, with a hash for quick comparison.
  struct FunctionSnapshot {
    llvm::stable_hash Hash;
    std::string TextualIR;
  };

  // A snapshot for each llvm::Function found in every module in the SandboxIR
  // Context. In practice there will always be one module, but sandbox IR
  // save/restore ops work at the Context level, so we must take the full state
  // into account.
  using ContextSnapshot = DenseMap<const llvm::Function *, FunctionSnapshot>;

  ContextSnapshot OrigContextSnapshot;

  // Dumps to a string the textual IR for a single Function.
  std::string dumpIR(const llvm::Function &F) const;

  // Returns a snapshot of all the modules in the sandbox IR context.
  ContextSnapshot takeSnapshot() const;

  // Compares two snapshots and returns true if they differ.
  bool diff(const ContextSnapshot &Orig, const ContextSnapshot &Curr) const;

public:
  /// Construct a checker bound to SandboxIR context \p Ctx.
  /// \param Ctx SandboxIR context to snapshot.
  IRSnapshotChecker(Context &Ctx) : Ctx(Ctx) {}

  /// Saves a snapshot of the current state. If there was any previous snapshot,
  /// it will be replaced with the new one.
  LLVM_ABI void save();

  /// Checks current state against saved state, crashes if different.
  LLVM_ABI void expectNoDiff();
};

#endif // NDEBUG

/// The base class for IR Change classes.
class IRChangeBase {
protected:
  friend class Tracker; // For Parent.

public:
  /// This runs when changes get reverted.
  /// \param Tracker The tracker that owns this change.
  virtual void revert(Tracker &Tracker) = 0;
  /// This runs when changes get accepted.
  virtual void accept() = 0;
  /// Destroy this IR change object.
  virtual ~IRChangeBase() = default;
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  virtual void dump(raw_ostream &OS) const = 0;
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD virtual void dump() const = 0;
  /// Print \p C to \p OS.
  /// \param OS Output stream.
  /// \param C Change to print.
  /// \Returns A reference to \p OS.
  friend raw_ostream &operator<<(raw_ostream &OS, const IRChangeBase &C) {
    C.dump(OS);
    return OS;
  }
#endif
};

/// Tracks the change of the source Value of a sandboxir::Use.
class UseSet : public IRChangeBase {
  Use U;
  Value *OrigV = nullptr;

public:
  /// Construct a change that records the original source of \p U.
  /// \param U Use whose source value is about to change.
  UseSet(const Use &U) : U(U), OrigV(U.get()) {}
  /// Restore the original source value of the use.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final { U.set(OrigV); }
  /// Accept the use-source change; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "UseSet"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif
};

/// Tracks removal of an incoming value from a PHI node.
class LLVM_ABI PHIRemoveIncoming : public IRChangeBase {
  PHINode *PHI;
  unsigned RemovedIdx;
  Value *RemovedV;
  BasicBlock *RemovedBB;

public:
  /// Construct a change that records the incoming value at \p RemovedIdx.
  /// \param PHI PHI node about to lose an incoming value.
  /// \param RemovedIdx Index of the incoming value being removed.
  PHIRemoveIncoming(PHINode *PHI, unsigned RemovedIdx);
  /// Restore the removed incoming value on the PHI node.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the PHI incoming removal; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "PHISetIncoming"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif
};

/// Tracks addition of an incoming value to a PHI node.
class LLVM_ABI PHIAddIncoming : public IRChangeBase {
  PHINode *PHI;
  unsigned Idx;

public:
  /// Construct a change that records the newly added incoming value on \p PHI.
  /// \param PHI PHI node that just gained an incoming value.
  PHIAddIncoming(PHINode *PHI);
  /// Remove the added incoming value from the PHI node.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the PHI incoming addition; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "PHISetIncoming"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif
};

/// Tracks swapping the operands of a compare instruction.
class LLVM_ABI CmpSwapOperands : public IRChangeBase {
  CmpInst *Cmp;

public:
  /// Construct a change that records operand swap on \p Cmp.
  /// \param Cmp Compare instruction whose operands were swapped.
  CmpSwapOperands(CmpInst *Cmp);
  /// Swap the compare operands back to their original order.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the operand swap; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "CmpSwapOperands"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif
};

/// Tracks swapping a Use with another Use.
class UseSwap : public IRChangeBase {
  Use ThisUse;
  Use OtherUse;

public:
  /// Construct a change that records a swap of \p ThisUse with \p OtherUse.
  /// \param ThisUse First use involved in the swap.
  /// \param OtherUse Second use involved in the swap; must share the same user.
  UseSwap(const Use &ThisUse, const Use &OtherUse)
      : ThisUse(ThisUse), OtherUse(OtherUse) {
    assert(ThisUse.getUser() == OtherUse.getUser() && "Expected same user!");
  }
  /// Swap the two uses back to their original order.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final { ThisUse.swap(OtherUse); }
  /// Accept the use swap; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "UseSwap"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif
};

/// Tracks erasing an instruction (and optionally related instructions) from
/// its parent, retaining enough state to restore them.
class LLVM_ABI EraseFromParent : public IRChangeBase {
  /// Contains all the data we need to restore an "erased" (i.e., detached)
  /// instruction: the instruction itself and its operands in order.
  struct InstrAndOperands {
    /// The operands that got dropped.
    SmallVector<llvm::Value *> Operands;
    /// The instruction that got "erased".
    llvm::Instruction *LLVMI;
  };
  /// The instruction data is in reverse program order, which helps create the
  /// original program order during revert().
  SmallVector<InstrAndOperands> InstrData;
  /// This is either the next Instruction in the stream, or the parent
  /// BasicBlock if at the end of the BB.
  PointerUnion<llvm::Instruction *, llvm::BasicBlock *> NextLLVMIOrBB;
  /// We take ownership of the "erased" instruction.
  std::unique_ptr<sandboxir::Value> ErasedIPtr;

public:
  /// Construct a change that takes ownership of erased instruction \p IPtr.
  /// \param IPtr SandboxIR value being erased; ownership is transferred.
  EraseFromParent(std::unique_ptr<sandboxir::Value> &&IPtr);
  /// Reinsert the erased instruction(s) at their original position.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the erasure and delete the underlying LLVM instruction(s).
  void accept() final;
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "EraseFromParent"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
  /// Print \p C to \p OS.
  /// \param OS Output stream.
  /// \param C Change to print.
  /// \Returns A reference to \p OS.
  friend raw_ostream &operator<<(raw_ostream &OS, const EraseFromParent &C) {
    C.dump(OS);
    return OS;
  }
#endif
};

/// Tracks removing an instruction from its parent without deleting it.
class LLVM_ABI RemoveFromParent : public IRChangeBase {
  /// The instruction that is about to get removed.
  Instruction *RemovedI = nullptr;
  /// This is either the next instr, or the parent BB if at the end of the BB.
  PointerUnion<Instruction *, BasicBlock *> NextInstrOrBB;

public:
  /// Construct a change that records \p RemovedI for later reinsertion.
  /// \param RemovedI Instruction about to be removed from its parent.
  RemoveFromParent(Instruction *RemovedI);
  /// Reinsert the instruction into its original position.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the removal; nothing further to do.
  void accept() final {};
  /// Return the instruction that was removed.
  /// \Returns The instruction that was removed from its parent.
  Instruction *getInstruction() const { return RemovedI; }
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "RemoveFromParent"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif // NDEBUG
};

/// Tracks most instruction setter changes via getter/setter member pointers.
///
/// The two template arguments are:
/// - GetterFn: The getter member function pointer (e.g., `&Foo::get`)
/// - SetterFn: The setter member function pointer (e.g., `&Foo::set`)
/// Upon construction, it saves a copy of the original value by calling the
/// getter function. Revert sets the value back to the one saved, using the
/// setter function provided.
///
/// Example:
///  Tracker.track(std::make_unique<
///                GenericSetter<&FooInst::get, &FooInst::set>>(I, Tracker));
template <auto GetterFn, auto SetterFn>
class GenericSetter final : public IRChangeBase {
  /// Traits for getting the class type from GetterFn type.
  template <typename> struct GetClassTypeFromGetter;
  template <typename RetT, typename ClassT>
  struct GetClassTypeFromGetter<RetT (ClassT::*)() const> {
    using ClassType = ClassT;
  };
  using InstrT = typename GetClassTypeFromGetter<decltype(GetterFn)>::ClassType;
  using SavedValT = std::invoke_result_t<decltype(GetterFn), InstrT>;
  InstrT *I;
  SavedValT OrigVal;

public:
  /// Construct a change that saves the current value from \p I via GetterFn.
  /// \param I Instruction whose setter is about to be called.
  GenericSetter(InstrT *I) : I(I), OrigVal((I->*GetterFn)()) {}
  /// Restore the saved value on the instruction via SetterFn.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final { (I->*SetterFn)(OrigVal); }
  /// Accept the setter change; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "GenericSetter"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final {
    dump(dbgs());
    dbgs() << "\n";
  }
#endif
};

/// Similar to GenericSetter but the setters/getters have an index as their
/// first argument. This is commont in cases like: getOperand(unsigned Idx)
template <auto GetterFn, auto SetterFn>
class GenericSetterWithIdx final : public IRChangeBase {
  /// Helper for getting the class type from the getter
  template <typename ClassT, typename RetT>
  static ClassT getClassTypeFromGetter(RetT (ClassT::*Fn)(unsigned) const);
  template <typename ClassT, typename RetT>
  static ClassT getClassTypeFromGetter(RetT (ClassT::*Fn)(unsigned));

  using InstrT = decltype(getClassTypeFromGetter(GetterFn));
  using SavedValT = std::invoke_result_t<decltype(GetterFn), InstrT, unsigned>;
  InstrT *I;
  SavedValT OrigVal;
  unsigned Idx;

public:
  /// Construct a change that saves the value at \p Idx from \p I via GetterFn.
  /// \param I Instruction whose indexed setter is about to be called.
  /// \param Idx Operand or field index passed to the getter/setter.
  GenericSetterWithIdx(InstrT *I, unsigned Idx)
      : I(I), OrigVal((I->*GetterFn)(Idx)), Idx(Idx) {}
  /// Restore the saved value at \p Idx on the instruction via SetterFn.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final { (I->*SetterFn)(Idx, OrigVal); }
  /// Accept the indexed setter change; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "GenericSetterWithIdx"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final {
    dump(dbgs());
    dbgs() << "\n";
  }
#endif
};

/// Tracks adding a handler to a catchswitch instruction.
class LLVM_ABI CatchSwitchAddHandler : public IRChangeBase {
  CatchSwitchInst *CSI;
  unsigned HandlerIdx;

public:
  /// Construct a change that records the handler just added to \p CSI.
  /// \param CSI Catchswitch that gained a handler.
  CatchSwitchAddHandler(CatchSwitchInst *CSI);
  /// Remove the added handler from the catchswitch.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the handler addition; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "CatchSwitchAddHandler"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final {
    dump(dbgs());
    dbgs() << "\n";
  }
#endif // NDEBUG
};

/// Tracks adding a case to a switch instruction.
class LLVM_ABI SwitchAddCase : public IRChangeBase {
  SwitchInst *Switch;
  ConstantInt *Val;

public:
  /// Construct a change that records case value \p Val added to \p Switch.
  /// \param Switch Switch instruction that gained a case.
  /// \param Val Case value that was added.
  SwitchAddCase(SwitchInst *Switch, ConstantInt *Val)
      : Switch(Switch), Val(Val) {}
  /// Remove the added case from the switch.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the case addition; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "SwitchAddCase"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif // NDEBUG
};

/// Tracks removing case(s) from a switch instruction.
class LLVM_ABI SwitchRemoveCase : public IRChangeBase {
  SwitchInst *Switch;
  struct Case {
    ConstantInt *Val;
    BasicBlock *Dest;
  };
  SmallVector<Case> Cases;

public:
  /// Construct a change that records all cases on \p Switch before removal.
  /// \param Switch Switch instruction about to lose case(s).
  SwitchRemoveCase(SwitchInst *Switch);

  /// Restore the removed switch cases in their original order.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the case removal; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "SwitchRemoveCase"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif // NDEBUG
};

/// Tracks moving an instruction to a new position in the IR.
class LLVM_ABI MoveInstr : public IRChangeBase {
  /// The instruction that moved.
  Instruction *MovedI;
  /// This is either the next instruction in the block, or the parent BB if at
  /// the end of the BB.
  PointerUnion<Instruction *, BasicBlock *> NextInstrOrBB;

public:
  /// Construct a change that records the original position of \p I.
  /// \param I Instruction that is about to be moved.
  MoveInstr(sandboxir::Instruction *I);
  /// Move the instruction back to its original position.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the move; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "MoveInstr"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif // NDEBUG
};

/// Tracks inserting an instruction into a basic block.
class LLVM_ABI InsertIntoBB final : public IRChangeBase {
  Instruction *InsertedI = nullptr;

public:
  /// Construct a change that records newly inserted instruction \p InsertedI.
  /// \param InsertedI Instruction that was inserted into a basic block.
  InsertIntoBB(Instruction *InsertedI);
  /// Remove the inserted instruction from its parent.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the insertion; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "InsertIntoBB"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif // NDEBUG
};

/// Tracks creating and inserting a new instruction.
class LLVM_ABI CreateAndInsertInst final : public IRChangeBase {
  Instruction *NewI = nullptr;

public:
  /// Construct a change that records newly created instruction \p NewI.
  /// \param NewI Instruction that was created and inserted.
  CreateAndInsertInst(Instruction *NewI) : NewI(NewI) {}
  /// Erase the created instruction from its parent.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the creation; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "CreateAndInsertInst"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif
};

/// Tracks changing the shuffle mask of a shufflevector instruction.
class LLVM_ABI ShuffleVectorSetMask final : public IRChangeBase {
  ShuffleVectorInst *SVI;
  SmallVector<int, 8> PrevMask;

public:
  /// Construct a change that saves the previous mask of \p SVI.
  /// \param SVI ShuffleVector whose mask is about to change.
  ShuffleVectorSetMask(ShuffleVectorInst *SVI);
  /// Restore the previous shuffle mask.
  /// \param Tracker The tracker that owns this change.
  void revert(Tracker &Tracker) final;
  /// Accept the mask change; nothing further to do.
  void accept() final {}
#ifndef NDEBUG
  /// Dump a short name for this change to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const final { OS << "ShuffleVectorSetMask"; }
  /// Dump this change to the debug stream.
  LLVM_DUMP_METHOD void dump() const final;
#endif
};

/// The tracker collects all the change objects and implements the main API for
/// saving / reverting / accepting.
class Tracker {
public:
  /// Whether the tracker is disabled, recording, or reverting.
  enum class TrackerState {
    /// Tracking is disabled.
    Disabled,
    /// Tracking changes.
    Record,
    /// Reverting changes.
    Reverting,
  };

private:
  /// The list of changes that are being tracked.
  SmallVector<std::unique_ptr<IRChangeBase>> Changes;
  /// The current state of the tracker.
  TrackerState State = TrackerState::Disabled;
  /// Nested snapshots require us to track the index of each snapshot in the
  /// `Changes` vector.
  SmallVector<unsigned, 8> Snapshots;
  Context &Ctx;

#ifndef NDEBUG
  /// One checker per nested snapshot.
  SmallVector<IRSnapshotChecker> SnapshotChecker;
#endif

public:
#ifndef NDEBUG
  /// Helps catch bugs where we are creating new change objects while in the
  /// middle of creating other change objects.
  bool InMiddleOfCreatingChange = false;
#endif // NDEBUG

  /// Construct a tracker for SandboxIR context \p Ctx.
  /// \param Ctx SandboxIR context whose changes are tracked.
  explicit Tracker(Context &Ctx) : Ctx(Ctx) {}

  /// Destroy the tracker; all changes must have been accepted or reverted.
  LLVM_ABI ~Tracker();
  /// Return the SandboxIR context associated with this tracker.
  /// \Returns The SandboxIR context associated with this tracker.
  Context &getContext() const { return Ctx; }
  /// Return true if there are no changes tracked.
  /// \Returns True if no changes are currently tracked.
  bool empty() const { return Changes.empty(); }
  /// Record \p Change and take ownership. This is the main function used to
  /// track Sandbox IR changes.
  /// \param Change The IR change to track; ownership is transferred.
  void track(std::unique_ptr<IRChangeBase> &&Change) {
    assert(State == TrackerState::Record && "The tracker should be tracking!");
#ifndef NDEBUG
    assert(!InMiddleOfCreatingChange &&
           "We are in the middle of creating another change!");
    if (isTracking())
      InMiddleOfCreatingChange = true;
#endif // NDEBUG
    Changes.push_back(std::move(Change));

#ifndef NDEBUG
    InMiddleOfCreatingChange = false;
#endif
  }
  /// Construct and track a \c ChangeT if tracking is enabled.
  /// \param Args Constructor arguments forwarded to \c ChangeT.
  /// \Returns true if tracking is enabled.
  template <typename ChangeT, typename... ArgsT>
  bool emplaceIfTracking(ArgsT... Args) {
    if (!isTracking())
      return false;
    track(std::make_unique<ChangeT>(Args...));
    return true;
  }
  /// Return true if the tracker is recording changes.
  /// \Returns True if the tracker is in the Record state.
  bool isTracking() const { return State == TrackerState::Record; }
  /// Return the current state of the tracker.
  /// \Returns The tracker's current TrackerState.
  TrackerState getState() const { return State; }
  /// Turns on IR tracking. If tracking is already enabled this creates a new
  /// nested checkpoint.
  LLVM_ABI void save();
  /// Stops tracking and accept changes. If we have nested checkpoints, this
  /// will remove the last checkpoint from the stack without modifying the
  /// state.
  /// \param AcceptAll If true, accept all nested checkpoints; otherwise only
  /// the last checkpoint.
  LLVM_ABI void accept(bool AcceptAll = false);
  /// Stops tracking and reverts to saved state. If we have nested checkpoints
  /// this will revert the state to the last checkpoint.
  /// \param RevertAll If true, revert all nested checkpoints; otherwise only
  /// the last checkpoint.
  LLVM_ABI void revert(bool RevertAll = false);
  /// Return the number of nested (outstanding) checkpoints.
  /// \Returns The number of nested checkpoints currently outstanding.
  unsigned nestingDepth() const { return Snapshots.size(); }

#ifndef NDEBUG
  /// Dump tracked changes to \p OS.
  /// \param OS Output stream.
  void dump(raw_ostream &OS) const;
  /// Dump tracked changes to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
  /// Print \p Tracker to \p OS.
  /// \param OS Output stream.
  /// \param Tracker Tracker to print.
  /// \Returns A reference to \p OS.
  friend raw_ostream &operator<<(raw_ostream &OS, const Tracker &Tracker) {
    Tracker.dump(OS);
    return OS;
  }
#endif // NDEBUG
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_TRACKER_H
