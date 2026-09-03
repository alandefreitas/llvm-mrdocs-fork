//==------------------------------------------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SDNODEINFO_H
#define LLVM_CODEGEN_SDNODEINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringTable.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"

namespace llvm {

class SDNode;
class SelectionDAG;

/// SelectionDAG node properties used by generated SDNode descriptions.
enum SDNP {
  /// Node reads and writes a chain operand and result.
  SDNPHasChain,
  /// Node produces a glue result.
  SDNPOutGlue,
  /// Node requires a glue operand.
  SDNPInGlue,
  /// Node optionally accepts a glue operand.
  SDNPOptInGlue,
  /// Node touches memory and has an associated memory operand.
  SDNPMemOperand,
  /// Node has a variable number of operands.
  SDNPVariadic,
};

/// SelectionDAG type-constraint kinds used by generated SDNode descriptions.
enum SDTC : uint8_t {
  /// Constrained value must have exactly the given value type.
  SDTCisVT,
  /// Constrained value must have pointer type.
  SDTCisPtrTy,
  /// Constrained value must have integer type.
  SDTCisInt,
  /// Constrained value must have floating-point type.
  SDTCisFP,
  /// Constrained value must have vector type.
  SDTCisVec,
  /// Constrained value must have the same type as the constraining value.
  SDTCisSameAs,
  /// Constrained VT node must be smaller than the constraining operand.
  SDTCisVTSmallerThanOp,
  /// Constrained operand must be smaller than the constraining operand.
  SDTCisOpSmallerThanOp,
  /// Constrained value is a scalar element type of the constraining vector.
  SDTCisEltOfVec,
  /// Constrained value is a shorter subvector of the constraining vector.
  SDTCisSubVecOfVec,
  /// Constrained vector must have the given element type.
  SDTCVecEltisVT,
  /// Constrained value must have the same element count as the constraining
  /// value.
  SDTCisSameNumEltsAs,
  /// Constrained value must have the same size as the constraining value.
  SDTCisSameSizeAs,
};

/// SelectionDAG node flags used by generated SDNode descriptions.
enum SDNF {
  /// Node is a strict floating-point operation.
  SDNFIsStrictFP,
};

/// Hardware-mode and value-type pair in a generated VT-by-HwMode table.
struct VTByHwModePair {
  /// Hardware mode identifier for this entry.
  uint8_t Mode;
  /// Simple value type associated with \p Mode.
  MVT::SimpleValueType VT;
};

/// Generated type constraint for a SelectionDAG node result or operand.
struct SDTypeConstraint {
  /// Kind of type constraint to apply.
  SDTC Kind;
  /// Index of the result or operand being constrained.
  ///
  /// Indexes node values after results and before chain/glue values.
  uint8_t ConstrainedValIdx;
  /// Index of the related result or operand used by relational constraints.
  uint8_t ConstrainingValIdx;
  /// Number of HwMode entries when \p VT indexes \c VTByHwModeTable.
  ///
  /// For Kind == SDTCisVT or SDTCVecEltisVT:
  /// - if not using HwMode, NumHwModes == 0 and VT is MVT::SimpleValueType;
  /// - otherwise, VT is offset into VTByHwModeTable and NumHwModes specifies
  ///   the number of entries.
  uint8_t NumHwModes;
  /// Expected value type, or offset into the VT-by-HwMode table.
  uint16_t VT;
};

/// Target-specific SelectionDAG node flags packed into a 32-bit word.
using SDNodeTSFlags = uint32_t;

/// Generated description of a target-specific SelectionDAG node.
struct SDNodeDesc {
  /// Number of normal (non-chain, non-glue) results.
  uint16_t NumResults;
  /// Number of normal operands, or negative if the fixed count is unknown.
  int16_t NumOperands;
  /// Bitmask of \c SDNP properties for this node.
  uint32_t Properties;
  /// Bitmask of \c SDNF flags for this node.
  uint32_t Flags;
  /// Target-specific flags from the SDNode TableGen definition.
  SDNodeTSFlags TSFlags;
  /// Offset of this node's name in the generated string table.
  unsigned NameOffset;
  /// Offset of this node's type constraints in the constraint table.
  unsigned ConstraintOffset;
  /// Number of type constraints for this node.
  unsigned ConstraintCount;

  /// Return true if this description has the given property.
  /// \param Property Property bit to test in \p Properties.
  /// \return True if \p Property is set in \p Properties.
  bool hasProperty(SDNP Property) const { return Properties & (1 << Property); }

  /// Return true if this description has the given flag.
  /// \param Flag Flag bit to test in \p Flags.
  /// \return True if \p Flag is set in \p Flags.
  bool hasFlag(SDNF Flag) const { return Flags & (1 << Flag); }
};

/// Generated descriptions and helpers for target-specific SelectionDAG nodes.
class SDNodeInfo final {
  unsigned NumOpcodes;
  const SDNodeDesc *Descs;
  StringTable Names;
  const VTByHwModePair *VTByHwModeTable;
  const SDTypeConstraint *Constraints;

public:
  /// Construct from generated opcode count, descriptions, names, and tables.
  /// \param NumOpcodes Number of target-specific opcodes described.
  /// \param Descs Array of per-opcode node descriptions.
  /// \param Names String table of opcode names.
  /// \param VTByHwModeTable Table of value types keyed by hardware mode.
  /// \param Constraints Array of type constraints referenced by descriptions.
  constexpr SDNodeInfo(unsigned NumOpcodes, const SDNodeDesc *Descs,
                       StringTable Names, const VTByHwModePair *VTByHwModeTable,
                       const SDTypeConstraint *Constraints)
      : NumOpcodes(NumOpcodes), Descs(Descs), Names(Names),
        VTByHwModeTable(VTByHwModeTable), Constraints(Constraints) {}

  /// Returns true if there is a generated description for a node with the given
  /// target-specific opcode.
  /// \param Opcode Target-specific SelectionDAG opcode to query.
  /// \return True if a generated description exists for \p Opcode.
  bool hasDesc(unsigned Opcode) const {
    assert(Opcode >= ISD::BUILTIN_OP_END && "Expected target-specific opcode");
    return Opcode < ISD::BUILTIN_OP_END + NumOpcodes;
  }

  /// Returns the description of a node with the given opcode.
  /// \param Opcode Target-specific SelectionDAG opcode to look up.
  /// \return Generated description for \p Opcode.
  const SDNodeDesc &getDesc(unsigned Opcode) const {
    assert(hasDesc(Opcode));
    return Descs[Opcode - ISD::BUILTIN_OP_END];
  }

  /// Returns operand constraints for a node with the given opcode.
  /// \param Opcode Target-specific SelectionDAG opcode to look up.
  /// \return Type constraints for the node with \p Opcode.
  ArrayRef<SDTypeConstraint> getConstraints(unsigned Opcode) const {
    const SDNodeDesc &Desc = getDesc(Opcode);
    return ArrayRef(&Constraints[Desc.ConstraintOffset], Desc.ConstraintCount);
  }

  /// Returns the name of the given target-specific opcode, suitable for
  /// debug printing.
  /// \param Opcode Target-specific SelectionDAG opcode to name.
  /// \return Name of \p Opcode, suitable for debug printing.
  StringRef getName(unsigned Opcode) const {
    return Names[getDesc(Opcode).NameOffset];
  }

  /// Verify that \p N matches its generated description in \p DAG.
  /// \param DAG SelectionDAG providing context for the check.
  /// \param N Node whose shape and types are verified.
  LLVM_ABI void verifyNode(const SelectionDAG &DAG, const SDNode *N) const;
};

} // namespace llvm

#endif // LLVM_CODEGEN_SDNODEINFO_H
