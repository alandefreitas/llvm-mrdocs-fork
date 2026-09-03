//===- llvm/IR/Comdat.h - Comdat definitions --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// This file contains the declaration of the Comdat class, which represents a
/// single COMDAT in LLVM.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_COMDAT_H
#define LLVM_IR_COMDAT_H

#include "llvm-c/Types.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class GlobalObject;
class raw_ostream;
class StringRef;
template <typename ValueTy> class StringMapEntry;

// This is a Name X SelectionKind pair. The reason for having this be an
// independent object instead of just adding the name and the SelectionKind
// to a GlobalObject is that it is invalid to have two Comdats with the same
// name but different SelectionKind. This structure makes that unrepresentable.
class Comdat {
public:
  /// How the linker resolves duplicate COMDAT groups with the same name.
  enum SelectionKind {
    Any,           ///< The linker may choose any COMDAT.
    ExactMatch,    ///< The data referenced by the COMDAT must be the same.
    Largest,       ///< The linker will choose the largest COMDAT.
    NoDeduplicate, ///< No deduplication is performed.
    SameSize,      ///< The data referenced by the COMDAT must be the same size.
  };

  /// Copy construction is deleted; Comdat is non-copyable.
  /// \param Other The Comdat that would be copied (deleted).
  Comdat(const Comdat &Other) = delete;
  /// Move-construct a Comdat from another.
  /// \param C The Comdat to move from.
  LLVM_ABI Comdat(Comdat &&C);

  /// Return the selection kind used when resolving this COMDAT.
  /// @return The selection kind for this COMDAT.
  SelectionKind getSelectionKind() const { return SK; }
  /// Set the selection kind used when resolving this COMDAT.
  /// \param Val The selection kind to assign.
  void setSelectionKind(SelectionKind Val) { SK = Val; }
  /// Return the name of this COMDAT.
  /// @return The COMDAT name as a string reference.
  LLVM_ABI StringRef getName() const;
  /// Print this COMDAT to \p OS.
  /// \param OS The stream to print to.
  /// \param IsForDebug If true, include extra debug formatting.
  LLVM_ABI void print(raw_ostream &OS, bool IsForDebug = false) const;
  /// Dump this COMDAT to stderr for debugging.
  LLVM_ABI void dump() const;
  /// Return the set of global objects that use this COMDAT.
  /// @return The set of global objects that reference this COMDAT.
  const SmallPtrSetImpl<GlobalObject *> &getUsers() const { return Users; }

private:
  friend class Module;
  friend class GlobalObject;

  Comdat();
  void addUser(GlobalObject *GO);
  void removeUser(GlobalObject *GO);

  // Points to the map in Module.
  StringMapEntry<Comdat> *Name = nullptr;
  SelectionKind SK = Any;
  // Globals using this comdat.
  SmallPtrSet<GlobalObject *, 2> Users;
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Convert an opaque \c LLVMComdatRef to a \c Comdat pointer.
/// \param P Opaque C API comdat reference to unwrap.
/// @return The Comdat pointer corresponding to \p P.
inline Comdat *unwrap(LLVMComdatRef P) {
  return reinterpret_cast<Comdat *>(P);
}

/// Convert a \c Comdat pointer to an opaque \c LLVMComdatRef.
/// \param P Comdat to wrap for the C API.
/// @return An opaque C API comdat reference for \p P.
inline LLVMComdatRef wrap(const Comdat *P) {
  return reinterpret_cast<LLVMComdatRef>(const_cast<Comdat *>(P));
}

/// Print \p C to \p OS using its LLVM assembly syntax.
/// \param OS The stream to print to.
/// \param C The Comdat to print.
/// @return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const Comdat &C) {
  C.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_IR_COMDAT_H
