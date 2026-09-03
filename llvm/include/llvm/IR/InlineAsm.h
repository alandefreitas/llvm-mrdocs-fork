//===- llvm/InlineAsm.h - Class to represent inline asm strings -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class represents the inline asm strings, which are Value*'s that are
// used as the callee operand of call instructions.  InlineAsm's are uniqued
// like constants, and created via InlineAsm::get(...).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INLINEASM_H
#define LLVM_IR_INLINEASM_H

#include "llvm/ADT/Bitfields.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <string>
#include <vector>

namespace llvm {

class Error;
class FunctionType;
class PointerType;
/// Map that uniques constant-like IR values such as \c InlineAsm by key.
template <class ConstantClass> class ConstantUniqueMap;

/// Lookup key used to uniquify \c InlineAsm values in \c ConstantUniqueMap.
struct InlineAsmKeyType;

/// Value representing an inline assembly string used as a call callee.
class InlineAsm final : public Value {
public:
  /// Assembly dialect used when printing or parsing inline assembly.
  enum AsmDialect {
    AD_ATT,  ///< AT&T assembly dialect (the GNU inline asm default).
    AD_Intel ///< Intel assembly dialect.
  };

private:
  friend struct InlineAsmKeyType;
  friend class ConstantUniqueMap<InlineAsm>;

  std::string AsmString, Constraints;
  FunctionType *FTy;
  bool HasSideEffects;
  bool IsAlignStack;
  AsmDialect Dialect;
  bool CanThrow;

  InlineAsm(FunctionType *Ty, const std::string &AsmString,
            const std::string &Constraints, bool hasSideEffects,
            bool isAlignStack, AsmDialect asmDialect, bool canThrow);

  /// When the ConstantUniqueMap merges two types and makes two InlineAsms
  /// identical, it destroys one of them with this method.
  void destroyConstant();

public:
  /// InlineAsm values are uniqued and cannot be copied.
  /// \param Other Unused; the copy constructor is deleted.
  InlineAsm(const InlineAsm &Other) = delete;
  /// InlineAsm values are uniqued and cannot be assigned.
  /// \param Other Unused; the copy assignment operator is deleted.
  InlineAsm &operator=(const InlineAsm &Other) = delete;

  /// Return the specified uniqued inline asm string.
  ///
  /// \param Ty Function type describing the asm's operands and result.
  /// \param AsmString The raw inline assembly template string.
  /// \param Constraints The constraint string for the asm operands.
  /// \param hasSideEffects Whether the asm may have side effects.
  /// \param isAlignStack Whether the asm requires stack alignment.
  /// \param asmDialect Assembly dialect used to print or parse the asm.
  /// \param canThrow Whether the asm may unwind or throw.
  /// \return The uniqued InlineAsm value for the given key.
  LLVM_ABI static InlineAsm *get(FunctionType *Ty, StringRef AsmString,
                                 StringRef Constraints, bool hasSideEffects,
                                 bool isAlignStack = false,
                                 AsmDialect asmDialect = AD_ATT,
                                 bool canThrow = false);

  /// Return true if this inline asm may have side effects.
  /// \return True if this inline asm may have side effects.
  bool hasSideEffects() const { return HasSideEffects; }
  /// Return true if this inline asm requires the stack to be aligned.
  /// \return True if this inline asm requires the stack to be aligned.
  bool isAlignStack() const { return IsAlignStack; }
  /// Return the assembly dialect for this inline asm.
  /// \return The assembly dialect for this inline asm.
  AsmDialect getDialect() const { return Dialect; }
  /// Return true if this inline asm may unwind or throw.
  /// \return True if this inline asm may unwind or throw.
  bool canThrow() const { return CanThrow; }

  /// getType - InlineAsm's are always pointers.
  ///
  /// \return The pointer type of this InlineAsm value.
  PointerType *getType() const {
    return reinterpret_cast<PointerType*>(Value::getType());
  }

  /// getFunctionType - InlineAsm's are always pointers to functions.
  ///
  /// \return The function type this inline asm pointer refers to.
  LLVM_ABI FunctionType *getFunctionType() const;

  /// Return the raw inline assembly template string.
  /// \return The raw asm template string.
  StringRef getAsmString() const { return AsmString; }
  /// Return the raw constraint string for this inline asm.
  /// \return The raw constraint string.
  StringRef getConstraintString() const { return Constraints; }
  /// Split the asm string on newlines into individual statement strings.
  /// \param AsmStrs Receives the per-line asm template fragments.
  LLVM_ABI void collectAsmStrs(SmallVectorImpl<StringRef> &AsmStrs) const;

  /// This static method can be used by the parser to check to see if the
  /// specified constraint string is legal for the type.
  /// \param Ty Function type the constraints must match.
  /// \param Constraints Constraint string to validate.
  /// \return Success, or an error if the constraints are illegal for \p Ty.
  LLVM_ABI static Error verify(FunctionType *Ty, StringRef Constraints);

  /// Role of an operand in an inline asm constraint string.
  enum ConstraintPrefix {
    isInput,   ///< Input operand ('x').
    isOutput,  ///< Output operand ('=x').
    isClobber, ///< Clobbered register ('~x').
    isLabel,   ///< Label constraint ('!x').
  };

  /// Vector of constraint code strings for one operand alternative.
  using ConstraintCodeVector = std::vector<std::string>;

  /// One alternative within a multiple-alternative constraint.
  struct SubConstraintInfo {
    /// Matching input constraint index, or -1 if none.
    ///
    /// If this is not -1, this is an output constraint where an input
    /// constraint is required to match it (e.g. "0"). The value is the
    /// constraint number that matches this one (for example, if this is
    /// constraint #0 and constraint #4 has the value "0", this will be 4).
    int MatchingInput = -1;

    /// Code - The constraint code, either the register name (in braces) or the
    /// constraint letter/number.
    ConstraintCodeVector Codes;

    /// Default constructor.
    SubConstraintInfo() = default;
  };

  /// Vector of sub-constraint alternatives for one operand.
  using SubConstraintInfoVector = std::vector<SubConstraintInfo>;
  struct ConstraintInfo;
  /// Vector of parsed operand constraints for an inline asm.
  using ConstraintInfoVector = std::vector<ConstraintInfo>;

  /// Parsed description of one operand constraint from an inline asm string.
  struct ConstraintInfo {
    /// Type - The basic type of the constraint: input/output/clobber/label
    ///
    ConstraintPrefix Type = isInput;

    /// isEarlyClobber - "&": output operand writes result before inputs are all
    /// read.  This is only ever set for an output operand.
    bool isEarlyClobber = false;

    /// Matching input constraint index, or -1 if none.
    ///
    /// If this is not -1, this is an output constraint where an input
    /// constraint is required to match it (e.g. "0"). The value is the
    /// constraint number that matches this one (for example, if this is
    /// constraint #0 and constraint #4 has the value "0", this will be 4).
    int MatchingInput = -1;

    /// hasMatchingInput - Return true if this is an output constraint that has
    /// a matching input constraint.
    /// \return True if this output has a matching input constraint.
    bool hasMatchingInput() const { return MatchingInput != -1; }

    /// isCommutative - This is set to true for a constraint that is commutative
    /// with the next operand.
    bool isCommutative = false;

    /// True if this operand is an indirect operand.
    ///
    /// This means that the address of the source or destination is present in
    /// the call instruction, instead of it being returned or passed in
    /// explicitly. This is represented with a '*' in the asm string.
    bool isIndirect = false;

    /// Code - The constraint code, either the register name (in braces) or the
    /// constraint letter/number.
    ConstraintCodeVector Codes;

    /// isMultipleAlternative - '|': has multiple-alternative constraints.
    bool isMultipleAlternative = false;

    /// multipleAlternatives - If there are multiple alternative constraints,
    /// this array will contain them.  Otherwise it will be empty.
    SubConstraintInfoVector multipleAlternatives;

    /// The currently selected alternative constraint index.
    unsigned currentAlternativeIndex = 0;

    /// Default constructor.
    ConstraintInfo() = default;

    /// Analyze a constraint string and fill in this structure's fields.
    ///
    /// Parses the specified string (e.g. "=*&{eax}"). If the constraint string
    /// is not understood, return true, otherwise return false.
    /// \param Str The constraint string to parse.
    /// \param ConstraintsSoFar Constraints parsed so far (for matching refs).
    /// \return True if the constraint string was not understood; false on success.
    LLVM_ABI bool Parse(StringRef Str, ConstraintInfoVector &ConstraintsSoFar);

    /// Point this constraint to the alternative indicated by \p index.
    /// \param index Index into \c multipleAlternatives to select.
    LLVM_ABI void selectAlternative(unsigned index);

    /// Whether this constraint corresponds to an argument.
    /// \return True if this constraint corresponds to a call argument.
    bool hasArg() const {
      return Type == isInput || (Type == isOutput && isIndirect);
    }
  };

  /// Split a constraint string into per-operand constraints and prefixes.
  ///
  /// If this returns an empty vector, and if the constraint string itself
  /// isn't empty, there was an error parsing.
  /// \param ConstraintString The full comma-separated constraint string.
  /// \return The parsed constraints, or an empty vector on error.
  LLVM_ABI static ConstraintInfoVector
  ParseConstraints(StringRef ConstraintString);

  /// ParseConstraints - Parse the constraints of this inlineasm object,
  /// returning them the same way that ParseConstraints(str) does.
  /// \return The parsed per-operand constraints for this InlineAsm.
  ConstraintInfoVector ParseConstraints() const {
    return ParseConstraints(Constraints);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// \return True if \p V is an InlineAsm value.
  static bool classof(const Value *V) {
    return V->getValueID() == Value::InlineAsmVal;
  }

  /// Fixed operand indices and ExtraInfo bit flags for INLINEASM SDNodes/MIs.
  enum : uint32_t {
    // Fixed operands on an INLINEASM SDNode.
    Op_InputChain = 0,   ///< Input chain operand on an INLINEASM SDNode.
    Op_AsmString = 1,    ///< Asm string operand on an INLINEASM SDNode.
    Op_MDNode = 2,       ///< Metadata operand on an INLINEASM SDNode.
    Op_ExtraInfo = 3, ///< ExtraInfo flags: side effects, alignstack, dialect.
    Op_FirstOperand = 4, ///< First variable operand on an INLINEASM SDNode.

    // Fixed operands on an INLINEASM MachineInstr.
    MIOp_AsmString = 0, ///< Asm string operand on an INLINEASM MachineInstr.
    MIOp_ExtraInfo = 1, ///< ExtraInfo flags on an INLINEASM MachineInstr.
    MIOp_FirstOperand = 2, ///< First variable operand on an INLINEASM MI.

    // Interpretation of the MIOp_ExtraInfo bit field.
    Extra_HasSideEffects = 1, ///< ExtraInfo bit: asm may have side effects.
    Extra_IsAlignStack = 2,   ///< ExtraInfo bit: asm requires stack alignment.
    Extra_AsmDialect = 4, ///< When set, use the Intel dialect; otherwise AT&T.
    Extra_MayLoad = 8,        ///< ExtraInfo bit: asm may load from memory.
    Extra_MayStore = 16,      ///< ExtraInfo bit: asm may store to memory.
    Extra_IsConvergent = 32,  ///< ExtraInfo bit: asm is convergent.
    Extra_MayUnwind = 64,     ///< ExtraInfo bit: asm may unwind.
  };

  // Inline asm operands map to multiple SDNode / MachineInstr operands.
  // The first operand is an immediate describing the asm operand, the low
  // bits is the kind:
  /// Kind of inline asm operand encoded in a Flag's low bits.
  enum class Kind : uint8_t {
    /// Input register operand (constraint "r").
    RegUse = 1,
    /// Output register operand (constraint "=r").
    RegDef = 2,
    /// Early-clobber output register (constraint "=&r").
    RegDefEarlyClobber = 3,
    /// Clobbered register (constraint "~r").
    Clobber = 4,
    /// Immediate operand.
    Imm = 5,
    /// Memory operand ("m") or address ("p").
    Mem = 6,
    /// Address operand of a function call.
    Func = 7,
  };

  // Memory constraint codes.
  // Addresses are included here as they need to be treated the same by the
  // backend, the only difference is that they are not used to actaully
  // access memory by the instruction.
  /// Memory or address constraint code carried in a Flag.
  enum class ConstraintCode : uint32_t {
    Unknown = 0, ///< Unspecified or cleared memory constraint.
    es,          ///< Memory constraint letter "es".
    i,           ///< Memory constraint letter "i".
    k, ///< Memory constraint letter "k" (e.g. LoongArch address in a GPR).
    m,           ///< Memory constraint letter "m".
    o,           ///< Memory constraint letter "o".
    v,           ///< Memory constraint letter "v".
    A,           ///< Memory constraint letter "A".
    Q,           ///< Memory constraint letter "Q".
    R,           ///< Memory constraint letter "R".
    S,           ///< Memory constraint letter "S".
    T,           ///< Memory constraint letter "T".
    Um,          ///< Memory constraint letter "Um".
    Un,          ///< Memory constraint letter "Un".
    Uq,          ///< Memory constraint letter "Uq".
    Us,          ///< Memory constraint letter "Us".
    Ut, ///< Memory constraint letter "Ut" (ARM: NEON struct memory operand).
    Uv,          ///< Memory constraint letter "Uv".
    Uy,          ///< Memory constraint letter "Uy".
    X,           ///< Memory constraint letter "X".
    Z,           ///< Memory constraint letter "Z".
    ZB, ///< LoongArch: address held in a general-purpose register.
    ZC,          ///< Memory constraint letter "ZC".
    Zy,          ///< Memory constraint letter "Zy".

    // Address constraints
    p,  ///< Address constraint letter "p".
    ZQ, ///< Address constraint letter "ZQ".
    ZR, ///< Address constraint letter "ZR".
    ZS, ///< Address constraint letter "ZS".
    ZT, ///< Address constraint letter "ZT".

    Max = ZT, ///< Largest valid ConstraintCode value (for bitfield bounds).
  };

  /// Packed 32-bit encoding of an inline asm operand for SelectionDAG and MIR.
  ///
  /// This class is intentionally packed into a 32b value as it is used as a
  /// MVT::i32 ConstantSDNode SDValue for SelectionDAG and as immediate operands
  /// on INLINEASM and INLINEASM_BR MachineInstr's.
  ///
  /// The encoding of Flag is currently:
  ///   Bits 2-0  - A Kind::* value indicating the kind of the operand.
  ///               (KindField)
  ///   Bits 15-3 - The number of SDNode operands associated with this inline
  ///               assembly operand. Once lowered to MIR, this represents the
  ///               number of MachineOperands necessary to refer to a
  ///               MachineOperandType::MO_FrameIndex. (NumOperands)
  ///   Bit 31    - Determines if this is a matched operand. (IsMatched)
  ///   If bit 31 is set:
  ///     Bits 30-16 - The operand number that this operand must match.
  ///                  (MatchedOperandNo)
  ///   Else if bits 2-0 are Kind::Mem:
  ///     Bits 30-16 - A ConstraintCode:: value indicating the original
  ///                  constraint code. (MemConstraintCode)
  ///   Else:
  ///     Bits 29-16 - The register class ID to use for the operand. (RegClass)
  ///     Bit  30    - If the register is permitted to be spilled.
  ///                  (RegMayBeFolded)
  ///                  Defaults to false "r", may be set for constraints like
  ///                  "rm" (or "g").
  ///
  ///   As such, MatchedOperandNo, MemConstraintCode, and
  ///   (RegClass+RegMayBeFolded) are views of the same slice of bits, but are
  ///   mutually exclusive depending on the fields IsMatched then KindField.
  class Flag {
    uint32_t Storage;
    using KindField = Bitfield::Element<Kind, 0, 3, Kind::Func>;
    using NumOperands = Bitfield::Element<unsigned, 3, 13>;
    using MatchedOperandNo = Bitfield::Element<unsigned, 16, 15>;
    using MemConstraintCode = Bitfield::Element<ConstraintCode, 16, 15, ConstraintCode::Max>;
    using RegClass = Bitfield::Element<unsigned, 16, 14>;
    using RegMayBeFolded = Bitfield::Element<bool, 30, 1>;
    using IsMatched = Bitfield::Element<bool, 31, 1>;


    unsigned getMatchedOperandNo() const { return Bitfield::get<MatchedOperandNo>(Storage); }
    unsigned getRegClass() const { return Bitfield::get<RegClass>(Storage); }
    bool isMatched() const { return Bitfield::get<IsMatched>(Storage); }

  public:
    /// Construct a zero-initialized flag.
    Flag() : Storage(0) {}
    /// Construct a flag from a raw 32-bit encoding.
    /// \param F The packed flag bits.
    explicit Flag(uint32_t F) : Storage(F) {}
    /// Construct a flag with the given kind and operand count.
    /// \param K The operand kind.
    /// \param NumOps Number of SDNode/Machine operands for this asm operand.
    Flag(enum Kind K, unsigned NumOps) : Storage(0) {
      Bitfield::set<KindField>(Storage, K);
      Bitfield::set<NumOperands>(Storage, NumOps);
    }
    /// Convert this flag to its raw 32-bit encoding.
    /// \return The packed 32-bit flag bits.
    operator uint32_t() { return Storage; }
    /// Return the operand kind encoded in this flag.
    /// \return The Kind encoded in this flag.
    Kind getKind() const { return Bitfield::get<KindField>(Storage); }
    /// Return true if this flag describes an input register (Kind::RegUse).
    /// \return True if this flag is Kind::RegUse.
    bool isRegUseKind() const { return getKind() == Kind::RegUse; }
    /// Return true if this flag describes an output register (Kind::RegDef).
    /// \return True if this flag is Kind::RegDef.
    bool isRegDefKind() const { return getKind() == Kind::RegDef; }
    /// Return true if this flag is an early-clobber output register.
    /// \return True if this flag is Kind::RegDefEarlyClobber.
    bool isRegDefEarlyClobberKind() const {
      return getKind() == Kind::RegDefEarlyClobber;
    }
    /// Return true if this flag describes a clobbered register.
    /// \return True if this flag is Kind::Clobber.
    bool isClobberKind() const { return getKind() == Kind::Clobber; }
    /// Return true if this flag describes an immediate operand.
    /// \return True if this flag is Kind::Imm.
    bool isImmKind() const { return getKind() == Kind::Imm; }
    /// Return true if this flag describes a memory operand.
    /// \return True if this flag is Kind::Mem.
    bool isMemKind() const { return getKind() == Kind::Mem; }
    /// Return true if this flag describes a function-call address operand.
    /// \return True if this flag is Kind::Func.
    bool isFuncKind() const { return getKind() == Kind::Func; }
    /// Return a short name string for this flag's kind.
    /// \return A short name for this flag's kind.
    StringRef getKindName() const {
      switch (getKind()) {
      case Kind::RegUse:
        return "reguse";
      case Kind::RegDef:
        return "regdef";
      case Kind::RegDefEarlyClobber:
        return "regdef-ec";
      case Kind::Clobber:
        return "clobber";
      case Kind::Imm:
        return "imm";
      case Kind::Mem:
      case Kind::Func:
        return "mem";
      }
      llvm_unreachable("impossible kind");
    }

    /// getNumOperandRegisters - Extract the number of registers field from the
    /// inline asm operand flag.
    /// \return The number of SDNode/Machine operands for this asm operand.
    unsigned getNumOperandRegisters() const {
      return Bitfield::get<NumOperands>(Storage);
    }

    /// isUseOperandTiedToDef - Return true if the flag of the inline asm
    /// operand indicates it is an use operand that's matched to a def operand.
    /// \param Idx Set to the matched def operand number on success.
    /// \return True if this use operand is tied to a def.
    bool isUseOperandTiedToDef(unsigned &Idx) const {
      if (!isMatched())
        return false;
      Idx = getMatchedOperandNo();
      return true;
    }

    /// hasRegClassConstraint - Returns true if the flag contains a register
    /// class constraint.  Sets RC to the register class ID.
    /// \param RC Set to the register class ID on success.
    /// \return True if a register class constraint is present.
    bool hasRegClassConstraint(unsigned &RC) const {
      if (isMatched())
        return false;
      // setRegClass() uses 0 to mean no register class, and otherwise stores
      // RC + 1.
      if (!getRegClass())
        return false;
      RC = getRegClass() - 1;
      return true;
    }

    /// Return the memory constraint code stored in this flag.
    /// \return The ConstraintCode stored in this flag.
    ConstraintCode getMemoryConstraintID() const {
      assert((isMemKind() || isFuncKind()) &&
             "Not expected mem or function flag!");
      return Bitfield::get<MemConstraintCode>(Storage);
    }

    /// setMatchingOp - Augment an existing flag with information indicating
    /// that this input operand is tied to a previous output operand.
    /// \param OperandNo Operand number of the matching output operand.
    void setMatchingOp(unsigned OperandNo) {
      assert(getMatchedOperandNo() == 0 && "Matching operand already set");
      Bitfield::set<MatchedOperandNo>(Storage, OperandNo);
      Bitfield::set<IsMatched>(Storage, true);
    }

    /// Augment this flag with the register class for following register operands.
    ///
    /// A tied use operand cannot have a register class; use the register class
    /// from the def operand instead.
    /// \param RC The register class ID.
    void setRegClass(unsigned RC) {
      assert(!isImmKind() && "Immediates cannot have a register class");
      assert(!isMemKind() && "Memory operand cannot have a register class");
      assert(getRegClass() == 0 && "Register class already set");
      // Store RC + 1, reserve the value 0 to mean 'no register class'.
      Bitfield::set<RegClass>(Storage, RC + 1);
    }

    /// setMemConstraint - Augment an existing flag with the constraint code for
    /// a memory constraint.
    /// \param C The memory constraint code to store.
    void setMemConstraint(ConstraintCode C) {
      assert(getMemoryConstraintID() == ConstraintCode::Unknown && "Mem constraint already set");
      Bitfield::set<MemConstraintCode>(Storage, C);
    }
    /// clearMemConstraint - Similar to setMemConstraint(0), but without the
    /// assertion checking that the constraint has not been set previously.
    void clearMemConstraint() {
      assert((isMemKind() || isFuncKind()) &&
             "Flag is not a memory or function constraint!");
      Bitfield::set<MemConstraintCode>(Storage, ConstraintCode::Unknown);
    }

    /// Mark whether this register operand may be folded to memory.
    ///
    /// Set a bit to denote that while this operand is some kind of register
    /// (use, def, ...), a memory flag did appear in the original constraint
    /// list. This is set by the instruction selection framework, and consumed
    /// by the register allocator. While the register allocator is generally
    /// responsible for spilling registers, we need to be able to distinguish
    /// between registers that the register allocator has permission to fold
    /// ("rm") vs ones it does not ("r"). This is because the inline asm may use
    /// instructions which don't support memory addressing modes for that
    /// operand.
    /// \param B True if the register may be spilled or folded to memory.
    void setRegMayBeFolded(bool B) {
      assert((isRegDefKind() || isRegDefEarlyClobberKind() || isRegUseKind()) &&
             "Must be reg");
      Bitfield::set<RegMayBeFolded>(Storage, B);
    }
    /// Return true if this register operand may be folded to memory ("rm").
    /// \return True if the register may be spilled or folded to memory.
    bool getRegMayBeFolded() const {
      assert((isRegDefKind() || isRegDefEarlyClobberKind() || isRegUseKind()) &&
             "Must be reg");
      return Bitfield::get<RegMayBeFolded>(Storage);
    }
  };

  /// Return the assembly dialect encoded in ExtraInfo bit flags.
  /// \param ExtraInfo Bitmask including Extra_AsmDialect.
  /// \return The Intel dialect if Extra_AsmDialect is set; otherwise AT&T.
  static AsmDialect getDialect(unsigned ExtraInfo) {
    return ExtraInfo & Extra_AsmDialect ? AD_Intel : AD_ATT;
  }

  /// Return mnemonic names for the set bits in \p ExtraInfo.
  /// \param ExtraInfo Bitmask of Extra_* flags (and dialect).
  /// \return Names for each set Extra_* or dialect flag bit.
  static std::vector<StringRef> getExtraInfoNames(unsigned ExtraInfo) {
    std::vector<StringRef> Result;
    if (ExtraInfo & InlineAsm::Extra_HasSideEffects)
      Result.push_back("sideeffect");
    if (ExtraInfo & InlineAsm::Extra_MayLoad)
      Result.push_back("mayload");
    if (ExtraInfo & InlineAsm::Extra_MayStore)
      Result.push_back("maystore");
    if (ExtraInfo & InlineAsm::Extra_IsConvergent)
      Result.push_back("isconvergent");
    if (ExtraInfo & InlineAsm::Extra_IsAlignStack)
      Result.push_back("alignstack");
    if (ExtraInfo & InlineAsm::Extra_MayUnwind)
      Result.push_back("unwind");

    AsmDialect Dialect = getDialect(ExtraInfo);
    if (Dialect == InlineAsm::AD_ATT)
      Result.push_back("attdialect");
    if (Dialect == InlineAsm::AD_Intel)
      Result.push_back("inteldialect");

    return Result;
  }

  /// Return the string spelling of memory constraint code \p C.
  /// \param C The memory constraint code.
  /// \return The spelling of \p C as a string.
  static StringRef getMemConstraintName(ConstraintCode C) {
    switch (C) {
    case ConstraintCode::es:
      return "es";
    case ConstraintCode::i:
      return "i";
    case ConstraintCode::k:
      return "k";
    case ConstraintCode::m:
      return "m";
    case ConstraintCode::o:
      return "o";
    case ConstraintCode::v:
      return "v";
    case ConstraintCode::A:
      return "A";
    case ConstraintCode::Q:
      return "Q";
    case ConstraintCode::R:
      return "R";
    case ConstraintCode::S:
      return "S";
    case ConstraintCode::T:
      return "T";
    case ConstraintCode::Um:
      return "Um";
    case ConstraintCode::Un:
      return "Un";
    case ConstraintCode::Uq:
      return "Uq";
    case ConstraintCode::Us:
      return "Us";
    case ConstraintCode::Ut:
      return "Ut";
    case ConstraintCode::Uv:
      return "Uv";
    case ConstraintCode::Uy:
      return "Uy";
    case ConstraintCode::X:
      return "X";
    case ConstraintCode::Z:
      return "Z";
    case ConstraintCode::ZB:
      return "ZB";
    case ConstraintCode::ZC:
      return "ZC";
    case ConstraintCode::Zy:
      return "Zy";
    case ConstraintCode::p:
      return "p";
    case ConstraintCode::ZQ:
      return "ZQ";
    case ConstraintCode::ZR:
      return "ZR";
    case ConstraintCode::ZS:
      return "ZS";
    case ConstraintCode::ZT:
      return "ZT";
    default:
      llvm_unreachable("Unknown memory constraint");
    }
  }
};

} // end namespace llvm

#endif // LLVM_IR_INLINEASM_H
