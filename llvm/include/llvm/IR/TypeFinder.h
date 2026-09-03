//===- llvm/IR/TypeFinder.h - Class to find used struct types ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the TypeFinder class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_TYPEFINDER_H
#define LLVM_IR_TYPEFINDER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Attributes.h"
#include <cstddef>
#include <vector>

namespace llvm {

class MDNode;
class Module;
class StructType;
class Type;
class Value;

/// TypeFinder - Walk over a module, identifying all of the types that are
/// used by the module.
class TypeFinder {
  // To avoid walking constant expressions multiple times and other IR
  // objects, we keep several helper maps.
  DenseSet<const Value*> VisitedConstants;
  DenseSet<const MDNode *> VisitedMetadata;
  DenseSet<AttributeList> VisitedAttributes;
  DenseSet<Type*> VisitedTypes;

  std::vector<StructType*> StructTypes;
  bool OnlyNamed = false;

public:
  /// Construct an empty TypeFinder.
  TypeFinder() = default;

  /// Walk \p M and collect the struct types it uses.
  /// \param M The module to search for used types.
  /// \param onlyNamed If true, only named struct types are retained.
  LLVM_ABI void run(const Module &M, bool onlyNamed);

  /// Reset the finder to an empty state, clearing all visited sets.
  LLVM_ABI void clear();

  /// Mutable iterator over the collected struct types.
  using iterator = std::vector<StructType*>::iterator;

  /// Const iterator over the collected struct types.
  using const_iterator = std::vector<StructType*>::const_iterator;

  /// Return an iterator to the first collected struct type.
  /// \returns An iterator to the first collected struct type.
  iterator begin() { return StructTypes.begin(); }

  /// Return an iterator past the last collected struct type.
  /// \returns An iterator past the last collected struct type.
  iterator end() { return StructTypes.end(); }

  /// Return a const iterator to the first collected struct type.
  /// \returns A const iterator to the first collected struct type.
  const_iterator begin() const { return StructTypes.begin(); }

  /// Return a const iterator past the last collected struct type.
  /// \returns A const iterator past the last collected struct type.
  const_iterator end() const { return StructTypes.end(); }

  /// Return true if no struct types have been collected.
  /// \returns True if no struct types have been collected.
  bool empty() const { return StructTypes.empty(); }

  /// Return the number of collected struct types.
  /// \returns The number of collected struct types.
  size_t size() const { return StructTypes.size(); }

  /// Erase the collected struct types in the half-open range [\p I, \p E).
  /// \param I Iterator to the first element to erase.
  /// \param E Iterator past the last element to erase.
  /// \returns An iterator following the last removed element.
  iterator erase(iterator I, iterator E) { return StructTypes.erase(I, E); }

  /// Return a reference to the collected struct type at \p Idx.
  /// \param Idx Zero-based index into the collected struct types.
  /// \returns A reference to the collected struct type at \p Idx.
  StructType *&operator[](unsigned Idx) { return StructTypes[Idx]; }

  /// Return the set of metadata nodes already visited during the search.
  /// \returns A mutable reference to the set of visited metadata nodes.
  DenseSet<const MDNode *> &getVisitedMetadata() { return VisitedMetadata; }

private:
  /// incorporateType - This method adds the type to the list of used
  /// structures if it's not in there already.
  void incorporateType(Type *Ty);

  /// incorporateValue - This method is used to walk operand lists finding types
  /// hiding in constant expressions and other operands that won't be walked in
  /// other ways.  GlobalValues, basic blocks, instructions, and inst operands
  /// are all explicitly enumerated.
  void incorporateValue(const Value *V);

  /// incorporateMDNode - This method is used to walk the operands of an MDNode
  /// to find types hiding within.
  void incorporateMDNode(const MDNode *V);

  /// Incorporate types referenced by attributes.
  void incorporateAttributes(AttributeList AL);
};

} // end namespace llvm

#endif // LLVM_IR_TYPEFINDER_H
