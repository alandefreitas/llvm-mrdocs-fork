//===--- Utility.h -------------------*- mode:c++;eval:(read-only-mode) -*-===//
//       Do not edit! See README.txt.
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provide some utility classes for use in the demangler.
// There are two copies of this file in the source tree.  The one in libcxxabi
// is the original and the one in llvm is the copy.  Use cp-to-llvm.sh to update
// the copy.  See README.txt for more details.
//
//===----------------------------------------------------------------------===//

#ifndef DEMANGLE_UTILITY_H
#define DEMANGLE_UTILITY_H

#include "DemangleConfig.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>

DEMANGLE_NAMESPACE_BEGIN

class Node;

/// Stream that AST nodes write their string representation into after the AST
/// has been parsed.
class DEMANGLE_ABI OutputBuffer {
  char *Buffer = nullptr;
  size_t CurrentPosition = 0;
  size_t BufferCapacity = 0;

  // Ensure there are at least N more positions in the buffer.
  void grow(size_t N) {
    size_t Need = N + CurrentPosition;
    if (Need > BufferCapacity) {
      // Reduce the number of reallocations, with a bit of hysteresis. The
      // number here is chosen so the first allocation will more-than-likely not
      // allocate more than 1K.
      Need += 1024 - 32;
      BufferCapacity *= 2;
      if (BufferCapacity < Need)
        BufferCapacity = Need;
      Buffer = static_cast<char *>(std::realloc(Buffer, BufferCapacity));
      if (Buffer == nullptr)
        std::abort();
    }
  }

  OutputBuffer &writeUnsigned(uint64_t N, bool isNeg = false) {
    std::array<char, 21> Temp;
    char *TempPtr = Temp.data() + Temp.size();

    // Output at least one character.
    do {
      *--TempPtr = char('0' + N % 10);
      N /= 10;
    } while (N);

    // Add negative sign.
    if (isNeg)
      *--TempPtr = '-';

    return operator+=(
        std::string_view(TempPtr, Temp.data() + Temp.size() - TempPtr));
  }

public:
  /// Construct an output buffer over the caller-owned range [\p StartBuf,
  /// \p StartBuf + \p Size).
  /// \param StartBuf Existing buffer storage, or null.
  /// \param Size Capacity of \p StartBuf in bytes.
  OutputBuffer(char *StartBuf, size_t Size)
      : Buffer(StartBuf), BufferCapacity(Size) {}
  /// Construct an output buffer whose capacity is taken from \p *SizePtr.
  /// \param StartBuf Existing buffer storage, or null.
  /// \param SizePtr Pointer to the capacity; treated as zero when null or when
  ///        \p StartBuf is null.
  OutputBuffer(char *StartBuf, size_t *SizePtr)
      : OutputBuffer(StartBuf, StartBuf ? *SizePtr : 0) {}
  /// Construct an empty output buffer with no storage.
  OutputBuffer() = default;
  /// Copy construction is deleted.
  /// \param Other Unused; copy construction is deleted.
  OutputBuffer(const OutputBuffer &Other) = delete;
  /// Copy assignment is deleted.
  /// \param Other Unused; copy assignment is deleted.
  OutputBuffer &operator=(const OutputBuffer &Other) = delete;

  /// Destroy the output buffer without freeing \c Buffer.
  virtual ~OutputBuffer() = default;

  /// Return the written contents as a string view.
  /// \returns A view of the written buffer contents.
  operator std::string_view() const {
    return std::string_view(Buffer, CurrentPosition);
  }

  /// Print the left-hand portion of \p N into this buffer.
  ///
  /// Called by the demangler when printing the demangle tree. By default
  /// calls into \c Node::printLeft but can be overridden by clients to
  /// track additional state when printing the demangled name.
  /// \param N AST node to print.
  virtual void printLeft(const Node &N);
  /// Print the right-hand portion of \p N into this buffer.
  ///
  /// By default calls into \c Node::printRight but can be overridden by
  /// clients to track additional state when printing the demangled name.
  /// \param N AST node to print.
  virtual void printRight(const Node &N);

  /// Called when we write to this object anywhere other than the end.
  /// \param Position Byte offset where the insertion begins.
  /// \param Count Number of bytes inserted at \p Position.
  virtual void notifyInsertion(size_t Position, size_t Count) {}

  /// Called when we make the \c CurrentPosition of this object smaller.
  /// \param OldPos Position before the deletion.
  /// \param NewPos Position after the deletion.
  virtual void notifyDeletion(size_t OldPos, size_t NewPos) {}

  /// If a ParameterPackExpansion (or similar type) is encountered, the offset
  /// into the pack that we're currently printing.
  unsigned CurrentPackIndex = std::numeric_limits<unsigned>::max();
  /// Number of elements in the parameter pack currently being printed.
  unsigned CurrentPackMax = std::numeric_limits<unsigned>::max();

  /// State for template-argument paren nesting while printing.
  struct {
    /// The depth of '(' and ')' inside the currently printed template
    /// arguments.
    unsigned ParenDepth = 0;

    /// True if we're currently printing a template argument.
    bool InsideTemplate = false;
  } TemplateTracker; ///< Whether printing is inside template arguments.

  /// Returns true if we're currently between a '(' and ')' when printing
  /// template args.
  /// \returns True when printing template args inside parentheses.
  bool isInParensInTemplateArgs() const {
    return TemplateTracker.ParenDepth > 0;
  }

  /// Returns true if we're printing template args.
  /// \returns True when currently printing a template argument.
  bool isInsideTemplateArgs() const { return TemplateTracker.InsideTemplate; }

  /// Append \p Open and, inside template args, increment the paren depth.
  /// \param Open Opening delimiter character to append.
  void printOpen(char Open = '(') {
    if (isInsideTemplateArgs())
      TemplateTracker.ParenDepth++;
    *this += Open;
  }
  /// Append \p Close and, inside template args, decrement the paren depth.
  /// \param Close Closing delimiter character to append.
  void printClose(char Close = ')') {
    if (isInsideTemplateArgs())
      TemplateTracker.ParenDepth--;
    *this += Close;
  }

  /// Append the characters of \p R to the end of the buffer.
  /// \param R String view to append.
  /// \returns Reference to this buffer.
  OutputBuffer &operator+=(std::string_view R) {
    if (size_t Size = R.size()) {
      grow(Size);
      std::memcpy(Buffer + CurrentPosition, &*R.begin(), Size);
      CurrentPosition += Size;
    }
    return *this;
  }

  /// Append the character \p C to the end of the buffer.
  /// \param C Character to append.
  /// \returns Reference to this buffer.
  OutputBuffer &operator+=(char C) {
    grow(1);
    Buffer[CurrentPosition++] = C;
    return *this;
  }

  /// Insert \p R at the front of the buffer and notify listeners.
  /// \param R String view to prepend.
  /// \returns Reference to this buffer.
  OutputBuffer &prepend(std::string_view R) {
    size_t Size = R.size();
    if (!Size)
      return *this;

    grow(Size);
    std::memmove(Buffer + Size, Buffer, CurrentPosition);
    std::memcpy(Buffer, &*R.begin(), Size);
    CurrentPosition += Size;

    notifyInsertion(/*Position=*/0, /*Count=*/Size);

    return *this;
  }

  /// Append the characters of \p R to the end of the buffer.
  /// \param R String view to append.
  /// \returns Reference to this buffer.
  OutputBuffer &operator<<(std::string_view R) { return (*this += R); }

  /// Append the character \p C to the end of the buffer.
  /// \param C Character to append.
  /// \returns Reference to this buffer.
  OutputBuffer &operator<<(char C) { return (*this += C); }

  /// Append the decimal representation of \p N to the buffer.
  /// \param N Signed value to print.
  /// \returns Reference to this buffer.
  OutputBuffer &operator<<(long long N) {
    return writeUnsigned(static_cast<unsigned long long>(std::abs(N)), N < 0);
  }

  /// Append the decimal representation of \p N to the buffer.
  /// \param N Unsigned value to print.
  /// \returns Reference to this buffer.
  OutputBuffer &operator<<(unsigned long long N) {
    return writeUnsigned(N, false);
  }

  /// Append the decimal representation of \p N to the buffer.
  /// \param N Signed value to print.
  /// \returns Reference to this buffer.
  OutputBuffer &operator<<(long N) {
    return this->operator<<(static_cast<long long>(N));
  }

  /// Append the decimal representation of \p N to the buffer.
  /// \param N Unsigned value to print.
  /// \returns Reference to this buffer.
  OutputBuffer &operator<<(unsigned long N) {
    return this->operator<<(static_cast<unsigned long long>(N));
  }

  /// Append the decimal representation of \p N to the buffer.
  /// \param N Signed value to print.
  /// \returns Reference to this buffer.
  OutputBuffer &operator<<(int N) {
    return this->operator<<(static_cast<long long>(N));
  }

  /// Append the decimal representation of \p N to the buffer.
  /// \param N Unsigned value to print.
  /// \returns Reference to this buffer.
  OutputBuffer &operator<<(unsigned int N) {
    return this->operator<<(static_cast<unsigned long long>(N));
  }

  /// Insert \p N bytes from \p S at byte offset \p Pos.
  /// \param Pos Insertion offset; must be <= the current length.
  /// \param S Bytes to insert; may be null when \p N is zero.
  /// \param N Number of bytes to insert from \p S.
  void insert(size_t Pos, const char *S, size_t N) {
    DEMANGLE_ASSERT(Pos <= CurrentPosition, "");
    if (N == 0)
      return;

    grow(N);
    std::memmove(Buffer + Pos + N, Buffer + Pos, CurrentPosition - Pos);
    std::memcpy(Buffer + Pos, S, N);
    CurrentPosition += N;

    notifyInsertion(Pos, N);
  }

  /// Return the number of bytes currently written.
  /// \returns The current write position in bytes.
  size_t getCurrentPosition() const { return CurrentPosition; }
  /// Set the current write position to \p NewPos and notify listeners.
  /// \param NewPos New write position.
  void setCurrentPosition(size_t NewPos) {
    notifyDeletion(CurrentPosition, NewPos);
    CurrentPosition = NewPos;
  }

  /// Return the last written character.
  /// \returns The character at the end of the written contents.
  char back() const {
    DEMANGLE_ASSERT(CurrentPosition, "");
    return Buffer[CurrentPosition - 1];
  }

  /// Return true if no characters have been written.
  /// \returns True when the buffer has no written characters.
  bool empty() const { return CurrentPosition == 0; }

  /// Return a pointer to the start of the buffer storage.
  /// \returns Pointer to the beginning of the buffer, or null if empty.
  char *getBuffer() { return Buffer; }
  /// Return a pointer to the last written character.
  /// \returns Pointer to the last written character in the buffer.
  char *getBufferEnd() { return Buffer + CurrentPosition - 1; }
  /// Return the allocated capacity of the buffer in bytes.
  /// \returns The allocated capacity of the buffer in bytes.
  size_t getBufferCapacity() const { return BufferCapacity; }
};

/// Temporarily replaces a referenced value and restores it on destruction.
template <class T> class ScopedOverride {
  T &Loc;
  T Original;

public:
  /// Capture \p Loc_ and leave its current value unchanged.
  /// \param Loc_ Reference whose value will be restored on destruction.
  ScopedOverride(T &Loc_) : ScopedOverride(Loc_, Loc_) {}

  /// Capture \p Loc_, save its current value, then assign \p NewVal.
  /// \param Loc_ Reference whose value will be restored on destruction.
  /// \param NewVal Value assigned to \p Loc_ for the duration of this scope.
  ScopedOverride(T &Loc_, T NewVal) : Loc(Loc_), Original(Loc_) {
    Loc_ = std::move(NewVal);
  }
  /// Restore the referenced value to the saved original.
  ~ScopedOverride() { Loc = std::move(Original); }

  /// Copy construction is deleted.
  /// \param Other Unused; copy construction is deleted.
  ScopedOverride(const ScopedOverride &Other) = delete;
  /// Copy assignment is deleted.
  /// \param Other Unused; copy assignment is deleted.
  ScopedOverride &operator=(const ScopedOverride &Other) = delete;
};

DEMANGLE_NAMESPACE_END

#endif
