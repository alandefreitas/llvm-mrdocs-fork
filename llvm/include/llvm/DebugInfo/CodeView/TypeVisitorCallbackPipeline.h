//===- TypeVisitorCallbackPipeline.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPEVISITORCALLBACKPIPELINE_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPEVISITORCALLBACKPIPELINE_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/Support/Error.h"
#include <vector>

namespace llvm {
namespace codeview {

/// Pipelines multiple TypeVisitorCallbacks, forwarding each visit in order.
class TypeVisitorCallbackPipeline : public TypeVisitorCallbacks {
public:
  /// Construct an empty type visitor callback pipeline.
  TypeVisitorCallbackPipeline() = default;

  /// Forward an unknown type record to every callback in the pipeline.
  ///
  /// \param Record The unknown type record being visited.
  ///
  /// \returns The first error from a callback, or success if all succeed.
  Error visitUnknownType(CVRecord<TypeLeafKind> &Record) override {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitUnknownType(Record))
        return EC;
    }
    return Error::success();
  }

  /// Forward an unknown member record to every callback in the pipeline.
  ///
  /// \param Record The unknown member record being visited.
  ///
  /// \returns The first error from a callback, or success if all succeed.
  Error visitUnknownMember(CVMemberRecord &Record) override {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitUnknownMember(Record))
        return EC;
    }
    return Error::success();
  }

  /// Forward type-begin to every callback without a type index.
  ///
  /// \param Record The type record whose visitation is beginning.
  ///
  /// \returns The first error from a callback, or success if all succeed.
  Error visitTypeBegin(CVType &Record) override {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitTypeBegin(Record))
        return EC;
    }
    return Error::success();
  }

  /// Forward type-begin to every callback with the record's type index.
  ///
  /// \param Record The type record whose visitation is beginning.
  /// \param Index Type index of \p Record in the type stream.
  ///
  /// \returns The first error from a callback, or success if all succeed.
  Error visitTypeBegin(CVType &Record, TypeIndex Index) override {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitTypeBegin(Record, Index))
        return EC;
    }
    return Error::success();
  }

  /// Forward type-end to every callback in the pipeline.
  ///
  /// \param Record The type record whose visitation is complete.
  ///
  /// \returns The first error from a callback, or success if all succeed.
  Error visitTypeEnd(CVType &Record) override {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitTypeEnd(Record))
        return EC;
    }
    return Error::success();
  }

  /// Forward member-begin to every callback in the pipeline.
  ///
  /// \param Record The member record whose visitation is beginning.
  ///
  /// \returns The first error from a callback, or success if all succeed.
  Error visitMemberBegin(CVMemberRecord &Record) override {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitMemberBegin(Record))
        return EC;
    }
    return Error::success();
  }

  /// Forward member-end to every callback in the pipeline.
  ///
  /// \param Record The member record whose visitation is complete.
  ///
  /// \returns The first error from a callback, or success if all succeed.
  Error visitMemberEnd(CVMemberRecord &Record) override {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitMemberEnd(Record))
        return EC;
    }
    return Error::success();
  }

  /// Append \p Callbacks to the end of this pipeline.
  ///
  /// \param Callbacks Visitor callbacks to invoke on subsequent visits.
  void addCallbackToPipeline(TypeVisitorCallbacks &Callbacks) {
    Pipeline.push_back(&Callbacks);
  }

#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  Error visitKnownRecord(CVType &CVR, Name##Record &Record) override {         \
    return visitKnownRecordImpl(CVR, Record);                                  \
  }
#define MEMBER_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownMember(CVMemberRecord &CVMR, Name##Record &Record)           \
      override {                                                               \
    return visitKnownMemberImpl(CVMR, Record);                                 \
  }
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"

private:
  template <typename T> Error visitKnownRecordImpl(CVType &CVR, T &Record) {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitKnownRecord(CVR, Record))
        return EC;
    }
    return Error::success();
  }

  template <typename T>
  Error visitKnownMemberImpl(CVMemberRecord &CVMR, T &Record) {
    for (auto *Visitor : Pipeline) {
      if (auto EC = Visitor->visitKnownMember(CVMR, Record))
        return EC;
    }
    return Error::success();
  }
  std::vector<TypeVisitorCallbacks *> Pipeline;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_TYPEVISITORCALLBACKPIPELINE_H
