//===- LiveRegMatrix.h - Track register interference ----------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The LiveRegMatrix analysis pass keeps track of virtual register interference
// along two dimensions: Slot indexes and register units. The matrix is used by
// register allocators to ensure that no interfering virtual registers get
// assigned to overlapping physical registers.
//
// Register units are defined in MCRegisterInfo.h, they represent the smallest
// unit of interference when dealing with overlapping physical registers. The
// LiveRegMatrix is represented as a LiveIntervalUnion per register unit. When
// a virtual register is assigned to a physical register, the live range for
// the virtual register is inserted into the LiveIntervalUnion for each regunit
// in the physreg.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEREGMATRIX_H
#define LLVM_CODEGEN_LIVEREGMATRIX_H

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/LiveIntervalUnion.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include <memory>

namespace llvm {

class AnalysisUsage;
class LiveInterval;
class LiveIntervals;
class MachineFunction;
class TargetRegisterInfo;
class VirtRegMap;

/// Tracks virtual register interference across slot indexes and register units.
class LiveRegMatrix {
  friend class LiveRegMatrixWrapperLegacy;
  friend class LiveRegMatrixAnalysis;
  const TargetRegisterInfo *TRI = nullptr;
  LiveIntervals *LIS = nullptr;
  VirtRegMap *VRM = nullptr;

  // UserTag changes whenever virtual registers have been modified.
  unsigned UserTag = 0;

  // The matrix is represented as a LiveIntervalUnion per register unit.
  std::unique_ptr<LiveIntervalUnion::Allocator> LIUAlloc;
  LiveIntervalUnion::Array Matrix;

  // Cached queries per register unit.
  std::unique_ptr<LiveIntervalUnion::Query[]> Queries;

  // Cached register mask interference info.
  unsigned RegMaskTag = 0;
  Register RegMaskVirtReg;
  BitVector RegMaskUsable;

  LiveRegMatrix()
      : LIUAlloc(std::make_unique<LiveIntervalUnion::Allocator>()) {};
  void releaseMemory();

public:
  /// Move-construct a LiveRegMatrix.
  ///
  /// \param Other Matrix to move from.
  LiveRegMatrix(LiveRegMatrix &&Other) = default;

  /// Initialize the matrix for machine function \p MF.
  ///
  /// \param MF Machine function whose registers are tracked.
  /// \param LIS Live intervals for \p MF.
  /// \param VRM Virtual-to-physical register map to update on assign/unassign.
  LLVM_ABI void init(MachineFunction &MF, LiveIntervals &LIS, VirtRegMap &VRM);

  //===--------------------------------------------------------------------===//
  // High-level interface.
  //===--------------------------------------------------------------------===//
  //
  // Check for interference before assigning virtual registers to physical
  // registers.
  //

  /// Invalidate cached interference queries.
  ///
  /// Call after modifying virtual register live ranges. Interference checks may
  /// return stale information unless caches are invalidated.
  void invalidateVirtRegs() { ++UserTag; }

  /// Kinds of interference that can block assignment of a virtual register.
  enum InterferenceKind {
    /// No interference, go ahead and assign.
    IK_Free = 0,

    /// Virtual register interference. There are interfering virtual registers
    /// assigned to PhysReg or its aliases. This interference could be resolved
    /// by unassigning those other virtual registers.
    IK_VirtReg,

    /// Register unit interference. A fixed live range is in the way, typically
    /// argument registers for a call. This can't be resolved by unassigning
    /// other virtual registers.
    IK_RegUnit,

    /// RegMask interference. The live range is crossing an instruction with a
    /// regmask operand that doesn't preserve PhysReg. This typically means
    /// VirtReg is live across a call, and PhysReg isn't call-preserved.
    IK_RegMask
  };

  /// Check for interference before assigning VirtReg to PhysReg.
  ///
  /// If this function returns IK_Free, it is legal to assign(VirtReg, PhysReg).
  /// When there is more than one kind of interference, the InterferenceKind
  /// with the highest enum value is returned.
  ///
  /// \param VirtReg Virtual register live interval to assign.
  /// \param PhysReg Physical register candidate for assignment.
  /// \return Strongest interference kind found, or IK_Free if none.
  LLVM_ABI InterferenceKind checkInterference(const LiveInterval &VirtReg,
                                              MCRegister PhysReg);

  /// Check for interference in a segment that may prevent assignment to PhysReg.
  ///
  /// If this function returns true, there is interference in the segment
  /// [Start, End) of some other interval already assigned to PhysReg. If this
  /// function returns false, PhysReg is free at the segment [Start, End).
  ///
  /// \param Start Inclusive start of the slot-index segment.
  /// \param End Exclusive end of the slot-index segment.
  /// \param PhysReg Physical register to test for interference.
  /// \return True if some assigned interval interferes in [Start, End).
  LLVM_ABI bool checkInterference(SlotIndex Start, SlotIndex End,
                                  MCRegister PhysReg);

  /// Check for lane-level interference in a segment for PhysReg.
  ///
  /// Like checkInterference, but returns a lane mask of which lanes of the
  /// physical register interfere in the segment [Start, End) of some other
  /// interval already assigned to PhysReg.
  ///
  /// If this function returns LaneBitmask::getNone(), PhysReg is completely
  /// free at the segment [Start, End).
  ///
  /// \param Start Inclusive start of the slot-index segment.
  /// \param End Exclusive end of the slot-index segment.
  /// \param PhysReg Physical register to test for lane interference.
  /// \return Lane mask of interfering lanes, or none if PhysReg is free.
  LLVM_ABI LaneBitmask checkInterferenceLanes(SlotIndex Start, SlotIndex End,
                                              MCRegister PhysReg);

  /// Assign VirtReg to PhysReg.
  ///
  /// This will mark VirtReg's live range as occupied in the LiveRegMatrix and
  /// update VirtRegMap. The live range is expected to be available in PhysReg.
  ///
  /// \param VirtReg Virtual register live interval to assign.
  /// \param PhysReg Physical register to assign to.
  LLVM_ABI void assign(const LiveInterval &VirtReg, MCRegister PhysReg);

  /// Unassign VirtReg from its physical register.
  ///
  /// Assuming that VirtReg was previously assigned to a PhysReg, this undoes
  /// the assignment and updates VirtRegMap accordingly.
  /// ClearAllReferencingSegments changes the way segments are removed from
  /// the matrix:
  ///   - If false (default), only segments that exactly match VirtReg's live
  ///     range are removed.
  ///   - If true, all segments that reference VirtReg are removed. This is
  ///     useful when VirtReg's live range(s) is already empty.
  ///
  /// \param VirtReg Virtual register live interval to unassign.
  /// \param ClearAllReferencingSegments If true, remove every matrix segment
  ///        that references VirtReg; otherwise remove only exact matches.
  LLVM_ABI void unassign(const LiveInterval &VirtReg,
                         bool ClearAllReferencingSegments = false);

  /// Returns true if the given \p PhysReg has any live intervals assigned.
  ///
  /// \param PhysReg Physical register to query.
  /// \return True if \p PhysReg has any assigned live intervals.
  LLVM_ABI bool isPhysRegUsed(MCRegister PhysReg) const;

  //===--------------------------------------------------------------------===//
  // Low-level interface.
  //===--------------------------------------------------------------------===//
  //
  // Provide access to the underlying LiveIntervalUnions.
  //

  /// Check for regmask interference only.
  ///
  /// Return true if VirtReg crosses a regmask operand that clobbers PhysReg.
  /// If PhysReg is null, check if VirtReg crosses any regmask operands.
  ///
  /// \param VirtReg Virtual register live interval to check.
  /// \param PhysReg Physical register that may be clobbered, or NoRegister to
  ///        test against any regmask.
  /// \return True if VirtReg crosses a clobbering regmask for PhysReg.
  LLVM_ABI bool
  checkRegMaskInterference(const LiveInterval &VirtReg,
                           MCRegister PhysReg = MCRegister::NoRegister);

  /// Check for regunit interference only.
  ///
  /// Return true if VirtReg overlaps a fixed assignment of one of PhysRegs's
  /// register units.
  ///
  /// \param VirtReg Virtual register live interval to check.
  /// \param PhysReg Physical register whose units are tested for overlap.
  /// \return True if VirtReg overlaps a fixed assignment of a PhysReg unit.
  LLVM_ABI bool checkRegUnitInterference(const LiveInterval &VirtReg,
                                         MCRegister PhysReg);

  /// Query a line of the assigned virtual register matrix directly.
  ///
  /// Use MCRegUnitIterator to enumerate all regunits in the desired PhysReg.
  /// This returns a reference to an internal Query data structure that is only
  /// valid until the next query() call.
  ///
  /// \param LR Live range to query against the matrix.
  /// \param RegUnit Register unit whose assigned intervals are queried.
  /// \return Reference to an internal query valid until the next query() call.
  LLVM_ABI LiveIntervalUnion::Query &query(const LiveRange &LR,
                                           MCRegUnit RegUnit);

  /// Directly access the live interval unions per regunit.
  ///
  /// The returned array is indexed by the regunit number.
  ///
  /// \return Pointer to the first LiveIntervalUnion in the per-regunit array.
  LiveIntervalUnion *getLiveUnions() {
    return &Matrix[static_cast<MCRegUnit>(0)];
  }

  /// Return one virtual register currently assigned to \p PhysReg.
  ///
  /// \param PhysReg Physical register to inspect.
  /// \return A virtual register assigned to \p PhysReg, or an invalid register
  ///         if none is assigned.
  LLVM_ABI Register getOneVReg(unsigned PhysReg) const;

#ifndef NDEBUG
  /// Check that each LiveInterval referenced in LiveIntervalUnion exists.
  ///
  /// Verifies that every LiveInterval referenced in LiveIntervalUnion actually
  /// exists in LiveIntervals and is not a dangling pointer.
  ///
  /// \return True if all referenced live intervals are valid.
  bool isValid() const;
#endif
};

/// Legacy MachineFunctionPass wrapper that computes and owns \c LiveRegMatrix.
class LLVM_ABI LiveRegMatrixWrapperLegacy : public MachineFunctionPass {
  LiveRegMatrix LRM;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy LiveRegMatrix wrapper pass.
  LiveRegMatrixWrapperLegacy() : MachineFunctionPass(ID) {}

  /// Return the LiveRegMatrix computed by this pass.
  ///
  /// \return Mutable reference to the owned LiveRegMatrix.
  LiveRegMatrix &getLRM() { return LRM; }
  /// Return the LiveRegMatrix computed by this pass.
  ///
  /// \return Const reference to the owned LiveRegMatrix.
  const LiveRegMatrix &getLRM() const { return LRM; }

  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Run LiveRegMatrix on machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \return False; this analysis does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &MF) override;
  /// Release memory used by the wrapped matrix.
  void releaseMemory() override;
};

/// Analysis pass that computes \c LiveRegMatrix for a machine function.
class LiveRegMatrixAnalysis : public AnalysisInfoMixin<LiveRegMatrixAnalysis> {
  friend AnalysisInfoMixin<LiveRegMatrixAnalysis>;
  static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = LiveRegMatrix;

  /// Compute LiveRegMatrix for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Analysis manager for the machine function.
  /// \return Live register matrix for \p MF.
  LLVM_ABI LiveRegMatrix run(MachineFunction &MF,
                             MachineFunctionAnalysisManager &MFAM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_LIVEREGMATRIX_H
