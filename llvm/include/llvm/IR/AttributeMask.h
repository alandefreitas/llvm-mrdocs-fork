//===- llvm/AttributeMask.h - Mask for Attributes ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
// This file declares the AttributeMask class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_ATTRIBUTEMASK_H
#define LLVM_IR_ATTRIBUTEMASK_H

#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Attributes.h"
#include <bitset>
#include <cassert>
#include <set>

namespace llvm {

//===----------------------------------------------------------------------===//
/// \class
/// This class stores enough information to efficiently remove some attributes
/// from an existing AttrBuilder, AttributeSet or AttributeList.
class AttributeMask {
  std::bitset<Attribute::EndAttrKinds> Attrs;
  std::set<SmallString<32>, std::less<>> TargetDepAttrs;

public:
  /// Construct an empty attribute mask.
  AttributeMask() = default;
  /// Copying is deleted; AttributeMask owns mutable attribute state.
  /// \param M Mask that would have been copied.
  AttributeMask(const AttributeMask &M) = delete;
  /// Move-construct from mask \p M.
  /// \param M Mask to move from.
  AttributeMask(AttributeMask &&M) = default;

  /// Construct a mask containing the attributes from \p AS.
  /// \param AS Attribute set whose kinds are added to the mask.
  AttributeMask(AttributeSet AS) {
    for (Attribute A : AS)
      addAttribute(A);
  }

  /// Add an attribute to the mask.
  /// \param Val Enum attribute kind to add.
  /// \return A reference to this mask.
  AttributeMask &addAttribute(Attribute::AttrKind Val) {
    assert((unsigned)Val < Attribute::EndAttrKinds &&
           "Attribute out of range!");
    Attrs[Val] = true;
    return *this;
  }

  /// Add the Attribute object to the builder.
  /// \param A Attribute to add.
  /// \return A reference to this mask.
  AttributeMask &addAttribute(Attribute A) {
    if (A.isStringAttribute())
      addAttribute(A.getKindAsString());
    else
      addAttribute(A.getKindAsEnum());
    return *this;
  }

  /// Add the target-dependent attribute to the builder.
  /// \param A Target-dependent attribute kind.
  /// \return A reference to this mask.
  AttributeMask &addAttribute(StringRef A) {
    TargetDepAttrs.insert(A);
    return *this;
  }

  /// Return true if the builder has the specified attribute.
  /// \param A Enum attribute kind to look up.
  /// \return True if the mask contains the attribute.
  bool contains(Attribute::AttrKind A) const {
    assert((unsigned)A < Attribute::EndAttrKinds && "Attribute out of range!");
    return Attrs[A];
  }

  /// Return true if the builder has the specified target-dependent
  /// attribute.
  /// \param A Target-dependent attribute kind to look up.
  /// \return True if the mask contains the attribute.
  bool contains(StringRef A) const { return TargetDepAttrs.count(A); }

  /// Return true if the mask contains the specified attribute.
  /// \param A Attribute to look up.
  /// \return True if the mask contains the attribute.
  bool contains(Attribute A) const {
    if (A.isStringAttribute())
      return contains(A.getKindAsString());
    return contains(A.getKindAsEnum());
  }
};

} // end namespace llvm

#endif // LLVM_IR_ATTRIBUTEMASK_H
