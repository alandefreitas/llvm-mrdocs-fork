//===- llvm/ADT/FoldingSet.h - Uniquing Hash Set ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines a hash set that can be used to remove duplication of nodes
/// in a graph.  This code was originally created by Chris Lattner for use with
/// SelectionDAGCSEMap, but was isolated to provide use across the llvm code
/// set.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_FOLDINGSET_H
#define LLVM_ADT_FOLDINGSET_H

#include "llvm/ADT/EpochTracker.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/xxhash.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace llvm {

/// This folding set is used for two purposes:
///   1. Given information about a node we want to create, look up the unique
///      instance of the node in the set.  If the node already exists, return
///      it, otherwise return a token that makes the insertion cheap.
///   2. Given a node that has already been created, remove it from the set.
///
/// The hash table is linear-probing open addressing with tombstone-free
/// deletion, power-of-two capacity, and a 0.75 maximum load factor.
///
/// Any node that is to be included in the folding set must be a subclass of
/// FoldingSetNode.  The node class must also define a Profile method used to
/// establish the unique bits of data for the node.  The Profile method is
/// passed a FoldingSetNodeID object which is used to gather the bits.  Just
/// call one of the Add* functions defined in the FoldingSetNodeID class.
/// NOTE: That the folding set does not own the nodes and it is the
/// responsibility of the user to dispose of the nodes.
///
/// Eg.
///    class MyNode : public FoldingSetNode {
///    private:
///      std::string Name;
///      unsigned Value;
///    public:
///      MyNode(const char *N, unsigned V) : Name(N), Value(V) {}
///       ...
///      void Profile(FoldingSetNodeID &ID) const {
///        ID.AddString(Name);
///        ID.AddInteger(Value);
///      }
///      ...
///    };
///
/// To define the folding set itself use the FoldingSet template;
///
/// Eg.
///    FoldingSet<MyNode> MyFoldingSet;
///
/// Four public methods are available to manipulate the folding set;
///
/// 1) If you have an existing node that you want add to the set but unsure
/// that the node might already exist then call;
///
///    MyNode *M = MyFoldingSet.GetOrInsertNode(N);
///
/// If The result is equal to the input then the node has been inserted.
/// Otherwise, the result is the node existing in the folding set, and the
/// input can be discarded (use the result instead.)
///
/// 2) If you are ready to construct a node but want to check if it already
/// exists, then call FindNodeOrInsertPos with a FoldingSetNodeID of the bits to
/// check;
///
///   FoldingSetNodeID ID;
///   ID.AddString(Name);
///   ID.AddInteger(Value);
///   void *InsertPoint;
///
///    MyNode *M = MyFoldingSet.FindNodeOrInsertPos(ID, InsertPoint);
///
/// If found then M will be non-NULL, else InsertPoint will point to where it
/// should be inserted using InsertNode.
///
/// 3) If you get a NULL result from FindNodeOrInsertPos then you can insert a
/// new node with InsertNode;
///
///    MyNode *N = new MyNode(Name, Value);
///    MyFoldingSet.InsertNode(N, InsertPoint);
///
/// InsertPoint survives intervening insertions, but N must profile identically
/// to the ID that produced it, or N becomes unfindable.
///
/// 4) Finally, if you want to remove a node from the folding set call;
///
///    bool WasRemoved = MyFoldingSet.RemoveNode(M);
///
/// The result indicates whether the node existed in the folding set.

class FoldingSetNodeID;
class StringRef;

//===----------------------------------------------------------------------===//

/// This class provides default implementations for FoldingSetTrait
/// implementations.
template <typename T> struct DefaultFoldingSetTrait {
  /// Empty marker type when no folding-set context is stored.
  struct ContextStorage {};

  /// Gather the unique data bits of \p X into \p ID by calling \p X's Profile
  /// method.
  static void Profile(const T &X, FoldingSetNodeID &ID) { X.Profile(ID); }
  /// Gather the unique data bits of \p X into \p ID by calling \p X's Profile
  /// method.
  static void Profile(T &X, FoldingSetNodeID &ID) { X.Profile(ID); }

  /// Test whether the profile for \p X matches \p ID, using \p TempID to
  /// compute a temporary ID if necessary. The default implementation profiles
  /// \p X and compares the result to \p ID. Implementations can override this
  /// to provide more efficient comparisons.
  static inline bool Equals(T &X, const FoldingSetNodeID &ID, unsigned IDHash,
                            FoldingSetNodeID &TempID);

  /// Compute a hash value for \p X, using \p TempID to compute a temporary ID
  /// if necessary. The default implementation profiles \p X and hashes the
  /// result. Implementations can override this to provide more efficient
  /// implementations.
  static inline unsigned ComputeHash(T &X, FoldingSetNodeID &TempID);
};

/// This trait class is used to define behavior of how to "profile" (in the
/// FoldingSet parlance) an object of a given type.
/// The default behavior is to invoke a 'Profile' method on an object, but
/// through template specialization the behavior can be tailored for specific
/// types.  Combined with the FoldingSetNodeWrapper class, one can add objects
/// to FoldingSets that were not originally designed to have that behavior.
template <typename T, typename Enable = void>
struct FoldingSetTrait : public DefaultFoldingSetTrait<T> {};

/// Like DefaultFoldingSetTrait, but for ContextualFoldingSets.
template <typename T, typename Ctx> struct DefaultContextualFoldingSetTrait {
  /// Holds the fixed context passed to Profile, Equals, and ComputeHash.
  struct ContextStorage {
    /// The context value supplied when the folding set was created.
    Ctx Context;
    /// Construct storage holding \p Context.
    /// @param Context Fixed context for profiling and hashing.
    explicit ContextStorage(Ctx Context) : Context(Context) {}
    /// Return the context stored in this folding set.
    Ctx getContext() const { return Context; }
  };

  /// Gather the unique data bits of \p X into \p ID, using \p Context.
  static void Profile(T &X, FoldingSetNodeID &ID, Ctx Context) {
    X.Profile(ID, Context);
  }

  /// Test whether the profile for \p X matches \p ID under \p Context, using
  /// \p TempID to compute a temporary ID if necessary.
  static inline bool Equals(T &X, const FoldingSetNodeID &ID, unsigned IDHash,
                            FoldingSetNodeID &TempID, Ctx Context);
  /// Compute a hash value for \p X under \p Context, using \p TempID to
  /// compute a temporary ID if necessary. The default implementation profiles
  /// \p X and hashes the result. Implementations can override this to provide
  /// more efficient implementations.
  static inline unsigned ComputeHash(T &X, FoldingSetNodeID &TempID,
                                     Ctx Context);
};

/// Like FoldingSetTrait, but for ContextualFoldingSets.
template <typename T, typename Ctx>
struct ContextualFoldingSetTrait : DefaultContextualFoldingSetTrait<T, Ctx> {};

//===--------------------------------------------------------------------===//
/// This class describes a reference to an interned FoldingSetNodeID, which can
/// be a useful to store node id data rather than using plain FoldingSetNodeIDs,
/// since the 32-element SmallVector is often much larger than necessary, and
/// the possibility of heap allocation means it requires a non-trivial
/// destructor call.
class FoldingSetNodeIDRef {
  const unsigned *Data = nullptr;
  size_t Size = 0;

public:
  /// Construct an empty reference with no data.
  FoldingSetNodeIDRef() = default;
  /// Construct a reference to \p S unsigned profile words starting at \p D.
  FoldingSetNodeIDRef(const unsigned *D, size_t S) : Data(D), Size(S) {}

  /// Sentinel hash value that ComputeHash never returns; used by FoldingSetBase.
  static constexpr unsigned NotAHash = 0;

  /// Compute a strong hash value used to look up the node in FoldingSetBase.
  /// The hash value is not guaranteed to be deterministic across processes.
  /// Never returns NotAHash: FoldingSetBase uses that sentinel to keep the
  /// InsertPos token non-null and to mark a node belonging to no set.
  unsigned ComputeHash() const {
    unsigned Hash =
        static_cast<unsigned>(hash_combine_range(Data, Data + Size));
    return Hash == NotAHash ? 1 : Hash;
  }

  /// Compute a deterministic hash value across processes that is suitable for
  /// on-disk serialization.
  unsigned computeStableHash() const {
    return static_cast<unsigned>(xxh3_64bits(
        reinterpret_cast<const uint8_t *>(Data), sizeof(unsigned) * Size));
  }

  /// Return true if this reference and \p RHS describe identical profile data.
  LLVM_ABI bool operator==(FoldingSetNodeIDRef) const;

  /// Return true if this reference and \p RHS describe different profile data.
  bool operator!=(FoldingSetNodeIDRef RHS) const { return !(*this == RHS); }

  /// Used to compare the "ordering" of two nodes as defined by the
  /// profiled bits and their ordering defined by memcmp().
  LLVM_ABI bool operator<(FoldingSetNodeIDRef) const;

  /// Return a pointer to the profile words this reference covers.
  const unsigned *getData() const { return Data; }
  /// Return the number of profile words this reference covers.
  size_t getSize() const { return Size; }
};

//===--------------------------------------------------------------------===//
/// This class is used to gather all the unique data bits of a node.  When all
/// the bits are gathered this class is used to produce a hash value for the
/// node.
class FoldingSetNodeID {
  /// Vector of all the data bits that make the node unique.
  /// Use a SmallVector to avoid a heap allocation in the common case.
  SmallVector<unsigned, 32> Bits;

  template <typename T> void AddIntegerImpl(T I) {
    static_assert(std::is_integral_v<T> && sizeof(T) <= sizeof(unsigned) * 2,
                  "T must be an integer type no wider than 64 bits");
    Bits.push_back(static_cast<unsigned>(I));
    if constexpr (sizeof(unsigned) < sizeof(T))
      Bits.push_back(static_cast<unsigned long long>(I) >> 32);
  }

public:
  /// Construct an empty node ID with no profile bits accumulated yet.
  FoldingSetNodeID() = default;

  /// Construct a node ID by copying the profile bits from \p Ref.
  FoldingSetNodeID(FoldingSetNodeIDRef Ref)
      : Bits(Ref.getData(), Ref.getData() + Ref.getSize()) {}

  /// Add* - Add various data types to Bit data.
  void AddPointer(const void *Ptr) {
    // Note: this adds pointers to the hash using sizes and endianness that
    // depend on the host. It doesn't matter, however, because hashing on
    // pointer values is inherently unstable. Nothing should depend on the
    // ordering of nodes in the folding set.
    static_assert(sizeof(uintptr_t) <= sizeof(unsigned long long),
                  "unexpected pointer size");
    AddInteger(reinterpret_cast<uintptr_t>(Ptr));
  }
  /// Add a signed integer to the profile bits.
  /// @param I Integer value to profile.
  void AddInteger(signed I) { AddIntegerImpl(I); }
  /// Add an unsigned integer to the profile bits.
  void AddInteger(unsigned I) { AddIntegerImpl(I); }
  /// Add a signed long integer to the profile bits.
  void AddInteger(long I) { AddIntegerImpl(I); }
  /// Add an unsigned long integer to the profile bits.
  void AddInteger(unsigned long I) { AddIntegerImpl(I); }
  /// Add a signed 64-bit integer to the profile bits.
  void AddInteger(long long I) { AddIntegerImpl(I); }
  /// Add an unsigned 64-bit integer to the profile bits.
  void AddInteger(unsigned long long I) { AddIntegerImpl(I); }
  /// Add a boolean value to the profile bits as 0 or 1.
  void AddBoolean(bool B) { AddInteger(B ? 1U : 0U); }
  /// Add the length and contents of \p String to the profile bits.
  /// @param String String whose bytes are folded into this node ID.
  LLVM_ABI void AddString(StringRef String);
  /// Append the profile bits from \p ID to this node ID.
  LLVM_ABI void AddNodeID(const FoldingSetNodeID &ID);

  /// Profile \p x via FoldingSetTrait and add its bits to this node ID.
  template <typename T> inline void Add(const T &x) {
    FoldingSetTrait<T>::Profile(x, *this);
  }

  /// Clear the accumulated profile, allowing this FoldingSetNodeID
  /// object to be used to compute a new profile.
  inline void clear() { Bits.clear(); }

  /// Compute a strong hash value for this node ID, used to look up the node in
  /// FoldingSetBase. The hash value is not guaranteed to be deterministic
  /// across processes.
  unsigned ComputeHash() const {
    return FoldingSetNodeIDRef(Bits.data(), Bits.size()).ComputeHash();
  }

  /// Compute a deterministic hash value across processes that is suitable for
  /// on-disk serialization.
  unsigned computeStableHash() const {
    return FoldingSetNodeIDRef(Bits.data(), Bits.size()).computeStableHash();
  }

  /// Return true if this node ID and \p RHS have identical profile bits.
  LLVM_ABI bool operator==(const FoldingSetNodeID &RHS) const;
  /// Return true if this node ID and \p RHS have identical profile bits.
  LLVM_ABI bool operator==(const FoldingSetNodeIDRef RHS) const;

  /// Return true if this node ID and \p RHS have different profile bits.
  bool operator!=(const FoldingSetNodeID &RHS) const { return !(*this == RHS); }
  /// Return true if this node ID and \p RHS have different profile bits.
  bool operator!=(const FoldingSetNodeIDRef RHS) const {
    return !(*this == RHS);
  }

  /// Used to compare the "ordering" of two nodes as defined by the
  /// profiled bits and their ordering defined by memcmp().
  LLVM_ABI bool operator<(const FoldingSetNodeID &RHS) const;
  /// Compare profile bits lexicographically as an unsigned word sequence.
  LLVM_ABI bool operator<(const FoldingSetNodeIDRef RHS) const;

  /// Copy this node's data to a memory region allocated from the
  /// given allocator and return a FoldingSetNodeIDRef describing the
  /// interned data.
  LLVM_ABI FoldingSetNodeIDRef Intern(BumpPtrAllocator &Allocator) const;
};

//===----------------------------------------------------------------------===//
/// Non-templated base class for FoldingSet and ContextualFoldingSet, holding
/// the memory management and probing that does not depend on the node type.
class FoldingSetBase : public DebugEpochBase {
protected:
  /// Array of node pointers; a null entry marks an empty slot.
  void **Buckets = nullptr;

  /// Length of the Buckets array.  Always a power of 2.
  unsigned NumBuckets = 0;

  /// Number of nodes in the folding set.
  unsigned NumNodes = 0;

  /// Construct an empty folding set with 2^Log2InitSize buckets.
  LLVM_ABI explicit FoldingSetBase(unsigned Log2InitSize);
  /// Move-construct a folding set, leaving \p Arg in a valid empty state.
  LLVM_ABI FoldingSetBase(FoldingSetBase &&Arg);
  /// Move-assign a folding set, leaving \p RHS in a valid empty state.
  LLVM_ABI FoldingSetBase &operator=(FoldingSetBase &&RHS);
  /// Destroy the folding set and release its bucket array.
  LLVM_ABI ~FoldingSetBase();

public:
  //===--------------------------------------------------------------------===//
  /// This class is used to maintain node state in a folding set.
  class Node {
  private:
    // Hash of the node's profile, cached so that growth and removal never
    // re-run Profile(). NotAHash while the node is in no folding set.
    uint32_t FoldingSetHash = FoldingSetNodeIDRef::NotAHash;

  public:
    /// Construct a node that is not yet associated with any folding set.
    Node() = default;

    /// Return the cached hash of this node's profile, or NotAHash if the node
    /// is not in a folding set.
    uint32_t getFoldingSetHash() const { return FoldingSetHash; }
    /// Set the cached hash of this node's profile.
    void setFoldingSetHash(uint32_t Hash) { FoldingSetHash = Hash; }
  };

  /// Remove all nodes from the folding set.
  LLVM_ABI void clear();

  /// Returns the number of nodes in the folding set.
  unsigned size() const { return NumNodes; }

  /// Returns true if there are no nodes in the folding set.
  [[nodiscard]] bool empty() const { return NumNodes == 0; }

  /// Grow the number of buckets so that we can hold at least \p N nodes
  /// before rebucketing. May allocate more space than requested.
  LLVM_ABI void reserve(unsigned N);

protected:
  /// Functions provided by the derived class to compute folding properties.
  /// This is effectively a vtable for FoldingSetBase, except that we don't
  /// actually store a pointer to it in the object.
  struct FoldingSetInfo {
    /// Instantiations of the FoldingSet template implement this function to
    /// gather data bits for the given node.
    void (*GetNodeProfile)(const FoldingSetBase *Self, Node *N,
                           FoldingSetNodeID &ID);

    /// Instantiations of the FoldingSet template implement this function to
    /// compare the given node with the given ID.
    bool (*NodeEquals)(const FoldingSetBase *Self, Node *N,
                       const FoldingSetNodeID &ID, unsigned IDHash,
                       FoldingSetNodeID &TempID);

    /// Instantiations of the FoldingSet template implement this function to
    /// compute a hash value for the given node.
    unsigned (*ComputeNodeHash)(const FoldingSetBase *Self, Node *N,
                                FoldingSetNodeID &TempID);
  };

private:
  /// Put \p N in the first empty slot following its home, without checking
  /// capacity. Does not touch \p N, so a rehash need not dirty every node.
  void placeNode(Node *N, uint32_t Hash);

  /// Compare \p N against \p ID. Out of line to keep FoldingSetNodeID's inline
  /// storage out of the probe loop's frame.
  static bool nodeEquals(const FoldingSetInfo &Info, const FoldingSetBase *Self,
                         Node *N, const FoldingSetNodeID &ID, unsigned IDHash);

  /// Rehash into at least \p MinNumBuckets buckets, rounded up to a power of
  /// two and floored at the constructor's minimum.
  void grow(unsigned MinNumBuckets);

protected:
  // The below methods are protected to encourage subclasses to provide a more
  // type-safe API.

  /// Remove a node from the folding set, returning true if one
  /// was removed or false if the node was not in the folding set.
  LLVM_ABI bool RemoveNode(Node *N);

  /// If there is an existing node exactly equal to the node \p N,
  /// return it.  Otherwise, insert \p N and return it instead.
  LLVM_ABI Node *GetOrInsertNode(Node *N, const FoldingSetInfo &Info);

  /// Look up the node specified by ID.  If it exists, return it.  If not,
  /// return the insertion token that will make insertion faster.
  LLVM_ABI Node *FindNodeOrInsertPos(const FoldingSetNodeID &ID,
                                     void *&InsertPos,
                                     const FoldingSetInfo &Info);

  /// Insert the specified node into the folding set, knowing that
  /// it is not already in the folding set.  InsertPos must be obtained from
  /// FindNodeOrInsertPos for an ID that \p N profiles identically to.
  LLVM_ABI void InsertNode(Node *N, void *InsertPos);
};

/// Convenience alias for the base node type stored in a folding set.
using FoldingSetNode = FoldingSetBase::Node;
template <class T> class FoldingSetIterator;

// Definitions of FoldingSetTrait and ContextualFoldingSetTrait functions, which
// require the definition of FoldingSetNodeID.
template <typename T>
inline bool DefaultFoldingSetTrait<T>::Equals(T &X, const FoldingSetNodeID &ID,
                                              unsigned /*IDHash*/,
                                              FoldingSetNodeID &TempID) {
  FoldingSetTrait<T>::Profile(X, TempID);
  return TempID == ID;
}
template <typename T>
inline unsigned
DefaultFoldingSetTrait<T>::ComputeHash(T &X, FoldingSetNodeID &TempID) {
  FoldingSetTrait<T>::Profile(X, TempID);
  return TempID.ComputeHash();
}
template <typename T, typename Ctx>
inline bool DefaultContextualFoldingSetTrait<T, Ctx>::Equals(
    T &X, const FoldingSetNodeID &ID, unsigned /*IDHash*/,
    FoldingSetNodeID &TempID, Ctx Context) {
  ContextualFoldingSetTrait<T, Ctx>::Profile(X, TempID, Context);
  return TempID == ID;
}
template <typename T, typename Ctx>
inline unsigned DefaultContextualFoldingSetTrait<T, Ctx>::ComputeHash(
    T &X, FoldingSetNodeID &TempID, Ctx Context) {
  ContextualFoldingSetTrait<T, Ctx>::Profile(X, TempID, Context);
  return TempID.ComputeHash();
}

//===----------------------------------------------------------------------===//
/// An implementation detail that lets us share code between FoldingSet and
/// ContextualFoldingSet.
template <class T, class Trait = FoldingSetTrait<T>>
class FoldingSetImpl : public FoldingSetBase, public Trait::ContextStorage {
  // We define Info inside a static member function rather than as a static
  // constexpr member variable to avoid eager instantiation on MSVC when T is an
  // incomplete type.
  static const FoldingSetBase::FoldingSetInfo &getFoldingSetInfo() {
    static constexpr FoldingSetBase::FoldingSetInfo Info = {
        // GetNodeProfile
        [](const FoldingSetBase *Base, FoldingSetNode *N,
           FoldingSetNodeID &ID) {
          if constexpr (std::is_empty_v<typename Trait::ContextStorage>)
            Trait::Profile(*static_cast<T *>(N), ID);
          else
            Trait::Profile(
                *static_cast<T *>(N), ID,
                static_cast<const FoldingSetImpl *>(Base)->getContext());
        },
        // NodeEquals
        [](const FoldingSetBase *Base, FoldingSetNode *N,
           const FoldingSetNodeID &ID, unsigned IDHash,
           FoldingSetNodeID &TempID) {
          if constexpr (std::is_empty_v<typename Trait::ContextStorage>)
            return Trait::Equals(*static_cast<T *>(N), ID, IDHash, TempID);
          else
            return Trait::Equals(
                *static_cast<T *>(N), ID, IDHash, TempID,
                static_cast<const FoldingSetImpl *>(Base)->getContext());
        },
        // ComputeNodeHash
        [](const FoldingSetBase *Base, FoldingSetNode *N,
           FoldingSetNodeID &TempID) {
          if constexpr (std::is_empty_v<typename Trait::ContextStorage>)
            return Trait::ComputeHash(*static_cast<T *>(N), TempID);
          else
            return Trait::ComputeHash(
                *static_cast<T *>(N), TempID,
                static_cast<const FoldingSetImpl *>(Base)->getContext());
        }};
    return Info;
  }

public:
  /// Construct an empty folding set with 2^Log2InitSize buckets.
  explicit FoldingSetImpl(unsigned Log2InitSize = 6)
      : FoldingSetBase(Log2InitSize) {}

  /// Construct a contextual folding set with fixed \p Context and
  /// 2^Log2InitSize buckets.
  template <typename C, typename = std::enable_if_t<std::is_constructible_v<
                            typename Trait::ContextStorage, C>>>
  explicit FoldingSetImpl(C &&Context, unsigned Log2InitSize = 6)
      : FoldingSetBase(Log2InitSize),
        Trait::ContextStorage(std::forward<C>(Context)) {}

  /// Move-construct a folding set from \p Arg, leaving it in a valid empty
  /// state.
  FoldingSetImpl(FoldingSetImpl &&Arg) = default;
  /// Move-assign a folding set, leaving \p RHS in a valid empty state.
  FoldingSetImpl &operator=(FoldingSetImpl &&RHS) = default;
  /// Destroy the folding set; nodes are not deleted by this destructor.
  ~FoldingSetImpl() = default;

public:
  /// Iterator over the non-null nodes stored in the folding set.
  using iterator = FoldingSetIterator<T>;

  /// Return an iterator to the first non-null node in the folding set.
  iterator begin() { return iterator(Buckets, Buckets + NumBuckets, this); }
  /// Return an iterator past the last node in the folding set.
  iterator end() {
    return iterator(Buckets + NumBuckets, Buckets + NumBuckets, this);
  }

  /// Const iterator over the non-null nodes stored in the folding set.
  using const_iterator = FoldingSetIterator<const T>;

  /// Return a const iterator to the first non-null node in the folding set.
  const_iterator begin() const {
    return const_iterator(Buckets, Buckets + NumBuckets, this);
  }
  /// Return a const iterator past the last node in the folding set.
  const_iterator end() const {
    return const_iterator(Buckets + NumBuckets, Buckets + NumBuckets, this);
  }

  /// Remove a node from the folding set, returning true if one
  /// was removed or false if the node was not in the folding set.
  bool RemoveNode(T *N) { return FoldingSetBase::RemoveNode(N); }

  /// If there is an existing node exactly equal to the specified node,
  /// return it.  Otherwise, insert 'N' and return it instead.
  T *GetOrInsertNode(T *N) {
    return static_cast<T *>(
        FoldingSetBase::GetOrInsertNode(N, getFoldingSetInfo()));
  }

  /// Look up the node specified by ID.  If it exists, return it.  If not,
  /// return the insertion token that will make insertion faster.
  T *FindNodeOrInsertPos(const FoldingSetNodeID &ID, void *&InsertPos) {
    return static_cast<T *>(FoldingSetBase::FindNodeOrInsertPos(
        ID, InsertPos, getFoldingSetInfo()));
  }

  /// Insert the specified node into the folding set, knowing that
  /// it is not already in the folding set.  InsertPos must be obtained from
  /// FindNodeOrInsertPos.
  void InsertNode(T *N, void *InsertPos) {
    FoldingSetBase::InsertNode(N, InsertPos);
  }

  /// Insert the specified node into the folding set, knowing that it is not
  /// already in the folding set.
  void InsertNode(T *N) {
    T *Inserted = GetOrInsertNode(N);
    (void)Inserted;
    assert(Inserted == N && "Node already inserted!");
  }
};

//===----------------------------------------------------------------------===//
/// This template class is used to instantiate a specialized
/// implementation of the folding set to the node class T.  T must be a
/// subclass of FoldingSetNode and implement a Profile function.
///
/// Note that this set type is movable and move-assignable. However, its
/// moved-from state is not a valid state for anything other than
/// move-assigning and destroying. This is primarily to enable movable APIs
/// that incorporate these objects.
template <class T, class Trait = FoldingSetTrait<T>>
using FoldingSet = FoldingSetImpl<T, Trait>;

//===----------------------------------------------------------------------===//
/// This template class is a further refinement of FoldingSet which provides a
/// context argument when calling Profile on its nodes.  Currently, that
/// argument is fixed at initialization time.
///
/// T must be a subclass of FoldingSetNode and implement a Profile
/// function with signature
///   void Profile(FoldingSetNodeID &, Ctx);
template <class T, class Ctx>
using ContextualFoldingSet =
    FoldingSetImpl<T, ContextualFoldingSetTrait<T, Ctx>>;

//===----------------------------------------------------------------------===//
/// FoldingSet with deterministic insertion-order iteration.
///
/// Combines a FoldingSet and a vector. T must be a FoldingSetNode subclass and
/// implement a Profile function.
template <class T, class VectorT = SmallVector<T *, 8>> class FoldingSetVector {
  FoldingSet<T> Set;
  VectorT Vector;

public:
  /// Construct an empty FoldingSetVector with 2^Log2InitSize buckets.
  /// @param Log2InitSize Log2 of the initial bucket count (default 6).
  explicit FoldingSetVector(unsigned Log2InitSize = 6) : Set(Log2InitSize) {}

  /// Iterator over nodes in insertion order.
  using iterator = pointee_iterator<typename VectorT::iterator>;

  /// Return an iterator to the first node in insertion order.
  iterator begin() { return Vector.begin(); }
  /// Return an iterator past the last node in insertion order.
  iterator end() { return Vector.end(); }

  /// Const iterator over nodes in insertion order.
  using const_iterator = pointee_iterator<typename VectorT::const_iterator>;

  /// Return a const iterator to the first node in insertion order.
  const_iterator begin() const { return Vector.begin(); }
  /// Return a const iterator past the last node in insertion order.
  const_iterator end() const { return Vector.end(); }

  /// Remove all nodes from the folding set.
  void clear() {
    Set.clear();
    Vector.clear();
  }

  /// Look up the node specified by ID.  If it exists, return it.  If not,
  /// return the insertion token that will make insertion faster.
  T *FindNodeOrInsertPos(const FoldingSetNodeID &ID, void *&InsertPos) {
    return Set.FindNodeOrInsertPos(ID, InsertPos);
  }

  /// If there is an existing node exactly equal to the specified node,
  /// return it.  Otherwise, insert 'N' and return it instead.
  T *GetOrInsertNode(T *N) {
    T *Result = Set.GetOrInsertNode(N);
    if (Result == N)
      Vector.push_back(N);
    return Result;
  }

  /// Insert the specified node into the folding set, knowing that
  /// it is not already in the folding set.  InsertPos must be obtained from
  /// FindNodeOrInsertPos.
  void InsertNode(T *N, void *InsertPos) {
    Set.InsertNode(N, InsertPos);
    Vector.push_back(N);
  }

  /// Insert the specified node into the folding set, knowing that
  /// it is not already in the folding set.
  void InsertNode(T *N) {
    Set.InsertNode(N);
    Vector.push_back(N);
  }

  /// Returns the number of nodes in the folding set.
  unsigned size() const { return Set.size(); }

  /// Returns true if there are no nodes in the folding set.
  [[nodiscard]] bool empty() const { return Set.empty(); }
};

//===----------------------------------------------------------------------===//
/// Forward iterator for FoldingSet and ContextualFoldingSet.
template <class T> class FoldingSetIterator : DebugEpochBase::HandleBase {
  void **Bucket = nullptr;
  void **End = nullptr;

  void advance() {
    assert(isHandleInSync() && "invalid iterator access!");
    do
      ++Bucket;
    while (Bucket != End && *Bucket == nullptr);
  }

public:
  /// Construct an iterator over non-null entries from \p Bucket up to \p End.
  FoldingSetIterator(void **Bucket, void **End, const DebugEpochBase *Epoch)
      : DebugEpochBase::HandleBase(Epoch), Bucket(Bucket), End(End) {
    while (this->Bucket != this->End && *this->Bucket == nullptr)
      ++this->Bucket;
  }

  /// Dereference the current node in the folding set.
  T &operator*() const {
    assert(isHandleInSync() && "invalid iterator access!");
    return *static_cast<T *>(*Bucket);
  }

  /// Return a pointer to the current node in the folding set.
  T *operator->() const { return &operator*(); }

  /// Advance to the next non-null node in the folding set.
  inline FoldingSetIterator &operator++() {
    advance();
    return *this;
  }
  /// Advance to the next non-null node and return the previous position.
  FoldingSetIterator operator++(int) {
    FoldingSetIterator tmp = *this;
    ++*this;
    return tmp;
  }

  /// Return true if both iterators refer to the same bucket position.
  bool operator==(const FoldingSetIterator &RHS) const {
    assert(isHandleInSync() && RHS.isHandleInSync() && "handle not in sync!");
    return Bucket == RHS.Bucket;
  }
  /// Return true if the iterators refer to different positions.
  bool operator!=(const FoldingSetIterator &RHS) const {
    return !(*this == RHS);
  }
};

//===----------------------------------------------------------------------===//
/// This template class is used to "wrap" arbitrary types in an enclosing object
/// so that they can be inserted into FoldingSets.
template <typename T> class FoldingSetNodeWrapper : public FoldingSetNode {
  T data;

public:
  /// Construct a wrapper around a \c T built from \p Args.
  template <typename... Ts>
  explicit FoldingSetNodeWrapper(Ts &&...Args)
      : data(std::forward<Ts>(Args)...) {}

  /// Add the wrapped value's profile bits to \p ID.
  void Profile(FoldingSetNodeID &ID) { FoldingSetTrait<T>::Profile(data, ID); }

  /// Return a mutable reference to the wrapped value.
  T &getValue() { return data; }
  /// Return a const reference to the wrapped value.
  const T &getValue() const { return data; }

  /// Implicit conversion to the wrapped value.
  operator T &() { return data; }
  /// Implicit conversion to the wrapped const value.
  operator const T &() const { return data; }
};

//===----------------------------------------------------------------------===//
/// FoldingSetNode that caches a precomputed FoldingSetNodeID.
///
/// Trading space for speed (significant when the ID is long), and allowing
/// nodes to drop information that would otherwise only be needed to recompute
/// an ID.
class FastFoldingSetNode : public FoldingSetNode {
  FoldingSetNodeID FastID;

protected:
  /// Construct a node that stores a precomputed profile in \p ID.
  explicit FastFoldingSetNode(const FoldingSetNodeID &ID) : FastID(ID) {}

public:
  /// Append the precomputed profile bits stored in this node to \p ID.
  void Profile(FoldingSetNodeID &ID) const { ID.AddNodeID(FastID); }
};

//===----------------------------------------------------------------------===//
// Partial specializations of FoldingSetTrait.

template <typename T> struct FoldingSetTrait<T *> {
  static inline void Profile(T *X, FoldingSetNodeID &ID) { ID.AddPointer(X); }
};
template <typename T1, typename T2> struct FoldingSetTrait<std::pair<T1, T2>> {
  static inline void Profile(const std::pair<T1, T2> &P, FoldingSetNodeID &ID) {
    ID.Add(P.first);
    ID.Add(P.second);
  }
};

template <typename T>
struct FoldingSetTrait<T, std::enable_if_t<std::is_enum<T>::value>> {
  static void Profile(const T &X, FoldingSetNodeID &ID) {
    ID.AddInteger(llvm::to_underlying(X));
  }
};

} // namespace llvm

#endif // LLVM_ADT_FOLDINGSET_H
