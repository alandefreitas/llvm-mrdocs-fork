//===-- llvm/CodeGen/PseudoSourceValue.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the PseudoSourceValue class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PSEUDOSOURCEVALUE_H
#define LLVM_CODEGEN_PSEUDOSOURCEVALUE_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class GlobalValue;
class MachineFrameInfo;
class MachineMemOperand;
/// Target-specific formatter for MIR operands and pseudo source values.
class MIRFormatter;
class PseudoSourceValue;
class raw_ostream;
/// Primary interface to target-specific machine code generation.
class TargetMachine;

/// Write a PseudoSourceValue to a stream.
///
/// \param OS Stream to write to.
/// \param PSV Pseudo source value to print; may be null.
/// \return The stream \p OS after writing.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PseudoSourceValue *PSV);

/// Special value supplied for machine level alias analysis.
///
/// It indicates that a memory access references the functions stack frame
/// (e.g., a spill slot), below the stack frame (e.g., argument space), or
/// constant pool.
class LLVM_ABI PseudoSourceValue {
public:
  /// Kind of pseudo source value used for machine-level alias analysis.
  enum PSVKind : unsigned {
    /// Reference to the function's stack frame.
    Stack,
    /// Reference to the Global Offset Table.
    GOT,
    /// Reference to a jump table.
    JumpTable,
    /// Reference to the constant pool.
    ConstantPool,
    /// Reference to a fixed stack object identified by a frame index.
    FixedStack,
    /// Call entry referencing a GlobalValue.
    GlobalValueCallEntry,
    /// Call entry referencing an external symbol.
    ExternalSymbolCallEntry,
    /// First target-specific custom pseudo source value kind.
    TargetCustom
  };

private:
  unsigned Kind;
  unsigned AddressSpace;
  LLVM_ABI friend raw_ostream &llvm::operator<<(raw_ostream &OS,
                                                const PseudoSourceValue *PSV);

  friend class MachineMemOperand; // For printCustom().
  friend class MIRFormatter;      // For printCustom().

  /// Implement printing for PseudoSourceValue. This is called from
  /// Value::print or Value's operator<<.
  virtual void printCustom(raw_ostream &O) const;

public:
  /// Construct a PseudoSourceValue of the given kind.
  ///
  /// \param Kind Discriminator for this pseudo source value.
  /// \param TM Target machine used to resolve address-space information.
  explicit PseudoSourceValue(unsigned Kind, const TargetMachine &TM);

  /// Destroy a PseudoSourceValue.
  virtual ~PseudoSourceValue();

  /// Return the kind discriminator for this PseudoSourceValue.
  ///
  /// \return The PSVKind discriminator for this value.
  unsigned kind() const { return Kind; }

  /// Return true if this pseudo source value refers to the function stack frame.
  ///
  /// \return True if this value refers to the stack frame.
  bool isStack() const { return Kind == Stack; }
  /// Return true if this pseudo source value refers to the GOT.
  ///
  /// \return True if this value refers to the GOT.
  bool isGOT() const { return Kind == GOT; }
  /// Return true if this pseudo source value refers to the constant pool.
  ///
  /// \return True if this value refers to the constant pool.
  bool isConstantPool() const { return Kind == ConstantPool; }
  /// Return true if this pseudo source value refers to a jump table.
  ///
  /// \return True if this value refers to a jump table.
  bool isJumpTable() const { return Kind == JumpTable; }

  /// Return the address space of the memory referenced by this value.
  ///
  /// \return The LLVM IR address space of the referenced memory.
  unsigned getAddressSpace() const { return AddressSpace; }

  /// Return the target-custom kind index, or 0 if not a target-custom value.
  ///
  /// \return One-based target-custom kind index, or 0 if not target-custom.
  unsigned getTargetCustom() const {
    return (Kind >= TargetCustom) ? ((Kind+1) - TargetCustom) : 0;
  }

  /// Test whether the memory pointed to by this PseudoSourceValue has a
  /// constant value.
  ///
  /// \param MFI Machine frame info used for stack-related queries.
  /// \return True if the referenced memory has a constant value.
  virtual bool isConstant(const MachineFrameInfo *MFI) const;

  /// Test whether the memory pointed to by this PseudoSourceValue may also be
  /// pointed to by an LLVM IR Value.
  ///
  /// \param MFI Machine frame info used for stack-related queries.
  /// \return True if an LLVM IR Value may also point to this memory.
  virtual bool isAliased(const MachineFrameInfo *MFI) const;

  /// Return true if the memory pointed to by this PseudoSourceValue can ever
  /// alias an LLVM IR Value.
  ///
  /// \param MFI Machine frame info used for stack-related queries.
  /// \return True if the referenced memory can ever alias an LLVM IR Value.
  virtual bool mayAlias(const MachineFrameInfo *MFI) const;
};

/// A specialized PseudoSourceValue for holding FixedStack values, which must
/// include a frame index.
class LLVM_ABI FixedStackPseudoSourceValue : public PseudoSourceValue {
  const int FI;

public:
  /// Construct a FixedStack PseudoSourceValue for frame index \p FI.
  ///
  /// \param FI Frame index of the fixed stack object.
  /// \param TM Target machine used to initialize the base PseudoSourceValue.
  explicit FixedStackPseudoSourceValue(int FI, const TargetMachine &TM)
      : PseudoSourceValue(FixedStack, TM), FI(FI) {}

  /// Return true if \p V is a FixedStackPseudoSourceValue.
  ///
  /// \param V Pseudo source value to test.
  /// \return True if \p V is a FixedStackPseudoSourceValue.
  static bool classof(const PseudoSourceValue *V) {
    return V->kind() == FixedStack;
  }

  /// Return true if the fixed stack slot for this PSV is immutable (never
  /// modified after initialization).
  ///
  /// \param MFI Machine frame info used to query stack object properties.
  /// \return True if the fixed stack slot is immutable.
  bool isConstant(const MachineFrameInfo *MFI) const override;

  /// Return true if this fixed stack object may be pointed to by an LLVM IR
  /// Value.
  ///
  /// \param MFI Machine frame info used to query stack object properties.
  /// \return True if an LLVM IR Value may point to this fixed stack object.
  bool isAliased(const MachineFrameInfo *MFI) const override;

  /// Return true if this fixed stack object can ever alias an LLVM IR Value.
  ///
  /// \param MFI Machine frame info used to query stack object properties.
  /// \return True if this fixed stack object can ever alias an LLVM IR Value.
  bool mayAlias(const MachineFrameInfo *MFI) const override;

  /// Print this FixedStack PseudoSourceValue to \p OS.
  ///
  /// \param OS Stream to print to.
  void printCustom(raw_ostream &OS) const override;

  /// Return the frame index of the fixed stack object.
  ///
  /// \return The frame index of the fixed stack object.
  int getFrameIndex() const { return FI; }
};

/// A PseudoSourceValue representing a call entry (GlobalValue or external
/// symbol).
class LLVM_ABI CallEntryPseudoSourceValue : public PseudoSourceValue {
protected:
  /// Construct a call-entry PseudoSourceValue of the given kind.
  ///
  /// \param Kind Discriminator for this call-entry value.
  /// \param TM Target machine used to initialize the base PseudoSourceValue.
  CallEntryPseudoSourceValue(unsigned Kind, const TargetMachine &TM);

public:
  /// Call-entry pseudo values are never constant; always returns false.
  ///
  /// \param MFI Machine frame info (unused for call entries).
  /// \return Always false.
  bool isConstant(const MachineFrameInfo *MFI) const override;
  /// Call-entry pseudo values are never aliased; always returns false.
  ///
  /// \param MFI Machine frame info (unused for call entries).
  /// \return Always false.
  bool isAliased(const MachineFrameInfo *MFI) const override;
  /// Call-entry pseudo values never alias IR values; always returns false.
  ///
  /// \param MFI Machine frame info (unused for call entries).
  /// \return Always false.
  bool mayAlias(const MachineFrameInfo *MFI) const override;
};

/// A specialized pseudo source value for holding GlobalValue values.
class GlobalValuePseudoSourceValue : public CallEntryPseudoSourceValue {
  const GlobalValue *GV;

public:
  /// Construct a call-entry PseudoSourceValue for GlobalValue \p GV.
  ///
  /// \param GV Global value referenced by this call entry.
  /// \param TM Target machine used to initialize the base PseudoSourceValue.
  LLVM_ABI GlobalValuePseudoSourceValue(const GlobalValue *GV,
                                        const TargetMachine &TM);

  /// Return true if \p V is a GlobalValuePseudoSourceValue.
  ///
  /// \param V Pseudo source value to test.
  /// \return True if \p V is a GlobalValuePseudoSourceValue.
  static bool classof(const PseudoSourceValue *V) {
    return V->kind() == GlobalValueCallEntry;
  }

  /// Return the GlobalValue represented by this call-entry pseudo source value.
  ///
  /// \return The GlobalValue referenced by this call entry.
  const GlobalValue *getValue() const { return GV; }
};

/// A specialized pseudo source value for holding external symbol values.
class ExternalSymbolPseudoSourceValue : public CallEntryPseudoSourceValue {
  const char *ES;

public:
  /// Construct a call-entry PseudoSourceValue for external symbol \p ES.
  ///
  /// \param ES External symbol name referenced by this call entry.
  /// \param TM Target machine used to initialize the base PseudoSourceValue.
  LLVM_ABI ExternalSymbolPseudoSourceValue(const char *ES,
                                           const TargetMachine &TM);

  /// Return true if \p V is an ExternalSymbolPseudoSourceValue.
  ///
  /// \param V Pseudo source value to test.
  /// \return True if \p V is an ExternalSymbolPseudoSourceValue.
  static bool classof(const PseudoSourceValue *V) {
    return V->kind() == ExternalSymbolCallEntry;
  }

  /// Return the external symbol name represented by this call-entry value.
  ///
  /// \return The external symbol name referenced by this call entry.
  const char *getSymbol() const { return ES; }
};

} // end namespace llvm

#endif
