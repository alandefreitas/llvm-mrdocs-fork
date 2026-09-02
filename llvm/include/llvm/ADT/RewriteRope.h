//===- RewriteRope.h - Rope specialized for rewriter ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the RewriteRope class, which is a powerful string class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_REWRITEROPE_H
#define LLVM_ADT_REWRITEROPE_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <utility>

namespace llvm {

//===--------------------------------------------------------------------===//
// RopeRefCountString Class
//===--------------------------------------------------------------------===//

/// Reference-counted character buffer for rope pieces.
///
/// Allocated with \c new char[] and deleted when the ref count reaches zero.
/// Managed primarily through \c RopePiece.
struct RopeRefCountString {
  /// Number of RopePiece views that currently share this buffer.
  unsigned RefCount;
  /// Flexible array holding the reference-counted character payload.
  char Data[1]; //  Variable sized.

  /// Add one reference to this buffer.
  void Retain() { ++RefCount; }

  /// Drop one reference and delete the buffer when the count reaches zero.
  void Release() {
    assert(RefCount > 0 && "Reference count is already zero.");
    if (--RefCount == 0)
      delete[] (char *)this;
  }
};

//===--------------------------------------------------------------------===//
// RopePiece Class
//===--------------------------------------------------------------------===//

/// View into a shared RopeRefCountString slice.
///
/// Large pieces can be split by creating two views over the same buffer with
/// different offsets, without copying character data.
struct RopePiece {
  /// Shared backing string for this slice.
  llvm::IntrusiveRefCntPtr<RopeRefCountString> StrData;
  /// Inclusive start offset into \c StrData->Data.
  unsigned StartOffs = 0;
  /// Exclusive end offset into \c StrData->Data.
  unsigned EndOffs = 0;

  /// Construct an empty piece with no backing string.
  RopePiece() = default;
  /// Construct a piece spanning [\p Start, \p End) in shared string \p Str.
  ///
  /// \param Str Reference-counted character buffer.
  /// \param Start First offset included in this piece.
  /// \param End One-past-last offset included in this piece.
  RopePiece(llvm::IntrusiveRefCntPtr<RopeRefCountString> Str, unsigned Start,
            unsigned End)
      : StrData(std::move(Str)), StartOffs(Start), EndOffs(End) {}

  /// Access the character at offset \p Offset within this piece.
  ///
  /// \param Offset Zero-based offset relative to \c StartOffs.
  const char &operator[](unsigned Offset) const {
    return StrData->Data[Offset + StartOffs];
  }
  /// Access the character at offset \p Offset within this piece.
  ///
  /// \param Offset Zero-based offset relative to \c StartOffs.
  char &operator[](unsigned Offset) {
    return StrData->Data[Offset + StartOffs];
  }

  /// Return the number of characters in this piece.
  unsigned size() const { return EndOffs - StartOffs; }
};

//===--------------------------------------------------------------------===//
// RopePieceBTreeIterator Class
//===--------------------------------------------------------------------===//

/// Forward iterator over bytes stored in a RopePieceBTree.
///
/// Walks characters inside a piece, then pieces in a leaf, then leaves in the
/// B+ tree.
class RopePieceBTreeIterator {
  /// CurNode - The current B+Tree node that we are inspecting.
  const void /*RopePieceBTreeLeaf*/ *CurNode = nullptr;

  /// CurPiece - The current RopePiece in the B+Tree node that we're
  /// inspecting.
  const RopePiece *CurPiece = nullptr;

  /// CurChar - The current byte in the RopePiece we are pointing to.
  unsigned CurChar = 0;

public:
  /// Category identifying this as a forward iterator.
  using iterator_category = std::forward_iterator_tag;
  /// Character type yielded while walking the rope.
  using value_type = const char;
  /// Signed distance type required by the iterator concept.
  using difference_type = std::ptrdiff_t;
  /// Pointer type for iterator traits.
  using pointer = value_type *;
  /// Reference type for iterator traits.
  using reference = value_type &;

  /// Construct a singular/end iterator.
  RopePieceBTreeIterator() = default;
  /// Construct an iterator positioned at the first byte under tree node \p N.
  ///
  /// \param N Opaque RopePieceBTree node pointer, or null for end.
  LLVM_ABI RopePieceBTreeIterator(const void /*RopePieceBTreeNode*/ *N);

  /// Return the current character in the rope.
  char operator*() const { return (*CurPiece)[CurChar]; }

  /// Return true if both iterators point at the same character position.
  bool operator==(const RopePieceBTreeIterator &RHS) const {
    return CurPiece == RHS.CurPiece && CurChar == RHS.CurChar;
  }
  /// Return true if the iterators point at different positions.
  bool operator!=(const RopePieceBTreeIterator &RHS) const {
    return !operator==(RHS);
  }

  /// Advance to the next character in the rope.
  RopePieceBTreeIterator &operator++() { // Preincrement
    if (CurChar + 1 < CurPiece->size())
      ++CurChar;
    else
      MoveToNextPiece();
    return *this;
  }

  /// Advance to the next character, returning the prior iterator value.
  RopePieceBTreeIterator operator++(int) { // Postincrement
    RopePieceBTreeIterator tmp = *this;
    ++*this;
    return tmp;
  }

  /// Return the remaining unread bytes of the current RopePiece as a StringRef.
  llvm::StringRef piece() const {
    return llvm::StringRef(&(*CurPiece)[0], CurPiece->size());
  }

  /// Advance from the end of the current piece to the first byte of the next.
  LLVM_ABI void MoveToNextPiece();
};

//===--------------------------------------------------------------------===//
// RopePieceBTree Class
//===--------------------------------------------------------------------===//

/// B+ tree of RopePiece chunks that stores a rope as ordered byte ranges.
class RopePieceBTree {
  void /*RopePieceBTreeNode*/ *Root;

public:
  /// Construct an empty rope piece tree.
  LLVM_ABI RopePieceBTree();
  /// Copy-construct from another rope piece tree.
  LLVM_ABI RopePieceBTree(const RopePieceBTree &RHS);
  /// Copy assignment is deleted.
  RopePieceBTree &operator=(const RopePieceBTree &) = delete;
  /// Destroy the tree and free its nodes.
  LLVM_ABI ~RopePieceBTree();

  /// Forward iterator over characters stored in the tree.
  using iterator = RopePieceBTreeIterator;

  /// Iterator to the first character, or end if empty.
  iterator begin() const { return iterator(Root); }
  /// Past-the-end character iterator.
  iterator end() const { return iterator(); }
  /// Return the number of characters stored in the tree.
  LLVM_ABI unsigned size() const;
  /// Return non-zero if the tree stores no characters.
  unsigned empty() const { return size() == 0; }

  /// Remove all pieces from the tree.
  LLVM_ABI void clear();

  /// Insert rope piece \p R at character offset \p Offset.
  ///
  /// \param Offset Insertion point measured in characters from the start.
  /// \param R Piece whose bytes are inserted.
  LLVM_ABI void insert(unsigned Offset, const RopePiece &R);

  /// Erase \p NumBytes characters beginning at \p Offset.
  ///
  /// \param Offset First character to remove.
  /// \param NumBytes Number of characters to erase.
  LLVM_ABI void erase(unsigned Offset, unsigned NumBytes);
};

//===--------------------------------------------------------------------===//
// RewriteRope Class
//===--------------------------------------------------------------------===//

/// Rope that supports efficient mid-string insert and erase.
class RewriteRope {
  RopePieceBTree Chunks;

  /// We allocate space for string data out of a buffer of size AllocChunkSize.
  /// This keeps track of how much space is left.
  llvm::IntrusiveRefCntPtr<RopeRefCountString> AllocBuffer;
  enum { AllocChunkSize = 4080 };
  unsigned AllocOffs = AllocChunkSize;

public:
  /// Construct an empty rewrite rope.
  RewriteRope() = default;
  /// Copy-construct from another rewrite rope, sharing piece storage.
  RewriteRope(const RewriteRope &RHS) : Chunks(RHS.Chunks) {}

  /// Copy assignment is deleted.
  RewriteRope &operator=(const RewriteRope &) = delete;

  /// Character iterator over the rope contents.
  using iterator = RopePieceBTree::iterator;
  /// Const character iterator over the rope contents.
  using const_iterator = RopePieceBTree::iterator;

  /// Iterator to the first character.
  iterator begin() const { return Chunks.begin(); }
  /// Past-the-end character iterator.
  iterator end() const { return Chunks.end(); }
  /// Return the number of characters in the rope.
  unsigned size() const { return Chunks.size(); }

  /// Remove all characters from the rope.
  void clear() { Chunks.clear(); }

  /// Replace the rope contents with the byte range [\p Start, \p End).
  ///
  /// \param Start Pointer to the first character to assign.
  /// \param End One-past-last character to assign.
  void assign(const char *Start, const char *End) {
    clear();
    if (Start != End)
      Chunks.insert(0, MakeRopeString(Start, End));
  }

  /// Insert the byte range [\p Start, \p End) at character offset \p Offset.
  ///
  /// \param Offset Insertion point measured from the start of the rope.
  /// \param Start Pointer to the first character to insert.
  /// \param End One-past-last character to insert.
  void insert(unsigned Offset, const char *Start, const char *End) {
    assert(Offset <= size() && "Invalid position to insert!");
    if (Start == End)
      return;
    Chunks.insert(Offset, MakeRopeString(Start, End));
  }

  /// Erase \p NumBytes characters beginning at \p Offset.
  ///
  /// \param Offset First character to remove.
  /// \param NumBytes Number of characters to erase.
  void erase(unsigned Offset, unsigned NumBytes) {
    assert(Offset + NumBytes <= size() && "Invalid region to erase!");
    if (NumBytes == 0)
      return;
    Chunks.erase(Offset, NumBytes);
  }

private:
  LLVM_ABI RopePiece MakeRopeString(const char *Start, const char *End);
};

} // namespace llvm

#endif // LLVM_ADT_REWRITEROPE_H
