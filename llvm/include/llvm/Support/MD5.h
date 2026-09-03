/* -*- C++ -*-
 * This code is derived from (original license follows):
 *
 * This is an OpenSSL-compatible implementation of the RSA Data Security, Inc.
 * MD5 Message-Digest Algorithm (RFC 1321).
 *
 * Homepage:
 * http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5
 *
 * Author:
 * Alexander Peslyak, better known as Solar Designer <solar at openwall.com>
 *
 * This software was written by Alexander Peslyak in 2001.  No copyright is
 * claimed, and the software is hereby placed in the public domain.
 * In case this attempt to disclaim copyright and place the software in the
 * public domain is deemed null and void, then the software is
 * Copyright (c) 2001 Alexander Peslyak and it is hereby released to the
 * general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 *
 * See md5.c for more information.
 */

#ifndef LLVM_SUPPORT_MD5_H
#define LLVM_SUPPORT_MD5_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include <array>
#include <cstdint>

namespace llvm {

template <unsigned N> class SmallString;
template <typename T> class ArrayRef;

/// Incremental MD5 message-digest computation.
class MD5 {
public:
  /// 16-byte MD5 message digest with helpers for hex and word views.
  ///
  /// Stores the digest in an internal \c std::array and exposes the same
  /// contiguous-byte interface callers expect from an array of 16 bytes.
  struct MD5Result {
  private:
    using Base = std::array<uint8_t, 16>;
    Base Storage{};

  public:
    /// Element type stored in the digest.
    using value_type = Base::value_type;
    /// Unsigned size type for indices and lengths.
    using size_type = Base::size_type;
    /// Signed type for iterator distances.
    using difference_type = Base::difference_type;
    /// Mutable reference to a digest byte.
    using reference = Base::reference;
    /// Immutable reference to a digest byte.
    using const_reference = Base::const_reference;
    /// Mutable pointer to a digest byte.
    using pointer = Base::pointer;
    /// Immutable pointer to a digest byte.
    using const_pointer = Base::const_pointer;
    /// Mutable random-access iterator.
    using iterator = Base::iterator;
    /// Immutable random-access iterator.
    using const_iterator = Base::const_iterator;
    /// Mutable reverse iterator.
    using reverse_iterator = Base::reverse_iterator;
    /// Immutable reverse iterator.
    using const_reverse_iterator = Base::const_reverse_iterator;

    /// Access the byte at \p N with bounds checking.
    ///
    /// \param N Index of the byte to access.
    /// \return Reference to the byte at \p N.
    reference at(size_type N) { return Storage.at(N); }
    /// Access the byte at \p N with bounds checking.
    ///
    /// \param N Index of the byte to access.
    /// \return Const reference to the byte at \p N.
    const_reference at(size_type N) const { return Storage.at(N); }

    /// Access the byte at \p N without bounds checking.
    ///
    /// \param N Index of the byte to access.
    /// \return Reference to the byte at \p N.
    reference operator[](size_type N) { return Storage[N]; }
    /// Access the byte at \p N without bounds checking.
    ///
    /// \param N Index of the byte to access.
    /// \return Const reference to the byte at \p N.
    const_reference operator[](size_type N) const { return Storage[N]; }

    /// Return a reference to the first byte.
    ///
    /// \return Reference to the first byte of the digest.
    reference front() { return Storage.front(); }
    /// Return a reference to the first byte.
    ///
    /// \return Const reference to the first byte of the digest.
    const_reference front() const { return Storage.front(); }

    /// Return a reference to the last byte.
    ///
    /// \return Reference to the last byte of the digest.
    reference back() { return Storage.back(); }
    /// Return a reference to the last byte.
    ///
    /// \return Const reference to the last byte of the digest.
    const_reference back() const { return Storage.back(); }

    /// Return a pointer to the underlying digest bytes.
    ///
    /// \return Pointer to the first byte of the digest.
    pointer data() { return Storage.data(); }
    /// Return a pointer to the underlying digest bytes.
    ///
    /// \return Const pointer to the first byte of the digest.
    const_pointer data() const { return Storage.data(); }

    /// Return an iterator to the first byte.
    ///
    /// \return Mutable iterator to the first byte.
    iterator begin() { return Storage.begin(); }
    /// Return an iterator to the first byte.
    ///
    /// \return Const iterator to the first byte.
    const_iterator begin() const { return Storage.begin(); }
    /// Return an iterator past the last byte.
    ///
    /// \return Mutable iterator past the last byte.
    iterator end() { return Storage.end(); }
    /// Return an iterator past the last byte.
    ///
    /// \return Const iterator past the last byte.
    const_iterator end() const { return Storage.end(); }

    /// Return a const iterator to the first byte.
    ///
    /// \return Const iterator to the first byte.
    const_iterator cbegin() const { return Storage.cbegin(); }
    /// Return a const iterator past the last byte.
    ///
    /// \return Const iterator past the last byte.
    const_iterator cend() const { return Storage.cend(); }

    /// Return a reverse iterator to the last byte.
    ///
    /// \return Mutable reverse iterator to the last byte.
    reverse_iterator rbegin() { return Storage.rbegin(); }
    /// Return a reverse iterator to the last byte.
    ///
    /// \return Const reverse iterator to the last byte.
    const_reverse_iterator rbegin() const { return Storage.rbegin(); }
    /// Return a reverse iterator past the first byte.
    ///
    /// \return Mutable reverse iterator past the first byte.
    reverse_iterator rend() { return Storage.rend(); }
    /// Return a reverse iterator past the first byte.
    ///
    /// \return Const reverse iterator past the first byte.
    const_reverse_iterator rend() const { return Storage.rend(); }

    /// Return a const reverse iterator to the last byte.
    ///
    /// \return Const reverse iterator to the last byte.
    const_reverse_iterator crbegin() const { return Storage.crbegin(); }
    /// Return a const reverse iterator past the first byte.
    ///
    /// \return Const reverse iterator past the first byte.
    const_reverse_iterator crend() const { return Storage.crend(); }

    /// Return whether the digest has no bytes.
    ///
    /// \return True if the digest has no bytes, false otherwise.
    bool empty() const { return Storage.empty(); }
    /// Return the number of bytes (always 16).
    ///
    /// \return The number of bytes in the digest (always 16).
    size_type size() const { return Storage.size(); }
    /// Return the maximum number of bytes.
    ///
    /// \return The maximum number of bytes the digest can hold.
    size_type max_size() const { return Storage.max_size(); }

    /// Assign \p Value to every byte.
    ///
    /// \param Value Byte written to each position in the digest.
    void fill(const_reference Value) { Storage.fill(Value); }
    /// Swap contents with another digest.
    ///
    /// \param Other Digest to exchange storage with.
    void swap(MD5Result &Other) { Storage.swap(Other.Storage); }

    /// Convert to a const reference to the underlying 16-byte array.
    ///
    /// \return Const reference to the underlying 16-byte array.
    operator Base const &() const { return Storage; }
    /// Convert to a reference to the underlying 16-byte array.
    ///
    /// \return Reference to the underlying 16-byte array.
    operator Base &() { return Storage; }

    /// Compare two digests for equality.
    ///
    /// \param LHS Left-hand digest.
    /// \param RHS Right-hand digest.
    /// \return True if both digests contain the same bytes.
    friend bool operator==(MD5Result const &LHS, MD5Result const &RHS) {
      return LHS.Storage == RHS.Storage;
    }

    /// Return the digest as a 32-character lowercase hex string.
    ///
    /// \return A 32-character lowercase hexadecimal encoding of the digest.
    LLVM_ABI SmallString<32> digest() const;

    /// Return the low 64 bits of the 128-bit MD5 digest.
    ///
    /// \return The least-significant 64 bits of the digest as a little-endian
    /// integer.
    uint64_t low() const {
      // Our MD5 implementation returns the result in little endian, so the low
      // word is first.
      using namespace support;
      return endian::read<uint64_t, llvm::endianness::little>(data());
    }

    /// Return the high 64 bits of the digest as a little-endian integer.
    ///
    /// \return The most-significant 64 bits of the digest as a little-endian
    /// integer.
    uint64_t high() const {
      using namespace support;
      return endian::read<uint64_t, llvm::endianness::little>(data() + 8);
    }
    /// Return the digest as a (high, low) pair of 64-bit words.
    ///
    /// \return A pair of (high, low) 64-bit words from the digest.
    std::pair<uint64_t, uint64_t> words() const {
      using namespace support;
      return {high(), low()};
    }
  };

  /// Construct an MD5 hasher with initial state.
  LLVM_ABI MD5();

  /// Updates the hash for the byte stream provided.
  ///
  /// \param Data Bytes to absorb into the hash state.
  LLVM_ABI void update(ArrayRef<uint8_t> Data);

  /// Updates the hash for the StringRef provided.
  ///
  /// \param Str String bytes to absorb into the hash state.
  LLVM_ABI void update(StringRef Str);

  /// Finishes off the hash and puts the result in result.
  ///
  /// \param Result Buffer that receives the finalized 16-byte digest.
  LLVM_ABI void final(MD5Result &Result);

  /// Finishes off the hash, and returns the 16-byte hash data.
  ///
  /// \return The finalized 16-byte MD5 digest.
  LLVM_ABI MD5Result final();

  /// Return the current 16-byte MD5 digest without finalizing state.
  ///
  /// This is suitable for getting the MD5 at any time without invalidating the
  /// internal state, so that more calls can be made into `update`.
  ///
  /// \return The current 16-byte MD5 digest.
  LLVM_ABI MD5Result result();

  /// Translates the bytes in \p Result to a hex string that is
  /// deposited into \p Str. The result will be of length 32.
  ///
  /// \param Result Digest whose bytes are rendered as hexadecimal.
  /// \param Str Destination that receives the 32-character hex string.
  LLVM_ABI static void stringifyResult(MD5Result &Result,
                                       SmallVectorImpl<char> &Str);

  /// Computes the hash for a given bytes.
  ///
  /// \param Data Bytes to hash in a single shot.
  /// \return The 16-byte MD5 digest of \p Data.
  LLVM_ABI static MD5Result hash(ArrayRef<uint8_t> Data);

private:
  // Any 32-bit or wider unsigned integer data type will do.
  using MD5_u32plus = uint32_t;

  // Internal State
  struct {
    MD5_u32plus a = 0x67452301;
    MD5_u32plus b = 0xefcdab89;
    MD5_u32plus c = 0x98badcfe;
    MD5_u32plus d = 0x10325476;
    MD5_u32plus hi = 0;
    MD5_u32plus lo = 0;
    uint8_t buffer[64];
    MD5_u32plus block[16];
  } InternalState;

  LLVM_ABI const uint8_t *body(ArrayRef<uint8_t> Data);
};

/// Helper to compute and return lower 64 bits of the given string's MD5 hash.
///
/// \param Str Input whose MD5 low word is returned.
/// \return The least-significant 64 bits of \p Str's MD5 digest.
inline uint64_t MD5Hash(StringRef Str) {
  using namespace support;

  MD5 Hash;
  Hash.update(Str);
  MD5::MD5Result Result;
  Hash.final(Result);
  // Return the least significant word.
  return Result.low();
}

} // end namespace llvm

#endif // LLVM_SUPPORT_MD5_H
