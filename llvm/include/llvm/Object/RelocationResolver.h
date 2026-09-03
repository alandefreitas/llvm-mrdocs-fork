//===- RelocVisitor.h - Visitor for object file relocations -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a wrapper around all the different types of relocations
// in different file formats, such that a client can handle them in a unified
// manner by only implementing a minimal number of functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_RELOCATIONRESOLVER_H
#define LLVM_OBJECT_RELOCATIONRESOLVER_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <utility>

namespace llvm {
namespace object {

class ObjectFile;
class RelocationRef;

/// Predicate that returns true if relocation type is supported.
using SupportsRelocation = bool (*)(uint64_t);
/// Callback that resolves a relocation given type, offset, symbol, and addend.
using RelocationResolver = uint64_t (*)(uint64_t Type, uint64_t Offset,
                                        uint64_t S, uint64_t LocData,
                                        int64_t Addend);

/// Returns format-specific support and resolve callbacks for \p Obj's relocations.
///
/// \param Obj Object file whose relocation format and architecture select the
/// callbacks.
/// \return A pair of the support predicate and the resolve callback for \p Obj.
LLVM_ABI std::pair<SupportsRelocation, RelocationResolver>
getRelocationResolver(const ObjectFile &Obj);

/// Apply \p Resolver to relocation \p R with symbol value \p S and local data.
///
/// \param Resolver Callback that computes the relocated value.
/// \param R Relocation to resolve.
/// \param S Symbol value used when applying the relocation.
/// \param LocData Value already present at the relocation site.
/// \return The relocated value computed by \p Resolver.
LLVM_ABI uint64_t resolveRelocation(RelocationResolver Resolver,
                                    const RelocationRef &R, uint64_t S,
                                    uint64_t LocData);

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_RELOCATIONRESOLVER_H
