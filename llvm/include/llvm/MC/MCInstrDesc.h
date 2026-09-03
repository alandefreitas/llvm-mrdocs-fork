//===-- llvm/MC/MCInstrDesc.h - Instruction Descriptors -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MCOperandInfo and MCInstrDesc classes, which
// are used to describe target instructions and their operands.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTRDESC_H
#define LLVM_MC_MCINSTRDESC_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class MCRegisterInfo;

class MCInst;

//===----------------------------------------------------------------------===//
// Machine Operand Flags and Description
//===----------------------------------------------------------------------===//

/// Machine operand information shared by TableGen instruction descriptions.
namespace MCOI {
/// Operand constraints encoded in 16 bits.
///
/// These are encoded in 16 bits with one of the low-order 3 bits specifying
/// that a constraint is present and the corresponding high-order hex digit
/// specifying the constraint value. This allows for a maximum of 3
/// constraints.
enum OperandConstraint {
  /// Operand must be allocated the same register as the specified value.
  TIED_TO = 0,
  EARLY_CLOBBER // If present, operand is an early clobber register.
};

// Define a macro to produce each constraint value.
#define MCOI_TIED_TO(op) \
  ((1 << MCOI::TIED_TO) | ((op) << (4 + MCOI::TIED_TO * 4)))

#define MCOI_EARLY_CLOBBER \
  (1 << MCOI::EARLY_CLOBBER)

/// Private flags set on operands; use MCOperandInfo accessors instead.
///
/// These are flags set on operands, but should be considered private; all
/// access should go through the MCOperandInfo accessors. See the accessors
/// for a description of what these are.
enum OperandFlags {
  /// Look up the register class through a hardware-mode table.
  LookupRegClassByHwMode = 0,
  /// Operand is part of a predicate that controls a predicable instruction.
  Predicate,
  /// Operand is an optional definition (e.g. ARM condition-code 's' bit).
  OptionalDef,
  /// Operand is a branch target.
  BranchTarget
};

/// Operands are tagged with one of the values of this enum.
enum OperandType {
  OPERAND_UNKNOWN = 0, ///< Operand type is unspecified.
  OPERAND_IMMEDIATE = 1, ///< Immediate constant operand.
  /// Register operand.
  OPERAND_REGISTER = 2,
  /// Memory operand.
  OPERAND_MEMORY = 3,
  /// PC-relative operand.
  OPERAND_PCREL = 4,

  /// First generic (target-independent) operand type.
  OPERAND_FIRST_GENERIC = 6,
  /// Generic (target-independent) operand type 0.
  OPERAND_GENERIC_0 = 6,
  /// Generic (target-independent) operand type 1.
  OPERAND_GENERIC_1 = 7,
  /// Generic (target-independent) operand type 2.
  OPERAND_GENERIC_2 = 8,
  /// Generic (target-independent) operand type 3.
  OPERAND_GENERIC_3 = 9,
  OPERAND_GENERIC_4 = 10, ///< Fourth generic (target-independent) operand type.
  /// Generic (target-independent) operand type 5.
  OPERAND_GENERIC_5 = 11,
  /// Last generic (target-independent) operand type.
  OPERAND_LAST_GENERIC = 11,

  /// First generic immediate operand type.
  OPERAND_FIRST_GENERIC_IMM = 12,
  /// Generic immediate operand type 0.
  OPERAND_GENERIC_IMM_0 = 12,
  /// Generic immediate operand type 1.
  OPERAND_GENERIC_IMM_1 = 13,
  /// Generic immediate operand type 2.
  OPERAND_GENERIC_IMM_2 = 14,
  /// Last generic immediate operand type.
  OPERAND_LAST_GENERIC_IMM = 14,

  /// First target-specific operand type.
  OPERAND_FIRST_TARGET = 15,
};

} // namespace MCOI

/// This holds information about one operand of a machine instruction,
/// indicating the register class for register operands, etc.
class MCOperandInfo {
public:
  /// Register class of this operand, or a hardware-mode lookup index.
  ///
  /// This specifies the register class enumeration of the operand if the
  /// operand is a register. If LookupRegClassByHwMode is set, then this is an
  /// index into a table in TargetInstrInfo or MCInstrInfo which contains the
  /// real register class ID.
  int16_t RegClass;

  /// These are flags from the MCOI::OperandFlags enum.
  uint8_t Flags;

  /// Information about the type of the operand.
  uint8_t OperandType;

  /// Operand constraints (see OperandConstraint enum).
  uint16_t Constraints;

  /// Set if this operand is a value that requires the current hwmode to look up
  /// its register class.
  ///
  /// \returns true if the register class must be looked up using the current
  /// hardware mode.
  bool isLookupRegClassByHwMode() const {
    return Flags & (1 << MCOI::LookupRegClassByHwMode);
  }

  /// Set if this is one of the operands that made up of the predicate
  /// operand that controls an isPredicable() instruction.
  ///
  /// \returns true if this operand is part of a predicate.
  bool isPredicate() const { return Flags & (1 << MCOI::Predicate); }

  /// Set if this operand is a optional def.
  ///
  /// \returns true if this operand is an optional definition.
  bool isOptionalDef() const { return Flags & (1 << MCOI::OptionalDef); }

  /// Set if this operand is a branch target.
  ///
  /// \returns true if this operand is a branch target.
  bool isBranchTarget() const { return Flags & (1 << MCOI::BranchTarget); }

  /// Return true if this operand has a generic (target-independent) type.
  ///
  /// \returns true if the operand type is a generic type.
  bool isGenericType() const {
    return OperandType >= MCOI::OPERAND_FIRST_GENERIC &&
           OperandType <= MCOI::OPERAND_LAST_GENERIC;
  }

  /// Return the zero-based index of this generic operand type.
  ///
  /// \returns the zero-based index of this generic operand type.
  unsigned getGenericTypeIndex() const {
    assert(isGenericType() && "non-generic types don't have an index");
    return OperandType - MCOI::OPERAND_FIRST_GENERIC;
  }

  /// Return true if this operand has a generic immediate type.
  ///
  /// \returns true if the operand type is a generic immediate.
  bool isGenericImm() const {
    return OperandType >= MCOI::OPERAND_FIRST_GENERIC_IMM &&
           OperandType <= MCOI::OPERAND_LAST_GENERIC_IMM;
  }

  /// Return the zero-based index of this generic immediate operand type.
  ///
  /// \returns the zero-based index of this generic immediate operand type.
  unsigned getGenericImmIndex() const {
    assert(isGenericImm() && "non-generic immediates don't have an index");
    return OperandType - MCOI::OPERAND_FIRST_GENERIC_IMM;
  }
};

//===----------------------------------------------------------------------===//
// Machine Instruction Flags and Description
//===----------------------------------------------------------------------===//

/// Machine instruction descriptor flags encoded in \c MCInstrDesc::Flags.
namespace MCID {
/// Bitfield flags for \c MCInstrDesc; prefer the predicate methods.
///
/// These should be considered private to the implementation of the
/// MCInstrDesc class. Clients should use the predicate methods on MCInstrDesc,
/// not use these directly. These all correspond to bitfields in the
/// MCInstrDesc::Flags field.
enum Flag {
  /// Instruction is emitted before instruction selection.
  PreISelOpcode = 0,
  /// Instruction can have a variable number of operands.
  Variadic,
  /// Instruction has an optional definition operand.
  HasOptionalDef,
  /// Pseudo instruction that does not correspond to a real machine opcode.
  Pseudo,
  /// Meta instruction that produces no executable output.
  Meta,
  /// Instruction is a return.
  Return,
  /// Marks the end of an EH scope (for example catchpad or cleanuppad).
  EHScopeReturn,
  Call,
  Barrier,
  Terminator,
  Branch,
  /// Branch target is not directly encoded in the instruction.
  IndirectBranch,
  Compare,
  MoveImm,
  MoveReg,
  Bitcast,
  Select,
  DelaySlot,
  FoldableAsLoad,
  MayLoad,
  MayStore,
  MayRaiseFPException,
  Predicable,
  NotDuplicable,
  UnmodeledSideEffects,
  Commutable,
  ConvertibleTo3Addr,
  UsesCustomInserter,
  HasPostISelHook,
  Rematerializable,
  CheapAsAMove,
  ExtraSrcRegAllocReq,
  ExtraDefRegAllocReq,
  RegSequence,
  ExtractSubreg,
  InsertSubreg,
  Convergent,
  Add,
  Trap,
  VariadicOpsAreDefs,
  /// Instruction authenticates a pointer (e.g. ARMv8.3 LDRAx/BRAx).
  Authenticated,
};
} // namespace MCID

/// Describe properties of one target instruction class.
///
/// This captures information about side effects, register use and many other
/// things. There is one instance of this struct for each target instruction
/// class, and the MachineInstr class points to this struct directly to describe
/// itself.
class MCInstrDesc {
public:
  // FIXME: Disable copies and moves.
  // Do not allow MCInstrDescs to be copied or moved. They should only exist in
  // the <Target>Insts table because they rely on knowing their own address to
  // find other information elsewhere in the same table.

  /// The opcode number.
  uint32_t Opcode;
  /// Number of declared operands (may be more if variable_ops).
  uint16_t NumOperands;
  /// Number of operands that are definitions.
  uint8_t NumDefs;
  /// Number of bytes in encoding.
  uint8_t Size;
  /// Enum identifying the instruction scheduling class.
  uint16_t SchedClass;
  /// Number of registers implicitly used.
  uint8_t NumImplicitUses;
  /// Number of registers implicitly defined.
  uint8_t NumImplicitDefs;
  /// Offset to info about operands.
  uint16_t OpInfoOffset;
  /// Offset to the start of the implicit operand list.
  uint16_t ImplicitOffset;
  /// Bitmask of MCID::Flag values describing instruction properties.
  uint64_t Flags;
  /// Target-specific flag values.
  uint64_t TSFlags;

  /// Returns the value of the specified operand constraint if
  /// it is present. Returns -1 if it is not present.
  ///
  /// \param OpNum - Operand index to query.
  /// \param Constraint - Constraint kind to look up for that operand.
  /// \returns the constraint value if present, otherwise -1.
  int getOperandConstraint(unsigned OpNum,
                           MCOI::OperandConstraint Constraint) const {
    if (OpNum < NumOperands &&
        (operands()[OpNum].Constraints & (1 << Constraint))) {
      unsigned ValuePos = 4 + Constraint * 4;
      return (int)(operands()[OpNum].Constraints >> ValuePos) & 0x0f;
    }
    return -1;
  }

  /// Return the opcode number for this descriptor.
  ///
  /// \returns the opcode number for this descriptor.
  unsigned getOpcode() const { return Opcode; }

  /// Return the number of declared MachineOperands for this instruction.
  ///
  /// Note that variadic (isVariadic() returns true) instructions may have
  /// additional operands at the end of the list, and note that the machine
  /// instruction may include implicit register def/uses as well.
  ///
  /// \returns the number of declared operands for this instruction.
  unsigned getNumOperands() const { return NumOperands; }

  /// Return the declared operand info for this instruction.
  ///
  /// \returns an array of operand info for the declared operands.
  ArrayRef<MCOperandInfo> operands() const {
    auto OpInfo = reinterpret_cast<const MCOperandInfo *>(this + Opcode + 1);
    return ArrayRef(OpInfo + OpInfoOffset, NumOperands);
  }

  /// Return the number of MachineOperands that are register definitions.
  ///
  /// Register definitions always occur at the start of the machine operand
  /// list. This is the number of "outs" in the .td file, and does not include
  /// implicit defs.
  ///
  /// \returns the number of explicit register definition operands.
  unsigned getNumDefs() const { return NumDefs; }

  /// Return flags of this instruction.
  ///
  /// \returns the bitmask of \c MCID::Flag values for this instruction.
  uint64_t getFlags() const { return Flags; }

  /// Return true if this is a pre-ISel opcode.
  ///
  /// \returns true if this instruction is emitted before instruction selection
  /// and should be legalized/regbankselected/selected.
  bool isPreISelOpcode() const { return Flags & (1ULL << MCID::PreISelOpcode); }

  /// Return true if this instruction can have a variable number of operands.
  ///
  /// In this case, the variable operands will be after the normal operands but
  /// before the implicit definitions and uses (if any are present).
  ///
  /// \returns true if this instruction can have a variable number of operands.
  bool isVariadic() const { return Flags & (1ULL << MCID::Variadic); }

  /// Set if this instruction has an optional definition, e.g.
  /// ARM instructions which can set condition code if 's' bit is set.
  ///
  /// \returns true if this instruction has an optional definition.
  bool hasOptionalDef() const { return Flags & (1ULL << MCID::HasOptionalDef); }

  /// Return true if this is a pseudo instruction that doesn't
  /// correspond to a real machine instruction.
  ///
  /// \returns true if this is a pseudo instruction.
  bool isPseudo() const { return Flags & (1ULL << MCID::Pseudo); }

  /// Return true if this is a meta instruction that doesn't
  /// produce any output in the form of executable instructions.
  ///
  /// \returns true if this is a meta instruction.
  bool isMetaInstruction() const { return Flags & (1ULL << MCID::Meta); }

  /// Return true if the instruction is a return.
  ///
  /// \returns true if the instruction is a return.
  bool isReturn() const { return Flags & (1ULL << MCID::Return); }

  /// Return true if the instruction is an add instruction.
  ///
  /// \returns true if the instruction is an add.
  bool isAdd() const { return Flags & (1ULL << MCID::Add); }

  /// Return true if this instruction is a trap.
  ///
  /// \returns true if this instruction is a trap.
  bool isTrap() const { return Flags & (1ULL << MCID::Trap); }

  /// Return true if the instruction is a register to register move.
  ///
  /// \returns true if the instruction is a register-to-register move.
  bool isMoveReg() const { return Flags & (1ULL << MCID::MoveReg); }

  ///  Return true if the instruction is a call.
  ///
  /// \returns true if the instruction is a call.
  bool isCall() const { return Flags & (1ULL << MCID::Call); }

  /// Return true if this instruction stops control flow from falling through.
  ///
  /// Examples include unconditional branches and return instructions.
  ///
  /// \returns true if this instruction is a barrier.
  bool isBarrier() const { return Flags & (1ULL << MCID::Barrier); }

  /// Returns true if this instruction part of the terminator for
  /// a basic block.  Typically this is things like return and branch
  /// instructions.
  ///
  /// Various passes use this to insert code into the bottom of a basic block,
  /// but before control flow occurs.
  ///
  /// \returns true if this instruction is a terminator.
  bool isTerminator() const { return Flags & (1ULL << MCID::Terminator); }

  /// Return true if this is a conditional, unconditional, or indirect branch.
  ///
  /// Predicates below can be used to discriminate between these cases, and the
  /// TargetInstrInfo::analyzeBranch method can be used to get more information.
  ///
  /// \returns true if this instruction is a branch.
  bool isBranch() const { return Flags & (1ULL << MCID::Branch); }

  /// Return true if this is an indirect branch, such as a
  /// branch through a register.
  ///
  /// \returns true if this instruction is an indirect branch.
  bool isIndirectBranch() const { return Flags & (1ULL << MCID::IndirectBranch); }

  /// Return true if this is a conditional branch that may fall through.
  ///
  /// The branch may fall through to the next instruction or may transfer
  /// control flow to some other block. The TargetInstrInfo::analyzeBranch
  /// method can be used to get more information about this branch.
  ///
  /// \returns true if this instruction is a conditional branch.
  bool isConditionalBranch() const {
    return isBranch() && !isBarrier() && !isIndirectBranch();
  }

  /// Return true if this is an unconditional branch.
  ///
  /// The branch always transfers control flow to some other block. The
  /// TargetInstrInfo::analyzeBranch method can be used to get more information
  /// about this branch.
  ///
  /// \returns true if this instruction is an unconditional branch.
  bool isUnconditionalBranch() const {
    return isBranch() && isBarrier() && !isIndirectBranch();
  }

  /// Return true if this instruction may affect control flow.
  ///
  /// True for a branch or an instruction which directly writes to the program
  /// counter. Considered 'may' affect rather than 'does' affect as things like
  /// predication are not taken into account.
  ///
  /// \param MI - Instruction instance to inspect.
  /// \param RI - Register info used to identify the program counter.
  /// \returns true if this instruction may affect control flow.
  LLVM_ABI bool mayAffectControlFlow(const MCInst &MI,
                                     const MCRegisterInfo &RI) const;

  /// Return true if this instruction has a predicate operand.
  ///
  /// The predicate controls execution. It may be set to 'always', or may be set
  /// to other values. There are various methods in TargetInstrInfo that can be
  /// used to control and modify the predicate in this instruction.
  ///
  /// \returns true if this instruction has a predicate operand.
  bool isPredicable() const { return Flags & (1ULL << MCID::Predicable); }

  /// Return true if this instruction is a comparison.
  ///
  /// \returns true if this instruction is a comparison.
  bool isCompare() const { return Flags & (1ULL << MCID::Compare); }

  /// Return true if this instruction is a move immediate
  /// (including conditional moves) instruction.
  ///
  /// \returns true if this instruction is a move immediate.
  bool isMoveImmediate() const { return Flags & (1ULL << MCID::MoveImm); }

  /// Return true if this instruction is a bitcast instruction.
  ///
  /// \returns true if this instruction is a bitcast.
  bool isBitcast() const { return Flags & (1ULL << MCID::Bitcast); }

  /// Return true if this is a select instruction.
  ///
  /// \returns true if this is a select instruction.
  bool isSelect() const { return Flags & (1ULL << MCID::Select); }

  /// Return true if this instruction cannot be safely duplicated.
  ///
  /// For example, if the instruction has unique labels attached to it,
  /// duplicating it would cause multiple definition errors.
  ///
  /// \returns true if this instruction cannot be safely duplicated.
  bool isNotDuplicable() const { return Flags & (1ULL << MCID::NotDuplicable); }

  /// Returns true if the specified instruction has a delay slot which
  /// must be filled by the code generator.
  ///
  /// \returns true if this instruction has a delay slot.
  bool hasDelaySlot() const { return Flags & (1ULL << MCID::DelaySlot); }

  /// Return true if this instruction can be folded as a memory operand.
  ///
  /// The most common use for this is instructions that are simple loads from
  /// memory that don't modify the loaded value in any way, but it can also be
  /// used for instructions that can be expressed as constant-pool loads, such
  /// as V_SETALLONES on x86, to allow them to be folded when it is beneficial.
  /// This should only be set on instructions that return a value in their only
  /// virtual register definition.
  ///
  /// \returns true if this instruction can be folded as a memory operand.
  bool canFoldAsLoad() const { return Flags & (1ULL << MCID::FoldableAsLoad); }

  /// Return true if this instruction behaves like REG_SEQUENCE.
  ///
  /// E.g., on ARM,
  /// dX VMOVDRR rY, rZ
  /// is equivalent to
  /// dX = REG_SEQUENCE rY, ssub_0, rZ, ssub_1.
  ///
  /// Note that for the optimizers to be able to take advantage of
  /// this property, TargetInstrInfo::getRegSequenceLikeInputs has to be
  /// override accordingly.
  ///
  /// \returns true if this instruction behaves like REG_SEQUENCE.
  bool isRegSequenceLike() const { return Flags & (1ULL << MCID::RegSequence); }

  /// Return true if this instruction behaves like EXTRACT_SUBREG.
  ///
  /// E.g., on ARM,
  /// rX, rY VMOVRRD dZ
  /// is equivalent to two EXTRACT_SUBREG:
  /// rX = EXTRACT_SUBREG dZ, ssub_0
  /// rY = EXTRACT_SUBREG dZ, ssub_1
  ///
  /// Note that for the optimizers to be able to take advantage of
  /// this property, TargetInstrInfo::getExtractSubregLikeInputs has to be
  /// override accordingly.
  ///
  /// \returns true if this instruction behaves like EXTRACT_SUBREG.
  bool isExtractSubregLike() const {
    return Flags & (1ULL << MCID::ExtractSubreg);
  }

  /// Return true if this instruction behaves like INSERT_SUBREG.
  ///
  /// E.g., on ARM,
  /// dX = VSETLNi32 dY, rZ, Imm
  /// is equivalent to a INSERT_SUBREG:
  /// dX = INSERT_SUBREG dY, rZ, translateImmToSubIdx(Imm)
  ///
  /// Note that for the optimizers to be able to take advantage of
  /// this property, TargetInstrInfo::getInsertSubregLikeInputs has to be
  /// override accordingly.
  ///
  /// \returns true if this instruction behaves like INSERT_SUBREG.
  bool isInsertSubregLike() const { return Flags & (1ULL << MCID::InsertSubreg); }


  /// Return true if this instruction is convergent.
  ///
  /// Convergent instructions may not be made control-dependent on any
  /// additional values.
  ///
  /// \returns true if this instruction is convergent.
  bool isConvergent() const { return Flags & (1ULL << MCID::Convergent); }

  /// Return true if variadic operands of this instruction are definitions.
  ///
  /// \returns true if variadic operands of this instruction are definitions.
  bool variadicOpsAreDefs() const {
    return Flags & (1ULL << MCID::VariadicOpsAreDefs);
  }

  /// Return true if this instruction authenticates a pointer (e.g. LDRAx/BRAx
  /// from ARMv8.3, which perform loads/branches with authentication).
  ///
  /// An authenticated instruction may fail in an ABI-defined manner when
  /// operating on an invalid signed pointer.
  ///
  /// \returns true if this instruction authenticates a pointer.
  bool isAuthenticated() const {
    return Flags & (1ULL << MCID::Authenticated);
  }

  //===--------------------------------------------------------------------===//
  // Side Effect Analysis
  //===--------------------------------------------------------------------===//

  /// Return true if this instruction could possibly read memory.
  ///
  /// Instructions with this flag set are not necessarily simple load
  /// instructions, they may load a value and modify it, for example.
  ///
  /// \returns true if this instruction could possibly read memory.
  bool mayLoad() const { return Flags & (1ULL << MCID::MayLoad); }

  /// Return true if this instruction could possibly modify memory.
  ///
  /// Instructions with this flag set are not necessarily simple store
  /// instructions, they may store a modified value based on their operands, or
  /// may not actually modify anything, for example.
  ///
  /// \returns true if this instruction could possibly modify memory.
  bool mayStore() const { return Flags & (1ULL << MCID::MayStore); }

  /// Return true if this instruction may raise a floating-point exception.
  ///
  /// \returns true if this instruction may raise a floating-point exception.
  bool mayRaiseFPException() const {
    return Flags & (1ULL << MCID::MayRaiseFPException);
  }

  /// Return true if this instruction has side
  /// effects that are not modeled by other flags.  This does not return true
  /// for instructions whose effects are captured by:
  ///
  ///  1. Their operand list and implicit definition/use list.  Register use/def
  ///     info is explicit for instructions.
  ///  2. Memory accesses.  Use mayLoad/mayStore.
  ///  3. Calling, branching, returning: use isCall/isReturn/isBranch.
  ///
  /// Examples of side effects would be modifying 'invisible' machine state like
  /// a control register, flushing a cache, modifying a register invisible to
  /// LLVM, etc.
  ///
  /// \returns true if this instruction has unmodeled side effects.
  bool hasUnmodeledSideEffects() const {
    return Flags & (1ULL << MCID::UnmodeledSideEffects);
  }

  //===--------------------------------------------------------------------===//
  // Flags that indicate whether an instruction can be modified by a method.
  //===--------------------------------------------------------------------===//

  /// Return true if this may be a commutable 2- or 3-address instruction.
  ///
  /// That is, an instruction of the form "X = op Y, Z, ..." which produces the
  /// same result if Y and Z are exchanged. If this flag is set, then the
  /// TargetInstrInfo::commuteInstruction method may be used to hack on the
  /// instruction.
  ///
  /// Note that this flag may be set on instructions that are only commutable
  /// sometimes. In these cases, the call to commuteInstruction will fail.
  /// Also note that some instructions require non-trivial modification to
  /// commute them.
  ///
  /// \returns true if this instruction may be commutable.
  bool isCommutable() const { return Flags & (1ULL << MCID::Commutable); }

  /// Return true if this 2-address instruction can become 3-address.
  ///
  /// Doing this transformation can be profitable in the register allocator,
  /// because it means that the instruction can use a 2-address form if
  /// possible, but degrade into a less efficient form if the source and dest
  /// register cannot be assigned to the same register. For example, this allows
  /// the x86 backend to turn a "shl reg, 3" instruction into an LEA instruction,
  /// which is the same speed as the shift but has bigger code size.
  ///
  /// If this returns true, then the target must implement the
  /// TargetInstrInfo::convertToThreeAddress method for this instruction, which
  /// is allowed to fail if the transformation isn't valid for this specific
  /// instruction (e.g. shl reg, 4 on x86).
  ///
  /// \returns true if this 2-address instruction can become 3-address.
  bool isConvertibleTo3Addr() const {
    return Flags & (1ULL << MCID::ConvertibleTo3Addr);
  }

  /// Return true if this instruction needs custom DAG insertion.
  ///
  /// When the DAG scheduler is inserting it into a machine basic block, if this
  /// is true for the instruction, it basically means that it is a pseudo
  /// instruction used at SelectionDAG time that is expanded out into magic code
  /// by the target when MachineInstrs are formed.
  ///
  /// If this is true, the TargetLoweringInfo::InsertAtEndOfBasicBlock method
  /// is used to insert this into the MachineBasicBlock.
  ///
  /// \returns true if this instruction needs custom DAG insertion.
  bool usesCustomInsertionHook() const {
    return Flags & (1ULL << MCID::UsesCustomInserter);
  }

  /// Return true if this instruction needs post-ISel adjustment.
  ///
  /// For example, this can be used to fill in ARM 's' optional operand depending
  /// on whether the conditional flag register is used.
  ///
  /// \returns true if this instruction needs post-ISel adjustment.
  bool hasPostISelHook() const { return Flags & (1ULL << MCID::HasPostISelHook); }

  /// Returns true if this instruction is a candidate for remat. This
  /// flag is only used in TargetInstrInfo method isTriviallyRematerializable.
  ///
  /// If this flag is set, the isReMaterializableImpl() method is
  /// called to verify the instruction is really rematerializable.
  ///
  /// \returns true if this instruction is a rematerialization candidate.
  bool isRematerializable() const {
    return Flags & (1ULL << MCID::Rematerializable);
  }

  /// Return true if this instruction costs no more than a move.
  ///
  /// This is useful during certain types of optimizations (e.g., remat during
  /// two-address conversion or machine licm) where we would like to remat or
  /// hoist the instruction, but not if it costs more than moving the instruction
  /// into the appropriate register. Note, we are not marking copies from and to
  /// the same register class with this flag.
  ///
  /// This method could be called by interface TargetInstrInfo::isAsCheapAsAMove
  /// for different subtargets.
  ///
  /// \returns true if this instruction costs no more than a move.
  bool isAsCheapAsAMove() const { return Flags & (1ULL << MCID::CheapAsAMove); }

  /// Return true if sources have extra register-allocation constraints.
  ///
  /// These requirements are not captured by the operand register classes. e.g.
  /// ARM::STRD's two source registers must be an even / odd pair, ARM::STM
  /// registers have to be in ascending order. Post-register allocation passes
  /// should not attempt to change allocations for sources of instructions with
  /// this flag.
  ///
  /// \returns true if sources have extra register-allocation constraints.
  bool hasExtraSrcRegAllocReq() const {
    return Flags & (1ULL << MCID::ExtraSrcRegAllocReq);
  }

  /// Return true if defs have extra register-allocation constraints.
  ///
  /// These requirements are not captured by the operand register classes. e.g.
  /// ARM::LDRD's two def registers must be an even / odd pair, ARM::LDM
  /// registers have to be in ascending order. Post-register allocation passes
  /// should not attempt to change allocations for definitions of instructions
  /// with this flag.
  ///
  /// \returns true if defs have extra register-allocation constraints.
  bool hasExtraDefRegAllocReq() const {
    return Flags & (1ULL << MCID::ExtraDefRegAllocReq);
  }

  /// Return registers potentially read by any instance of this instruction.
  ///
  /// For example, on X86, the "adc" instruction adds two register operands and
  /// adds the carry bit in from the flags register. In this case, the
  /// instruction is marked as implicitly reading the flags. Likewise, the
  /// variable shift instruction on X86 is marked as implicitly reading the 'CL'
  /// register, which it always does.
  ///
  /// \returns the list of physical registers implicitly used by this instruction.
  ArrayRef<MCPhysReg> implicit_uses() const {
    auto ImplicitOps =
        reinterpret_cast<const MCPhysReg *>(this + Opcode + 1) + ImplicitOffset;
    return {ImplicitOps, NumImplicitUses};
  }

  /// Return registers potentially written by any instance of this instruction.
  ///
  /// For example, on X86, many instructions implicitly set the flags register.
  /// In this case, they are marked as setting the FLAGS. Likewise, many
  /// instructions always deposit their result in a physical register. For
  /// example, the X86 divide instruction always deposits the quotient and
  /// remainder in the EAX/EDX registers. For that instruction, this will return
  /// a list containing the EAX/EDX/EFLAGS registers.
  ///
  /// \returns the list of physical registers implicitly defined by this
  /// instruction.
  ArrayRef<MCPhysReg> implicit_defs() const {
    auto ImplicitOps =
        reinterpret_cast<const MCPhysReg *>(this + Opcode + 1) + ImplicitOffset;
    return {ImplicitOps + NumImplicitUses, NumImplicitDefs};
  }

  /// Return true if this instruction implicitly uses the specified physical
  /// register.
  ///
  /// \param Reg - Physical register to test for an implicit use.
  /// \returns true if this instruction implicitly uses \p Reg.
  bool hasImplicitUseOfPhysReg(MCRegister Reg) const {
    return is_contained(implicit_uses(), Reg);
  }

  /// Return true if this instruction implicitly defines the specified physical
  /// register.
  ///
  /// \param Reg - Physical register to test for an implicit definition.
  /// \param MRI - Optional register info used when matching aliases.
  /// \returns true if this instruction implicitly defines \p Reg.
  LLVM_ABI bool
  hasImplicitDefOfPhysReg(MCRegister Reg,
                          const MCRegisterInfo *MRI = nullptr) const;

  /// Return the scheduling class index for this instruction.
  ///
  /// The scheduling class is an index into the InstrItineraryData table. This
  /// returns zero if there is no known scheduling information for the
  /// instruction.
  ///
  /// \returns the scheduling class index, or zero if unknown.
  unsigned getSchedClass() const { return SchedClass; }

  /// Return the number of bytes in the encoding of this instruction,
  /// or zero if the encoding size cannot be known from the opcode.
  ///
  /// \returns the encoding size in bytes, or zero if unknown.
  unsigned getSize() const { return Size; }

  /// Find the index of the first operand in the
  /// operand list that is used to represent the predicate. It returns -1 if
  /// none is found.
  ///
  /// \returns the index of the first predicate operand, or -1 if none is found.
  int findFirstPredOperandIdx() const {
    if (isPredicable()) {
      for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
        if (operands()[i].isPredicate())
          return i;
    }
    return -1;
  }

  /// Return true if this instruction explicitly defines the specified physical
  /// register.
  ///
  /// \param MI - Instruction instance whose explicit defs are examined.
  /// \param Reg - Physical register to test for an explicit definition.
  /// \param RI - Register info used when matching register aliases.
  /// \returns true if this instruction explicitly defines \p Reg.
  LLVM_ABI bool hasExplicitDefOfPhysReg(const MCInst &MI, MCRegister Reg,
                                        const MCRegisterInfo &RI) const;

  /// Return true if this instruction defines the specified physical
  /// register, either explicitly or implicitly.
  ///
  /// \param MI - Instruction instance whose defs are examined.
  /// \param Reg - Physical register to test for a definition.
  /// \param RI - Register info used when matching register aliases.
  /// \returns true if this instruction defines \p Reg explicitly or implicitly.
  LLVM_ABI bool hasDefOfPhysReg(const MCInst &MI, MCRegister Reg,
                                const MCRegisterInfo &RI) const;
};

} // end namespace llvm

#endif
