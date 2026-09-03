//===- llvm/PassInfo.h - Pass Info class ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines and implements the PassInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSINFO_H
#define LLVM_PASSINFO_H

#include "llvm/ADT/StringRef.h"
#include <cassert>

namespace llvm {

class Pass;

//===---------------------------------------------------------------------------
/// Metadata describing a single pass known to the pass registry.
///
/// An instance of this class exists for every pass known by the system, and
/// can be obtained from a live Pass by calling its getPassInfo() method.
/// These objects are set up by the RegisterPass<> template.
///
class PassInfo {
public:
  /// Function pointer type that default-constructs a Pass instance.
  using NormalCtor_t = Pass* (*)();

private:
  StringRef PassName;     // Nice name for Pass
  StringRef PassArgument; // Command Line argument to run this pass
  const void *PassID;
  const bool IsCFGOnlyPass = false;      // Pass only looks at the CFG.
  const bool IsAnalysis;                 // True if an analysis pass.
  NormalCtor_t NormalCtor = nullptr;

public:
  /// PassInfo ctor - Do not call this directly, this should only be invoked
  /// through RegisterPass.
  /// \param name Friendly display name for the pass.
  /// \param arg Command-line argument that selects this pass.
  /// \param pi Unique type-id pointer for the pass.
  /// \param normal Default constructor function, or null if none.
  /// \param isCFGOnly True if the pass only inspects the CFG.
  /// \param is_analysis True if this is an analysis pass.
  PassInfo(StringRef name, StringRef arg, const void *pi, NormalCtor_t normal,
           bool isCFGOnly, bool is_analysis)
      : PassName(name), PassArgument(arg), PassID(pi), IsCFGOnlyPass(isCFGOnly),
        IsAnalysis(is_analysis), NormalCtor(normal) {}

  /// PassInfo is not copyable; register each pass once via RegisterPass.
  /// \param Other Unused; copy construction is deleted.
  PassInfo(const PassInfo &Other) = delete;

  /// Assignment is deleted; register each pass once via RegisterPass.
  /// \param Other Unused; copy assignment is deleted.
  PassInfo &operator=(const PassInfo &Other) = delete;

  /// getPassName - Return the friendly name for the pass, never returns null
  /// @return The friendly display name for the pass.
  StringRef getPassName() const { return PassName; }

  /// getPassArgument - Return the command line option that may be passed to
  /// 'opt' that will cause this pass to be run.  This will return null if there
  /// is no argument.
  /// @return The command-line argument for this pass, or empty if none.
  StringRef getPassArgument() const { return PassArgument; }

  /// getTypeInfo - Return the id object for the pass...
  /// TODO : Rename
  /// @return The unique type-id pointer for the pass.
  const void *getTypeInfo() const { return PassID; }

  /// Return true if this PassID implements the specified ID pointer.
  /// \param IDPtr Pass type-id pointer to compare against.
  /// @return True if this pass's type-id matches \p IDPtr.
  bool isPassID(const void *IDPtr) const { return PassID == IDPtr; }

  /// Return true if this pass is an analysis pass.
  /// @return True if this is an analysis pass.
  bool isAnalysis() const { return IsAnalysis; }

  /// isCFGOnlyPass - return true if this pass only looks at the CFG for the
  /// function.
  /// @return True if this pass only looks at the CFG.
  bool isCFGOnlyPass() const { return IsCFGOnlyPass; }

  /// Return a pointer to a function that creates an instance of this pass.
  ///
  /// When called, the function creates an instance of the pass and returns it.
  /// This pointer may be null if there is no default constructor for the pass.
  /// @return The default constructor function, or null if none.
  NormalCtor_t getNormalCtor() const {
    return NormalCtor;
  }
  /// setNormalCtor - Set the default constructor function used by createPass().
  /// \param Ctor Function that constructs a Pass, or null.
  void setNormalCtor(NormalCtor_t Ctor) {
    NormalCtor = Ctor;
  }

  /// createPass() - Use this method to create an instance of this pass.
  /// @return A newly constructed Pass instance.
  Pass *createPass() const {
    assert(NormalCtor &&
           "Cannot call createPass on PassInfo without default ctor!");
    return NormalCtor();
  }
};

} // end namespace llvm

#endif // LLVM_PASSINFO_H
