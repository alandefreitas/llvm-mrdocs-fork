//===--------------------- SourceMgr.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file contains abstract class SourceMgr and the default implementation,
/// CircularSourceMgr.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_SOURCEMGR_H
#define LLVM_MCA_SOURCEMGR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/MCA/Instruction.h"

namespace llvm {
namespace mca {

// MSVC >= 19.15, < 19.20 need to see the definition of class Instruction to
// prevent compiler error C2139 about intrinsic type trait '__is_assignable'.
/// Pair of a source index and the corresponding \a Instruction.
typedef std::pair<unsigned, const Instruction &> SourceRef;

/// Abstracting the input code sequence (a sequence of MCInst) and assigning
/// unique identifiers to every instruction in the sequence.
struct SourceMgr {
  /// Owning pointer to a decoded MCA \a Instruction.
  using UniqueInst = std::unique_ptr<Instruction>;

  /// Provides a fixed range of \a UniqueInst to iterate.
  /// \return A fixed range of owning instruction pointers.
  virtual ArrayRef<UniqueInst> getInstructions() const = 0;

  /// (Fixed) Number of \a UniqueInst. Returns the size of
  /// \a getInstructions by default.
  /// \return The number of instructions in \a getInstructions.
  virtual size_t size() const { return getInstructions().size(); }

  /// Whether there is any \a SourceRef to inspect / peek next.
  /// Note that returning false from this doesn't mean the instruction
  /// stream has ended.
  /// \return True if there is a next \a SourceRef to peek.
  virtual bool hasNext() const = 0;

  /// Whether the instruction stream has eneded.
  /// \return True if the instruction stream has ended.
  virtual bool isEnd() const = 0;

  /// The next \a SourceRef.
  /// \return The next source index paired with its instruction.
  virtual SourceRef peekNext() const = 0;

  /// Advance to the next \a SourceRef.
  virtual void updateNext() = 0;

  /// Virtual destructor for polymorphic source managers.
  virtual ~SourceMgr() = default;
};

/// Default \a SourceMgr over a fixed instruction sequence with optional looping.
///
/// Always takes a fixed number of instructions and provides an option to loop
/// the given sequence for a certain number of iterations.
class CircularSourceMgr : public SourceMgr {
  ArrayRef<UniqueInst> Sequence;
  unsigned Current;
  const unsigned Iterations;
  static const unsigned DefaultIterations = 100;

public:
  /// Construct a circular source manager over sequence \p S.
  ///
  /// If \p Iter is zero, \a DefaultIterations (100) is used instead.
  /// \param S Fixed sequence of instructions to iterate.
  /// \param Iter Number of times to loop over \p S; zero means use the default.
  CircularSourceMgr(ArrayRef<UniqueInst> S, unsigned Iter)
      : Sequence(S), Current(0U), Iterations(Iter ? Iter : DefaultIterations) {}

  /// Provides the fixed range of \a UniqueInst managed by this source manager.
  /// \return The fixed instruction sequence managed by this source manager.
  ArrayRef<UniqueInst> getInstructions() const override { return Sequence; }

  /// Number of times the instruction sequence will be iterated.
  /// \return The configured iteration count for the sequence.
  unsigned getNumIterations() const { return Iterations; }
  /// Whether there is any \a SourceRef left before the iteration limit.
  /// \return True if more instructions remain before the iteration limit.
  bool hasNext() const override {
    return Current < (Iterations * Sequence.size());
  }
  /// Whether the instruction stream has ended.
  /// \return True if the iteration limit has been reached.
  bool isEnd() const override { return !hasNext(); }

  /// The next \a SourceRef in the circular sequence.
  /// \return The next source index paired with its instruction.
  SourceRef peekNext() const override {
    assert(hasNext() && "Already at end of sequence!");
    return SourceRef(Current, *Sequence[Current % Sequence.size()]);
  }

  /// Advance to the next \a SourceRef.
  void updateNext() override { ++Current; }
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_SOURCEMGR_H
