//===-- llvm/Remarks/RemarkLinker.h -----------------------------*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an interface to link together multiple remark files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_REMARKLINKER_H
#define LLVM_REMARKS_REMARKLINKER_H

#include "llvm/Remarks/Remark.h"
#include "llvm/Remarks/RemarkFormat.h"
#include "llvm/Remarks/RemarkStringTable.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <optional>
#include <set>

namespace llvm {

namespace object {
class ObjectFile;
}

namespace remarks {

/// Links together remarks from multiple remark files or object files.
struct RemarkLinker {
private:
  /// Compare through the pointers.
  struct RemarkPtrCompare {
    bool operator()(const std::unique_ptr<Remark> &LHS,
                    const std::unique_ptr<Remark> &RHS) const {
      assert(LHS && RHS && "Invalid pointers to compare.");
      return *LHS < *RHS;
    };
  };

  /// The main string table for the remarks.
  /// Note: all remarks should use the strings from this string table to avoid
  /// dangling references.
  StringTable StrTab;

  /// A set holding unique remarks.
  /// FIXME: std::set is probably not the most appropriate data structure here.
  /// Due to the limitation of having a move-only key, there isn't another
  /// obvious choice for now.
  std::set<std::unique_ptr<Remark>, RemarkPtrCompare> Remarks;

  /// A path to append before the external file path found in remark metadata.
  std::optional<std::string> PrependPath;

  /// If true, keep all remarks, otherwise only keep remarks with valid debug
  /// locations.
  bool KeepAllRemarks = true;

  /// Keep this remark. If it's already in the set, discard it.
  Remark &keep(std::unique_ptr<Remark> Remark);

  /// Returns true if \p R should be kept. If KeepAllRemarks is false, only
  /// return true if \p R has a valid debug location.
  bool shouldKeepRemark(const Remark &R) {
    return KeepAllRemarks ? true : R.Loc.has_value();
  }

public:
  /// Set a path to prepend to the external file path.
  ///
  /// \param PrependPath The path to prepend before external file paths found in
  /// remark metadata.
  LLVM_ABI void setExternalFilePrependPath(StringRef PrependPath);

  /// Set KeepAllRemarks to \p B.
  ///
  /// \param B If true, keep all remarks; otherwise only keep remarks with valid
  /// debug locations.
  void setKeepAllRemarks(bool B) { KeepAllRemarks = B; }

  /// Link the remarks found in \p Buffer.
  ///
  /// If \p RemarkFormat is not provided, try to deduce it from the metadata in
  /// \p Buffer.
  /// \p Buffer can be either a standalone remark container or just
  /// metadata. This takes care of uniquing and merging the remarks.
  ///
  /// \param Buffer The remark container or metadata to link.
  /// \param RemarkFormat The format of the remarks, or Format::Auto to deduce
  /// it.
  /// \return Error::success() on success, or an error if linking fails.
  LLVM_ABI Error link(StringRef Buffer, Format RemarkFormat = Format::Auto);

  /// Link the remarks found in \p Obj by looking for the right section and
  /// calling the method above.
  ///
  /// \param Obj The object file to search for a remarks section.
  /// \param RemarkFormat The format of the remarks, or Format::Auto to deduce
  /// it.
  /// \return Error::success() on success, or an error if linking fails.
  LLVM_ABI Error link(const object::ObjectFile &Obj,
                      Format RemarkFormat = Format::Auto);

  /// Serialize the linked remarks to a stream.
  ///
  /// This clears internal state such as the string table.
  /// Note: this implies that the serialization mode is standalone.
  ///
  /// \param OS The output stream to write the serialized remarks to.
  /// \param RemarksFormat The remark format to use for serialization.
  /// \return Error::success() on success, or an error if serialization fails.
  LLVM_ABI Error serialize(raw_ostream &OS, Format RemarksFormat) const;

  /// Check whether there are any remarks linked.
  ///
  /// \return True if no remarks have been linked.
  bool empty() const { return Remarks.empty(); }

  /// Iterator over the linked unique remarks.
  using iterator = pointee_iterator<decltype(Remarks)::const_iterator>;

  /// Return a collection of the linked unique remarks to iterate on.
  ///
  /// Ex:
  /// for (const Remark &R : RL.remarks() { [...] }
  ///
  /// \return A range over the linked unique remarks.
  iterator_range<iterator> remarks() const { return Remarks; }
};

/// Returns a buffer with the contents of the remarks section depending on the
/// format of the file. If the section doesn't exist, this returns an empty
/// optional.
///
/// \param Obj The object file to read the remarks section from.
/// \return The remarks section contents, an empty optional if the section is
/// missing, or an error if reading fails.
LLVM_ABI Expected<std::optional<StringRef>>
getRemarksSectionContents(const object::ObjectFile &Obj);

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_REMARKLINKER_H
