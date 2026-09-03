//===- DebugFrameDataSubsection.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGFRAMEDATASUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGFRAMEDATASUBSECTION_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

namespace llvm {
class BinaryStreamReader;
class BinaryStreamWriter;

namespace codeview {
/// Read-only view of a CodeView FrameData debug subsection.
class DebugFrameDataSubsectionRef final : public DebugSubsectionRef {
public:
  /// Construct an empty, uninitialized frame-data subsection reference.
  DebugFrameDataSubsectionRef()
      : DebugSubsectionRef(DebugSubsectionKind::FrameData) {}
  /// Return true if \p S is a FrameData subsection.
  ///
  /// \param S Subsection to test.
  ///
  /// \returns True if \p S is a FrameData subsection.
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::FrameData;
  }

  /// Initialize this view from frame data read via \p Reader.
  ///
  /// \param Reader Reader positioned at the start of the frame-data subsection.
  ///
  /// \returns An Error on failure, or success if initialization succeeded.
  LLVM_ABI Error initialize(BinaryStreamReader Reader);
  /// Initialize this view from the frame data in \p Stream.
  ///
  /// \param Stream Stream containing the serialized frame-data subsection.
  ///
  /// \returns An Error on failure, or success if initialization succeeded.
  LLVM_ABI Error initialize(BinaryStreamRef Stream);

  /// Return an iterator to the first frame-data entry.
  ///
  /// \returns An iterator to the first frame-data entry.
  FixedStreamArray<FrameData>::Iterator begin() const { return Frames.begin(); }
  /// Return an iterator past the last frame-data entry.
  ///
  /// \returns An iterator past the last frame-data entry.
  FixedStreamArray<FrameData>::Iterator end() const { return Frames.end(); }

  /// Return a pointer to the optional relocation field, or null if absent.
  ///
  /// \returns A pointer to the optional relocation field, or null if absent.
  const support::ulittle32_t *getRelocPtr() const { return RelocPtr; }

private:
  const support::ulittle32_t *RelocPtr = nullptr;
  FixedStreamArray<FrameData> Frames;
};

/// Writable CodeView FrameData debug subsection.
class LLVM_ABI DebugFrameDataSubsection final : public DebugSubsection {
public:
  /// Construct a frame-data subsection, optionally including a reloc pointer.
  ///
  /// \param IncludeRelocPtr Whether to serialize the optional relocation field.
  DebugFrameDataSubsection(bool IncludeRelocPtr)
      : DebugSubsection(DebugSubsectionKind::FrameData),
        IncludeRelocPtr(IncludeRelocPtr) {}
  /// Return true if \p S is a FrameData subsection.
  ///
  /// \param S Subsection to test.
  ///
  /// \returns True if \p S is a FrameData subsection.
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::FrameData;
  }

  /// Return the serialized size of this subsection in bytes.
  ///
  /// \returns The serialized size of this subsection in bytes.
  uint32_t calculateSerializedSize() const override;
  /// Write this subsection's serialized form to \p Writer.
  ///
  /// \param Writer Destination stream writer.
  ///
  /// \returns An Error on failure, or success if the write completed.
  Error commit(BinaryStreamWriter &Writer) const override;

  /// Append \p Frame to the list of frame-data entries.
  ///
  /// \param Frame Frame-data entry to append.
  void addFrameData(const FrameData &Frame);
  /// Replace the subsection's frame-data entries with \p Frames.
  ///
  /// \param Frames New frame-data entries for this subsection.
  void setFrames(ArrayRef<FrameData> Frames);

private:
  bool IncludeRelocPtr = false;
  std::vector<FrameData> Frames;
};
}
}

#endif
