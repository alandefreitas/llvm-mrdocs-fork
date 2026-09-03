//===- llvm/BundleAttributes.h - LLVM Bundle Attributes ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_BUNDLE_ATTRIBUTES_H
#define LLVM_IR_BUNDLE_ATTRIBUTES_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/InstrTypes.h"

enum class BundleAttr {
  None,
#define ATTR(Name, String) Name,
#include "BundleAttributes.def"
};

namespace llvm {

/// Return the string name of the given operand-bundle attribute kind.
/// \param BA Bundle attribute kind to convert to a name.
/// \return The string name corresponding to \p BA.
LLVM_ABI StringRef getNameFromBundleAttr(BundleAttr BA);
/// Return the bundle attribute kind for the given operand-bundle tag ID.
/// \param ID Operand-bundle tag ID (e.g. from \c OperandBundleUse::getTagID).
/// \return The \c BundleAttr for \p ID, or \c BundleAttr::None if unrecognized.
LLVM_ABI BundleAttr getBundleAttrFromID(uint32_t ID);

/// Return the bundle attribute kind carried by the given operand bundle.
/// \param OBU Operand bundle whose tag identifies the attribute.
/// \return The \c BundleAttr identified by \p OBU's tag.
inline BundleAttr getBundleAttrFromOBU(OperandBundleUse OBU) {
  return getBundleAttrFromID(OBU.getTagID());
}

/// Parsed operands of an \c align assume operand bundle.
struct AssumeAlignInfo {
  /// Pointer whose alignment is assumed.
  const Use &Ptr;
  /// Alignment operand from the bundle.
  const Use &Alignment;
  /// Optional offset operand, or null if the bundle has no offset.
  const Use *Offset;
  /// Constant alignment value when the alignment operand is a constant.
  std::optional<uint64_t> AlignmentVal;
  /// Constant offset value when present (zero if the bundle omits an offset).
  std::optional<uint64_t> OffsetVal;
};

/// Extract alignment information from an \c align assume operand bundle.
/// \param OBU Align assume operand bundle to parse.
/// \return Parsed alignment operands and constant values from \p OBU.
LLVM_ABI AssumeAlignInfo getAssumeAlignInfo(OperandBundleUse OBU);

/// Parsed operands of a \c dereferenceable assume operand bundle.
struct AssumeDereferenceableInfo {
  /// Pointer that is assumed dereferenceable.
  const Use &Ptr;
  /// Byte-count operand from the bundle.
  const Use &Count;
  /// Constant byte count when the count operand is a constant.
  std::optional<uint64_t> CountVal;
};

/// Extract dereferenceable information from a \c dereferenceable assume
/// operand bundle.
/// \param OBU Dereferenceable assume operand bundle to parse.
/// \return Parsed dereferenceable operands and constant values from \p OBU.
LLVM_ABI
AssumeDereferenceableInfo getAssumeDereferenceableInfo(OperandBundleUse OBU);

/// Parsed operands of a \c nonnull assume operand bundle.
struct AssumeNonNullInfo {
  /// Pointer that is assumed non-null.
  const Use &Ptr;
};

/// Extract non-null information from a \c nonnull assume operand bundle.
/// \param OBU Nonnull assume operand bundle to parse.
/// \return Parsed non-null operands from \p OBU.
LLVM_ABI AssumeNonNullInfo getAssumeNonNullInfo(OperandBundleUse OBU);

/// Parsed operands of a \c noundef assume operand bundle.
struct AssumeNoUndefInfo {
  /// Value that is assumed not undef.
  const Use &Val;
};

/// Extract noundef information from a \c noundef assume operand bundle.
/// \param OBU Noundef assume operand bundle to parse.
/// \return Parsed noundef operands from \p OBU.
LLVM_ABI AssumeNoUndefInfo getAssumeNoUndefInfo(OperandBundleUse OBU);

/// Parsed operands of a \c separate_storage assume operand bundle.
struct AssumeSeparateStorageInfo {
  /// First pointer in the separate-storage pair.
  const Use &Ptr1;
  /// Second pointer in the separate-storage pair.
  const Use &Ptr2;
};

/// Extract separate-storage information from a \c separate_storage assume
/// operand bundle.
/// \param OBU Separate-storage assume operand bundle to parse.
/// \return Parsed separate-storage operands from \p OBU.
LLVM_ABI
AssumeSeparateStorageInfo getAssumeSeparateStorageInfo(OperandBundleUse OBU);

/// Return true if the given assume operand bundle implies that \p Val is
/// non-null.
/// \param Val Value that may be proven non-null by the bundle.
/// \param Context Function providing null-pointer address-space rules.
/// \param OBU Assume operand bundle to interpret.
/// \return True if \p OBU implies that \p Val is non-null.
LLVM_ABI bool assumeBundleImpliesNonNull(const Value *Val,
                                         const Function *Context,
                                         OperandBundleUse OBU);

} // namespace llvm

#endif // LLVM_IR_BUNDLE_ATTRIBUTES_H
