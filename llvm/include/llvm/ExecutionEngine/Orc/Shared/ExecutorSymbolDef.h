//===--------- ExecutorSymbolDef.h - (Addr, Flags) pair ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Represents a defining location for a JIT symbol.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_EXECUTORSYMBOLDEF_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_EXECUTORSYMBOLDEF_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"

namespace llvm {
namespace orc {

/// Represents a defining location for a JIT symbol.
class ExecutorSymbolDef {
public:
  /// Create an ExecutorSymbolDef from the given pointer.
  /// Warning: This should only be used when JITing in-process.
  /// @param Ptr Host pointer to convert.
  /// @param BaseFlags Initial symbol flags; Callable is added for function types.
  /// @param Unwrap Function applied to \p Ptr before taking its address bits.
  /// @return An ExecutorSymbolDef for the unwrapped host pointer.
  template <typename T, typename UnwrapFn = ExecutorAddr::defaultUnwrap<T>>
  static ExecutorSymbolDef fromPtr(T *Ptr,
                                   JITSymbolFlags BaseFlags = JITSymbolFlags(),
                                   UnwrapFn &&Unwrap = UnwrapFn()) {
    auto *UP = Unwrap(Ptr);
    JITSymbolFlags Flags = BaseFlags;
    if (std::is_function_v<T>)
      Flags |= JITSymbolFlags::Callable;
    return ExecutorSymbolDef(ExecutorAddr::fromPtr(UP, ExecutorAddr::rawPtr()),
                             Flags);
  }

  /// Cast this ExecutorSymbolDef to a pointer of the given type.
  /// Warning: This should only be used when JITing in-process.
  /// @param Wrap Function applied to the reconstructed host pointer.
  /// @return A host pointer of type \p T for this symbol.
  template <typename T, typename WrapFn =
                            ExecutorAddr::defaultWrap<std::remove_pointer_t<T>>>
  std::enable_if_t<std::is_pointer<T>::value, T>
  toPtr(WrapFn &&Wrap = WrapFn()) const {
    return Addr.toPtr<T>(std::forward<WrapFn>(Wrap));
  }

  /// Cast this ExecutorSymbolDef to a pointer of the given function type.
  /// Warning: This should only be used when JITing in-process.
  /// @param Wrap Function applied to the reconstructed host function pointer.
  /// @return A host function pointer of type \p T * for this symbol.
  template <typename T, typename WrapFn = ExecutorAddr::defaultWrap<T>>
  std::enable_if_t<std::is_function<T>::value, T *>
  toPtr(WrapFn &&Wrap = WrapFn()) const {
    return Addr.toPtr<T>(std::forward<WrapFn>(Wrap));
  }

  /// Construct a null ExecutorSymbolDef.
  ExecutorSymbolDef() = default;
  /// Create an ExecutorSymbolDef from the given address and flags.
  /// @param Addr Defining address of the symbol.
  /// @param Flags Symbol flags for this definition.
  ExecutorSymbolDef(ExecutorAddr Addr, JITSymbolFlags Flags)
    : Addr(Addr), Flags(Flags) {}

  /// Return the defining address of this symbol.
  /// @return The defining address of this symbol.
  const ExecutorAddr &getAddress() const { return Addr; }

  /// Return the flags for this symbol definition.
  /// @return The flags for this symbol definition.
  const JITSymbolFlags &getFlags() const { return Flags; }

  /// Set the flags for this symbol definition to \p Flags.
  /// @param Flags New symbol flags.
  void setFlags(JITSymbolFlags Flags) { this->Flags = Flags; }

  /// Return true if \p LHS and \p RHS have the same address and flags.
  /// @param LHS Left-hand symbol definition.
  /// @param RHS Right-hand symbol definition.
  /// @return True if \p LHS and \p RHS have the same address and flags.
  friend bool operator==(const ExecutorSymbolDef &LHS,
                         const ExecutorSymbolDef &RHS) {
    return LHS.getAddress() == RHS.getAddress() &&
           LHS.getFlags() == RHS.getFlags();
  }

  /// Return true if \p LHS and \p RHS differ in address or flags.
  /// @param LHS Left-hand symbol definition.
  /// @param RHS Right-hand symbol definition.
  /// @return True if \p LHS and \p RHS differ in address or flags.
  friend bool operator!=(const ExecutorSymbolDef &LHS,
                         const ExecutorSymbolDef &RHS) {
    return !(LHS == RHS);
  }

private:
  ExecutorAddr Addr;
  JITSymbolFlags Flags;
};

namespace shared {

/// SPS tag type for JITSymbolFlags.
using SPSJITSymbolFlags =
    SPSTuple<JITSymbolFlags::UnderlyingType, JITSymbolFlags::TargetFlagsType>;

/// SPS serializatior for JITSymbolFlags.
template <> class SPSSerializationTraits<SPSJITSymbolFlags, JITSymbolFlags> {
  using FlagsArgList = SPSJITSymbolFlags::AsArgList;

public:
  /// Return the serialized size of \p F.
  /// @param F Flags to measure.
  /// @return Number of bytes needed to serialize \p F.
  static size_t size(const JITSymbolFlags &F) {
    return FlagsArgList::size(F.getRawFlagsValue(), F.getTargetFlags());
  }

  /// Serialize \p F into \p BOB.
  /// @param BOB Output buffer.
  /// @param F Flags to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &BOB, const JITSymbolFlags &F) {
    return FlagsArgList::serialize(BOB, F.getRawFlagsValue(),
                                   F.getTargetFlags());
  }

  /// Deserialize JITSymbolFlags from \p BIB into \p F.
  /// @param BIB Input buffer.
  /// @param F Destination flags.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &BIB, JITSymbolFlags &F) {
    JITSymbolFlags::UnderlyingType RawFlags;
    JITSymbolFlags::TargetFlagsType TargetFlags;
    if (!FlagsArgList::deserialize(BIB, RawFlags, TargetFlags))
      return false;
    F = JITSymbolFlags{static_cast<JITSymbolFlags::FlagNames>(RawFlags),
                       TargetFlags};
    return true;
  }
};

/// SPS tag type for ExecutorSymbolDef.
using SPSExecutorSymbolDef = SPSTuple<SPSExecutorAddr, SPSJITSymbolFlags>;

/// SPS serializatior for ExecutorSymbolDef.
template <>
class SPSSerializationTraits<SPSExecutorSymbolDef, ExecutorSymbolDef> {
  using DefArgList = SPSExecutorSymbolDef::AsArgList;

public:
  /// Return the serialized size of \p ESD.
  /// @param ESD Symbol definition to measure.
  /// @return Number of bytes needed to serialize \p ESD.
  static size_t size(const ExecutorSymbolDef &ESD) {
    return DefArgList::size(ESD.getAddress(), ESD.getFlags());
  }

  /// Serialize \p ESD into \p BOB.
  /// @param BOB Output buffer.
  /// @param ESD Symbol definition to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &BOB, const ExecutorSymbolDef &ESD) {
    return DefArgList::serialize(BOB, ESD.getAddress(), ESD.getFlags());
  }

  /// Deserialize an ExecutorSymbolDef from \p BIB into \p ESD.
  /// @param BIB Input buffer.
  /// @param ESD Destination symbol definition.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &BIB, ExecutorSymbolDef &ESD) {
    ExecutorAddr Addr;
    JITSymbolFlags Flags;
    if (!DefArgList::deserialize(BIB, Addr, Flags))
      return false;
    ESD = ExecutorSymbolDef{Addr, Flags};
    return true;
  }
};

} // End namespace shared.
} // End namespace orc.
} // End namespace llvm.

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_EXECUTORSYMBOLDEF_H
