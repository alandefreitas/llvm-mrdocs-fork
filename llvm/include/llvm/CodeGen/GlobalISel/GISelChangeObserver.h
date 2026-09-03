//===----- llvm/CodeGen/GlobalISel/GISelChangeObserver.h --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This contains common code to allow clients to notify changes to machine
/// instr.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_GISELCHANGEOBSERVER_H
#define LLVM_CODEGEN_GLOBALISEL_GISELCHANGEOBSERVER_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class MachineInstr;
class MachineRegisterInfo;

/// Abstract base for clients that observe GlobalISel instruction changes.
///
/// This should be the preferred way for APIs to notify changes.
/// Typically calling erasingInstr/createdInstr multiple times should not affect
/// the result. The observer would likely need to check if it was already
/// notified earlier (consider using GISelWorkList).
class GISelChangeObserver {
  SmallPtrSet<MachineInstr *, 4> ChangingAllUsesOfReg;

public:
  /// Destroy the change observer.
  virtual ~GISelChangeObserver() = default;

  /// An instruction is about to be erased.
  /// \param MI Instruction that is about to be erased.
  virtual void erasingInstr(MachineInstr &MI) = 0;

  /// Notify that an instruction has been created and inserted.
  ///
  /// Note that the instruction might not be a fully fledged instruction at this
  /// point and won't be if the MachineFunction::Delegate is calling it. This is
  /// because the delegate only sees the construction of the MachineInstr before
  /// operands have been added.
  /// \param MI Instruction that was created and inserted.
  virtual void createdInstr(MachineInstr &MI) = 0;

  /// This instruction is about to be mutated in some way.
  /// \param MI Instruction that is about to be mutated.
  virtual void changingInstr(MachineInstr &MI) = 0;

  /// This instruction was mutated in some way.
  /// \param MI Instruction that was mutated.
  virtual void changedInstr(MachineInstr &MI) = 0;

  /// Notify that all instructions using \p Reg are being changed.
  ///
  /// For convenience, finishedChangingAllUsesOfReg() will report the completion
  /// of the changes. The use list may change between this call and
  /// finishedChangingAllUsesOfReg().
  /// \param MRI Register info used to find the uses of \p Reg.
  /// \param Reg Register whose using instructions are being changed.
  LLVM_ABI void changingAllUsesOfReg(const MachineRegisterInfo &MRI,
                                     Register Reg);
  /// All instructions reported as changing by changingAllUsesOfReg() have
  /// finished being changed.
  LLVM_ABI void finishedChangingAllUsesOfReg();
};

/// Wrapper that forwards each change event to a list of observers.
///
/// If there are multiple observers (say CSE, Legalizer, Combiner), it's
/// sufficient to register this to the machine function as the delegate.
class GISelObserverWrapper : public MachineFunction::Delegate,
                             public GISelChangeObserver {
  SmallVector<GISelChangeObserver *, 4> Observers;

public:
  /// Construct an empty observer wrapper with no observers.
  GISelObserverWrapper() = default;
  /// Construct a wrapper that starts with the given observers.
  /// \param Obs Initial observers to forward events to.
  GISelObserverWrapper(ArrayRef<GISelChangeObserver *> Obs) : Observers(Obs) {}
  /// Add an observer to the list.
  /// \param O Observer to add.
  void addObserver(GISelChangeObserver *O) { Observers.push_back(O); }
  /// Remove an observer from the list if present.
  /// \param O Observer to remove.
  void removeObserver(GISelChangeObserver *O) {
    auto It = llvm::find(Observers, O);
    if (It != Observers.end())
      Observers.erase(It);
  }
  /// Remove all observers from the list.
  void clearObservers() { Observers.clear(); }

  /// Forward an erase notification to every registered observer.
  /// \param MI Instruction that is about to be erased.
  void erasingInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->erasingInstr(MI);
  }
  /// Forward a create notification to every registered observer.
  /// \param MI Instruction that was created and inserted.
  void createdInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->createdInstr(MI);
  }
  /// Forward a changing notification to every registered observer.
  /// \param MI Instruction that is about to be mutated.
  void changingInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->changingInstr(MI);
  }
  /// Forward a changed notification to every registered observer.
  /// \param MI Instruction that was mutated.
  void changedInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->changedInstr(MI);
  }
  /// MachineFunction::Delegate hook that treats insertion as createdInstr.
  /// \param MI Instruction that was inserted.
  void MF_HandleInsertion(MachineInstr &MI) override { createdInstr(MI); }
  /// MachineFunction::Delegate hook that treats removal as erasingInstr.
  /// \param MI Instruction that is being removed.
  void MF_HandleRemoval(MachineInstr &MI) override { erasingInstr(MI); }
};

/// A simple RAII based Delegate installer.
/// Use this in a scope to install a delegate to the MachineFunction and reset
/// it at the end of the scope.
class RAIIDelegateInstaller {
  MachineFunction &MF;
  MachineFunction::Delegate *Delegate;

public:
  /// Install \p Del as the MachineFunction delegate for this object's lifetime.
  /// \param MF Machine function whose delegate is temporarily replaced.
  /// \param Del Delegate to install on \p MF.
  LLVM_ABI RAIIDelegateInstaller(MachineFunction &MF,
                                 MachineFunction::Delegate *Del);
  /// Restore the previous MachineFunction delegate.
  LLVM_ABI ~RAIIDelegateInstaller();
};

/// A simple RAII based Observer installer.
/// Use this in a scope to install the Observer to the MachineFunction and reset
/// it at the end of the scope.
class RAIIMFObserverInstaller {
  MachineFunction &MF;

public:
  /// Install \p Observer on \p MF for this object's lifetime.
  /// \param MF Machine function that receives the observer.
  /// \param Observer Observer to install on \p MF.
  LLVM_ABI RAIIMFObserverInstaller(MachineFunction &MF,
                                   GISelChangeObserver &Observer);
  /// Remove the installed observer from the MachineFunction.
  LLVM_ABI ~RAIIMFObserverInstaller();
};

/// Class to install both of the above.
class RAIIMFObsDelInstaller {
  RAIIDelegateInstaller DelI;
  RAIIMFObserverInstaller ObsI;

public:
  /// Install both the delegate and observer roles of \p Wrapper on \p MF.
  /// \param MF Machine function that receives the wrapper.
  /// \param Wrapper Observer wrapper installed as both delegate and observer.
  RAIIMFObsDelInstaller(MachineFunction &MF, GISelObserverWrapper &Wrapper)
      : DelI(MF, &Wrapper), ObsI(MF, Wrapper) {}
  /// Restore the previous MachineFunction delegate and observer.
  ~RAIIMFObsDelInstaller() = default;
};

/// A simple RAII based Observer installer.
/// Use this in a scope to install the Observer to the MachineFunction and reset
/// it at the end of the scope.
class RAIITemporaryObserverInstaller {
public:
  /// Temporarily add \p TemporaryObserver to \p Observers for this lifetime.
  /// \param Observers Wrapper that receives the temporary observer.
  /// \param TemporaryObserver Observer added for the duration of this object.
  LLVM_ABI
  RAIITemporaryObserverInstaller(GISelObserverWrapper &Observers,
                                 GISelChangeObserver &TemporaryObserver);
  /// Remove the temporary observer from the wrapper.
  LLVM_ABI ~RAIITemporaryObserverInstaller();

private:
  GISelObserverWrapper &Observers;
  GISelChangeObserver &TemporaryObserver;
};

} // namespace llvm
#endif
