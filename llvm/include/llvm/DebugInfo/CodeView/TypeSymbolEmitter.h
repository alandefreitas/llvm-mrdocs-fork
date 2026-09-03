//===- TypeSymbolEmitter.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPESYMBOLEMITTER_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPESYMBOLEMITTER_H

namespace llvm {
class StringRef;

namespace codeview {
class TypeIndex;

/// Abstract interface for emitting CodeView user-defined type symbols.
class TypeSymbolEmitter {
private:
  TypeSymbolEmitter(const TypeSymbolEmitter &) = delete;
  TypeSymbolEmitter &operator=(const TypeSymbolEmitter &) = delete;

protected:
  /// Construct a type symbol emitter.
  TypeSymbolEmitter() {}

public:
  /// Destroy the type symbol emitter.
  virtual ~TypeSymbolEmitter() {}

public:
  /// Emit a user-defined type with the given type index and name.
  ///
  /// \param TI Type index of the user-defined type being emitted.
  /// \param Name Display name associated with \p TI.
  virtual void writeUserDefinedType(TypeIndex TI, StringRef Name) = 0;
};
}
}

#endif
