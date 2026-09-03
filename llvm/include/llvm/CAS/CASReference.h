//===- llvm/CAS/CASReference.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CAS_CASREFERENCE_H
#define LLVM_CAS_CASREFERENCE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {

class raw_ostream;

namespace cas {

/// Content-addressable object store. @seebelow
class ObjectStore;
class ObjectHandle;
class ObjectRef;

/// Base class for references to things in \a ObjectStore.
class ReferenceBase {
protected:
  /// DenseMap empty-bucket sentinel value.
  ///
  /// \return Reserved internal ref used as the DenseMap empty key.
  static constexpr uint64_t getDenseMapEmptyRef() { return -1ULL; }
  /// DenseMap tombstone-bucket sentinel value.
  ///
  /// \return Reserved internal ref used as the DenseMap tombstone key.
  static constexpr uint64_t getDenseMapTombstoneRef() { return -2ULL; }

public:
  /// Get an internal reference.
  ///
  /// \param ExpectedCAS Object store this reference is expected to belong to.
  /// \return Store-local identifier bits for the referenced object.
  uint64_t getInternalRef(const ObjectStore &ExpectedCAS) const {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    assert(CAS == &ExpectedCAS && "Extracting reference for the wrong CAS");
#endif
    return InternalRef;
  }

  /// Helper functions for DenseMapInfo.
  ///
  /// \return Hash value derived from the internal ref for DenseMap.
  unsigned getDenseMapHash() const {
    return static_cast<unsigned>(llvm::hash_value(InternalRef));
  }
  /// Return true if this is the DenseMap empty sentinel.
  ///
  /// \return True if this is the DenseMap empty sentinel.
  bool isDenseMapEmpty() const { return InternalRef == getDenseMapEmptyRef(); }
  /// Return true if this is the DenseMap tombstone sentinel.
  ///
  /// \return True if this is the DenseMap tombstone sentinel.
  bool isDenseMapTombstone() const {
    return InternalRef == getDenseMapTombstoneRef();
  }
  /// Return true if this is any DenseMap sentinel.
  ///
  /// \return True if this is any DenseMap sentinel.
  bool isDenseMapSentinel() const {
    return isDenseMapEmpty() || isDenseMapTombstone();
  }

protected:
  /// Print an object handle for debugging.
  ///
  /// \param OS Stream to print to.
  /// \param This Handle whose identity is printed.
  void print(raw_ostream &OS, const ObjectHandle &This) const;
  /// Print an object reference for debugging.
  ///
  /// \param OS Stream to print to.
  /// \param This Reference whose identity is printed.
  void print(raw_ostream &OS, const ObjectRef &This) const;

  /// Return true if both references share the same internal CAS ref bits.
  ///
  /// \param RHS Other reference to compare against.
  /// \return True if both references share the same internal CAS ref bits.
  bool hasSameInternalRef(const ReferenceBase &RHS) const {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    assert(
        (isDenseMapSentinel() || RHS.isDenseMapSentinel() || CAS == RHS.CAS) &&
        "Cannot compare across CAS instances");
#endif
    return InternalRef == RHS.InternalRef;
  }

protected:
  friend class ObjectStore;
  /// Construct a reference bound to \p CAS with internal id \p InternalRef.
  ///
  /// \param CAS Object store that owns this reference, or null when unused.
  /// \param InternalRef Store-local identifier bits for the referenced object.
  /// \param IsHandle true when the reference represents a loaded object handle.
  ReferenceBase(const ObjectStore *CAS, uint64_t InternalRef, bool IsHandle)
      : InternalRef(InternalRef) {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    this->CAS = CAS;
#endif
    assert(InternalRef != getDenseMapEmptyRef() && "Reserved for DenseMapInfo");
    assert(InternalRef != getDenseMapTombstoneRef() &&
           "Reserved for DenseMapInfo");
  }

private:
  uint64_t InternalRef;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  const ObjectStore *CAS = nullptr;
#endif
};

/// Reference to an object in an \a ObjectStore instance.
///
/// If you have an ObjectRef, you know the object exists, and you can point at
/// it from new nodes with \a ObjectStore::store(), but you don't know anything
/// about it. "Loading" the object is a separate step that may not have
/// happened yet, and which can fail (due to filesystem corruption) or
/// introduce latency (if downloading from a remote store).
///
/// \a ObjectStore::store() takes a list of these, and these are returned by \a
/// ObjectStore::forEachRef() and \a ObjectStore::readRef(), which are accessors
/// for nodes, and \a ObjectStore::getReference().
///
/// \a ObjectStore::load() will load the referenced object, and returns \a
/// ObjectHandle, a variant that knows what kind of entity it is. \a
/// ObjectStore::getReferenceKind() can expect the type of reference without
/// asking for unloaded objects to be loaded.
class ObjectRef : public ReferenceBase {
public:
  /// Compare two object references for equality of their internal refs.
  ///
  /// \param LHS Left-hand object reference.
  /// \param RHS Right-hand object reference.
  /// \return True if \p LHS and \p RHS share the same internal refs.
  friend bool operator==(const ObjectRef &LHS, const ObjectRef &RHS) {
    return LHS.hasSameInternalRef(RHS);
  }
  /// Compare two object references for inequality.
  ///
  /// \param LHS Left-hand object reference.
  /// \param RHS Right-hand object reference.
  /// \return True if \p LHS and \p RHS identify different object references.
  friend bool operator!=(const ObjectRef &LHS, const ObjectRef &RHS) {
    return !(LHS == RHS);
  }

  /// Print internal ref and/or CASID. Only suitable for debugging.
  ///
  /// \param OS Stream to print to.
  void print(raw_ostream &OS) const { return ReferenceBase::print(OS, *this); }

  /// Dump this reference to the debug stream.
  LLVM_DUMP_METHOD void dump() const;

private:
  friend class ObjectStore;
  friend class ReferenceBase;
  using ReferenceBase::ReferenceBase;
  ObjectRef(const ObjectStore &CAS, uint64_t InternalRef)
      : ReferenceBase(&CAS, InternalRef, /*IsHandle=*/false) {
    assert(InternalRef != -1ULL && "Reserved for DenseMapInfo");
    assert(InternalRef != -2ULL && "Reserved for DenseMapInfo");
  }
  explicit ObjectRef(ReferenceBase) = delete;
};

/// Handle to a loaded object in a \a ObjectStore instance.
///
/// ObjectHandle encapulates a *loaded* object in the CAS. You need one
/// of these to inspect the content of an object: to look at its stored
/// data and references.
class ObjectHandle : public ReferenceBase {
public:
  /// Compare two object handles for equality of their internal refs.
  ///
  /// \param LHS Left-hand object handle.
  /// \param RHS Right-hand object handle.
  /// \return True if \p LHS and \p RHS share the same internal refs.
  friend bool operator==(const ObjectHandle &LHS, const ObjectHandle &RHS) {
    return LHS.hasSameInternalRef(RHS);
  }
  /// Compare two object handles for inequality.
  ///
  /// \param LHS Left-hand object handle.
  /// \param RHS Right-hand object handle.
  /// \return True if \p LHS and \p RHS identify different object handles.
  friend bool operator!=(const ObjectHandle &LHS, const ObjectHandle &RHS) {
    return !(LHS == RHS);
  }

  /// Print internal ref and/or CASID. Only suitable for debugging.
  ///
  /// \param OS Stream to print to.
  void print(raw_ostream &OS) const { return ReferenceBase::print(OS, *this); }

  /// Dump this handle to the debug stream.
  LLVM_DUMP_METHOD void dump() const;

private:
  friend class ObjectStore;
  friend class ReferenceBase;
  using ReferenceBase::ReferenceBase;
  explicit ObjectHandle(ReferenceBase) = delete;
  ObjectHandle(const ObjectStore &CAS, uint64_t InternalRef)
      : ReferenceBase(&CAS, InternalRef, /*IsHandle=*/true) {}
};

} // namespace cas

/// DenseMapInfo specialization for \a cas::ObjectRef.
template <> struct DenseMapInfo<cas::ObjectRef> {
  /// Compute a hash value for \p Ref.
  ///
  /// \param Ref Object reference to hash.
  /// \return Hash value for \p Ref suitable for DenseMap.
  static unsigned getHashValue(cas::ObjectRef Ref) {
    return Ref.getDenseMapHash();
  }

  /// Return true if \p LHS and \p RHS are equal.
  ///
  /// \param LHS Left-hand object reference.
  /// \param RHS Right-hand object reference.
  /// \return True if \p LHS and \p RHS are equal.
  static bool isEqual(cas::ObjectRef LHS, cas::ObjectRef RHS) {
    return LHS == RHS;
  }
};

} // namespace llvm

#endif // LLVM_CAS_CASREFERENCE_H
