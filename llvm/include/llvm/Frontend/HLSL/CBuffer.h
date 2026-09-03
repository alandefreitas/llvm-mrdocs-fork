//===- CBuffer.h - HLSL constant buffer handling ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains utilities to work with constant buffers in HLSL.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_HLSL_CBUFFER_H
#define LLVM_FRONTEND_HLSL_CBUFFER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include <optional>

namespace llvm {
class Module;
class GlobalVariable;
class NamedMDNode;

/// HLSL frontend utilities for resources, bindings, and related IR helpers.
namespace hlsl {

/// A global variable member of an HLSL constant buffer and its byte offset.
struct CBufferMember {
  /// Global variable for this cbuffer member.
  GlobalVariable *GV;
  /// Byte offset of this member within the cbuffer layout.
  size_t Offset;

  /// Construct a member from its global and layout offset.
  /// \param GV Global variable representing the member.
  /// \param Offset Byte offset of the member in the cbuffer.
  CBufferMember(GlobalVariable *GV, size_t Offset) : GV(GV), Offset(Offset) {}
};

/// Mapping from an HLSL cbuffer handle to its constituent members.
struct CBufferMapping {
  /// Global variable for the cbuffer handle.
  GlobalVariable *Handle;
  /// Members of this cbuffer with their layout offsets.
  SmallVector<CBufferMember> Members;

  /// Construct a mapping for the given cbuffer handle.
  /// \param Handle Global variable for the cbuffer handle.
  CBufferMapping(GlobalVariable *Handle) : Handle(Handle) {}
};

/// Parsed view of the \c hlsl.cbs named metadata on a module.
class CBufferMetadata {
  NamedMDNode *MD;
  SmallVector<CBufferMapping> Mappings;

  CBufferMetadata(NamedMDNode *MD) : MD(MD) {}

public:
  /// Load cbuffer metadata from \p M, if present.
  ///
  /// \param M Module to read \c hlsl.cbs from.
  /// \param IsPadding Predicate that returns true for padding element types
  ///        that should be skipped when computing member offsets.
  /// \return Parsed metadata, or \c std::nullopt if the module has none.
  LLVM_ABI static std::optional<CBufferMetadata>
  get(Module &M, llvm::function_ref<bool(Type *)> IsPadding);

  /// Iterator over cbuffer mappings.
  using iterator = SmallVector<CBufferMapping>::iterator;
  /// Return an iterator to the first cbuffer mapping.
  /// \return Iterator to the first cbuffer mapping.
  iterator begin() { return Mappings.begin(); }
  /// Return an iterator past the last cbuffer mapping.
  /// \return Iterator past the last cbuffer mapping.
  iterator end() { return Mappings.end(); }

  /// Erase the underlying \c hlsl.cbs named metadata from its module.
  LLVM_ABI void eraseFromModule();
};

} // namespace hlsl
} // namespace llvm

#endif // LLVM_FRONTEND_HLSL_CBUFFER_H
