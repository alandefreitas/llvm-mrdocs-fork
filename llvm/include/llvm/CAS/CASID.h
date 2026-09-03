//===- llvm/CAS/CASID.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CAS_CASID_H
#define LLVM_CAS_CASID_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {

class raw_ostream;

namespace cas {

class CASID;

/// Context for CAS identifiers.
class LLVM_ABI CASContext {
  virtual void anchor();

public:
  /// Destroy the CAS context.
  virtual ~CASContext() = default;

  /// Get a hash schema identifier for this CAS context.
  ///
  /// Two CAS instances should return \c true for this identifier if and only if
  /// their CASIDs are safe to compare by hash. This is used by \a
  /// CASID::equalsImpl().
  ///
  /// \return Hash schema identifier for this CAS context.
  virtual StringRef getHashSchemaIdentifier() const = 0;

protected:
  /// Print \p ID to \p OS.
  ///
  /// \param OS Stream to print to.
  /// \param ID Identifier to print.
  virtual void printIDImpl(raw_ostream &OS, const CASID &ID) const = 0;

  friend class CASID;
};

/// Unique identifier for a CAS object.
///
/// Locally, stores an internal CAS identifier that's specific to a single CAS
/// instance. It's guaranteed not to change across the view of that CAS, but
/// might change between runs.
///
/// It also has \a CASIDContext pointer to allow comparison of these
/// identifiers. If two CASIDs are from the same CASIDContext, they can be
/// compared directly. If they are, then \a
/// CASIDContext::getHashSchemaIdentifier() is compared to see if they can be
/// compared by hash, in which case the result of \a getHash() is compared.
class CASID {
public:
  /// Dump this CASID to the debug stream.
  LLVM_ABI void dump() const;

  /// Print \p ID to \p OS.
  ///
  /// \param OS Stream to print to.
  /// \param ID Identifier to print.
  /// \return Reference to \p OS after printing.
  friend raw_ostream &operator<<(raw_ostream &OS, const CASID &ID) {
    ID.print(OS);
    return OS;
  }

  /// Print CASID.
  ///
  /// \param OS Stream to print to.
  void print(raw_ostream &OS) const {
    return getContext().printIDImpl(OS, *this);
  }

  /// Return a printable string for CASID.
  ///
  /// \return Printable string representation of this identifier.
  LLVM_ABI std::string toString() const;

  /// Return the raw hash bytes for this identifier.
  ///
  /// \return Raw hash bytes for this identifier.
  ArrayRef<uint8_t> getHash() const {
    return arrayRefFromStringRef<uint8_t>(Hash);
  }

  /// Compare two CAS identifiers for equality.
  ///
  /// \param LHS Left-hand identifier.
  /// \param RHS Right-hand identifier.
  /// \return True if \p LHS and \p RHS identify the same CAS object.
  friend bool operator==(const CASID &LHS, const CASID &RHS) {
    if (LHS.Context == RHS.Context)
      return LHS.Hash == RHS.Hash;

    // TombstoneKey.
    if (!LHS.Context || !RHS.Context)
      return false;

    // CASIDs are equal when they have the same hash schema and same hash value.
    return LHS.Context->getHashSchemaIdentifier() ==
               RHS.Context->getHashSchemaIdentifier() &&
           LHS.Hash == RHS.Hash;
  }

  /// Compare two CAS identifiers for inequality.
  ///
  /// \param LHS Left-hand identifier.
  /// \param RHS Right-hand identifier.
  /// \return True if \p LHS and \p RHS identify different CAS objects.
  friend bool operator!=(const CASID &LHS, const CASID &RHS) {
    return !(LHS == RHS);
  }

  /// Hash a CASID from its raw hash bytes for use in DenseMap and similar.
  ///
  /// \param ID Identifier whose hash bytes are combined.
  /// \return Hash code derived from \p ID's raw hash bytes.
  friend hash_code hash_value(const CASID &ID) {
    return hash_combine_range(ID.getHash());
  }

  /// Return the CAS context that owns this identifier.
  ///
  /// \return CAS context that owns this identifier.
  const CASContext &getContext() const {
    assert(Context && "Tombstone key for DenseMap?");
    return *Context;
  }

  /// DenseMap tombstone sentinel key for CASID.
  ///
  /// \return Tombstone sentinel CASID for DenseMap.
  static CASID getDenseMapTombstoneKey() {
    // A reserved StringRef value distinct from the empty key, used only as a
    // DenseMap sentinel for CASID.
    return CASID(nullptr, StringRef(reinterpret_cast<const char *>(
                                        ~static_cast<uintptr_t>(1)),
                                    0));
  }

  /// CASID cannot be default-constructed.
  CASID() = delete;

  /// Create CASID from CASContext and raw hash bytes.
  ///
  /// \param Context CAS context that owns this identifier, or null for
  /// DenseMap sentinels.
  /// \param Hash Raw hash bytes for the identifier.
  /// \return CASID wrapping \p Context and \p Hash.
  static CASID create(const CASContext *Context, StringRef Hash) {
    return CASID(Context, Hash);
  }

private:
  CASID(const CASContext *Context, StringRef Hash)
      : Context(Context), Hash(Hash) {}

  const CASContext *Context;
  SmallString<32> Hash;
};

} // namespace cas

/// DenseMapInfo specialization for \a cas::CASID.
template <> struct DenseMapInfo<cas::CASID> {
  /// Compute a hash value for \p ID.
  ///
  /// \param ID CAS identifier to hash.
  /// \return Hash value for \p ID suitable for DenseMap.
  static unsigned getHashValue(cas::CASID ID) {
    return (unsigned)hash_value(ID);
  }

  /// Return true if \p LHS and \p RHS are equal.
  ///
  /// \param LHS Left-hand CAS identifier.
  /// \param RHS Right-hand CAS identifier.
  /// \return True if \p LHS and \p RHS are equal.
  static bool isEqual(cas::CASID LHS, cas::CASID RHS) { return LHS == RHS; }
};

} // namespace llvm

#endif // LLVM_CAS_CASID_H
