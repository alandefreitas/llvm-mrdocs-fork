//===- llvm/IR/TrackingMDRef.h - Tracking Metadata references ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// References to metadata that track RAUW.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_TRACKINGMDREF_H
#define LLVM_IR_TRACKINGMDREF_H

#include "llvm/IR/Metadata.h"
#include <algorithm>
#include <cassert>

namespace llvm {

/// Tracking metadata reference.
///
/// This class behaves like \a TrackingVH, but for metadata.
class TrackingMDRef {
  Metadata *MD = nullptr;

public:
  /// Construct a null tracking metadata reference.
  TrackingMDRef() = default;
  /// Construct a tracking reference to \p MD.
  /// \param MD Metadata to track, or null.
  explicit TrackingMDRef(Metadata *MD) : MD(MD) { track(); }

  /// Move-construct, taking ownership of tracking from \p X.
  /// \param X Reference whose tracking is transferred.
  TrackingMDRef(TrackingMDRef &&X) : MD(X.MD) { retrack(X); }
  /// Copy-construct, registering a new tracking reference to the same metadata.
  /// \param X Reference whose metadata pointer is copied.
  TrackingMDRef(const TrackingMDRef &X) : MD(X.MD) { track(); }

  /// Move-assign, transferring tracking from \p X.
  /// \param X Reference whose tracking is transferred.
  /// \return Reference to this object.
  TrackingMDRef &operator=(TrackingMDRef &&X) {
    if (&X == this || MD == X.MD)
      return *this;

    untrack();
    MD = X.MD;
    retrack(X);
    return *this;
  }

  /// Copy-assign, tracking the same metadata as \p X.
  /// \param X Reference whose metadata pointer is copied.
  /// \return Reference to this object.
  TrackingMDRef &operator=(const TrackingMDRef &X) {
    if (&X == this || MD == X.MD)
      return *this;

    untrack();
    MD = X.MD;
    track();
    return *this;
  }

  /// Destroy the reference and stop tracking.
  ~TrackingMDRef() { untrack(); }

  /// Return the tracked metadata pointer, or null if none.
  /// \return The tracked metadata pointer, or null if none.
  Metadata *get() const { return MD; }
  /// Implicit conversion to the tracked metadata pointer.
  /// \return The tracked metadata pointer, or null if none.
  operator Metadata *() const { return get(); }
  /// Return a pointer to the tracked metadata.
  /// \return Pointer to the tracked metadata.
  Metadata *operator->() const { return get(); }
  /// Return a reference to the tracked metadata.
  /// \return Reference to the tracked metadata.
  Metadata &operator*() const { return *get(); }

  /// Stop tracking and clear the reference to null.
  void reset() {
    untrack();
    MD = nullptr;
  }
  /// Stop tracking and begin tracking \p MD instead.
  /// \param MD Metadata to track, or null.
  void reset(Metadata *MD) {
    untrack();
    this->MD = MD;
    track();
  }

  /// Return true if both refs track the same metadata pointer.
  /// \param X Other reference to compare against.
  /// \return True if both track the same pointer.
  bool operator==(const TrackingMDRef &X) const { return MD == X.MD; }
  /// Return true if the refs track different metadata pointers.
  /// \param X Other reference to compare against.
  /// \return True if the tracked pointers differ.
  bool operator!=(const TrackingMDRef &X) const { return MD != X.MD; }

private:
  void track() {
    if (MD)
      MetadataTracking::track(MD);
  }

  void untrack() {
    if (MD)
      MetadataTracking::untrack(MD);
  }

  void retrack(TrackingMDRef &X) {
    assert(MD == X.MD && "Expected values to match");
    if (X.MD) {
      MetadataTracking::retrack(X.MD, MD);
      X.MD = nullptr;
    }
  }
};

/// Typed tracking ref.
///
/// Track refererences of a particular type.  It's useful to use this for \a
/// MDNode and \a ValueAsMetadata.
template <class T> class TypedTrackingMDRef {
  TrackingMDRef Ref;

public:
  /// Construct a null typed tracking metadata reference.
  TypedTrackingMDRef() = default;
  /// Construct a typed tracking reference to \p MD.
  /// \param MD Typed metadata to track, or null.
  explicit TypedTrackingMDRef(T *MD) : Ref(static_cast<Metadata *>(MD)) {}

  /// Move-construct, transferring tracking from \p X.
  /// \param X Reference whose tracking is transferred.
  TypedTrackingMDRef(TypedTrackingMDRef &&X) : Ref(std::move(X.Ref)) {}
  /// Copy-construct, registering a new tracking reference to the same metadata.
  /// \param X Reference whose metadata pointer is copied.
  TypedTrackingMDRef(const TypedTrackingMDRef &X) = default;

  /// Move-assign, transferring tracking from \p X.
  /// \param X Reference whose tracking is transferred.
  /// \return Reference to this object.
  TypedTrackingMDRef &operator=(TypedTrackingMDRef &&X) {
    Ref = std::move(X.Ref);
    return *this;
  }

  /// Copy-assign, tracking the same metadata as \p X.
  /// \param X Reference whose metadata pointer is copied.
  /// \return Reference to this object.
  TypedTrackingMDRef &operator=(const TypedTrackingMDRef &X) = default;

  /// Return the tracked typed metadata pointer, or null if none.
  /// \return The typed metadata pointer, or null if none.
  T *get() const { return (T *)Ref.get(); }
  /// Implicit conversion to a typed metadata pointer.
  /// \return The typed metadata pointer, or null if none.
  operator T *() const { return get(); }
  /// Return a pointer to the tracked typed metadata.
  /// \return Pointer to the tracked typed metadata.
  T *operator->() const { return get(); }
  /// Return a reference to the tracked typed metadata.
  /// \return Reference to the tracked typed metadata.
  T &operator*() const { return *get(); }

  /// Return true if both refs track the same metadata pointer.
  /// \param X Other reference to compare against.
  /// \return True if both track the same pointer.
  bool operator==(const TypedTrackingMDRef &X) const { return Ref == X.Ref; }
  /// Return true if the refs track different metadata pointers.
  /// \param X Other reference to compare against.
  /// \return True if the tracked pointers differ.
  bool operator!=(const TypedTrackingMDRef &X) const { return Ref != X.Ref; }

  /// Stop tracking and clear the reference to null.
  void reset() { Ref.reset(); }
  /// Stop tracking and begin tracking \p MD instead.
  /// \param MD Typed metadata to track, or null.
  void reset(T *MD) { Ref.reset(static_cast<Metadata *>(MD)); }
};

/// Tracking reference specialized for \a MDNode.
using TrackingMDNodeRef = TypedTrackingMDRef<MDNode>;
/// Tracking reference specialized for \a ValueAsMetadata.
using TrackingValueAsMetadataRef = TypedTrackingMDRef<ValueAsMetadata>;

// Expose the underlying metadata to casting.
/// simplify_type specialization so TrackingMDRef participates in cast/isa.
template <> struct simplify_type<TrackingMDRef> {
  /// Underlying type used by casting operators.
  using SimpleType = Metadata *;

  /// Return the metadata pointer held by \p MD.
  /// \param MD Tracking reference to unwrap.
  /// \return The metadata pointer, or null if none.
  static SimpleType getSimplifiedValue(TrackingMDRef &MD) { return MD.get(); }
};

/// simplify_type specialization so const TrackingMDRef participates in cast/isa.
template <> struct simplify_type<const TrackingMDRef> {
  /// Underlying type used by casting operators.
  using SimpleType = Metadata *;

  /// Return the metadata pointer held by \p MD.
  /// \param MD Const tracking reference to unwrap.
  /// \return The metadata pointer, or null if none.
  static SimpleType getSimplifiedValue(const TrackingMDRef &MD) {
    return MD.get();
  }
};

/// simplify_type specialization so TypedTrackingMDRef participates in cast/isa.
template <class T> struct simplify_type<TypedTrackingMDRef<T>> {
  /// Underlying type used by casting operators.
  using SimpleType = T *;

  /// Return the typed metadata pointer held by \p MD.
  /// \param MD Typed tracking reference to unwrap.
  /// \return The typed metadata pointer, or null if none.
  static SimpleType getSimplifiedValue(TypedTrackingMDRef<T> &MD) {
    return MD.get();
  }
};

/// simplify_type specialization so const TypedTrackingMDRef participates in cast/isa.
template <class T> struct simplify_type<const TypedTrackingMDRef<T>> {
  /// Underlying type used by casting operators.
  using SimpleType = T *;

  /// Return the typed metadata pointer held by \p MD.
  /// \param MD Const typed tracking reference to unwrap.
  /// \return The typed metadata pointer, or null if none.
  static SimpleType getSimplifiedValue(const TypedTrackingMDRef<T> &MD) {
    return MD.get();
  }
};

} // end namespace llvm

#endif // LLVM_IR_TRACKINGMDREF_H
