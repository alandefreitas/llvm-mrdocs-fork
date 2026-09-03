//===- llvm/CodeGen/PBQPRAConstraint.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the PBQPBuilder interface, for classes which build PBQP
// instances to represent register allocation problems, and the RegAllocPBQP
// interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PBQPRACONSTRAINT_H
#define LLVM_CODEGEN_PBQPRACONSTRAINT_H

#include "llvm/Support/Compiler.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace llvm {

namespace PBQP {
/// Namespace for PBQP register-allocation graphs and related helpers.
namespace RegAlloc {

// Forward declare PBQP graph class.
class PBQPRAGraph;

} // end namespace RegAlloc
} // end namespace PBQP

/// Alias for the PBQP register-allocation graph type.
using PBQPRAGraph = PBQP::RegAlloc::PBQPRAGraph;

/// Abstract base for classes implementing PBQP register allocation
///        constraints (e.g. Spill-costs, interference, coalescing).
class LLVM_ABI PBQPRAConstraint {
public:
  /// Virtual destructor.
  virtual ~PBQPRAConstraint() = 0;
  /// Apply this constraint to the PBQP register-allocation graph \p G.
  ///
  /// \param G Graph to which the constraint is applied.
  virtual void apply(PBQPRAGraph &G) = 0;

private:
  virtual void anchor();
};

/// PBQP register allocation constraint composer.
///
///   Constraints added to this list will be applied, in the order that they are
/// added, to the PBQP graph.
class LLVM_ABI PBQPRAConstraintList : public PBQPRAConstraint {
public:
  // Explicitly non-copyable.
  /// Construct an empty constraint list.
  PBQPRAConstraintList() = default;
  /// Copy assignment is deleted; constraint lists are not copyable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  PBQPRAConstraintList &operator=(const PBQPRAConstraintList &Other) = delete;
  /// Copy construction is deleted; constraint lists are not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  PBQPRAConstraintList(const PBQPRAConstraintList &Other) = delete;

  /// Apply all constraints in this list to the PBQP graph \p G.
  ///
  /// Constraints are applied in the order they were added.
  /// \param G Graph to which the constraints are applied.
  void apply(PBQPRAGraph &G) override {
    for (auto &C : Constraints)
      C->apply(G);
  }

  /// Append a constraint to this list if \p C is non-null.
  ///
  /// \param C Constraint to add; null pointers are ignored.
  void addConstraint(std::unique_ptr<PBQPRAConstraint> C) {
    if (C)
      Constraints.push_back(std::move(C));
  }

private:
  std::vector<std::unique_ptr<PBQPRAConstraint>> Constraints;

  void anchor() override;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_PBQPRACONSTRAINT_H
