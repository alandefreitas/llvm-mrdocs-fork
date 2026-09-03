//===- Pass.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_PASS_H
#define LLVM_SANDBOXIR_PASS_H

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class AAResults;
class ScalarEvolution;
class TargetTransformInfo;

namespace sandboxir {

class Function;
class Region;

/// Analysis results available to Sandbox IR passes.
class Analyses {
  AAResults *AA = nullptr;
  ScalarEvolution *SE = nullptr;
  TargetTransformInfo *TTI = nullptr;

  Analyses() = default;

public:
  /// Construct analyses from alias, SCEV, and TTI results.
  /// \param AA Alias analysis results.
  /// \param SE Scalar evolution analysis.
  /// \param TTI Target transform info.
  Analyses(AAResults &AA, ScalarEvolution &SE, TargetTransformInfo &TTI)
      : AA(&AA), SE(&SE), TTI(&TTI) {}

public:
  /// Alias analysis results.
  /// \Returns The alias analysis results.
  AAResults &getAA() const { return *AA; }
  /// Scalar evolution analysis.
  /// \Returns The scalar evolution analysis.
  ScalarEvolution &getScalarEvolution() const { return *SE; }
  /// Target transform info.
  /// \Returns The target transform info.
  TargetTransformInfo &getTTI() const { return *TTI; }
  /// For use by unit tests.
  /// \Returns An Analyses instance with null analysis pointers.
  static Analyses emptyForTesting() { return Analyses(); }
};

/// The base class of a Sandbox IR Pass.
class Pass {
protected:
  /// The pass name. This is also used as a command-line flag and should not
  /// contain whitespaces.
  const std::string Name;

public:
  /// Construct a pass with the given name.
  /// \p Name can't contain any spaces or start with '-'.
  /// \param Name Pass name; must not contain spaces or start with '-'.
  Pass(StringRef Name) : Name(Name) {
    assert(!Name.contains(' ') &&
           "A pass name should not contain whitespaces!");
    assert(!Name.starts_with('-') && "A pass name should not start with '-'!");
  }
  /// Destroy the pass.
  virtual ~Pass() = default;
  /// The name of the pass.
  /// \Returns The pass name.
  StringRef getName() const { return Name; }
#ifndef NDEBUG
  /// Print \p Pass to \p OS.
  /// \param OS Output stream.
  /// \param Pass Pass to print.
  /// \Returns \p OS after printing \p Pass.
  friend raw_ostream &operator<<(raw_ostream &OS, const Pass &Pass) {
    Pass.print(OS);
    return OS;
  }
  /// Print this pass to \p OS.
  /// \param OS Output stream.
  virtual void print(raw_ostream &OS) const { OS << Name; }
  /// Dump this pass to the debug stream.
  LLVM_ABI LLVM_DUMP_METHOD virtual void dump() const;
#endif
  /// Similar to print() but adds a newline. Used for testing.
  /// \param OS Output stream.
  virtual void printPipeline(raw_ostream &OS) const { OS << Name << "\n"; }
};

/// A pass that runs on a sandbox::Function.
class FunctionPass : public Pass {
public:
  /// Construct a function pass with the given name.
  /// \p Name can't contain any spaces or start with '-'.
  /// \param Name Pass name; must not contain spaces or start with '-'.
  FunctionPass(StringRef Name) : Pass(Name) {}
  /// Run this pass on function \p F.
  /// \param F Function to transform.
  /// \param A Analyses available to the pass.
  /// \Returns true if it modifies \p F.
  virtual bool runOnFunction(Function &F, const Analyses &A) = 0;
};

/// A pass that runs on a sandbox::Region.
class RegionPass : public Pass {
public:
  /// Construct a region pass with the given name.
  /// \p Name can't contain any spaces or start with '-'.
  /// \param Name Pass name; must not contain spaces or start with '-'.
  RegionPass(StringRef Name) : Pass(Name) {}
  /// Run this pass on region \p R.
  /// \param R Region to transform.
  /// \param A Analyses available to the pass.
  /// \Returns true if it modifies \p R.
  virtual bool runOnRegion(Region &R, const Analyses &A) = 0;
};

} // namespace sandboxir
} // namespace llvm

#endif // LLVM_SANDBOXIR_PASS_H
