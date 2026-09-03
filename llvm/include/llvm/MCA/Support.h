//===--------------------- Support.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Helper functions used by various pipeline components.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_SUPPORT_H
#define LLVM_MCA_SUPPORT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"

namespace llvm {
namespace mca {

/// Error carrying a human-readable message and the instruction that failed.
template <typename T>
class InstructionError : public ErrorInfo<InstructionError<T>> {
public:
  /// RTTI identifier used by ErrorInfo::classID.
  static char ID;
  /// Human-readable description of the failure.
  std::string Message;
  /// Instruction associated with this error.
  const T &Inst;

  /// Construct an error from message \p M and instruction \p MCI.
  ///
  /// \param M Human-readable failure message.
  /// \param MCI Instruction that caused the error.
  InstructionError(std::string M, const T &MCI)
      : Message(std::move(M)), Inst(MCI) {}

  /// Write this error's message to \p OS.
  ///
  /// \param OS Stream to receive the message.
  void log(raw_ostream &OS) const override { OS << Message; }

  /// Convert this error to a \c std::error_code.
  ///
  /// \return An inconvertible error code.
  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }
};

template <typename T> char InstructionError<T>::ID;

/// Ratio of resource cycles used to compute average pressure.
///
/// This class represents the number of cycles per resource (fractions of
/// cycles). That quantity is managed here as a ratio, and accessed via the
/// double cast-operator below. The two quantities, number of cycles and
/// number of resources, are kept separate. This is used by the
/// ResourcePressureView to calculate the average resource cycles
/// per instruction/iteration.
class ReleaseAtCycles {
  unsigned Numerator, Denominator;

public:
  /// Construct a zero-cycle ratio with denominator one.
  ReleaseAtCycles() : Numerator(0), Denominator(1) {}
  /// Construct a ratio of \p Cycles cycles over \p ResourceUnits resources.
  ///
  /// \param Cycles Number of resource cycles (numerator).
  /// \param ResourceUnits Number of resource units (denominator); defaults to 1.
  ReleaseAtCycles(unsigned Cycles, unsigned ResourceUnits = 1)
      : Numerator(Cycles), Denominator(ResourceUnits) {}

  /// Convert this ratio to a floating-point cycle count.
  ///
  /// \return The ratio as a double-precision value.
  operator double() const {
    assert(Denominator && "Invalid denominator (must be non-zero).");
    return (Denominator == 1) ? Numerator : (double)Numerator / Denominator;
  }

  /// Return the cycle-count numerator of this ratio.
  ///
  /// \return The numerator (resource cycles).
  unsigned getNumerator() const { return Numerator; }
  /// Return the resource-unit denominator of this ratio.
  ///
  /// \return The denominator (resource units).
  unsigned getDenominator() const { return Denominator; }

  /// Add the components of \p RHS to this instance.
  ///
  /// Instead of calculating the final value here, we keep track of the
  /// numerator and denominator separately, to reduce floating point error.
  ///
  /// \param RHS Ratio whose numerator and denominator are added in.
  /// \return A reference to this updated ratio.
  LLVM_ABI ReleaseAtCycles &operator+=(const ReleaseAtCycles &RHS);
};

/// Populates vector Masks with processor resource masks.
///
/// The number of bits set in a mask depends on the processor resource type.
/// Each processor resource mask has at least one bit set. For groups, the
/// number of bits set in the mask is equal to the cardinality of the group plus
/// one. Excluding the most significant bit, the remaining bits in the mask
/// identify processor resources that are part of the group.
///
/// Example:
///
///  ResourceA  -- Mask: 0b001
///  ResourceB  -- Mask: 0b010
///  ResourceAB -- Mask: 0b100 U (ResourceA::Mask | ResourceB::Mask) == 0b111
///
/// ResourceAB is a processor resource group containing ResourceA and ResourceB.
/// Each resource mask uniquely identifies a resource; both ResourceA and
/// ResourceB only have one bit set.
/// ResourceAB is a group; excluding the most significant bit in the mask, the
/// remaining bits identify the composition of the group.
///
/// Resource masks are used by the ResourceManager to solve set membership
/// problems with simple bit manipulation operations.
///
/// \param SM Scheduling model that defines the processor resources.
/// \param Masks Output array filled with one mask per processor resource.
LLVM_ABI void computeProcResourceMasks(const MCSchedModel &SM,
                                       MutableArrayRef<uint64_t> Masks);

#ifndef NDEBUG
/// Dump processor resource masks from \p SM for debugging.
///
/// \param SM Scheduling model that defines the processor resources.
/// \param Masks Resource masks previously computed for \p SM.
LLVM_ABI void dumpProcResourceMasks(const MCSchedModel &SM,
                                    ArrayRef<uint64_t> Masks);
#endif

/// Return the index of the highest bit set in \p Mask.
///
/// For resource masks, the position of the highest bit set can be used to
/// construct a resource mask identifier.
///
/// \param Mask Non-zero processor resource mask.
/// \return Zero-based index of the highest bit set in \p Mask.
inline unsigned getResourceStateIndex(uint64_t Mask) {
  assert(Mask && "Processor Resource Mask cannot be zero!");
  return llvm::Log2_64(Mask);
}

/// Compute the reciprocal block throughput for a code block.
///
/// The reciprocal block throughput is computed as the MAX between:
///  - NumMicroOps / DispatchWidth
///  - ProcReleaseAtCycles / #ProcResourceUnits  (for every consumed resource).
///
/// \param SM Scheduling model used to interpret resource usage.
/// \param DispatchWidth Maximum number of micro-ops dispatched per cycle.
/// \param NumMicroOps Total micro-ops in the block.
/// \param ProcResourceUsage Per-resource cycle counts consumed by the block.
/// \return Reciprocal throughput of the block in cycles per iteration.
LLVM_ABI double computeBlockRThroughput(const MCSchedModel &SM,
                                        unsigned DispatchWidth,
                                        unsigned NumMicroOps,
                                        ArrayRef<unsigned> ProcResourceUsage);
} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_SUPPORT_H
