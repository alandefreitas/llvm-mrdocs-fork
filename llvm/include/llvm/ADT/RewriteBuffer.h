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
  iterator begin() const { return Buffer.begin(); }
  /// Iterator one past the last character of the rewritten text.
  iterator end() const { return Buffer.end(); }
  /// Number of characters currently stored in the rewritten buffer.
  unsigned size() const { return Buffer.size(); }

  /// Initialize - Start this rewrite buffer out with a copy of the unmodified
  /// input buffer.
  void Initialize(const char *BufStart, const char *BufEnd) {
    Buffer.assign(BufStart, BufEnd);
  }
  /// Initialize from the characters in \p Input.
  void Initialize(StringRef Input) { Initialize(Input.begin(), Input.end()); }

  /// Write to \p Stream the result of applying all changes to the
  /// original buffer.
  /// Note that it isn't safe to use this function to overwrite memory mapped
  /// files in-place (PR17960). Consider using a higher-level utility such as
  /// Rewriter::overwriteChangedFiles() instead.
  ///
  /// The original buffer is not actually changed.
  LLVM_ABI raw_ostream &write(raw_ostream &Stream) const;

  /// RemoveText - Remove the specified text.
  LLVM_ABI void RemoveText(unsigned OrigOffset, unsigned Size,
                           bool removeLineIfEmpty = false);

  /// InsertText - Insert some text at the specified point, where the offset in
  /// the buffer is specified relative to the original SourceBuffer.  The
  /// text is inserted after the specified location.
  LLVM_ABI void InsertText(unsigned OrigOffset, StringRef Str,
                           bool InsertAfter = true);

  /// Insert \p Str before original offset \p OrigOffset.
  ///
  /// Same as InsertText with InsertAfter == false.
  void InsertTextBefore(unsigned OrigOffset, StringRef Str) {
    InsertText(OrigOffset, Str, false);
  }

  /// Insert \p Str after original offset \p OrigOffset.
  void InsertTextAfter(unsigned OrigOffset, StringRef Str) {
    InsertText(OrigOffset, Str);
  }

  /// ReplaceText - This method replaces a range of characters in the input
  /// buffer with a new string.  This is effectively a combined "remove/insert"
  /// operation.
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
