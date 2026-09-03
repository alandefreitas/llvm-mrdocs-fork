//===- MemoryModelRelaxationAnnotations.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides utility for Memory Model Relaxation Annotations (MMRAs).
/// Those annotations are represented using Metadata. The MMRATagSet class
/// offers a simple API to parse the metadata and perform common operations on
/// it. The MMRAMetadata class is a simple tuple of MDNode that provides easy
/// access to all MMRA annotations on an instruction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MEMORYMODELRELAXATIONANNOTATIONS_H
#define LLVM_IR_MEMORYMODELRELAXATIONANNOTATIONS_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"

#include <utility>

namespace llvm {

template <typename T> class ArrayRef;

class MDNode;
class MDTuple;
class Metadata;
class raw_ostream;
class LLVMContext;
class Instruction;

/// Helper class to manipulate `!mmra` metadata nodes.
///
/// This can be visualized as a set of "tags", with each tag
/// representing a particular property of an instruction, as
/// explained in the MemoryModelRelaxationAnnotations docs.
///
/// This class (and the optimizer in general) does not reason
/// about the exact nature of the tags and the properties they
/// imply. It just sees the metadata as a collection of tags, which
/// are a prefix/suffix pair of strings.
class MMRAMetadata {
public:
  /// Prefix/suffix pair that identifies a single MMRA tag.
  using TagT = std::pair<StringRef, StringRef>;
  /// Dense set of MMRA tags.
  using SetT = DenseSet<TagT>;
  /// Const iterator over the tags in this set.
  using const_iterator = SetT::const_iterator;

  /// \name Constructors
  /// @{
  /// Construct an empty set of MMRA tags.
  MMRAMetadata() = default;
  /// Parse the !mmra metadata attached to instruction \p I.
  /// \param I Instruction whose \c !mmra metadata is parsed.
  LLVM_ABI MMRAMetadata(const Instruction &I);
  /// Parse !mmra metadata from node \p MD.
  /// \param MD Metadata node to parse, or null for an empty set.
  LLVM_ABI MMRAMetadata(MDNode *MD);
  /// @}

  /// \name Metadata Helpers & Builders
  /// @{

  /// Combines \p A and \p B according to MMRA semantics.
  /// \param Ctx LLVM context used to create the combined metadata.
  /// \param A First set of MMRA tags.
  /// \param B Second set of MMRA tags.
  /// \returns !mmra metadata for the combined MMRAs.
  LLVM_ABI static MDNode *combine(LLVMContext &Ctx, const MMRAMetadata &A,
                                  const MMRAMetadata &B);

  /// Creates !mmra metadata for a single tag.
  ///
  /// !mmra metadata can either be a single tag, or a MDTuple containing
  /// multiple tags.
  /// \param Ctx LLVM context used to create the metadata.
  /// \param Prefix Tag prefix string.
  /// \param Suffix Tag suffix string.
  /// \returns !mmra metadata tuple for the single tag.
  LLVM_ABI static MDTuple *getTagMD(LLVMContext &Ctx, StringRef Prefix,
                                    StringRef Suffix);
  /// Creates !mmra metadata for the prefix/suffix pair \p T.
  /// \param Ctx LLVM context used to create the metadata.
  /// \param T Tag as a prefix/suffix pair.
  /// \returns !mmra metadata tuple for \p T.
  static MDTuple *getTagMD(LLVMContext &Ctx, const TagT &T) {
    return getTagMD(Ctx, T.first, T.second);
  }

  /// Creates !mmra metadata from \p Tags.
  /// \param Ctx LLVM context used to create the metadata.
  /// \param Tags Tags to encode in the metadata.
  /// \returns nullptr or a MDTuple* from \p Tags.
  LLVM_ABI static MDTuple *getMD(LLVMContext &Ctx, ArrayRef<TagT> Tags);

  /// Return true if \p MD is a well-formed MMRA tag.
  /// \param MD Metadata node to inspect.
  /// \returns True if \p MD is a well-formed MMRA tag.
  LLVM_ABI static bool isTagMD(const Metadata *MD);

  /// Appends \p Tags to the !mmra metadata on \p I,
  /// merging with any existing MMRA metadata.
  /// \param I Instruction whose \c !mmra metadata is updated.
  /// \param Tags Tags to append.
  LLVM_ABI static void appendTags(Instruction &I, ArrayRef<TagT> Tags);

  /// @}

  /// \name Compatibility Helpers
  /// @{

  /// Return whether the MMRAs on \p A and \p B are compatible.
  /// \param A First instruction whose MMRAs are compared.
  /// \param B Second instruction whose MMRAs are compared.
  /// \returns True if the MMRAs on \p A and \p B are compatible.
  static bool checkCompatibility(const Instruction &A, const Instruction &B) {
    return MMRAMetadata(A).isCompatibleWith(B);
  }

  /// Return whether this set of tags is compatible with \p Other.
  /// \param Other The other set of MMRA tags.
  /// \returns True if this set of tags is compatible with \p Other.
  LLVM_ABI bool isCompatibleWith(const MMRAMetadata &Other) const;

  /// @}

  /// \name Content Queries
  /// @{

  /// Return true if this set contains the tag (\p Prefix, \p Suffix).
  /// \param Prefix Tag prefix to look up.
  /// \param Suffix Tag suffix to look up.
  /// \returns True if this set contains the given tag.
  LLVM_ABI bool hasTag(StringRef Prefix, StringRef Suffix) const;
  /// Return true if this set contains any tag with prefix \p Prefix.
  /// \param Prefix Tag prefix to look up.
  /// \returns True if this set contains any tag with \p Prefix.
  LLVM_ABI bool hasTagWithPrefix(StringRef Prefix) const;

  /// Return an iterator to the first tag.
  /// \returns Iterator to the first tag.
  LLVM_ABI const_iterator begin() const;
  /// Return an iterator past the last tag.
  /// \returns Iterator past the last tag.
  LLVM_ABI const_iterator end() const;
  /// Return true if this set contains no tags.
  /// \returns True if this set contains no tags.
  LLVM_ABI bool empty() const;
  /// Return the number of tags in this set.
  /// \returns The number of tags in this set.
  LLVM_ABI unsigned size() const;

  /// @}

  /// Print this set of tags to \p OS.
  /// \param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Dump this set of tags to stderr for debugging.
  LLVM_ABI void dump() const;

  /// Return true if this set of tags is non-empty.
  /// \returns True if this set of tags is non-empty.
  operator bool() const { return !Tags.empty(); }
  /// Return true if this set of tags equals \p Other.
  /// \param Other The other set of MMRA tags.
  /// \returns True if this set of tags equals \p Other.
  bool operator==(const MMRAMetadata &Other) const {
    return Tags == Other.Tags;
  }
  /// Return true if this set of tags differs from \p Other.
  /// \param Other The other set of MMRA tags.
  /// \returns True if this set of tags differs from \p Other.
  bool operator!=(const MMRAMetadata &Other) const {
    return Tags != Other.Tags;
  }

private:
  SetT Tags;
};

/// Return true if \p I can have !mmra metadata.
/// \param I Instruction to inspect.
/// \returns True if \p I can have !mmra metadata.
LLVM_ABI bool canInstructionHaveMMRAs(const Instruction &I);

} // namespace llvm

#endif
