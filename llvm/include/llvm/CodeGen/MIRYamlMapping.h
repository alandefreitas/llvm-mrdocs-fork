//===- MIRYamlMapping.h - Describe mapping between MIR and YAML--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the mapping between various MIR data structures and
// their corresponding YAML representation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRYAMLMAPPING_H
#define LLVM_CODEGEN_MIRYAMLMAPPING_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
/// YAML serialization support for Machine IR (MIR).
namespace yaml {

/// A wrapper around std::string which contains a source range that's being
/// set during parsing.
struct StringValue {
  /// The string contents.
  std::string Value;
  /// Source range of the scalar in the input YAML, if available.
  SMRange SourceRange;

  /// Construct an empty string value.
  StringValue() = default;
  /// Construct from string contents \p Value.
  /// \param Value String to store.
  StringValue(std::string Value) : Value(std::move(Value)) {}
  /// Construct from a NUL-terminated C string \p Val.
  /// \param Val C string to store.
  StringValue(const char Val[]) : Value(Val) {}

  /// Return true if this equals \p Other by string contents.
  /// \param Other Value to compare against.
  /// \returns True if the string contents are equal.
  bool operator==(const StringValue &Other) const {
    return Value == Other.Value;
  }
};

/// YAMLIO scalar traits for \c StringValue.
template <> struct ScalarTraits<StringValue> {
  /// Write \p S to \p OS.
  /// \param S Value to write.
  /// \param Ctx Unused client context.
  /// \param OS Output stream.
  static void output(const StringValue &S, void *Ctx, raw_ostream &OS) {
    (void)Ctx;
    OS << S.Value;
  }

  /// Parse \p Scalar into \p S and record its source range.
  /// \param Scalar YAML scalar text.
  /// \param Ctx YAML input context used to fetch the source range.
  /// \param S Destination value.
  /// \returns Empty on success; otherwise an error string.
  static StringRef input(StringRef Scalar, void *Ctx, StringValue &S) {
    S.Value = Scalar.str();
    if (const auto *Node =
            reinterpret_cast<yaml::Input *>(Ctx)->getCurrentNode())
      S.SourceRange = Node->getSourceRange();
    return "";
  }

  /// Decide whether \p S needs quoting in YAML output.
  /// \param S Scalar text to inspect.
  /// \returns The quoting type required for \p S.
  static QuotingType mustQuote(StringRef S) { return needsQuotes(S); }
};

/// A \c StringValue that prefers flow-sequence formatting when used in lists.
struct FlowStringValue : StringValue {
  /// Construct an empty flow string value.
  FlowStringValue() = default;
  /// Construct from string contents \p Value.
  /// \param Value String to store.
  FlowStringValue(std::string Value) : StringValue(std::move(Value)) {}
};

/// YAMLIO scalar traits for \c FlowStringValue.
template <> struct ScalarTraits<FlowStringValue> {
  /// Write \p S to \p OS via \c StringValue traits.
  /// \param S Value to write.
  /// \param Ctx Unused client context.
  /// \param OS Output stream.
  static void output(const FlowStringValue &S, void *Ctx, raw_ostream &OS) {
    (void)Ctx;
    return ScalarTraits<StringValue>::output(S, nullptr, OS);
  }

  /// Parse \p Scalar into \p S via \c StringValue traits.
  /// \param Scalar YAML scalar text.
  /// \param Ctx YAML input context used to fetch the source range.
  /// \param S Destination value.
  /// \returns Empty on success; otherwise an error string.
  static StringRef input(StringRef Scalar, void *Ctx, FlowStringValue &S) {
    return ScalarTraits<StringValue>::input(Scalar, Ctx, S);
  }

  /// Decide whether \p S needs quoting in YAML output.
  /// \param S Scalar text to inspect.
  /// \returns The quoting type required for \p S.
  static QuotingType mustQuote(StringRef S) { return needsQuotes(S); }
};

/// A string value intended for YAML literal block scalars.
struct BlockStringValue {
  /// The wrapped string value.
  StringValue Value;

  /// Return true if this equals \p Other by string contents.
  /// \param Other Value to compare against.
  /// \returns True if the string contents are equal.
  bool operator==(const BlockStringValue &Other) const {
    return Value == Other.Value;
  }
};

/// YAMLIO block-scalar traits for \c BlockStringValue.
template <> struct BlockScalarTraits<BlockStringValue> {
  /// Write \p S to \p OS as a block scalar.
  /// \param S Value to write.
  /// \param Ctx Client context passed through to scalar traits.
  /// \param OS Output stream.
  static void output(const BlockStringValue &S, void *Ctx, raw_ostream &OS) {
    return ScalarTraits<StringValue>::output(S.Value, Ctx, OS);
  }

  /// Parse block-scalar text \p Scalar into \p S.
  /// \param Scalar YAML block-scalar text.
  /// \param Ctx YAML input context used to fetch the source range.
  /// \param S Destination value.
  /// \returns Empty on success; otherwise an error string.
  static StringRef input(StringRef Scalar, void *Ctx, BlockStringValue &S) {
    return ScalarTraits<StringValue>::input(Scalar, Ctx, S.Value);
  }
};

/// A wrapper around unsigned which contains a source range that's being set
/// during parsing.
struct UnsignedValue {
  /// The unsigned integer value.
  unsigned Value = 0;
  /// Source range of the scalar in the input YAML, if available.
  SMRange SourceRange;

  /// Construct a zero value.
  UnsignedValue() = default;
  /// Construct from unsigned \p Value.
  /// \param Value Integer to store.
  UnsignedValue(unsigned Value) : Value(Value) {}

  /// Return true if this equals \p Other by numeric value.
  /// \param Other Value to compare against.
  /// \returns True if the numeric values are equal.
  bool operator==(const UnsignedValue &Other) const {
    return Value == Other.Value;
  }
};

/// YAMLIO scalar traits for \c UnsignedValue.
template <> struct ScalarTraits<UnsignedValue> {
  /// Write \p Value to \p OS as an unsigned integer.
  /// \param Value Value to write.
  /// \param Ctx Client context passed through to unsigned traits.
  /// \param OS Output stream.
  static void output(const UnsignedValue &Value, void *Ctx, raw_ostream &OS) {
    return ScalarTraits<unsigned>::output(Value.Value, Ctx, OS);
  }

  /// Parse \p Scalar into \p Value and record its source range.
  /// \param Scalar YAML scalar text.
  /// \param Ctx YAML input context used to fetch the source range.
  /// \param Value Destination value.
  /// \returns Empty on success; otherwise an error string.
  static StringRef input(StringRef Scalar, void *Ctx, UnsignedValue &Value) {
    if (const auto *Node =
            reinterpret_cast<yaml::Input *>(Ctx)->getCurrentNode())
      Value.SourceRange = Node->getSourceRange();
    return ScalarTraits<unsigned>::input(Scalar, Ctx, Value.Value);
  }

  /// Decide whether \p Scalar needs quoting in YAML output.
  /// \param Scalar Scalar text to inspect.
  /// \returns The quoting type required for \p Scalar.
  static QuotingType mustQuote(StringRef Scalar) {
    return ScalarTraits<unsigned>::mustQuote(Scalar);
  }
};

/// YAMLIO scalar enumeration traits for \c MachineJumpTableInfo::JTEntryKind.
template <> struct ScalarEnumerationTraits<MachineJumpTableInfo::JTEntryKind> {
  /// Map jump-table entry kind enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param EntryKind Entry kind being mapped.
  static void enumeration(yaml::IO &IO,
                          MachineJumpTableInfo::JTEntryKind &EntryKind) {
    IO.enumCase(EntryKind, "block-address",
                MachineJumpTableInfo::EK_BlockAddress);
    IO.enumCase(EntryKind, "gp-rel64-block-address",
                MachineJumpTableInfo::EK_GPRel64BlockAddress);
    IO.enumCase(EntryKind, "gp-rel32-block-address",
                MachineJumpTableInfo::EK_GPRel32BlockAddress);
    IO.enumCase(EntryKind, "label-difference32",
                MachineJumpTableInfo::EK_LabelDifference32);
    IO.enumCase(EntryKind, "label-difference64",
                MachineJumpTableInfo::EK_LabelDifference64);
    IO.enumCase(EntryKind, "inline", MachineJumpTableInfo::EK_Inline);
    IO.enumCase(EntryKind, "custom32", MachineJumpTableInfo::EK_Custom32);
  }
};

/// YAMLIO scalar enumeration traits for \c FramePointerKind.
template <> struct ScalarEnumerationTraits<FramePointerKind> {
  /// Map frame-pointer policy enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param FP Frame-pointer policy being mapped.
  static void enumeration(IO &IO, FramePointerKind &FP) {
    IO.enumCase(FP, "none", FramePointerKind::None);
    IO.enumCase(FP, "non-leaf", FramePointerKind::NonLeaf);
    IO.enumCase(FP, "all", FramePointerKind::All);
    IO.enumCase(FP, "reserved", FramePointerKind::Reserved);
    IO.enumCase(FP, "non-leaf-no-reserve", FramePointerKind::NonLeafNoReserve);
  }
};

/// YAMLIO scalar traits for \c MaybeAlign.
template <> struct ScalarTraits<MaybeAlign> {
  /// Write \p Alignment to \p out as an integer (0 when unset).
  /// \param Alignment Value to write.
  /// \param Ctx Unused client context.
  /// \param out Output stream.
  static void output(const MaybeAlign &Alignment, void *Ctx,
                     llvm::raw_ostream &out) {
    (void)Ctx;
    out << uint64_t(Alignment ? Alignment->value() : 0U);
  }
  /// Parse \p Scalar into \p Alignment (0 or a power of two).
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Alignment Destination value.
  /// \returns Empty on success; otherwise an error string.
  static StringRef input(StringRef Scalar, void *Ctx, MaybeAlign &Alignment) {
    (void)Ctx;
    unsigned long long n;
    if (getAsUnsignedInteger(Scalar, 10, n))
      return "invalid number";
    if (n > 0 && !isPowerOf2_64(n))
      return "must be 0 or a power of two";
    Alignment = MaybeAlign(n);
    return StringRef();
  }
  /// Alignments never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) {
    (void)Scalar;
    return QuotingType::None;
  }
};

/// YAMLIO scalar traits for \c Align.
template <> struct ScalarTraits<Align> {
  /// Write \p Alignment to \p OS as an integer.
  /// \param Alignment Value to write.
  /// \param Ctx Unused client context.
  /// \param OS Output stream.
  static void output(const Align &Alignment, void *Ctx, llvm::raw_ostream &OS) {
    (void)Ctx;
    OS << Alignment.value();
  }
  /// Parse \p Scalar into \p Alignment (must be a power of two).
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Alignment Destination value.
  /// \returns Empty on success; otherwise an error string.
  static StringRef input(StringRef Scalar, void *Ctx, Align &Alignment) {
    (void)Ctx;
    unsigned long long N;
    if (getAsUnsignedInteger(Scalar, 10, N))
      return "invalid number";
    if (!isPowerOf2_64(N))
      return "must be a power of two";
    Alignment = Align(N);
    return StringRef();
  }
  /// Alignments never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) {
    (void)Scalar;
    return QuotingType::None;
  }
};

/// Sequences of \c StringValue use block formatting.
template <> struct SequenceElementTraits<StringValue> {
  /// Emit sequences of StringValue in block style.
  static const bool flow = false;
};
/// Sequences of \c FlowStringValue use flow formatting.
template <> struct SequenceElementTraits<FlowStringValue> {
  /// Emit sequences of FlowStringValue in flow style.
  static const bool flow = true;
};
/// Sequences of \c UnsignedValue use flow formatting.
template <> struct SequenceElementTraits<UnsignedValue> {
  /// Emit sequences of UnsignedValue in flow style.
  static const bool flow = true;
};

/// Serializable representation of a virtual register definition.
struct VirtualRegisterDefinition {
  /// Virtual register identifier.
  UnsignedValue ID;
  /// Register class name.
  StringValue Class;
  /// Preferred physical register, if any.
  StringValue PreferredRegister;
  /// Named register flags applied to this virtual register.
  std::vector<FlowStringValue> RegisterFlags;
  // VirtRegMap state.
  // SplitFrom: id-form virtual register only (e.g. '%0'); physregs and named
  //            vregs are rejected by the parser.
  // AssignedPhys: physical register only (e.g. '$r5'); virtregs are rejected.
  /// Virtual register this one was split from, if any.
  StringValue SplitFrom;
  /// Assigned physical register, if any.
  StringValue AssignedPhys;

  // TODO: Serialize the target specific register hints.

  /// Return true if this equals \p Other.
  /// \param Other Definition to compare against.
  /// \returns True if the virtual register definitions are equal.
  bool operator==(const VirtualRegisterDefinition &Other) const {
    return ID == Other.ID && Class == Other.Class &&
           PreferredRegister == Other.PreferredRegister &&
           SplitFrom == Other.SplitFrom && AssignedPhys == Other.AssignedPhys;
  }
};

/// YAMLIO mapping traits for \c VirtualRegisterDefinition.
template <> struct MappingTraits<VirtualRegisterDefinition> {
  /// Map virtual register definition fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param Reg Virtual register definition being mapped.
  static void mapping(IO &YamlIO, VirtualRegisterDefinition &Reg) {
    YamlIO.mapRequired("id", Reg.ID);
    YamlIO.mapRequired("class", Reg.Class);
    YamlIO.mapOptional("preferred-register", Reg.PreferredRegister,
                       StringValue()); // Don't print out when it's empty.
    YamlIO.mapOptional("flags", Reg.RegisterFlags,
                       std::vector<FlowStringValue>());
    // MIRPrinter sets WriteDefaultValues=true unless -simplify-mir is passed,
    // so a plain mapOptional with an empty default would still emit the keys
    // and change every existing test's output.
    // Skip the call on output when empty to keep them off entirely.
    if (!YamlIO.outputting() || !Reg.SplitFrom.Value.empty())
      YamlIO.mapOptional("split-from", Reg.SplitFrom, StringValue());
    if (!YamlIO.outputting() || !Reg.AssignedPhys.Value.empty())
      YamlIO.mapOptional("assigned-phys", Reg.AssignedPhys, StringValue());
  }

  /// When true, emit this mapping in flow style.
  static const bool flow = true;
};

/// Serializable representation of a live-in register for a machine function.
struct MachineFunctionLiveIn {
  /// Physical register that is live into the function.
  StringValue Register;
  /// Optional virtual register corresponding to \c Register.
  StringValue VirtualRegister;

  /// Return true if this equals \p Other.
  /// \param Other Live-in to compare against.
  /// \returns True if the live-ins are equal.
  bool operator==(const MachineFunctionLiveIn &Other) const {
    return Register == Other.Register &&
           VirtualRegister == Other.VirtualRegister;
  }
};

/// YAMLIO mapping traits for \c MachineFunctionLiveIn.
template <> struct MappingTraits<MachineFunctionLiveIn> {
  /// Map machine-function live-in fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param LiveIn Live-in being mapped.
  static void mapping(IO &YamlIO, MachineFunctionLiveIn &LiveIn) {
    YamlIO.mapRequired("reg", LiveIn.Register);
    YamlIO.mapOptional(
        "virtual-reg", LiveIn.VirtualRegister,
        StringValue()); // Don't print the virtual register when it's empty.
  }

  /// When true, emit this mapping in flow style.
  static const bool flow = true;
};

/// Serializable representation of stack object from the MachineFrameInfo class.
///
/// The flags 'isImmutable' and 'isAliased' aren't serialized, as they are
/// determined by the object's type and frame information flags.
/// Dead stack objects aren't serialized.
///
/// The 'isPreallocated' flag is determined by the local offset.
struct MachineStackObject {
  /// Kind of stack object.
  enum ObjectType {
    /// Ordinary stack object.
    DefaultType,
    /// Spill-slot stack object.
    SpillSlot,
    /// Variable-sized stack object.
    VariableSized
  };
  /// Stack object identifier.
  UnsignedValue ID;
  /// Optional object name.
  StringValue Name;
  // TODO: Serialize unnamed LLVM alloca reference.
  /// Object kind.
  ObjectType Type = DefaultType;
  /// Offset from the stack pointer or frame pointer.
  int64_t Offset = 0;
  /// Object size in bytes (omitted for variable-sized objects).
  uint64_t Size = 0;
  /// Required alignment, if known.
  MaybeAlign Alignment = std::nullopt;
  /// Target-specific stack ID.
  TargetStackID::Value StackID;
  /// Callee-saved register associated with this object, if any.
  StringValue CalleeSavedRegister;
  /// Whether the callee-saved register is restored.
  bool CalleeSavedRestored = true;
  /// Local frame offset when the object is preallocated.
  std::optional<int64_t> LocalOffset;
  /// Debug variable associated with this object, if any.
  StringValue DebugVar;
  /// Debug expression associated with this object, if any.
  StringValue DebugExpr;
  /// Debug location associated with this object, if any.
  StringValue DebugLoc;

  /// Return true if this equals \p Other.
  /// \param Other Stack object to compare against.
  /// \returns True if the stack objects are equal.
  bool operator==(const MachineStackObject &Other) const {
    return ID == Other.ID && Name == Other.Name && Type == Other.Type &&
           Offset == Other.Offset && Size == Other.Size &&
           Alignment == Other.Alignment &&
           StackID == Other.StackID &&
           CalleeSavedRegister == Other.CalleeSavedRegister &&
           CalleeSavedRestored == Other.CalleeSavedRestored &&
           LocalOffset == Other.LocalOffset && DebugVar == Other.DebugVar &&
           DebugExpr == Other.DebugExpr && DebugLoc == Other.DebugLoc;
  }
};

/// YAMLIO scalar enumeration traits for \c MachineStackObject::ObjectType.
template <> struct ScalarEnumerationTraits<MachineStackObject::ObjectType> {
  /// Map machine stack object type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Type Object type being mapped.
  static void enumeration(yaml::IO &IO, MachineStackObject::ObjectType &Type) {
    IO.enumCase(Type, "default", MachineStackObject::DefaultType);
    IO.enumCase(Type, "spill-slot", MachineStackObject::SpillSlot);
    IO.enumCase(Type, "variable-sized", MachineStackObject::VariableSized);
  }
};

/// YAMLIO mapping traits for \c MachineStackObject.
template <> struct MappingTraits<MachineStackObject> {
  /// Map machine stack object fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param Object Stack object being mapped.
  static void mapping(yaml::IO &YamlIO, MachineStackObject &Object) {
    YamlIO.mapRequired("id", Object.ID);
    YamlIO.mapOptional("name", Object.Name,
                       StringValue()); // Don't print out an empty name.
    YamlIO.mapOptional(
        "type", Object.Type,
        MachineStackObject::DefaultType); // Don't print the default type.
    YamlIO.mapOptional("offset", Object.Offset, (int64_t)0);
    if (Object.Type != MachineStackObject::VariableSized)
      YamlIO.mapRequired("size", Object.Size);
    YamlIO.mapOptional("alignment", Object.Alignment, std::nullopt);
    YamlIO.mapOptional("stack-id", Object.StackID, TargetStackID::Default);
    YamlIO.mapOptional("callee-saved-register", Object.CalleeSavedRegister,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("callee-saved-restored", Object.CalleeSavedRestored,
                       true);
    YamlIO.mapOptional("local-offset", Object.LocalOffset,
                       std::optional<int64_t>());
    YamlIO.mapOptional("debug-info-variable", Object.DebugVar,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("debug-info-expression", Object.DebugExpr,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("debug-info-location", Object.DebugLoc,
                       StringValue()); // Don't print it out when it's empty.
  }

  /// When true, emit this mapping in flow style.
  static const bool flow = true;
};

/// Serializable representation of the MCRegister variant of
/// MachineFunction::VariableDbgInfo.
struct EntryValueObject {
  /// Register holding the entry value.
  StringValue EntryValueRegister;
  /// Debug variable associated with the entry value.
  StringValue DebugVar;
  /// Debug expression associated with the entry value.
  StringValue DebugExpr;
  /// Debug location associated with the entry value.
  StringValue DebugLoc;
  /// Return true if this equals \p Other.
  /// \param Other Entry-value object to compare against.
  /// \returns True if the entry-value objects are equal.
  bool operator==(const EntryValueObject &Other) const {
    return EntryValueRegister == Other.EntryValueRegister &&
           DebugVar == Other.DebugVar && DebugExpr == Other.DebugExpr &&
           DebugLoc == Other.DebugLoc;
  }
};

/// YAMLIO mapping traits for \c EntryValueObject.
template <> struct MappingTraits<EntryValueObject> {
  /// Map entry-value object fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param Object Entry-value object being mapped.
  static void mapping(yaml::IO &YamlIO, EntryValueObject &Object) {
    YamlIO.mapRequired("entry-value-register", Object.EntryValueRegister);
    YamlIO.mapRequired("debug-info-variable", Object.DebugVar);
    YamlIO.mapRequired("debug-info-expression", Object.DebugExpr);
    YamlIO.mapRequired("debug-info-location", Object.DebugLoc);
  }
  /// When true, emit this mapping in flow style.
  static const bool flow = true;
};

/// Serializable representation of the fixed stack object from the
/// MachineFrameInfo class.
struct FixedMachineStackObject {
  /// Kind of fixed stack object.
  enum ObjectType {
    /// Ordinary fixed stack object.
    DefaultType,
    /// Spill-slot fixed stack object.
    SpillSlot
  };
  /// Fixed stack object identifier.
  UnsignedValue ID;
  /// Object kind.
  ObjectType Type = DefaultType;
  /// Offset from the stack pointer or frame pointer.
  int64_t Offset = 0;
  /// Object size in bytes.
  uint64_t Size = 0;
  /// Required alignment, if known.
  MaybeAlign Alignment = std::nullopt;
  /// Target-specific stack ID.
  TargetStackID::Value StackID;
  /// Whether the object is immutable.
  bool IsImmutable = false;
  /// Whether the object is aliased.
  bool IsAliased = false;
  /// Callee-saved register associated with this object, if any.
  StringValue CalleeSavedRegister;
  /// Whether the callee-saved register is restored.
  bool CalleeSavedRestored = true;
  /// Debug variable associated with this object, if any.
  StringValue DebugVar;
  /// Debug expression associated with this object, if any.
  StringValue DebugExpr;
  /// Debug location associated with this object, if any.
  StringValue DebugLoc;

  /// Return true if this equals \p Other.
  /// \param Other Fixed stack object to compare against.
  /// \returns True if the fixed stack objects are equal.
  bool operator==(const FixedMachineStackObject &Other) const {
    return ID == Other.ID && Type == Other.Type && Offset == Other.Offset &&
           Size == Other.Size && Alignment == Other.Alignment &&
           StackID == Other.StackID &&
           IsImmutable == Other.IsImmutable && IsAliased == Other.IsAliased &&
           CalleeSavedRegister == Other.CalleeSavedRegister &&
           CalleeSavedRestored == Other.CalleeSavedRestored &&
           DebugVar == Other.DebugVar && DebugExpr == Other.DebugExpr
           && DebugLoc == Other.DebugLoc;
  }
};

/// YAMLIO scalar enumeration traits for \c FixedMachineStackObject::ObjectType.
template <>
struct ScalarEnumerationTraits<FixedMachineStackObject::ObjectType> {
  /// Map fixed machine stack object type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Type Object type being mapped.
  static void enumeration(yaml::IO &IO,
                          FixedMachineStackObject::ObjectType &Type) {
    IO.enumCase(Type, "default", FixedMachineStackObject::DefaultType);
    IO.enumCase(Type, "spill-slot", FixedMachineStackObject::SpillSlot);
  }
};

/// YAMLIO scalar enumeration traits for \c TargetStackID::Value.
template <>
struct ScalarEnumerationTraits<TargetStackID::Value> {
  /// Map target stack ID enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ID Stack ID being mapped.
  static void enumeration(yaml::IO &IO, TargetStackID::Value &ID) {
    IO.enumCase(ID, "default", TargetStackID::Default);
    IO.enumCase(ID, "sgpr-spill", TargetStackID::SGPRSpill);
    IO.enumCase(ID, "scalable-vector", TargetStackID::ScalableVector);
    IO.enumCase(ID, "scalable-predicate-vector",
                TargetStackID::ScalablePredicateVector);
    IO.enumCase(ID, "wasm-local", TargetStackID::WasmLocal);
    IO.enumCase(ID, "avr-align", TargetStackID::AvrAlign);
    IO.enumCase(ID, "noalloc", TargetStackID::NoAlloc);
  }
};

/// YAMLIO mapping traits for \c FixedMachineStackObject.
template <> struct MappingTraits<FixedMachineStackObject> {
  /// Map fixed machine stack object fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param Object Fixed stack object being mapped.
  static void mapping(yaml::IO &YamlIO, FixedMachineStackObject &Object) {
    YamlIO.mapRequired("id", Object.ID);
    YamlIO.mapOptional(
        "type", Object.Type,
        FixedMachineStackObject::DefaultType); // Don't print the default type.
    YamlIO.mapOptional("offset", Object.Offset, (int64_t)0);
    YamlIO.mapOptional("size", Object.Size, (uint64_t)0);
    YamlIO.mapOptional("alignment", Object.Alignment, std::nullopt);
    YamlIO.mapOptional("stack-id", Object.StackID, TargetStackID::Default);
    if (Object.Type != FixedMachineStackObject::SpillSlot) {
      YamlIO.mapOptional("isImmutable", Object.IsImmutable, false);
      YamlIO.mapOptional("isAliased", Object.IsAliased, false);
    }
    YamlIO.mapOptional("callee-saved-register", Object.CalleeSavedRegister,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("callee-saved-restored", Object.CalleeSavedRestored,
                     true);
    YamlIO.mapOptional("debug-info-variable", Object.DebugVar,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("debug-info-expression", Object.DebugExpr,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("debug-info-location", Object.DebugLoc,
                       StringValue()); // Don't print it out when it's empty.
  }

  /// When true, emit this mapping in flow style.
  static const bool flow = true;
};

/// A serializaable representation of a reference to a stack object or fixed
/// stack object.
struct FrameIndex {
  /// Printed frame index (always non-negative, including for fixed objects).
  ///
  /// To obtain the real index, MachineFrameInfo::getObjectIndexBegin has to be
  /// added.
  int FI;
  /// Whether this refers to a fixed stack object.
  bool IsFixed;
  /// Source range of the scalar in the input YAML, if available.
  SMRange SourceRange;

  /// Construct an uninitialized frame index reference.
  FrameIndex() = default;
  /// Construct from machine frame index \p FI using frame info \p MFI.
  /// \param FI Machine frame index (may be negative for fixed objects).
  /// \param MFI Frame info used to normalize the printed index.
  LLVM_ABI FrameIndex(int FI, const llvm::MachineFrameInfo &MFI);

  /// Convert this printed reference back to a machine frame index.
  /// \param MFI Frame info used to recover the real index.
  /// \returns The machine frame index, or an error on failure.
  LLVM_ABI Expected<int> getFI(const llvm::MachineFrameInfo &MFI) const;
};

/// YAMLIO scalar traits for \c FrameIndex.
template <> struct ScalarTraits<FrameIndex> {
  /// Write \p FI to \p OS as \c %stack.N or \c %fixed-stack.N.
  /// \param FI Frame index reference to write.
  /// \param Ctx Unused client context.
  /// \param OS Output stream.
  static void output(const FrameIndex &FI, void *Ctx, raw_ostream &OS) {
    (void)Ctx;
    MachineOperand::printStackObjectReference(OS, FI.FI, FI.IsFixed, "");
  }

  /// Parse \p Scalar into \p FI.
  /// \param Scalar YAML scalar text.
  /// \param Ctx YAML input context used to fetch the source range.
  /// \param FI Destination frame index reference.
  /// \returns Empty on success; otherwise an error string.
  static StringRef input(StringRef Scalar, void *Ctx, FrameIndex &FI) {
    FI.IsFixed = false;
    StringRef Num;
    if (Scalar.starts_with("%stack.")) {
      Num = Scalar.substr(7);
    } else if (Scalar.starts_with("%fixed-stack.")) {
      Num = Scalar.substr(13);
      FI.IsFixed = true;
    } else {
      return "Invalid frame index, needs to start with %stack. or "
             "%fixed-stack.";
    }
    if (Num.consumeInteger(10, FI.FI))
      return "Invalid frame index, not a valid number";

    if (const auto *Node =
            reinterpret_cast<yaml::Input *>(Ctx)->getCurrentNode())
      FI.SourceRange = Node->getSourceRange();
    return StringRef();
  }

  /// Decide whether \p S needs quoting in YAML output.
  /// \param S Scalar text to inspect.
  /// \returns The quoting type required for \p S.
  static QuotingType mustQuote(StringRef S) { return needsQuotes(S); }
};

/// Identifies call instruction location in machine function.
struct MachineInstrLoc {
  /// Basic-block number containing the instruction.
  unsigned BlockNum;
  /// Instruction offset within the basic block.
  unsigned Offset;

  /// Return true if this equals \p Other.
  /// \param Other Location to compare against.
  /// \returns True if the instruction locations are equal.
  bool operator==(const MachineInstrLoc &Other) const {
    return BlockNum == Other.BlockNum && Offset == Other.Offset;
  }
};

/// Serializable representation of CallSiteInfo.
struct CallSiteInfo {
  /// Representation of call argument and register which is used to
  /// transfer it.
  struct ArgRegPair {
    /// Register used to forward the argument.
    StringValue Reg;
    /// Zero-based argument number.
    uint16_t ArgNo;

    /// Return true if this equals \p Other.
    /// \param Other Argument/register pair to compare against.
    /// \returns True if the argument/register pairs are equal.
    bool operator==(const ArgRegPair &Other) const {
      return Reg == Other.Reg && ArgNo == Other.ArgNo;
    }
  };

  /// Location of the call instruction.
  MachineInstrLoc CallLocation;
  /// Argument-forwarding register pairs for the call.
  std::vector<ArgRegPair> ArgForwardingRegs;
  /// Numeric callee type identifiers for the callgraph section.
  std::vector<uint64_t> CalleeTypeIds;

  /// Return true if this equals \p Other by call location.
  /// \param Other Call-site info to compare against.
  /// \returns True if the call locations are equal.
  bool operator==(const CallSiteInfo &Other) const {
    return CallLocation.BlockNum == Other.CallLocation.BlockNum &&
           CallLocation.Offset == Other.CallLocation.Offset;
  }
};

/// YAMLIO mapping traits for \c CallSiteInfo::ArgRegPair.
template <> struct MappingTraits<CallSiteInfo::ArgRegPair> {
  /// Map argument/register pair fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param ArgReg Argument/register pair being mapped.
  static void mapping(IO &YamlIO, CallSiteInfo::ArgRegPair &ArgReg) {
    YamlIO.mapRequired("arg", ArgReg.ArgNo);
    YamlIO.mapRequired("reg", ArgReg.Reg);
  }

  /// When true, emit this mapping in flow style.
  static const bool flow = true;
};

/// Sequences of \c CallSiteInfo::ArgRegPair use block formatting.
template <> struct SequenceElementTraits<CallSiteInfo::ArgRegPair> {
  /// Emit sequences of CallSiteInfo::ArgRegPair in block style.
  static const bool flow = false;
};

/// YAMLIO mapping traits for \c CallSiteInfo.
template <> struct MappingTraits<CallSiteInfo> {
  /// Map call-site info fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param CSInfo Call-site info being mapped.
  static void mapping(IO &YamlIO, CallSiteInfo &CSInfo) {
    YamlIO.mapRequired("bb", CSInfo.CallLocation.BlockNum);
    YamlIO.mapRequired("offset", CSInfo.CallLocation.Offset);
    YamlIO.mapOptional("fwdArgRegs", CSInfo.ArgForwardingRegs,
                       std::vector<CallSiteInfo::ArgRegPair>());
    YamlIO.mapOptional("calleeTypeIds", CSInfo.CalleeTypeIds);
  }

  /// When true, emit this mapping in flow style.
  static const bool flow = true;
};

/// Serializable representation of debug value substitutions.
struct DebugValueSubstitution {
  /// Source instruction number.
  unsigned SrcInst;
  /// Source operand number.
  unsigned SrcOp;
  /// Destination instruction number.
  unsigned DstInst;
  /// Destination operand number.
  unsigned DstOp;
  /// Subregister index for the substitution.
  unsigned Subreg;

  /// Return true if this equals \p Other (ignoring \c Subreg).
  /// \param Other Substitution to compare against.
  /// \returns True if the substitutions match (ignoring \c Subreg).
  bool operator==(const DebugValueSubstitution &Other) const {
    return std::tie(SrcInst, SrcOp, DstInst, DstOp) ==
           std::tie(Other.SrcInst, Other.SrcOp, Other.DstInst, Other.DstOp);
  }
};

/// YAMLIO mapping traits for \c DebugValueSubstitution.
template <> struct MappingTraits<DebugValueSubstitution> {
  /// Map debug-value substitution fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param Sub Substitution being mapped.
  static void mapping(IO &YamlIO, DebugValueSubstitution &Sub) {
    YamlIO.mapRequired("srcinst", Sub.SrcInst);
    YamlIO.mapRequired("srcop", Sub.SrcOp);
    YamlIO.mapRequired("dstinst", Sub.DstInst);
    YamlIO.mapRequired("dstop", Sub.DstOp);
    YamlIO.mapRequired("subreg", Sub.Subreg);
  }

  /// When true, emit this mapping in flow style.
  static const bool flow = true;
};

/// Sequences of \c DebugValueSubstitution use block formatting.
template <> struct SequenceElementTraits<DebugValueSubstitution> {
  /// Emit sequences of DebugValueSubstitution in block style.
  static const bool flow = false;
};

/// Serializable representation of a machine constant-pool entry.
struct MachineConstantPoolValue {
  /// Constant-pool entry identifier.
  UnsignedValue ID;
  /// Printed constant value.
  StringValue Value;
  /// Required alignment, if known.
  MaybeAlign Alignment = std::nullopt;
  /// Whether the constant is target-specific.
  bool IsTargetSpecific = false;

  /// Return true if this equals \p Other.
  /// \param Other Constant-pool value to compare against.
  /// \returns True if the constant-pool values are equal.
  bool operator==(const MachineConstantPoolValue &Other) const {
    return ID == Other.ID && Value == Other.Value &&
           Alignment == Other.Alignment &&
           IsTargetSpecific == Other.IsTargetSpecific;
  }
};

/// YAMLIO mapping traits for \c MachineConstantPoolValue.
template <> struct MappingTraits<MachineConstantPoolValue> {
  /// Map machine constant-pool value fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param Constant Constant-pool value being mapped.
  static void mapping(IO &YamlIO, MachineConstantPoolValue &Constant) {
    YamlIO.mapRequired("id", Constant.ID);
    YamlIO.mapOptional("value", Constant.Value, StringValue());
    YamlIO.mapOptional("alignment", Constant.Alignment, std::nullopt);
    YamlIO.mapOptional("isTargetSpecific", Constant.IsTargetSpecific, false);
  }
};

/// Serializable representation of a machine jump table.
struct MachineJumpTable {
  /// One jump-table entry and its successor block labels.
  struct Entry {
    /// Jump-table entry identifier.
    UnsignedValue ID;
    /// Successor basic-block labels for this entry.
    std::vector<FlowStringValue> Blocks;

    /// Return true if this equals \p Other.
    /// \param Other Entry to compare against.
    /// \returns True if the jump-table entries are equal.
    bool operator==(const Entry &Other) const {
      return ID == Other.ID && Blocks == Other.Blocks;
    }
  };

  /// Encoding kind for jump-table entries.
  MachineJumpTableInfo::JTEntryKind Kind = MachineJumpTableInfo::EK_Custom32;
  /// Jump-table entries.
  std::vector<Entry> Entries;

  /// Return true if this equals \p Other.
  /// \param Other Jump table to compare against.
  /// \returns True if the jump tables are equal.
  bool operator==(const MachineJumpTable &Other) const {
    return Kind == Other.Kind && Entries == Other.Entries;
  }
};

/// YAMLIO mapping traits for \c MachineJumpTable::Entry.
template <> struct MappingTraits<MachineJumpTable::Entry> {
  /// Map jump-table entry fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param Entry Jump-table entry being mapped.
  static void mapping(IO &YamlIO, MachineJumpTable::Entry &Entry) {
    YamlIO.mapRequired("id", Entry.ID);
    YamlIO.mapOptional("blocks", Entry.Blocks, std::vector<FlowStringValue>());
  }
};

/// Serializable representation of a called global referenced from MIR.
struct CalledGlobal {
  /// Call-site location that references the global.
  MachineInstrLoc CallSite;
  /// Callee global name.
  StringValue Callee;
  /// Target-specific call flags.
  unsigned Flags;

  /// Return true if this equals \p Other.
  /// \param Other Called global to compare against.
  /// \returns True if the called globals are equal.
  bool operator==(const CalledGlobal &Other) const {
    return CallSite == Other.CallSite && Callee == Other.Callee &&
           Flags == Other.Flags;
  }
};

/// YAMLIO mapping traits for \c CalledGlobal.
template <> struct MappingTraits<CalledGlobal> {
  /// Map called-global fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param CG Called global being mapped.
  static void mapping(IO &YamlIO, CalledGlobal &CG) {
    YamlIO.mapRequired("bb", CG.CallSite.BlockNum);
    YamlIO.mapRequired("offset", CG.CallSite.Offset);
    YamlIO.mapRequired("callee", CG.Callee);
    YamlIO.mapRequired("flags", CG.Flags);
  }
};

/// Sequences of \c MachineFunctionLiveIn use block formatting.
template <> struct SequenceElementTraits<MachineFunctionLiveIn> {
  /// Emit sequences of MachineFunctionLiveIn in block style.
  static const bool flow = false;
};
/// Sequences of \c VirtualRegisterDefinition use block formatting.
template <> struct SequenceElementTraits<VirtualRegisterDefinition> {
  /// Emit sequences of VirtualRegisterDefinition in block style.
  static const bool flow = false;
};
/// Sequences of \c MachineStackObject use block formatting.
template <> struct SequenceElementTraits<MachineStackObject> {
  /// Emit sequences of MachineStackObject in block style.
  static const bool flow = false;
};
/// Sequences of \c EntryValueObject use block formatting.
template <> struct SequenceElementTraits<EntryValueObject> {
  /// Emit sequences of EntryValueObject in block style.
  static const bool flow = false;
};
/// Sequences of \c FixedMachineStackObject use block formatting.
template <> struct SequenceElementTraits<FixedMachineStackObject> {
  /// Emit sequences of FixedMachineStackObject in block style.
  static const bool flow = false;
};
/// Sequences of \c CallSiteInfo use block formatting.
template <> struct SequenceElementTraits<CallSiteInfo> {
  /// Emit sequences of CallSiteInfo in block style.
  static const bool flow = false;
};
/// Sequences of \c MachineConstantPoolValue use block formatting.
template <> struct SequenceElementTraits<MachineConstantPoolValue> {
  /// Emit sequences of MachineConstantPoolValue in block style.
  static const bool flow = false;
};
/// Sequences of \c MachineJumpTable::Entry use block formatting.
template <> struct SequenceElementTraits<MachineJumpTable::Entry> {
  /// Emit sequences of MachineJumpTable::Entry in block style.
  static const bool flow = false;
};
/// Sequences of \c CalledGlobal use block formatting.
template <> struct SequenceElementTraits<CalledGlobal> {
  /// Emit sequences of CalledGlobal in block style.
  static const bool flow = false;
};

/// One save or restore point in a \c savePoint / \c restorePoint list.
///
/// One point consists of a machine basic block name and the list of registers
/// saved or restored in that basic block. In MIR it looks like:
/// \code
///  savePoint:
///    - point:           '%bb.1'
///      registers:
///        - '$rbx'
///        - '$r12'
///        ...
///  restorePoint:
///    - point:           '%bb.1'
///      registers:
///        - '$rbx'
///        - '$r12'
/// \endcode
/// If no register is saved or restored in the selected BB, field
/// \c registers is omitted.
struct SaveRestorePointEntry {
  /// Basic block where registers are saved or restored.
  StringValue Point;
  /// Registers saved or restored at \c Point.
  std::vector<StringValue> Registers;

  /// Return true if this equals \p Other.
  /// \param Other Save/restore point to compare against.
  /// \returns True if the save/restore points are equal.
  bool operator==(const SaveRestorePointEntry &Other) const {
    return Point == Other.Point && Registers == Other.Registers;
  }
};

/// YAMLIO mapping traits for \c SaveRestorePointEntry.
template <> struct MappingTraits<SaveRestorePointEntry> {
  /// Map save/restore point fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param Entry Save/restore point being mapped.
  static void mapping(IO &YamlIO, SaveRestorePointEntry &Entry) {
    YamlIO.mapRequired("point", Entry.Point);
    YamlIO.mapOptional("registers", Entry.Registers,
                       std::vector<StringValue>());
  }
};

/// YAMLIO mapping traits for \c MachineJumpTable.
template <> struct MappingTraits<MachineJumpTable> {
  /// Map machine jump-table fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param JT Jump table being mapped.
  static void mapping(IO &YamlIO, MachineJumpTable &JT) {
    YamlIO.mapRequired("kind", JT.Kind);
    YamlIO.mapOptional("entries", JT.Entries,
                       std::vector<MachineJumpTable::Entry>());
  }
};

/// Sequences of \c SaveRestorePointEntry use block formatting.
template <> struct SequenceElementTraits<SaveRestorePointEntry> {
  /// Emit sequences of SaveRestorePointEntry in block style.
  static const bool flow = false;
};

/// Serializable representation of MachineFrameInfo.
///
/// Doesn't serialize attributes like 'StackAlignment', 'IsStackRealignable' and
/// 'RealignOption' as they are determined by the target and LLVM function
/// attributes.
/// It also doesn't serialize attributes like 'NumFixedObject' and
/// 'HasVarSizedObjects' as they are determined by the frame objects themselves.
struct MachineFrameInfo {
  /// Whether the frame address is taken.
  bool IsFrameAddressTaken = false;
  /// Whether the return address is taken.
  bool IsReturnAddressTaken = false;
  /// Whether the function has a stack map.
  bool HasStackMap = false;
  /// Whether the function has a patch point.
  bool HasPatchPoint = false;
  /// Total stack size in bytes.
  uint64_t StackSize = 0;
  /// Offset adjustment applied to the stack pointer.
  int OffsetAdjustment = 0;
  /// Maximum alignment of all stack objects.
  unsigned MaxAlignment = 0;
  /// Whether the function adjusts the stack pointer.
  bool AdjustsStack = false;
  /// Whether the function contains calls.
  bool HasCalls = false;
  /// Frame-pointer usage policy.
  FramePointerKind FramePointerPolicy = FramePointerKind::None;
  /// Stack protector slot reference, if any.
  StringValue StackProtector;
  /// Function context slot reference, if any.
  StringValue FunctionContext;
  unsigned MaxCallFrameSize = ~0u; ///< ~0u means: not computed yet.
  /// CodeView bytes occupied by callee-saved registers.
  unsigned CVBytesOfCalleeSavedRegisters = 0;
  /// Whether the function has an opaque SP adjustment.
  bool HasOpaqueSPAdjustment = false;
  /// Whether the function calls \c va_start.
  bool HasVAStart = false;
  /// Whether a vararg function contains a musttail call.
  bool HasMustTailInVarArgFunc = false;
  /// Whether the function has a tail call.
  bool HasTailCall = false;
  /// Whether callee-saved register info is valid.
  bool IsCalleeSavedInfoValid = false;
  /// Size of the local frame area in bytes.
  unsigned LocalFrameSize = 0;
  /// Save points for callee-saved registers.
  std::vector<SaveRestorePointEntry> SavePoints;
  /// Restore points for callee-saved registers.
  std::vector<SaveRestorePointEntry> RestorePoints;

  /// Return true if this equals \p Other.
  /// \param Other Frame info to compare against.
  /// \returns True if the frame infos are equal.
  bool operator==(const MachineFrameInfo &Other) const {
    return IsFrameAddressTaken == Other.IsFrameAddressTaken &&
           IsReturnAddressTaken == Other.IsReturnAddressTaken &&
           HasStackMap == Other.HasStackMap &&
           HasPatchPoint == Other.HasPatchPoint &&
           StackSize == Other.StackSize &&
           OffsetAdjustment == Other.OffsetAdjustment &&
           MaxAlignment == Other.MaxAlignment &&
           AdjustsStack == Other.AdjustsStack && HasCalls == Other.HasCalls &&
           FramePointerPolicy == Other.FramePointerPolicy &&
           StackProtector == Other.StackProtector &&
           FunctionContext == Other.FunctionContext &&
           MaxCallFrameSize == Other.MaxCallFrameSize &&
           CVBytesOfCalleeSavedRegisters ==
               Other.CVBytesOfCalleeSavedRegisters &&
           HasOpaqueSPAdjustment == Other.HasOpaqueSPAdjustment &&
           HasVAStart == Other.HasVAStart &&
           HasMustTailInVarArgFunc == Other.HasMustTailInVarArgFunc &&
           HasTailCall == Other.HasTailCall &&
           LocalFrameSize == Other.LocalFrameSize &&
           SavePoints == Other.SavePoints &&
           RestorePoints == Other.RestorePoints &&
           IsCalleeSavedInfoValid == Other.IsCalleeSavedInfoValid;
  }
};

/// YAMLIO mapping traits for \c MachineFrameInfo.
template <> struct MappingTraits<MachineFrameInfo> {
  /// Map machine frame info fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param MFI Frame info being mapped.
  static void mapping(IO &YamlIO, MachineFrameInfo &MFI) {
    YamlIO.mapOptional("isFrameAddressTaken", MFI.IsFrameAddressTaken, false);
    YamlIO.mapOptional("isReturnAddressTaken", MFI.IsReturnAddressTaken, false);
    YamlIO.mapOptional("hasStackMap", MFI.HasStackMap, false);
    YamlIO.mapOptional("hasPatchPoint", MFI.HasPatchPoint, false);
    YamlIO.mapOptional("stackSize", MFI.StackSize, (uint64_t)0);
    YamlIO.mapOptional("offsetAdjustment", MFI.OffsetAdjustment, (int)0);
    YamlIO.mapOptional("maxAlignment", MFI.MaxAlignment, (unsigned)0);
    YamlIO.mapOptional("adjustsStack", MFI.AdjustsStack, false);
    YamlIO.mapOptional("hasCalls", MFI.HasCalls, false);
    YamlIO.mapOptional("framePointerPolicy", MFI.FramePointerPolicy);
    YamlIO.mapOptional("stackProtector", MFI.StackProtector,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("functionContext", MFI.FunctionContext,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("maxCallFrameSize", MFI.MaxCallFrameSize, (unsigned)~0);
    YamlIO.mapOptional("cvBytesOfCalleeSavedRegisters",
                       MFI.CVBytesOfCalleeSavedRegisters, 0U);
    YamlIO.mapOptional("hasOpaqueSPAdjustment", MFI.HasOpaqueSPAdjustment,
                       false);
    YamlIO.mapOptional("hasVAStart", MFI.HasVAStart, false);
    YamlIO.mapOptional("hasMustTailInVarArgFunc", MFI.HasMustTailInVarArgFunc,
                       false);
    YamlIO.mapOptional("hasTailCall", MFI.HasTailCall, false);
    YamlIO.mapOptional("isCalleeSavedInfoValid", MFI.IsCalleeSavedInfoValid,
                       false);
    YamlIO.mapOptional("localFrameSize", MFI.LocalFrameSize, (unsigned)0);
    YamlIO.mapOptional("savePoint", MFI.SavePoints);
    YamlIO.mapOptional("restorePoint", MFI.RestorePoints);
  }
};

/// Targets should override this in a way that mirrors the implementation of
/// llvm::MachineFunctionInfo.
struct MachineFunctionInfo {
  /// Destroy the target-specific machine function info.
  virtual ~MachineFunctionInfo() = default;
  /// Map target-specific machine function info fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  virtual void mappingImpl(IO &YamlIO) {}
};

/// YAMLIO mapping traits for owned \c MachineFunctionInfo.
template <> struct MappingTraits<std::unique_ptr<MachineFunctionInfo>> {
  /// Map owned machine function info through \c mappingImpl when present.
  /// \param YamlIO YAML input/output state.
  /// \param MFI Owned machine function info being mapped.
  static void mapping(IO &YamlIO, std::unique_ptr<MachineFunctionInfo> &MFI) {
    if (MFI)
      MFI->mappingImpl(YamlIO);
  }
};

/// Serializable representation of a machine function in MIR YAML.
struct MachineFunction {
  /// Function name.
  StringRef Name;
  /// Function alignment, if known.
  MaybeAlign Alignment = std::nullopt;
  /// Whether the function exposes \c returns_twice behavior.
  bool ExposesReturnsTwice = false;
  // GISel MachineFunctionProperties.
  /// Whether GlobalISel legalization has completed.
  bool Legalized = false;
  /// Whether GlobalISel register-bank selection has completed.
  bool RegBankSelected = false;
  /// Whether GlobalISel instruction selection has completed.
  bool Selected = false;
  /// Whether GlobalISel failed for this function.
  bool FailedISel = false;
  // Register information
  /// Whether register liveness is tracked.
  bool TracksRegLiveness = false;
  /// Whether the function has Windows CFI.
  bool HasWinCFI = false;

  // Computed properties that should be overridable
  /// Optional override for the NoPHIs property.
  std::optional<bool> NoPHIs;
  /// Optional override for the IsSSA property.
  std::optional<bool> IsSSA;
  /// Optional override for the NoVRegs property.
  std::optional<bool> NoVRegs;
  /// Optional override for the HasFakeUses property.
  std::optional<bool> HasFakeUses;

  /// Whether the function calls EH return helpers.
  bool CallsEHReturn = false;
  /// Whether the function calls unwind-init helpers.
  bool CallsUnwindInit = false;
  /// Whether the function has an EH continuation target.
  bool HasEHContTarget = false;
  /// Whether the function has EH scopes.
  bool HasEHScopes = false;
  /// Whether the function has EH funclets.
  bool HasEHFunclets = false;
  /// Whether this function was created by the machine outliner.
  bool IsOutlined = false;

  /// Whether verification is expected to fail for this function.
  bool FailsVerification = false;
  /// Whether debug user values are tracked.
  bool TracksDebugUserValues = false;
  /// Whether debug instruction references are used.
  bool UseDebugInstrRef = false;
  /// Virtual register definitions.
  std::vector<VirtualRegisterDefinition> VirtualRegisters;
  /// Function live-in registers.
  std::vector<MachineFunctionLiveIn> LiveIns;
  /// Optional explicit callee-saved register list.
  std::optional<std::vector<FlowStringValue>> CalleeSavedRegisters;
  // TODO: Serialize the various register masks.
  // Frame information
  /// Frame information for the function.
  MachineFrameInfo FrameInfo;
  /// Fixed stack objects.
  std::vector<FixedMachineStackObject> FixedStackObjects;
  /// Entry-value debug objects.
  std::vector<EntryValueObject> EntryValueObjects;
  /// Ordinary stack objects.
  std::vector<MachineStackObject> StackObjects;
  /// Constant pool entries.
  std::vector<MachineConstantPoolValue> Constants;
  /// Target-specific machine function info, if any.
  std::unique_ptr<MachineFunctionInfo> MachineFuncInfo;
  /// Call-site information entries.
  std::vector<CallSiteInfo> CallSitesInfo;
  /// Debug value substitutions.
  std::vector<DebugValueSubstitution> DebugValueSubstitutions;
  /// Jump-table information.
  MachineJumpTable JumpTableInfo;
  /// Machine metadata nodes referenced by the function.
  std::vector<StringValue> MachineMetadataNodes;
  /// Called globals referenced from the function.
  std::vector<CalledGlobal> CalledGlobals;
  /// Prefetch target labels.
  std::vector<FlowStringValue> PrefetchTargets;
  /// MIR body as a YAML block scalar.
  BlockStringValue Body;
};

/// YAMLIO mapping traits for \c MachineFunction.
template <> struct MappingTraits<MachineFunction> {
  /// Map machine function fields to and from YAML.
  /// \param YamlIO YAML input/output state.
  /// \param MF Machine function being mapped.
  static void mapping(IO &YamlIO, MachineFunction &MF) {
    YamlIO.mapRequired("name", MF.Name);
    YamlIO.mapOptional("alignment", MF.Alignment, std::nullopt);
    YamlIO.mapOptional("exposesReturnsTwice", MF.ExposesReturnsTwice, false);
    YamlIO.mapOptional("legalized", MF.Legalized, false);
    YamlIO.mapOptional("regBankSelected", MF.RegBankSelected, false);
    YamlIO.mapOptional("selected", MF.Selected, false);
    YamlIO.mapOptional("failedISel", MF.FailedISel, false);
    YamlIO.mapOptional("tracksRegLiveness", MF.TracksRegLiveness, false);
    YamlIO.mapOptional("hasWinCFI", MF.HasWinCFI, false);

    // PHIs must be not be capitalized, since it will clash with the MIR opcode
    // leading to false-positive FileCheck hits with CHECK-NOT
    YamlIO.mapOptional("noPhis", MF.NoPHIs, std::optional<bool>());
    YamlIO.mapOptional("isSSA", MF.IsSSA, std::optional<bool>());
    YamlIO.mapOptional("noVRegs", MF.NoVRegs, std::optional<bool>());
    YamlIO.mapOptional("hasFakeUses", MF.HasFakeUses, std::optional<bool>());

    YamlIO.mapOptional("callsEHReturn", MF.CallsEHReturn, false);
    YamlIO.mapOptional("callsUnwindInit", MF.CallsUnwindInit, false);
    YamlIO.mapOptional("hasEHContTarget", MF.HasEHContTarget, false);
    YamlIO.mapOptional("hasEHScopes", MF.HasEHScopes, false);
    YamlIO.mapOptional("hasEHFunclets", MF.HasEHFunclets, false);
    YamlIO.mapOptional("isOutlined", MF.IsOutlined, false);
    YamlIO.mapOptional("debugInstrRef", MF.UseDebugInstrRef, false);

    YamlIO.mapOptional("failsVerification", MF.FailsVerification, false);
    YamlIO.mapOptional("tracksDebugUserValues", MF.TracksDebugUserValues,
                       false);
    YamlIO.mapOptional("registers", MF.VirtualRegisters,
                       std::vector<VirtualRegisterDefinition>());
    YamlIO.mapOptional("liveins", MF.LiveIns,
                       std::vector<MachineFunctionLiveIn>());
    YamlIO.mapOptional("calleeSavedRegisters", MF.CalleeSavedRegisters,
                       std::optional<std::vector<FlowStringValue>>());
    YamlIO.mapOptional("frameInfo", MF.FrameInfo, MachineFrameInfo());
    YamlIO.mapOptional("fixedStack", MF.FixedStackObjects,
                       std::vector<FixedMachineStackObject>());
    YamlIO.mapOptional("stack", MF.StackObjects,
                       std::vector<MachineStackObject>());
    YamlIO.mapOptional("entry_values", MF.EntryValueObjects,
                       std::vector<EntryValueObject>());
    YamlIO.mapOptional("callSites", MF.CallSitesInfo,
                       std::vector<CallSiteInfo>());
    YamlIO.mapOptional("debugValueSubstitutions", MF.DebugValueSubstitutions,
                       std::vector<DebugValueSubstitution>());
    YamlIO.mapOptional("constants", MF.Constants,
                       std::vector<MachineConstantPoolValue>());
    YamlIO.mapOptional("machineFunctionInfo", MF.MachineFuncInfo);
    if (!YamlIO.outputting() || !MF.JumpTableInfo.Entries.empty())
      YamlIO.mapOptional("jumpTable", MF.JumpTableInfo, MachineJumpTable());
    if (!YamlIO.outputting() || !MF.MachineMetadataNodes.empty())
      YamlIO.mapOptional("machineMetadataNodes", MF.MachineMetadataNodes,
                         std::vector<StringValue>());
    if (!YamlIO.outputting() || !MF.CalledGlobals.empty())
      YamlIO.mapOptional("calledGlobals", MF.CalledGlobals,
                         std::vector<CalledGlobal>());
    if (!YamlIO.outputting() || !MF.PrefetchTargets.empty())
      YamlIO.mapOptional("prefetch-targets", MF.PrefetchTargets,
                         std::vector<FlowStringValue>());

    YamlIO.mapOptional("body", MF.Body, BlockStringValue());
  }
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_CODEGEN_MIRYAMLMAPPING_H
