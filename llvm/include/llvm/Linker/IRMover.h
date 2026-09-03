//===- IRMover.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LINKER_IRMOVER_H
#define LLVM_LINKER_IRMOVER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Compiler.h"
#include <functional>

namespace llvm {
class Error;
class GlobalValue;
class Metadata;
class MDNode;
class NamedMDNode;
class Module;
class StructType;
class TrackingMDRef;
class Type;

/// Moves IR from source modules into a composite destination module.
///
/// Keeps a reference to the composite module and tracks identified struct
/// types and shared metadata across successive \a move() calls.
class IRMover {
  struct StructTypeKeyInfo {
    struct KeyTy {
      ArrayRef<Type *> ETypes;
      bool IsPacked;
      LLVM_ABI KeyTy(ArrayRef<Type *> E, bool P);
      LLVM_ABI KeyTy(const StructType *ST);
      LLVM_ABI bool operator==(const KeyTy &that) const;
      LLVM_ABI bool operator!=(const KeyTy &that) const;
    };
    LLVM_ABI static unsigned getHashValue(const KeyTy &Key);
    LLVM_ABI static unsigned getHashValue(const StructType *ST);
    LLVM_ABI static bool isEqual(const KeyTy &LHS, const StructType *RHS);
    LLVM_ABI static bool isEqual(const StructType *LHS, const StructType *RHS);
  };

  /// Type of the Metadata map in \a ValueToValueMapTy.
  typedef DenseMap<const Metadata *, TrackingMDRef> MDMapT;

public:
  /// Tracks identified struct types in the composite module.
  ///
  /// Separates opaque types from non-opaque types so linking can match
  /// layouts and promote opaque types when a definition is found.
  class IdentifiedStructTypeSet {
    // The set of opaque types is the composite module.
    DenseSet<StructType *> OpaqueStructTypes;

    // The set of identified but non opaque structures in the composite module.
    DenseSet<StructType *, StructTypeKeyInfo> NonOpaqueStructTypes;

  public:
    /// Add a non-opaque identified struct type to the set.
    /// \param Ty Non-opaque struct type to record.
    LLVM_ABI void addNonOpaque(StructType *Ty);
    /// Promote an opaque type to non-opaque after its body is filled in.
    /// \param Ty Struct type that was opaque and is now non-opaque.
    LLVM_ABI void switchToNonOpaque(StructType *Ty);
    /// Add an opaque identified struct type to the set.
    /// \param Ty Opaque struct type to record.
    LLVM_ABI void addOpaque(StructType *Ty);
    /// Find a non-opaque struct type with the given elements and packing.
    /// \param ETypes Element types of the struct layout to look up.
    /// \param IsPacked Whether the struct is packed.
    /// \returns Matching non-opaque struct type, or nullptr if none exists.
    LLVM_ABI StructType *findNonOpaque(ArrayRef<Type *> ETypes, bool IsPacked);
    /// Return whether \p Ty is already tracked as opaque or non-opaque.
    /// \param Ty Struct type to query.
    /// \returns True if \p Ty is already tracked as opaque or non-opaque.
    LLVM_ABI bool hasType(StructType *Ty);
  };

  /// Construct an IRMover that links into composite module \p M.
  /// \param M Destination module that receives moved IR.
  LLVM_ABI IRMover(Module &M);

  /// Callback that requests a global value be added to the link set.
  typedef std::function<void(GlobalValue &)> ValueAdder;
  /// Callback invoked for globals referenced lazily during a move.
  ///
  /// Receives the referenced global and a \a ValueAdder to request that it
  /// be linked.
  using LazyCallback =
      llvm::unique_function<void(GlobalValue &GV, ValueAdder Add)>;

  /// Map from named metadata nodes to the set of MDNodes already linked.
  using NamedMDNodesT =
      DenseMap<const NamedMDNode *, SmallPtrSet<const MDNode *, 8>>;

  /// Move in the provided values in \p ValuesToLink from \p Src.
  ///
  /// \param Src Source module whose values are moved; ownership is taken.
  /// \param ValuesToLink Global values from \p Src to link into the composite.
  /// \param AddLazyFor Callback the IRMover invokes when a global value is
  ///        referenced by one of the ValuesToLink (transitively) but was not
  ///        present in ValuesToLink. The GlobalValue and a ValueAdder callback
  ///        are passed as arguments; the callback is expected to be called if
  ///        the GlobalValue needs to be added to ValuesToLink and linked.
  ///        Pass nullptr if there is no work to be done in such cases.
  /// \param IsPerformingImport True when this IR link is to perform ThinLTO
  ///        function importing from \p Src.
  /// \returns Success, or an error describing why the move failed.
  LLVM_ABI Error move(std::unique_ptr<Module> Src,
                      ArrayRef<GlobalValue *> ValuesToLink,
                      LazyCallback AddLazyFor, bool IsPerformingImport);
  /// Return the composite destination module.
  /// \returns The composite destination module.
  Module &getModule() { return Composite; }

private:
  Module &Composite;
  IdentifiedStructTypeSet IdentifiedStructTypes;
  MDMapT SharedMDs; ///< A Metadata map to use for all calls to \a move().
  NamedMDNodesT NamedMDNodes; ///< Cache for IRMover::linkNamedMDNodes().
};

} // End llvm namespace

#endif
