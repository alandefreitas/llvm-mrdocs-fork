//===- CostAllocator.h - PBQP Cost Allocator --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines classes conforming to the PBQP cost value manager concept.
//
// Cost value managers are memory managers for PBQP cost values (vectors and
// matrices). Since PBQP graphs can grow very large (E.g. hundreds of thousands
// of edges on the largest function in SPEC2006).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PBQP_COSTALLOCATOR_H
#define LLVM_CODEGEN_PBQP_COSTALLOCATOR_H

#include "llvm/ADT/DenseSet.h"
#include <algorithm>
#include <cstdint>
#include <memory>

namespace llvm {
/// Partitioned Boolean Quadratic Programming (PBQP) types and utilities.
namespace PBQP {

/// Pool that stores unique values and returns shared references to them.
template <typename ValueT> class ValuePool {
public:
  /// Shared pointer to a pooled const value.
  using PoolRef = std::shared_ptr<const ValueT>;

private:
  class PoolEntry : public std::enable_shared_from_this<PoolEntry> {
  public:
    template <typename ValueKeyT>
    PoolEntry(ValuePool &Pool, ValueKeyT Value)
        : Pool(Pool), Value(std::move(Value)) {}

    ~PoolEntry() { Pool.removeEntry(this); }

    const ValueT &getValue() const { return Value; }

  private:
    ValuePool &Pool;
    ValueT Value;
  };

  class PoolEntryDSInfo {
  public:
    template <typename ValueKeyT>
    static unsigned getHashValue(const ValueKeyT &C) {
      return hash_value(C);
    }

    static unsigned getHashValue(PoolEntry *P) {
      return getHashValue(P->getValue());
    }

    static unsigned getHashValue(const PoolEntry *P) {
      return getHashValue(P->getValue());
    }

    template <typename ValueKeyT1, typename ValueKeyT2>
    static bool isEqual(const ValueKeyT1 &C1, const ValueKeyT2 &C2) {
      return C1 == C2;
    }

    template <typename ValueKeyT>
    static bool isEqual(const ValueKeyT &C, PoolEntry *P) {
      return isEqual(C, P->getValue());
    }

    static bool isEqual(PoolEntry *P1, PoolEntry *P2) {
      return isEqual(P1->getValue(), P2);
    }
  };

  using EntrySetT = DenseSet<PoolEntry *, PoolEntryDSInfo>;

  EntrySetT EntrySet;

  void removeEntry(PoolEntry *P) { EntrySet.erase(P); }

public:
  /// Return a pooled reference to \p ValueKey, inserting it if absent.
  /// @param ValueKey Value to look up or insert into the pool.
  /// @return Shared reference to the unique pooled copy of \p ValueKey.
  template <typename ValueKeyT> PoolRef getValue(ValueKeyT ValueKey) {
    typename EntrySetT::iterator I = EntrySet.find_as(ValueKey);

    if (I != EntrySet.end())
      return PoolRef((*I)->shared_from_this(), &(*I)->getValue());

    auto P = std::make_shared<PoolEntry>(*this, std::move(ValueKey));
    EntrySet.insert(P.get());
    return PoolRef(P, &P->getValue());
  }
};

/// Cost allocator that pools equal cost vectors and matrices.
template <typename VectorT, typename MatrixT> class PoolCostAllocator {
private:
  using VectorCostPool = ValuePool<VectorT>;
  using MatrixCostPool = ValuePool<MatrixT>;

public:
  /// Cost vector type managed by this allocator.
  using Vector = VectorT;
  /// Cost matrix type managed by this allocator.
  using Matrix = MatrixT;
  /// Shared pointer to a pooled cost vector.
  using VectorPtr = typename VectorCostPool::PoolRef;
  /// Shared pointer to a pooled cost matrix.
  using MatrixPtr = typename MatrixCostPool::PoolRef;

  /// Return a pooled reference to cost vector \p v, inserting it if absent.
  /// @param v Cost vector to look up or insert into the vector pool.
  /// @return Shared reference to the unique pooled copy of \p v.
  template <typename VectorKeyT> VectorPtr getVector(VectorKeyT v) {
    return VectorPool.getValue(std::move(v));
  }

  /// Return a pooled reference to cost matrix \p m, inserting it if absent.
  /// @param m Cost matrix to look up or insert into the matrix pool.
  /// @return Shared reference to the unique pooled copy of \p m.
  template <typename MatrixKeyT> MatrixPtr getMatrix(MatrixKeyT m) {
    return MatrixPool.getValue(std::move(m));
  }

private:
  VectorCostPool VectorPool;
  MatrixCostPool MatrixPool;
};

} // end namespace PBQP
} // end namespace llvm

#endif // LLVM_CODEGEN_PBQP_COSTALLOCATOR_H
