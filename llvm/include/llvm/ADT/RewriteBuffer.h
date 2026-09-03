//===- RewriteBuffer.h - Buffer rewriting interface -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_REWRITEBUFFER_H
#define LLVM_ADT_REWRITEBUFFER_H

#include "llvm/ADT/DeltaTree.h"
#include "llvm/ADT/RewriteRope.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"

/// Clang frontend types that interact with LLVM rewrite buffers.
namespace clang {
/// Rewrites source text using RewriteBuffer instances.
class Rewriter;
} // namespace clang

namespace llvm {

class raw_ostream;

/// Buffer holding rewritten source text and original-to-rewritten offset maps.
///
/// As code is rewritten, SourceBuffers from the original input with
/// modifications get a RewriteBuffer associated with them. The RewriteBuffer
/// captures the modified text itself as well as information used to map between
/// SourceLocations in the original input and offsets in the RewriteBuffer. For
/// example, if text is inserted into the buffer, any locations after the
/// insertion point have to be mapped.
class RewriteBuffer {
  friend class clang::Rewriter;

  /// Deltas - Keep track of all the deltas in the source code due to insertions
  /// and deletions.
  DeltaTree Deltas;

  RewriteRope Buffer;

public:
  /// Const iterator over characters in the rewritten buffer.
  using iterator = RewriteRope::const_iterator;

  /// Iterator to the first character of the rewritten text.
  ///
  /// \return Iterator to the first character.
  iterator begin() const { return Buffer.begin(); }
  /// Iterator one past the last character of the rewritten text.
  ///
  /// \return Past-the-end iterator over the rewritten text.
  iterator end() const { return Buffer.end(); }
  /// Number of characters currently stored in the rewritten buffer.
  ///
  /// \return The number of characters in the rewritten buffer.
  unsigned size() const { return Buffer.size(); }

  /// Initialize this rewrite buffer with a copy of the unmodified input.
  ///
  /// \param BufStart Pointer to the first character of the input buffer.
  /// \param BufEnd Pointer one past the last character of the input buffer.
  void Initialize(const char *BufStart, const char *BufEnd) {
    Buffer.assign(BufStart, BufEnd);
  }
  /// Initialize from the characters in \p Input.
  ///
  /// \param Input Source text used to seed this rewrite buffer.
  void Initialize(StringRef Input) { Initialize(Input.begin(), Input.end()); }

  /// Write to \p Stream the result of applying all changes to the original
  /// buffer.
  ///
  /// Note that it isn't safe to use this function to overwrite memory mapped
  /// files in-place (PR17960). Consider using a higher-level utility such as
  /// Rewriter::overwriteChangedFiles() instead.
  ///
  /// The original buffer is not actually changed.
  ///
  /// \param Stream Output stream that receives the rewritten text.
  /// \return Reference to \p Stream after writing the rewritten text.
  LLVM_ABI raw_ostream &write(raw_ostream &Stream) const;

  /// Remove the specified text from the buffer.
  ///
  /// \param OrigOffset Offset into the original SourceBuffer where removal
  /// begins.
  /// \param Size Number of characters to remove.
  /// \param removeLineIfEmpty If true, also remove the enclosing line when it
  /// becomes empty after the removal.
  LLVM_ABI void RemoveText(unsigned OrigOffset, unsigned Size,
                           bool removeLineIfEmpty = false);

  /// Insert text at an offset relative to the original SourceBuffer.
  ///
  /// The text is inserted after the specified location by default.
  ///
  /// \param OrigOffset Offset into the original SourceBuffer where text is
  /// inserted.
  /// \param Str Text to insert.
  /// \param InsertAfter If true, insert after \p OrigOffset; otherwise insert
  /// before it.
  LLVM_ABI void InsertText(unsigned OrigOffset, StringRef Str,
                           bool InsertAfter = true);

  /// Insert \p Str before original offset \p OrigOffset.
  ///
  /// Same as InsertText with InsertAfter == false.
  ///
  /// \param OrigOffset Offset into the original SourceBuffer before which text
  /// is inserted.
  /// \param Str Text to insert.
  void InsertTextBefore(unsigned OrigOffset, StringRef Str) {
    InsertText(OrigOffset, Str, false);
  }

  /// Insert \p Str after original offset \p OrigOffset.
  ///
  /// \param OrigOffset Offset into the original SourceBuffer after which text
  /// is inserted.
  /// \param Str Text to insert.
  void InsertTextAfter(unsigned OrigOffset, StringRef Str) {
    InsertText(OrigOffset, Str);
  }

  /// Replace a range of characters in the input buffer with a new string.
  ///
  /// This is effectively a combined remove/insert operation.
  ///
  /// \param OrigOffset Offset into the original SourceBuffer where replacement
  /// begins.
  /// \param OrigLength Number of characters in the original buffer to replace.
  /// \param NewStr Replacement text.
  LLVM_ABI void ReplaceText(unsigned OrigOffset, unsigned OrigLength,
                            StringRef NewStr);

private:
  /// getMappedOffset - Given an offset into the original SourceBuffer that this
  /// RewriteBuffer is based on, map it into the offset space of the
  /// RewriteBuffer.  If AfterInserts is true and if the OrigOffset indicates a
  /// position where text is inserted, the location returned will be after any
  /// inserted text at the position.
  unsigned getMappedOffset(unsigned OrigOffset,
                           bool AfterInserts = false) const {
    return Deltas.getDeltaAt(2 * OrigOffset + AfterInserts) + OrigOffset;
  }

  /// AddInsertDelta - When an insertion is made at a position, this
  /// method is used to record that information.
  void AddInsertDelta(unsigned OrigOffset, int Change) {
    return Deltas.AddDelta(2 * OrigOffset, Change);
  }

  /// AddReplaceDelta - When a replacement/deletion is made at a position, this
  /// method is used to record that information.
  void AddReplaceDelta(unsigned OrigOffset, int Change) {
    return Deltas.AddDelta(2 * OrigOffset + 1, Change);
  }
};

} // namespace llvm

#endif // LLVM_ADT_REWRITEBUFFER_H
