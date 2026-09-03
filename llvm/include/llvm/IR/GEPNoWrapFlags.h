//===-- llvm/GEPNoWrapFlags.h - NoWrap flags for GEPs -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the nowrap flags for getelementptr operators.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GEPNOWRAPFLAGS_H
#define LLVM_IR_GEPNOWRAPFLAGS_H

#include <assert.h>

namespace llvm {

/// Represents nowrap flags for a getelementptr instruction or expression.
///
/// The following flags are supported:
///  * inbounds (implies nusw)
///  * nusw (no unsigned signed wrap)
///  * nuw (no unsigned wrap)
/// See LangRef for a description of their semantics.
class GEPNoWrapFlags {
  enum : unsigned {
    InBoundsFlag = (1 << 0),
    NUSWFlag = (1 << 1),
    NUWFlag = (1 << 2),
  };

  unsigned Flags;
  /// Construct from packed nowrap flag bits.
  /// \param Flags Packed bits; inbounds implies nusw.
  GEPNoWrapFlags(unsigned Flags) : Flags(Flags) {
    assert((!isInBounds() || hasNoUnsignedSignedWrap()) &&
           "inbounds implies nusw");
  }

public:
  /// Construct an empty set of nowrap flags.
  GEPNoWrapFlags() : Flags(0) {}
  /// Construct flags from a boolean treated as inbounds.
  ///
  /// For historical reasons, interpret a plain boolean as InBounds.
  /// TODO: Migrate users to pass explicit GEPNoWrapFlags and remove this ctor.
  /// \param IsInBounds True to set inbounds and the implied nusw flag.
  GEPNoWrapFlags(bool IsInBounds)
      : Flags(IsInBounds ? (InBoundsFlag | NUSWFlag) : 0) {}

  /// Return flags with no nowrap bits set.
  /// \return Flags with no nowrap bits set.
  static GEPNoWrapFlags none() { return GEPNoWrapFlags(); }
  /// Return flags with inbounds, nusw, and nuw all set.
  /// \return Flags with inbounds, nusw, and nuw all set.
  static GEPNoWrapFlags all() {
    return GEPNoWrapFlags(InBoundsFlag | NUSWFlag | NUWFlag);
  }
  /// Return flags with inbounds and nusw set.
  /// \return Flags with inbounds and nusw set.
  static GEPNoWrapFlags inBounds() {
    return GEPNoWrapFlags(InBoundsFlag | NUSWFlag);
  }
  /// Return flags with only the nusw bit set.
  /// \return Flags with only the nusw bit set.
  static GEPNoWrapFlags noUnsignedSignedWrap() {
    return GEPNoWrapFlags(NUSWFlag);
  }
  /// Return flags with only the nuw bit set.
  /// \return Flags with only the nuw bit set.
  static GEPNoWrapFlags noUnsignedWrap() { return GEPNoWrapFlags(NUWFlag); }

  /// Reconstruct flags from the packed bit representation returned by \ref getRaw.
  /// \param Flags The packed nowrap flag bits.
  /// \return Flags reconstructed from the packed bits.
  static GEPNoWrapFlags fromRaw(unsigned Flags) {
    return GEPNoWrapFlags(Flags);
  }
  /// Return the packed nowrap flag bits.
  /// \return The packed nowrap flag bits.
  unsigned getRaw() const { return Flags; }

  /// Return true if the inbounds flag is set.
  /// \return True if the inbounds flag is set.
  bool isInBounds() const { return Flags & InBoundsFlag; }
  /// Return true if the nusw flag is set.
  /// \return True if the nusw flag is set.
  bool hasNoUnsignedSignedWrap() const { return Flags & NUSWFlag; }
  /// Return true if the nuw flag is set.
  /// \return True if the nuw flag is set.
  bool hasNoUnsignedWrap() const { return Flags & NUWFlag; }

  /// Return a copy with the inbounds flag cleared.
  /// \return A copy with the inbounds flag cleared.
  GEPNoWrapFlags withoutInBounds() const {
    return GEPNoWrapFlags(Flags & ~InBoundsFlag);
  }
  /// Return a copy with the inbounds and nusw flags cleared.
  /// \return A copy with the inbounds and nusw flags cleared.
  GEPNoWrapFlags withoutNoUnsignedSignedWrap() const {
    return GEPNoWrapFlags(Flags & ~(InBoundsFlag | NUSWFlag));
  }
  /// Return a copy with the nuw flag cleared.
  /// \return A copy with the nuw flag cleared.
  GEPNoWrapFlags withoutNoUnsignedWrap() const {
    return GEPNoWrapFlags(Flags & ~NUWFlag);
  }

  /// Given (gep (gep p, x), y), determine the nowrap flags for (gep p, x+y).
  /// \param Other The other getelementptr's nowrap flags.
  /// \return The nowrap flags for the combined offset add.
  GEPNoWrapFlags intersectForOffsetAdd(GEPNoWrapFlags Other) const {
    GEPNoWrapFlags Res = *this & Other;
    // Without inbounds, we could only preserve nusw if we know that x + y does
    // not wrap.
    if (!Res.isInBounds() && Res.hasNoUnsignedSignedWrap())
      Res = Res.withoutNoUnsignedSignedWrap();
    return Res;
  }

  /// Given (gep (gep p, x), y), determine the nowrap flags for
  /// (gep (gep, p, y), x).
  /// \param Other The other getelementptr's nowrap flags.
  /// \return The nowrap flags after reassociation, or none if nusw cannot be
  /// preserved.
  GEPNoWrapFlags intersectForReassociate(GEPNoWrapFlags Other) const {
    GEPNoWrapFlags Res = *this & Other;
    // We can only preserve inbounds and nusw if nuw is also set.
    if (!Res.hasNoUnsignedWrap())
      return none();
    return Res;
  }

  /// Return true if this flag set equals \p Other.
  /// \param Other The flags to compare against.
  /// \return True if the flag sets are equal.
  bool operator==(GEPNoWrapFlags Other) const { return Flags == Other.Flags; }
  /// Return true if this flag set differs from \p Other.
  /// \param Other The flags to compare against.
  /// \return True if the flag sets differ.
  bool operator!=(GEPNoWrapFlags Other) const { return !(*this == Other); }

  /// Return the bitwise-AND of this flag set with \p Other.
  /// \param Other The flags to intersect with.
  /// \return The intersection of this flag set and \p Other.
  GEPNoWrapFlags operator&(GEPNoWrapFlags Other) const {
    return GEPNoWrapFlags(Flags & Other.Flags);
  }
  /// Return the bitwise-OR of this flag set with \p Other.
  /// \param Other The flags to union with.
  /// \return The union of this flag set and \p Other.
  GEPNoWrapFlags operator|(GEPNoWrapFlags Other) const {
    return GEPNoWrapFlags(Flags | Other.Flags);
  }
  /// Bitwise-AND the flags from \p Other into this set.
  /// \param Other The flags to intersect with.
  /// \return A reference to this flag set.
  GEPNoWrapFlags &operator&=(GEPNoWrapFlags Other) {
    Flags &= Other.Flags;
    return *this;
  }
  /// Bitwise-OR the flags from \p Other into this set.
  /// \param Other The flags to union with.
  /// \return A reference to this flag set.
  GEPNoWrapFlags &operator|=(GEPNoWrapFlags Other) {
    Flags |= Other.Flags;
    return *this;
  }
};

} // end namespace llvm

#endif // LLVM_IR_GEPNOWRAPFLAGS_H
